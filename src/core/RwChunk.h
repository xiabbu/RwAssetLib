/**
 * core/RwChunk.h - Chunk metadata layer.
 *
 * The DBO exporter writes chunk headers whose declared `size` frequently
 * disagrees with the bytes that follow (see docs: NtlMaterialExt.c uses
 * sizeof(RwTexture) instead of the real streamed length, and several container
 * levels are consistently +4). Those declarations are *data*: reproducing a DBO
 * file byte-for-byte means writing them back exactly.
 *
 * RwChunkSpan captures that as an object-level content fingerprint:
 *
 *   - `declaredSize` : header.size as it appeared in the source file
 *   - `actualSize`   : how many bytes the payload really occupied
 *
 * On write, the chunk writer measures the payload it just produced:
 *   payload size and FNV-1a content hash unchanged -> re-emit declaredSize
 *   otherwise -> recompute the vendor's recursive StreamGetSize
 *
 * No caller-managed dirty flag is involved: whether a chunk's content changed is
 * derived from the content itself.
 */

#pragma once

#include "core/RwStream.h"

#include <vector>

struct RwChunkSpan
{
    RwUInt32 declaredSize = 0;
    RwUInt32 actualSize   = 0;
    RwUInt32 version      = 0;
    bool     recorded     = false;
    std::uint64_t contentHash = 0;

    bool Record(RwUInt32 declared, RwUInt32 actual, RwUInt32 ver, RwStream& stream)
    {
        declaredSize = declared;
        actualSize = actual;
        version = ver;
        recorded = true;
        return stream.Fingerprint(stream.GetPosition() - actual, actual, contentHash);
    }

    void Reset() { *this = RwChunkSpan(); }

    /** Size to declare in the header for a payload of `writtenSize` bytes. */
    RwUInt32 ResolveDeclaredSize(RwUInt32 writtenSize, std::uint64_t hash) const
    {
        if (recorded && writtenSize == actualSize && hash == contentHash) {
            return declaredSize;
        }
        return writtenSize;
    }

    /** Version to write; falls back to the document version when not recorded. */
    RwUInt32 ResolveVersion(RwUInt32 defaultVersion) const
    {
        return (recorded && version != 0) ? version : defaultVersion;
    }
};

/**
 * RawChunk - an unmodelled chunk kept verbatim so it can be re-emitted.
 *
 * Used for chunk types this library does not (yet) model semantically. The
 * Sample corpus contains none that remain unmodelled, but keeping the escape
 * hatch means an unknown vendor plugin can never silently corrupt a file.
 */
struct RawChunk
{
    RwUInt32             type = 0;
    RwUInt32             version = 0;
    std::vector<RwUInt8> data;
    /// Declared/actual pair, so a chunk that declares more than it writes
    /// (DBO's dangling headers) is reproduced exactly.
    RwChunkSpan          span;
};

/**
 * ChunkWriter - writes nested chunks, back-patching sizes on End().
 *
 * Begin(type)                 : size always recomputed
 * Begin(type, span)           : size follows the span fingerprint rule above
 * Begin(type, span, version)  : same, with an explicit version override
 *
 * Declared-size recomputation follows RW's own ...StreamGetSize recursion: a
 * container declares the sum of `rwCHUNKHEADERSIZE + child declared size`, not
 * the number of bytes it actually emitted. Those differ whenever a child
 * declares more than it writes, which is exactly what DBO's dangling
 * NTL_WORLD_MATERIAL header does -- and is why every enclosing container in a
 * DBO file is 4 bytes "too large". Modelling the recursion reproduces that
 * offset instead of having to record it at every level.
 */
class ChunkWriter
{
public:
    explicit ChunkWriter(RwStream& stream, RwUInt32 version = rwLIBRARYVERSION37);

    bool Begin(RwUInt32 type);
    bool Begin(RwUInt32 type, const RwChunkSpan& span);
    bool BeginVersioned(RwUInt32 type, RwUInt32 version);
    bool Begin(RwUInt32 type, const RwChunkSpan& span, RwUInt32 version);

    /**
     * Opens a chunk whose StreamGetSize is a known constant that need not match
     * the bytes written.
     *
     * DBO has two such plugins: NtlMaterialExt reports `2 + sizeof(RwTexture)`
     * (a fixed 90) while writing a variable-length texture, and the world
     * material plugin reports 4 while writing nothing. Declaring the modelled
     * size here reproduces those headers without recording anything from a file.
     */
    bool BeginModelledSize(RwUInt32 type, RwUInt32 declaredSize, RwUInt32 version);

    /** Back-patches the header size; returns the payload byte count written. */
    bool End(RwUInt32* outWrittenSize = nullptr);

    /** Writes a whole raw chunk (header + payload). */
    bool WriteRaw(const RawChunk& chunk);

    RwStream& GetStream() { return m_stream; }
    RwUInt32 GetVersion() const { return m_version; }

    int Depth() const { return static_cast<int>(m_stack.size()); }

private:
    struct Frame
    {
        RwUInt32     headerPos = 0;
        RwUInt32     dataStart = 0;
        RwChunkSpan  span;
        bool         hasSpan = false;
        /// Set by BeginModelledSize: declare exactly this, whatever gets written.
        bool         hasModelledSize = false;
        RwUInt32     modelledSize = 0;
    };

    RwStream&          m_stream;
    RwUInt32           m_version;
    std::vector<Frame> m_stack;
};

/** Reads a full chunk (header already consumed by caller) into a RawChunk. */
bool ReadRawChunkPayload(RwStream& stream, const RwChunkHeader& header, RawChunk& out);

/** Reads header + payload into a RawChunk. */
bool ReadRawChunk(RwStream& stream, RawChunk& out);
