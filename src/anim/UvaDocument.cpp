/**
 * anim/UvaDocument.cpp - RpUVAnim dictionary stream read/write.
 */

#include "anim/UvaDocument.h"

#include <algorithm>
#include <cstdio>

std::string UvaAnimation::Name() const
{
    size_t len = 0;
    while (len < name.size() && name[len] != 0) {
        ++len;
    }
    return std::string(name.data(), len);
}

bool UvaDocument::Load(RwStream& stream)
{
    m_animations.clear();
    m_diag.Clear();

    const RwUInt32 headerPos = stream.GetPosition();
    RwChunkHeader header;
    if (!stream.ReadChunkHeader(header)) {
        return m_diag.Fail(headerPos, "truncated file: no chunk header");
    }
    if (header.type != rwID_UVANIMDICT) {
        return m_diag.FailChunkType(headerPos, rwID_UVANIMDICT, header.type);
    }
    m_version = header.version;

    DffScope scope(m_diag, rwID_UVANIMDICT);

    const RwUInt32 dictDataStart = stream.GetPosition();
    const RwUInt32 dictEnd = dictDataStart + header.size;

    const RwUInt32 structPos = stream.GetPosition();
    RwChunkHeader structHeader;
    if (!stream.ReadChunkHeader(structHeader)) {
        return m_diag.Fail(structPos, "truncated UVANIMDICT STRUCT header");
    }
    if (structHeader.type != rwID_STRUCT) {
        return m_diag.FailChunkType(structPos, rwID_STRUCT, structHeader.type);
    }
    if (structHeader.size != sizeof(RwInt32)) {
        return m_diag.Fail(structPos, "UVANIMDICT STRUCT must hold one count",
                           std::to_string(sizeof(RwInt32)), std::to_string(structHeader.size));
    }
    m_structVersion = structHeader.version;

    RwInt32 count = 0;
    if (!stream.ReadInt32(&count)) {
        return m_diag.Fail(stream.GetPosition(), "truncated UVANIMDICT entry count");
    }
    if (count < 0) {
        return m_diag.Fail(structPos, "negative UVANIMDICT entry count");
    }
    stream.Seek(structPos + rwCHUNKHEADERSIZE + structHeader.size);

    for (RwInt32 i = 0; i < count; ++i) {
        const RwUInt32 entryPos = stream.GetPosition();
        RwChunkHeader entryHeader;
        if (!stream.ReadChunkHeader(entryHeader)) {
            return m_diag.Fail(entryPos, "truncated UVANIMDICT entry header");
        }
        if (entryHeader.type != rwID_ANIMANIMATION) {
            return m_diag.FailChunkType(entryPos, rwID_ANIMANIMATION, entryHeader.type);
        }

        UvaAnimation anim;
        anim.chunkVersion = entryHeader.version;
        if (!stream.ReadInt32(&anim.streamVersion) ||
            !stream.ReadInt32(&anim.typeID) ||
            !stream.ReadInt32(&anim.numFrames) ||
            !stream.ReadInt32(&anim.flags) ||
            !stream.ReadReal(&anim.duration) ||
            !stream.ReadInt32(&anim.uvStreamVersion)) {
            return m_diag.Fail(entryPos, "truncated UVAnim animation header");
        }
        if (anim.numFrames < 0) {
            return m_diag.Fail(entryPos, "negative UVAnim keyframe count");
        }
        if (!stream.Read(anim.name.data(), kUvaMaxName)) {
            return m_diag.Fail(stream.GetPosition(), "truncated UVAnim animation name");
        }
        for (RwInt32 slot = 0; slot < kUvaMaxSlots; ++slot) {
            if (!stream.ReadInt32(&anim.channelMap[static_cast<size_t>(slot)])) {
                return m_diag.Fail(stream.GetPosition(), "truncated UVAnim node to channel map");
            }
        }

        anim.keyframes.resize(static_cast<size_t>(anim.numFrames));
        for (auto& keyframe : anim.keyframes) {
            if (!stream.ReadReal(&keyframe.time)) {
                return m_diag.Fail(stream.GetPosition(), "truncated UVAnim keyframe time");
            }
            for (RwReal& value : keyframe.values) {
                if (!stream.ReadReal(&value)) {
                    return m_diag.Fail(stream.GetPosition(), "truncated UVAnim keyframe data");
                }
            }
            if (!stream.ReadInt32(&keyframe.prevFrameIndex)) {
                return m_diag.Fail(stream.GetPosition(), "truncated UVAnim keyframe link");
            }
        }

        const RwUInt32 expected = kUvaAnimHeaderSize + kUvaMaxName + kUvaMaxSlots * sizeof(RwInt32) +
                                  static_cast<RwUInt32>(anim.numFrames) * kUvaKeyFrameSize;
        if (entryHeader.size != expected) {
            return m_diag.Fail(entryPos, "UVAnim entry size disagrees with keyframe count",
                               std::to_string(expected), std::to_string(entryHeader.size));
        }

        if (!anim.span.Record(entryHeader.size, entryHeader.size, entryHeader.version, stream)) {
            return m_diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
        }
        stream.Seek(entryPos + rwCHUNKHEADERSIZE + entryHeader.size);
        m_animations.push_back(std::move(anim));
    }

    if (stream.GetPosition() != dictEnd) {
        return m_diag.Fail(stream.GetPosition(), "UVANIMDICT payload size disagrees with entry count",
                           std::to_string(dictEnd - dictDataStart),
                           std::to_string(stream.GetPosition() - dictDataStart));
    }
    stream.Seek(dictEnd);
    return true;
}

