/**
 * core/RwStream.h - RenderWare binary stream (read/write, file/memory).
 *
 * Mirrors the role of RwStream in RenderWare babinary.c but
 * carries no RW runtime state: it is a plain positioned byte stream plus the
 * chunk-header primitives every ba*.c reader/writer is built on.
 */

#pragma once

#include "core/RwTypes.h"

#include <cstdio>
#include <string>
#include <vector>

/** On-the-wire chunk header: type, payload size, library version. */
struct RwChunkHeader
{
    RwUInt32 type = 0;
    RwUInt32 size = 0;
    RwUInt32 version = 0;
};

class RwStream
{
public:
    RwStream();
    ~RwStream();

    RwStream(const RwStream&) = delete;
    RwStream& operator=(const RwStream&) = delete;

    // -- opening ------------------------------------------------------------
    bool OpenRead(const char* filename);
    bool OpenRead(const std::string& filename) { return OpenRead(filename.c_str()); }
    bool OpenReadMemory(const void* data, size_t size);

    bool OpenWrite(const char* filename);
    bool OpenWrite(const std::string& filename) { return OpenWrite(filename.c_str()); }
    /** Write into a growable in-memory buffer (owned by the stream). */
    bool OpenWriteMemory();

#ifdef _WIN32
    bool OpenRead(const wchar_t* filename);
    bool OpenWrite(const wchar_t* filename);
#endif

    bool Close();

    bool IsOpen() const { return m_file != nullptr || m_isMemory; }
    bool IsWriteMode() const { return m_isWrite; }
    bool IsMemory() const { return m_isMemory; }

    /** Buffer written by OpenWriteMemory(). Empty for file streams. */
    const std::vector<RwUInt8>& GetMemoryBuffer() const { return m_writeBuf; }

    // -- position -----------------------------------------------------------
    RwUInt32 GetPosition() const;
    bool Seek(RwUInt32 position);
    bool Skip(RwInt32 offset);
    RwUInt32 GetSize() const { return m_fileSize; }
    bool IsEOF() const;
    bool Fingerprint(RwUInt32 offset, RwUInt32 size, std::uint64_t& value);

    // -- reading ------------------------------------------------------------
    bool Read(void* buffer, size_t size);
    bool ReadInt8(RwInt8* value);
    bool ReadUInt8(RwUInt8* value);
    bool ReadInt16(RwInt16* value);
    bool ReadUInt16(RwUInt16* value);
    bool ReadInt32(RwInt32* value);
    bool ReadUInt32(RwUInt32* value);
    bool ReadReal(RwReal* value);
    bool ReadReals(RwReal* values, size_t count);
    bool ReadV3d(RwV3d* value) { return ReadReals(reinterpret_cast<RwReal*>(value), 3); }
    /** Full 64-byte RwMatrix including the three pad words (see RpSkin). */
    bool ReadMatrix(RwMatrix* value);

    // -- writing ------------------------------------------------------------
    bool Write(const void* buffer, size_t size);
    bool WriteInt8(RwInt8 value);
    bool WriteUInt8(RwUInt8 value);
    bool WriteInt16(RwInt16 value);
    bool WriteUInt16(RwUInt16 value);
    bool WriteInt32(RwInt32 value);
    bool WriteUInt32(RwUInt32 value);
    bool WriteReal(RwReal value);
    bool WriteReals(const RwReal* values, size_t count);
    bool WriteV3d(const RwV3d& value) { return WriteReals(reinterpret_cast<const RwReal*>(&value), 3); }
    bool WriteMatrix(const RwMatrix& value);
    bool WritePadding(size_t count, RwUInt8 value = 0);

    // -- chunk primitives ---------------------------------------------------
    bool ReadChunkHeader(RwChunkHeader& header);
    bool WriteChunkHeader(RwUInt32 type, RwUInt32 size, RwUInt32 version = rwLIBRARYVERSION37);

    // -- declared-size scope tracking ---------------------------------------
    //
    // A container's recomputed declared size is the sum of its children's
    // *declared* sizes, which can exceed the bytes actually written (DBO's
    // dangling headers). Nesting is a property of the stream, not of a single
    // ChunkWriter, because each codec function creates its own writer.

    /** Opens a scope for a chunk whose children may declare excess bytes. */
    void PushDeclaredScope();
    /** Closes the innermost scope and returns the excess its children declared. */
    std::int64_t PopDeclaredScope();
    /** Records a finished child's (declared - written) in the enclosing scope. */
    void AddDeclaredExcess(std::int64_t excess);
    int DeclaredScopeDepth() const { return static_cast<int>(m_declaredScopes.size()); }

    /**
     * Ignore every declared size recorded at load time and derive all of them
     * from the modelled ...StreamGetSize semantics instead.
     *
     * This is the mode the 3ds Max exporter runs in: a document rebuilt from a
     * scene has no recorded sizes, so the writer must be able to produce the
     * original bytes from semantics alone. Running the corpus in this mode is
     * what proves that it can.
     */
    void SetDeriveAllChunkSizes(bool derive) { m_deriveAllChunkSizes = derive; }
    bool DeriveAllChunkSizes() const { return m_deriveAllChunkSizes; }

    // -- endian helpers (RW streams are little-endian) ----------------------
    static void SwapEndian16(void* data);
    static void SwapEndian32(void* data);
    static void SwapEndian32Array(void* data, size_t count);
    static bool IsLittleEndian();

    const std::string& GetLastError() const { return m_lastError; }

private:
    std::FILE*     m_file = nullptr;
    bool           m_isWrite = false;
    RwUInt32       m_fileSize = 0;
    std::string    m_lastError;

    // memory read
    bool           m_isMemory = false;
    const RwUInt8* m_memData = nullptr;
    size_t         m_memSize = 0;
    size_t         m_memPos = 0;

    // memory write
    std::vector<RwUInt8> m_writeBuf;

    // declared-size scope stack (see PushDeclaredScope)
    std::vector<std::int64_t> m_declaredScopes;
    bool                      m_deriveAllChunkSizes = false;

    void SetError(const char* msg);
};
