/**
 * anim/AnmDocument.cpp - RtAnimAnimation read/write.
 */

#include "anim/AnmDocument.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <sstream>

// ============================================================================
// Compressed float codec (rtcmpkey.h)
// ============================================================================

RwReal AnmDecompressFloat(RwUInt16 v)
{
    union
    {
        float    f;
        RwUInt32 i;
    } tmp;

    tmp.i = (static_cast<RwUInt32>(v & 0x8000u) << 16);
    if (v & 0x7FFFu) {
        tmp.i |= (static_cast<RwUInt32>(v & 0x7800u) << 12) + 0x38000000u;
        tmp.i |= (static_cast<RwUInt32>(v & 0x07FFu) << 12);
    }
    return tmp.f;
}

RwUInt16 AnmCompressFloat(RwReal value)
{
    union
    {
        float    f;
        RwUInt32 i;
    } tmp;
    tmp.f = value;

    // RtCompressedKeyFrameCompressFloat rounds before dropping 12 mantissa
    // bits. Its overflow and signed-zero rules are part of the RW encoding.
    if ((tmp.i & 0xFFFu) >= 0x800u) {
        const RwUInt32 exponent = (tmp.i >> 23) & 0xFFu;
        if (exponent > 12 && (exponent < 254 || (tmp.i & 0x007FF000u) != 0x007FF000u)) {
            union
            {
                float f;
                RwUInt32 i;
            } rounding;
            rounding.i = (tmp.i & 0x80000000u) | ((exponent - 12) << 23);
            tmp.f += rounding.f;
        }
    }
    tmp.i &= ~0xFFFu;
    if ((tmp.i & 0x7F800000u) > 0x3F800000u) {
        tmp.i = (tmp.i & 0x80000000u) | 0x7FFFF000u;
    }
    if ((tmp.i & 0x7F800000u) < 0x38000000u) {
        tmp.i &= 0x80000000u;
    }
    RwUInt16 out = static_cast<RwUInt16>((tmp.i >> 16) & 0x8000u);
    if (tmp.i & 0x7FFFFFFFu) {
        out |= static_cast<RwUInt16>((((tmp.i >> 23) & 0xFFu) - 112) << 11);
        out |= static_cast<RwUInt16>((tmp.i >> 12) & 0x7FFu);
    }
    return out;
}

static void DecompressKeyFrame(const AnmCompressedKeyFrame& in,
                               const AnmCompressedCustomData& cd,
                               AnmKeyFrame& out)
{
    out.time = in.time;

    out.q.x = AnmDecompressFloat(in.qx);
    out.q.y = AnmDecompressFloat(in.qy);
    out.q.z = AnmDecompressFloat(in.qz);
    out.q.w = AnmDecompressFloat(in.qr);

    out.t.x = AnmDecompressFloat(in.tx) * cd.scalar.x + cd.offset.x;
    out.t.y = AnmDecompressFloat(in.ty) * cd.scalar.y + cd.offset.y;
    out.t.z = AnmDecompressFloat(in.tz) * cd.scalar.z + cd.offset.z;

    out.prevFrameOffset = in.prevFrameOffset;
}

// ============================================================================
// Node grouping
// ============================================================================