bool UvaDocument::Load(const std::string& filename)
{
    RwStream stream;
    if (!stream.OpenRead(filename)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open file: " + filename);
    }
    return Load(stream);
}

bool UvaDocument::LoadFromMemory(const void* data, size_t size)
{
    RwStream stream;
    if (!stream.OpenReadMemory(data, size)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open memory buffer");
    }
    return Load(stream);
}

#ifdef _WIN32
bool UvaDocument::Load(const wchar_t* filename)
{
    RwStream stream;
    if (!stream.OpenRead(filename)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open file");
    }
    return Load(stream);
}
#endif

bool UvaDocument::Save(RwStream& stream)
{
    m_diag.Clear();

    if (!stream.IsOpen() || !stream.IsWriteMode()) {
        return m_diag.Fail(0, "stream is not open for writing");
    }

    DffScope scope(m_diag, rwID_UVANIMDICT);

    ChunkWriter writer(stream, m_version);
    if (!writer.BeginVersioned(rwID_UVANIMDICT, m_version)) {
        return m_diag.Fail(stream.GetPosition(), "failed to begin UVANIMDICT chunk");
    }

    const RwUInt32 structVersion = m_structVersion ? m_structVersion : m_version;
    if (!writer.BeginVersioned(rwID_STRUCT, structVersion)) {
        return m_diag.Fail(stream.GetPosition(), "failed to begin UVANIMDICT STRUCT");
    }
    if (!stream.WriteInt32(static_cast<RwInt32>(m_animations.size())) || !writer.End()) {
        return m_diag.Fail(stream.GetPosition(), "failed to write UVANIMDICT STRUCT");
    }

    for (const UvaAnimation& anim : m_animations) {
        if (static_cast<size_t>(anim.numFrames) != anim.keyframes.size()) {
            return m_diag.Fail(stream.GetPosition(), "keyframe array size does not match numFrames",
                               std::to_string(anim.numFrames), std::to_string(anim.keyframes.size()));
        }

        const RwUInt32 entryVersion = anim.chunkVersion ? anim.chunkVersion : m_version;
        if (!writer.BeginVersioned(rwID_ANIMANIMATION, entryVersion)) {
            return m_diag.Fail(stream.GetPosition(), "failed to begin UVAnim animation chunk");
        }
        if (!stream.WriteInt32(anim.streamVersion) || !stream.WriteInt32(anim.typeID) ||
            !stream.WriteInt32(anim.numFrames) || !stream.WriteInt32(anim.flags) ||
            !stream.WriteReal(anim.duration) || !stream.WriteInt32(anim.uvStreamVersion) ||
            !stream.Write(anim.name.data(), kUvaMaxName)) {
            return m_diag.Fail(stream.GetPosition(), "failed to write UVAnim animation header");
        }
        for (RwInt32 slot = 0; slot < kUvaMaxSlots; ++slot) {
            if (!stream.WriteInt32(anim.channelMap[static_cast<size_t>(slot)])) {
                return m_diag.Fail(stream.GetPosition(), "failed to write UVAnim channel map");
            }
        }
        for (const UvaKeyFrame& keyframe : anim.keyframes) {
            if (!stream.WriteReal(keyframe.time)) {
                return m_diag.Fail(stream.GetPosition(), "failed to write UVAnim keyframe time");
            }
            for (RwReal value : keyframe.values) {
                if (!stream.WriteReal(value)) {
                    return m_diag.Fail(stream.GetPosition(), "failed to write UVAnim keyframe value");
                }
            }
            if (!stream.WriteInt32(keyframe.prevFrameIndex)) {
                return m_diag.Fail(stream.GetPosition(), "failed to write UVAnim previous keyframe index");
            }
        }
        if (!writer.End()) {
            return m_diag.Fail(stream.GetPosition(), "failed to close UVAnim animation chunk");
        }
    }

    return writer.End() ? true : m_diag.Fail(stream.GetPosition(), "failed to close UVANIMDICT chunk");
}

