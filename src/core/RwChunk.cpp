/**
 * core/RwChunk.cpp - Chunk metadata layer implementation.
 */

#include "core/RwChunk.h"

ChunkWriter::ChunkWriter(RwStream& stream, RwUInt32 version)
    : m_stream(stream)
    , m_version(version)
{
}

bool ChunkWriter::Begin(RwUInt32 type)
{
    return Begin(type, RwChunkSpan(), m_version);
}

bool ChunkWriter::Begin(RwUInt32 type, const RwChunkSpan& span)
{
    return Begin(type, span, span.ResolveVersion(m_version));
}

bool ChunkWriter::BeginVersioned(RwUInt32 type, RwUInt32 version)
{
    return Begin(type, RwChunkSpan(), version != 0 ? version : m_version);
}

bool ChunkWriter::Begin(RwUInt32 type, const RwChunkSpan& span, RwUInt32 version)
{
    Frame frame;
    frame.headerPos = m_stream.GetPosition();
    frame.span = span;
    frame.hasSpan = span.recorded;

    if (!m_stream.WriteChunkHeader(type, 0, version != 0 ? version : m_version)) {
        return false;
    }

    frame.dataStart = m_stream.GetPosition();
    m_stack.push_back(frame);
    m_stream.PushDeclaredScope();
    return true;
}

bool ChunkWriter::BeginModelledSize(RwUInt32 type, RwUInt32 declaredSize, RwUInt32 version)
{
    if (!Begin(type, RwChunkSpan(), version)) {
        return false;
    }
    m_stack.back().hasModelledSize = true;
    m_stack.back().modelledSize = declaredSize;
    return true;
}

bool ChunkWriter::End(RwUInt32* outWrittenSize)
{
    if (m_stack.empty()) {
        return false;
    }

    const Frame frame = m_stack.back();
    m_stack.pop_back();

    const std::int64_t childExcess = m_stream.PopDeclaredScope();

    const RwUInt32 endPos = m_stream.GetPosition();
    const RwUInt32 written = endPos - frame.dataStart;
    if (outWrittenSize) {
        *outWrittenSize = written;
    }

    RwUInt32 declared;
    std::uint64_t contentHash = 0;
    if (frame.hasSpan && !m_stream.DeriveAllChunkSizes() &&
        !m_stream.Fingerprint(frame.dataStart, written, contentHash)) {
        return false;
    }
    if (frame.hasModelledSize) {
        // The plugin's StreamGetSize is a known constant (see BeginModelledSize).
        declared = frame.modelledSize;
    } else if (frame.hasSpan && !m_stream.DeriveAllChunkSizes() && written == frame.span.actualSize &&
               contentHash == frame.span.contentHash) {
        // Payload byte-for-byte as it was read: replay the original declaration,
        // however wrong it was.
        declared = frame.span.declaredSize;
    } else {
        // Recompute the way RW's ...StreamGetSize does: the bytes we emitted plus
        // whatever our children declared beyond what they wrote.
        const std::int64_t recomputed = static_cast<std::int64_t>(written) + childExcess;
        declared = (recomputed > 0) ? static_cast<RwUInt32>(recomputed) : 0u;
    }

    m_stream.AddDeclaredExcess(static_cast<std::int64_t>(declared) - static_cast<std::int64_t>(written));

    if (!m_stream.Seek(frame.headerPos + sizeof(RwUInt32))) {
        return false;
    }
    if (!m_stream.WriteUInt32(declared)) {
        return false;
    }
    return m_stream.Seek(endPos);
}

bool ChunkWriter::WriteRaw(const RawChunk& chunk)
{
    if (!Begin(chunk.type, chunk.span, chunk.version != 0 ? chunk.version : m_version)) {
        return false;
    }
    if (!chunk.data.empty() && !m_stream.Write(chunk.data.data(), chunk.data.size())) {
        return false;
    }
    return End();
}

bool ReadRawChunkPayload(RwStream& stream, const RwChunkHeader& header, RawChunk& out)
{
    out.type = header.type;
    out.version = header.version;
    out.data.clear();

    if (header.size == 0) {
        return out.span.Record(header.size, 0, header.version, stream);
    }

    out.data.resize(header.size);
    if (!stream.Read(out.data.data(), header.size)) {
        return false;
    }
    return out.span.Record(header.size, header.size, header.version, stream);
}

bool ReadRawChunk(RwStream& stream, RawChunk& out)
{
    RwChunkHeader header;
    if (!stream.ReadChunkHeader(header)) {
        return false;
    }
    return ReadRawChunkPayload(stream, header, out);
}