bool AnmAnimation::BuildNodeTracks(std::string& outError)
{
    outError.clear();
    nodeTracks.clear();

    if (keyframes.empty()) {
        return true;
    }

    // RtAnimAnimationGetNumNodes: scan the initial keys until one refers back to
    // keyframe 0 (that is node 0's second key).
    int numNodes = 0;
    for (int i = 1; i < static_cast<int>(keyframes.size()); ++i) {
        if (keyframes[static_cast<size_t>(i)].prevFrameOffset == 0) {
            numNodes = i;
            break;
        }
    }

    if (numNodes == 0) {
        // Single-key-per-node animation: every key is an initial key.
        numNodes = 0;
        for (const auto& kf : keyframes) {
            if (kf.time == 0.0f) {
                ++numNodes;
            } else {
                break;
            }
        }
    }

    if (numNodes <= 0 || numNodes > static_cast<int>(keyframes.size())) {
        outError = "cannot determine animated node count";
        return false;
    }

    nodeTracks.resize(static_cast<size_t>(numNodes));
    std::vector<int> nodeOfKeyFrame(keyframes.size(), -1);

    for (int i = 0; i < numNodes; ++i) {
        nodeTracks[static_cast<size_t>(i)].nodeID = i;
        nodeOfKeyFrame[static_cast<size_t>(i)] = i;
        nodeTracks[static_cast<size_t>(i)].keyframes.push_back(keyframes[static_cast<size_t>(i)]);
    }

    const RwInt32 stride = KeyFrameStride();
    if (stride <= 0) {
        outError = "invalid keyframe stride";
        return false;
    }

    for (int i = numNodes; i < static_cast<int>(keyframes.size()); ++i) {
        const RwInt32 offset = keyframes[static_cast<size_t>(i)].prevFrameOffset;
        const int prevIdx = static_cast<int>(offset / stride);
        if (prevIdx < 0 || prevIdx >= i) {
            outError = "keyframe " + std::to_string(i) + " has an out-of-range prevFrameOffset";
            return false;
        }
        const int node = nodeOfKeyFrame[static_cast<size_t>(prevIdx)];
        if (node < 0 || node >= numNodes) {
            outError = "keyframe " + std::to_string(i) + " links into an unassigned node";
            return false;
        }
        nodeOfKeyFrame[static_cast<size_t>(i)] = node;
        nodeTracks[static_cast<size_t>(node)].keyframes.push_back(keyframes[static_cast<size_t>(i)]);
    }

    for (auto& track : nodeTracks) {
        std::stable_sort(track.keyframes.begin(), track.keyframes.end(),
                         [](const AnmKeyFrame& a, const AnmKeyFrame& b) { return a.time < b.time; });
    }

    return true;
}

bool AnmAnimation::RebuildFromNodeTracks(RwInt32 initialPrevFrameOffset, std::string& outError)
{
    outError.clear();

    if (nodeTracks.empty()) {
        outError = "no node tracks to rebuild from";
        return false;
    }

    for (size_t n = 0; n < nodeTracks.size(); ++n) {
        const auto& track = nodeTracks[n].keyframes;
        if (track.size() < 2 || track.front().time != 0.0f) {
            outError = "node track " + std::to_string(n) + " needs a key at time 0 and at least two keys";
            return false;
        }
    }

    compressedKeyframes.clear();

    const size_t numNodes = nodeTracks.size();
    size_t total = 0;
    for (const auto& track : nodeTracks) {
        total += track.keyframes.size();
    }

    keyframes.clear();
    keyframes.reserve(total);

    std::vector<size_t> nextKey(numNodes, 0);
    std::vector<RwReal> lastTime(numNodes, 0.0f);
    std::vector<RwInt32> lastOffset(numNodes, initialPrevFrameOffset);

    auto emit = [&](size_t n) {
        AnmKeyFrame kf = nodeTracks[n].keyframes[nextKey[n]];
        kf.prevFrameOffset = lastOffset[n];
        lastTime[n] = kf.time;
        lastOffset[n] = static_cast<RwInt32>(keyframes.size()) * KeyFrameStride();
        ++nextKey[n];
        keyframes.push_back(kf);
    };

    for (size_t i = 0; i < numNodes * 2; ++i) {
        emit(i % numNodes);
    }
    while (keyframes.size() < total) {
        size_t best = numNodes;
        for (size_t n = 0; n < numNodes; ++n) {
            if (nextKey[n] < nodeTracks[n].keyframes.size() &&
                (best == numNodes || lastTime[n] < lastTime[best])) {
                best = n;
            }
        }
        emit(best);
    }

    numFrames = static_cast<RwInt32>(keyframes.size());

    RwReal maxTime = -FLT_MAX;
    for (const auto& kf : keyframes) {
        maxTime = (kf.time > maxTime) ? kf.time : maxTime;
    }
    duration = maxTime;

    if (IsCompressed()) {
        // RtCompressedKeyConvertFromStdKey normalises translation about the
        // bounding-box centre. Imported compression parameters are scene data.
        if (!hasCompressedCustomData) {
            RwV3d inf = keyframes[0].t;
            RwV3d sup = inf;
            for (const auto& key : keyframes) {
                inf.x = std::min(inf.x, key.t.x);
                inf.y = std::min(inf.y, key.t.y);
                inf.z = std::min(inf.z, key.t.z);
                sup.x = std::max(sup.x, key.t.x);
                sup.y = std::max(sup.y, key.t.y);
                sup.z = std::max(sup.z, key.t.z);
            }
            compressedCustomData.scalar = RwV3d((sup.x - inf.x) * 0.5f,
                                                (sup.y - inf.y) * 0.5f,
                                                (sup.z - inf.z) * 0.5f);
            compressedCustomData.offset = RwV3d(compressedCustomData.scalar.x + inf.x,
                                                compressedCustomData.scalar.y + inf.y,
                                                compressedCustomData.scalar.z + inf.z);
            hasCompressedCustomData = true;
        }
        for (const auto& key : keyframes) {
            AnmCompressedKeyFrame packed;
            packed.time = key.time;
            packed.qx = AnmCompressFloat(key.q.x);
            packed.qy = AnmCompressFloat(key.q.y);
            packed.qz = AnmCompressFloat(key.q.z);
            packed.qr = AnmCompressFloat(key.q.w);
            packed.tx = AnmCompressFloat((key.t.x - compressedCustomData.offset.x) / compressedCustomData.scalar.x);
            packed.ty = AnmCompressFloat((key.t.y - compressedCustomData.offset.y) / compressedCustomData.scalar.y);
            packed.tz = AnmCompressFloat((key.t.z - compressedCustomData.offset.z) / compressedCustomData.scalar.z);
            packed.prevFrameOffset = key.prevFrameOffset;
            compressedKeyframes.push_back(packed);
        }
    }

    // The payload length changed, so drop the recorded span: the writer will
    // compute a fresh size.
    span.Reset();
    trailing.clear();
    return true;
}

