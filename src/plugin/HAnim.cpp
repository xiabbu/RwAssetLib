/**
 * plugin/HAnim.cpp - RpHAnim frame extension read/write.
 */

#include "plugin/HAnim.h"

std::vector<int> DffHAnim::BuildParentIndices() const
{
    std::vector<int> parents(nodeInfos.size(), -1);
    std::vector<int> stack;
    stack.push_back(-1);

    int parent = -1;
    for (size_t i = 0; i < nodeInfos.size(); ++i) {
        parents[i] = parent;

        if (nodeInfos[i].flags & rpHANIMPUSHPARENTMATRIX) {
            stack.push_back(parent);
        }
        if (nodeInfos[i].flags & rpHANIMPOPPARENTMATRIX) {
            if (!stack.empty()) {
                parent = stack.back();
                stack.pop_back();
            }
        } else {
            parent = static_cast<int>(i);
        }
    }
    return parents;
}

bool ReadHAnim(RwStream& stream, DffDiagnostics& diag, DffHAnim& out, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_HANIMPLUGIN);

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    out = DffHAnim();
    out.chunkVersion = chunkVersion;

    if (!stream.ReadInt32(&out.streamVersion)) {
        stream.Seek(endPos);
        return diag.Fail(startPos, "truncated HAnim stream version");
    }
    if (out.streamVersion != rpHANIMSTREAMCURRENTVERSION) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "unsupported HAnim stream version",
                         "0x100",
                         std::to_string(out.streamVersion));
    }

    if (!stream.ReadInt32(&out.nodeId)) {
        stream.Seek(endPos);
        return diag.Fail(stream.GetPosition(), "truncated HAnim node id");
    }

    RwInt32 numNodes = 0;
    if (!stream.ReadInt32(&numNodes) || numNodes < 0) {
        stream.Seek(endPos);
        return diag.Fail(stream.GetPosition(), "invalid HAnim node count");
    }

    if (numNodes > 0) {
        if (!stream.ReadInt32(&out.flags) || !stream.ReadInt32(&out.maxInterpKeyFrameSize)) {
            stream.Seek(endPos);
            return diag.Fail(stream.GetPosition(), "truncated HAnim hierarchy header");
        }

        out.nodeInfos.resize(static_cast<size_t>(numNodes));
        for (RwInt32 i = 0; i < numNodes; ++i) {
            auto& info = out.nodeInfos[static_cast<size_t>(i)];
            if (!stream.ReadInt32(&info.nodeID) ||
                !stream.ReadInt32(&info.nodeIndex) ||
                !stream.ReadInt32(&info.flags)) {
                stream.Seek(endPos);
                return diag.Fail(stream.GetPosition(), "truncated HAnim node info");
            }
        }
    }

    const RwUInt32 consumed = stream.GetPosition() - startPos;
    if (!out.span.Record(payloadSize, consumed, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    stream.Seek(endPos);
    return true;
}

bool WriteHAnim(RwStream& stream, DffDiagnostics& diag, const DffHAnim& hanim, RwUInt32 version)
{
    DffScope scope(diag, rwID_HANIMPLUGIN);

    const RwUInt32 chunkVersion = hanim.chunkVersion ? hanim.chunkVersion : version;
    ChunkWriter writer(stream, chunkVersion);

    if (!writer.Begin(rwID_HANIMPLUGIN, hanim.span, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin HANIM chunk");
    }

    stream.WriteInt32(hanim.streamVersion);
    stream.WriteInt32(hanim.nodeId);
    stream.WriteInt32(static_cast<RwInt32>(hanim.nodeInfos.size()));

    if (!hanim.nodeInfos.empty()) {
        stream.WriteInt32(hanim.flags);
        stream.WriteInt32(hanim.maxInterpKeyFrameSize);
        for (const auto& info : hanim.nodeInfos) {
            stream.WriteInt32(info.nodeID);
            stream.WriteInt32(info.nodeIndex);
            stream.WriteInt32(info.flags);
        }
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close HANIM chunk");
}