bool UvaDocument::Save(const std::string& filename)
{
    RwStream stream;
    if (!stream.OpenWrite(filename)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open file for writing: " + filename);
    }
    if (!Save(stream)) {
        return false;
    }
    if (!stream.Close()) {
        return m_diag.Fail(0, stream.GetLastError() + ": " + filename);
    }
    return true;
}

bool UvaDocument::SaveToMemory(std::vector<RwUInt8>& out)
{
    out.clear();

    RwStream stream;
    if (!stream.OpenWriteMemory()) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open memory write stream");
    }
    if (!Save(stream)) {
        return false;
    }
    out = stream.GetMemoryBuffer();
    return true;
}

#ifdef _WIN32
bool UvaDocument::Save(const wchar_t* filename)
{
    RwStream stream;
    if (!stream.OpenWrite(filename)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open file for writing");
    }
    if (!Save(stream)) {
        return false;
    }
    if (!stream.Close()) {
        return m_diag.Fail(0, stream.GetLastError());
    }
    return true;
}
#endif

// ============================================================================
// Round-trip check
// ============================================================================

UvaRoundTripResult UvaCheckRoundTripBytes(const std::vector<RwUInt8>& source)
{
    UvaRoundTripResult result;
    result.sourceSize = source.size();

    UvaDocument doc;
    if (!doc.LoadFromMemory(source.data(), source.size())) {
        result.error = doc.GetLastError();
        return result;
    }
    result.loaded = true;

    std::vector<RwUInt8> output;
    if (!doc.SaveToMemory(output)) {
        result.error = doc.GetLastError();
        return result;
    }
    result.saved = true;
    result.outputSize = output.size();

    const size_t common = std::min(source.size(), output.size());
    size_t firstDiff = common;
    size_t diffCount = 0;
    for (size_t i = 0; i < common; ++i) {
        if (source[i] != output[i]) {
            if (diffCount == 0) {
                firstDiff = i;
            }
            ++diffCount;
        }
    }
    diffCount += source.size() > common ? source.size() - common : output.size() - common;

    result.firstDiffOffset = firstDiff;
    result.diffByteCount = diffCount;
    result.identical = diffCount == 0;
    return result;
}

UvaRoundTripResult UvaCheckRoundTrip(const std::string& filename)
{
    UvaRoundTripResult result;

    std::FILE* f = std::fopen(filename.c_str(), "rb");
    if (!f) {
        result.error = "failed to open file: " + filename;
        return result;
    }

    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(f);
        result.error = "failed to determine file size: " + filename;
        return result;
    }

    std::vector<RwUInt8> source(static_cast<size_t>(size));
    if (size > 0 && std::fread(source.data(), 1, source.size(), f) != source.size()) {
        std::fclose(f);
        result.error = "failed to read file: " + filename;
        return result;
    }
    std::fclose(f);

    return UvaCheckRoundTripBytes(source);
}