// ============================================================================
// AnmDocument
// ============================================================================

AnmAnimation& AnmDocument::CreateAnimation()
{
    m_anim = std::make_unique<AnmAnimation>();
    return *m_anim;
}

bool AnmDocument::Load(RwStream& stream)
{
    m_anim.reset();
    m_diag.Clear();

    const RwUInt32 headerPos = stream.GetPosition();
    RwChunkHeader header;
    if (!stream.ReadChunkHeader(header)) {
        return m_diag.Fail(headerPos, "truncated file: no chunk header");
    }
    if (header.type != rwID_ANIMANIMATION) {
        return m_diag.FailChunkType(headerPos, rwID_ANIMANIMATION, header.type);
    }

    DffScope scope(m_diag, rwID_ANIMANIMATION);

    m_version = header.version;
    const RwUInt32 dataStart = stream.GetPosition();
    const RwUInt32 chunkEnd = dataStart + header.size;

    auto anim = std::make_unique<AnmAnimation>();

    if (!stream.ReadInt32(&anim->streamVersion) ||
        !stream.ReadInt32(&anim->typeID) ||
        !stream.ReadInt32(&anim->numFrames) ||
        !stream.ReadInt32(&anim->flags) ||
        !stream.ReadReal(&anim->duration)) {
        return m_diag.Fail(dataStart, "truncated animation header");
    }

    if (anim->numFrames < 0) {
        return m_diag.Fail(dataStart, "negative keyframe count");
    }

    if (anim->typeID == rpHANIMSTDKEYFRAMETYPEID) {
        const RwUInt32 need = static_cast<RwUInt32>(anim->numFrames) * kStdKeyFrameStreamSize;
        if (stream.GetPosition() + need > chunkEnd) {
            return m_diag.Fail(stream.GetPosition(),
                               "standard keyframe array exceeds the chunk payload",
                               std::to_string(need),
                               std::to_string(chunkEnd - stream.GetPosition()));
        }

        anim->keyframes.resize(static_cast<size_t>(anim->numFrames));
        for (RwInt32 i = 0; i < anim->numFrames; ++i) {
            AnmKeyFrame& kf = anim->keyframes[static_cast<size_t>(i)];
            const bool ok = stream.ReadReal(&kf.time) &&
                            stream.ReadReal(&kf.q.x) && stream.ReadReal(&kf.q.y) &&
                            stream.ReadReal(&kf.q.z) && stream.ReadReal(&kf.q.w) &&
                            stream.ReadV3d(&kf.t) &&
                            stream.ReadInt32(&kf.prevFrameOffset);
            if (!ok) {
                return m_diag.Fail(stream.GetPosition(), "truncated standard keyframe");
            }
        }
    } else if (anim->typeID == rtANIMCOMPRESSEDKEYFRAMETYPEID) {
        const RwUInt32 need = static_cast<RwUInt32>(anim->numFrames) * kCompressedKeyFrameStreamSize +
                              static_cast<RwUInt32>(sizeof(RwReal) * 6);
        if (stream.GetPosition() + need > chunkEnd) {
            return m_diag.Fail(stream.GetPosition(),
                               "compressed keyframe array exceeds the chunk payload",
                               std::to_string(need),
                               std::to_string(chunkEnd - stream.GetPosition()));
        }

        anim->compressedKeyframes.resize(static_cast<size_t>(anim->numFrames));
        for (RwInt32 i = 0; i < anim->numFrames; ++i) {
            AnmCompressedKeyFrame& kf = anim->compressedKeyframes[static_cast<size_t>(i)];
            const bool ok = stream.ReadReal(&kf.time) &&
                            stream.ReadUInt16(&kf.qx) && stream.ReadUInt16(&kf.qy) &&
                            stream.ReadUInt16(&kf.qz) && stream.ReadUInt16(&kf.qr) &&
                            stream.ReadUInt16(&kf.tx) && stream.ReadUInt16(&kf.ty) &&
                            stream.ReadUInt16(&kf.tz) &&
                            stream.ReadInt32(&kf.prevFrameOffset);
            if (!ok) {
                return m_diag.Fail(stream.GetPosition(), "truncated compressed keyframe");
            }
        }

        if (!stream.ReadV3d(&anim->compressedCustomData.offset) ||
            !stream.ReadV3d(&anim->compressedCustomData.scalar)) {
            return m_diag.Fail(stream.GetPosition(), "truncated RtCompressedKeyFrameCustomData");
        }
        anim->hasCompressedCustomData = true;

        anim->keyframes.resize(static_cast<size_t>(anim->numFrames));
        for (RwInt32 i = 0; i < anim->numFrames; ++i) {
            DecompressKeyFrame(anim->compressedKeyframes[static_cast<size_t>(i)],
                               anim->compressedCustomData,
                               anim->keyframes[static_cast<size_t>(i)]);
        }
    } else {
        return m_diag.Fail(dataStart,
                           "unsupported interpolator typeID",
                           "1 (standard) or 2 (compressed)",
                           std::to_string(anim->typeID));
    }

    const RwUInt32 afterKeys = stream.GetPosition();
    if (afterKeys < chunkEnd) {
        anim->trailing.resize(chunkEnd - afterKeys);
        if (!stream.Read(anim->trailing.data(), anim->trailing.size())) {
            return m_diag.Fail(afterKeys, "truncated animation trailing data");
        }
    } else if (afterKeys > chunkEnd) {
        return m_diag.Fail(afterKeys, "animation payload overran its declared length");
    }

    if (!anim->span.Record(header.size, stream.GetPosition() - dataStart, header.version, stream)) {
        return m_diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }

    std::string groupError;
    if (!anim->BuildNodeTracks(groupError)) {
        // Grouping is derived information; a failure must not block a 1:1
        // round-trip, so record it as a diagnostic and keep the raw keyframes.
        m_diag.Fail(dataStart, "node grouping: " + groupError);
        m_diag.Clear();
    }

    m_anim = std::move(anim);
    return true;
}

bool AnmDocument::Load(const std::string& filename)
{
    RwStream stream;
    if (!stream.OpenRead(filename)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open file: " + filename);
    }
    return Load(stream);
}

bool AnmDocument::LoadFromMemory(const void* data, size_t size)
{
    RwStream stream;
    if (!stream.OpenReadMemory(data, size)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open memory buffer");
    }
    return Load(stream);
}

#ifdef _WIN32
bool AnmDocument::Load(const wchar_t* filename)
{
    RwStream stream;
    if (!stream.OpenRead(filename)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open file");
    }
    return Load(stream);
}
#endif

bool AnmDocument::Save(RwStream& stream)
{
    m_diag.Clear();

    if (!m_anim) {
        return m_diag.Fail(0, "no animation to save");
    }
    if (!stream.IsOpen() || !stream.IsWriteMode()) {
        return m_diag.Fail(0, "stream is not open for writing");
    }

    const AnmAnimation& anim = *m_anim;
    DffScope scope(m_diag, rwID_ANIMANIMATION);

    if (anim.typeID == rpHANIMSTDKEYFRAMETYPEID) {
        if (static_cast<RwInt32>(anim.keyframes.size()) != anim.numFrames) {
            return m_diag.Fail(0, "keyframe array size does not match numFrames",
                               std::to_string(anim.numFrames),
                               std::to_string(anim.keyframes.size()));
        }
    } else if (anim.typeID == rtANIMCOMPRESSEDKEYFRAMETYPEID) {
        if (static_cast<RwInt32>(anim.compressedKeyframes.size()) != anim.numFrames) {
            return m_diag.Fail(0, "compressed keyframe array size does not match numFrames",
                               std::to_string(anim.numFrames),
                               std::to_string(anim.compressedKeyframes.size()));
        }
    } else {
        return m_diag.Fail(0, "unsupported interpolator typeID", "1 or 2", std::to_string(anim.typeID));
    }

    ChunkWriter writer(stream, anim.span.ResolveVersion(m_version));
    if (!writer.Begin(rwID_ANIMANIMATION, anim.span, anim.span.ResolveVersion(m_version))) {
        return m_diag.Fail(stream.GetPosition(), "failed to begin ANIMANIMATION chunk");
    }

    if (!stream.WriteInt32(anim.streamVersion ? anim.streamVersion : rtANIMSTREAMCURRENTVERSION) ||
        !stream.WriteInt32(anim.typeID) || !stream.WriteInt32(anim.numFrames) ||
        !stream.WriteInt32(anim.flags) || !stream.WriteReal(anim.duration)) {
        return m_diag.Fail(stream.GetPosition(), "failed to write animation header");
    }

    if (anim.typeID == rpHANIMSTDKEYFRAMETYPEID) {
        for (const auto& kf : anim.keyframes) {
            if (!stream.WriteReal(kf.time) || !stream.WriteReal(kf.q.x) ||
                !stream.WriteReal(kf.q.y) || !stream.WriteReal(kf.q.z) ||
                !stream.WriteReal(kf.q.w) || !stream.WriteV3d(kf.t) ||
                !stream.WriteInt32(kf.prevFrameOffset)) {
                return m_diag.Fail(stream.GetPosition(), "failed to write animation keyframe");
            }
        }
    } else {
        for (const auto& kf : anim.compressedKeyframes) {
            if (!stream.WriteReal(kf.time) || !stream.WriteUInt16(kf.qx) ||
                !stream.WriteUInt16(kf.qy) || !stream.WriteUInt16(kf.qz) ||
                !stream.WriteUInt16(kf.qr) || !stream.WriteUInt16(kf.tx) ||
                !stream.WriteUInt16(kf.ty) || !stream.WriteUInt16(kf.tz) ||
                !stream.WriteInt32(kf.prevFrameOffset)) {
                return m_diag.Fail(stream.GetPosition(), "failed to write compressed animation keyframe");
            }
        }

        const AnmCompressedCustomData cd =
            anim.hasCompressedCustomData ? anim.compressedCustomData : AnmCompressedCustomData();
        if (!stream.WriteV3d(cd.offset) || !stream.WriteV3d(cd.scalar)) {
            return m_diag.Fail(stream.GetPosition(), "failed to write animation compression parameters");
        }
    }

    if (!anim.trailing.empty() && !stream.Write(anim.trailing.data(), anim.trailing.size())) {
        return m_diag.Fail(stream.GetPosition(), "failed to write animation trailing data");
    }

    return writer.End() ? true : m_diag.Fail(stream.GetPosition(), "failed to close ANIMANIMATION chunk");
}

bool AnmDocument::Save(const std::string& filename)
{
    RwStream stream;
    if (!stream.OpenWrite(filename)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to create file: " + filename);
    }
    if (!Save(stream)) {
        return false;
    }
    if (!stream.Close()) {
        return m_diag.Fail(0, stream.GetLastError() + ": " + filename);
    }
    return true;
}

bool AnmDocument::SaveToMemory(std::vector<RwUInt8>& out)
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
bool AnmDocument::Save(const wchar_t* filename)
{
    RwStream stream;
    if (!stream.OpenWrite(filename)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to create file");
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

bool AnmDocument::ApplyHAnimIds(const DffClump& refClump)
{
    if (!m_anim) {
        m_diag.Clear();
        return m_diag.Fail(0, "no animation loaded");
    }

    const DffFrame* root = refClump.FindHAnimRoot();
    if (!root || !root->hanim || root->hanim->nodeInfos.empty()) {
        m_diag.Clear();
        return m_diag.Fail(0, "reference clump has no HAnim hierarchy");
    }

    const auto& nodeInfos = root->hanim->nodeInfos;
    const size_t count = std::min(nodeInfos.size(), m_anim->nodeTracks.size());
    for (size_t i = 0; i < count; ++i) {
        m_anim->nodeTracks[i].nodeID = nodeInfos[i].nodeID;
    }
    return true;
}

std::string AnmDocument::DescribeStructure() const
{
    std::ostringstream os;

    if (!m_anim) {
        os << "no animation loaded\n";
        return os.str();
    }

    char versionBuf[96];
    const RwUInt32 unpacked = RwLibraryIDUnpackVersion(m_version);
    std::snprintf(versionBuf, sizeof(versionBuf), "0x%08X (RW %u.%u.%u.%u build %u)",
                  m_version,
                  RwVersionGetMajor(unpacked),
                  RwVersionGetMinor(unpacked),
                  RwVersionGetRevision(unpacked),
                  RwVersionGetBinaryFormat(unpacked),
                  RwLibraryIDUnpackBuildNum(m_version));

    os << "=== ANM structure ===\n";
    os << "version   : " << versionBuf << "\n";
    os << "typeID    : " << m_anim->typeID
       << (m_anim->IsCompressed() ? "  (compressed)" : "  (standard)") << "\n";
    os << "numFrames : " << m_anim->numFrames << "\n";
    os << "flags     : 0x" << std::hex << m_anim->flags << std::dec << "\n";
    os << "duration  : " << m_anim->duration << " s\n";
    os << "nodes     : " << m_anim->NumNodes() << "\n";

    if (m_anim->hasCompressedCustomData) {
        os << "customData: offset=(" << m_anim->compressedCustomData.offset.x << ","
           << m_anim->compressedCustomData.offset.y << "," << m_anim->compressedCustomData.offset.z
           << ") scalar=(" << m_anim->compressedCustomData.scalar.x << ","
           << m_anim->compressedCustomData.scalar.y << "," << m_anim->compressedCustomData.scalar.z << ")\n";
    }

    os << "\n-- keyframes per node --\n";
    for (size_t i = 0; i < m_anim->nodeTracks.size(); ++i) {
        const auto& track = m_anim->nodeTracks[i];
        os << "  node[" << i << "] id=" << track.nodeID << " keys=" << track.keyframes.size();
        if (!track.keyframes.empty()) {
            const auto& kf = track.keyframes.front();
            os << "  t0=" << kf.time << " pos=(" << kf.t.x << "," << kf.t.y << "," << kf.t.z << ")";
        }
        os << "\n";
    }

    return os.str();
}

// ============================================================================
// Round-trip check
// ============================================================================

AnmRoundTripResult AnmCheckRoundTripBytes(const std::vector<RwUInt8>& source)
{
    AnmRoundTripResult result;
    result.sourceSize = source.size();

    AnmDocument doc;
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
    diffCount += (source.size() > output.size()) ? (source.size() - output.size())
                                                 : (output.size() - source.size());

    result.firstDiffOffset = firstDiff;
    result.diffByteCount = diffCount;
    result.identical = (diffCount == 0);
    return result;
}

AnmRoundTripResult AnmCheckRoundTrip(const std::string& filename)
{
    AnmRoundTripResult result;

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

    return AnmCheckRoundTripBytes(source);
}
