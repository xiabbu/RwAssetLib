/**
 * plugin/Skin.cpp - RpSkin geometry extension read/write.
 */

#include "plugin/Skin.h"

bool ReadSkin(RwStream& stream,
              DffDiagnostics& diag,
              DffSkin& out,
              RwInt32 numVertices,
              RwUInt32 payloadSize,
              RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_SKINPLUGIN);

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    out = DffSkin();
    out.chunkVersion = chunkVersion;

    auto bail = [&](const char* msg) {
        stream.Seek(endPos);
        return diag.Fail(startPos, msg);
    };

    RwUInt32 skinInfo = 0;
    if (!stream.ReadUInt32(&skinInfo)) {
        return bail("truncated skin info word");
    }

    out.numBones     = static_cast<RwInt32>((skinInfo >> 0) & 0xFF);
    out.numUsedBones = static_cast<RwInt32>((skinInfo >> 8) & 0xFF);
    out.maxWeights   = static_cast<RwInt32>((skinInfo >> 16) & 0xFF);

    if (out.numUsedBones > 0) {
        out.usedBoneList.resize(static_cast<size_t>(out.numUsedBones));
        if (!stream.Read(out.usedBoneList.data(), static_cast<size_t>(out.numUsedBones))) {
            return bail("truncated skin usedBoneList");
        }
    }

    if (numVertices < 0) {
        return bail("negative vertex count for skin");
    }

    out.vertexBoneIndices.resize(static_cast<size_t>(numVertices));
    for (RwInt32 i = 0; i < numVertices; ++i) {
        if (!stream.ReadUInt32(&out.vertexBoneIndices[static_cast<size_t>(i)])) {
            return bail("truncated skin matrixIndices");
        }
    }

    out.vertexBoneWeights.resize(static_cast<size_t>(numVertices));
    if (numVertices > 0 &&
        !stream.ReadReals(reinterpret_cast<RwReal*>(out.vertexBoneWeights.data()),
                          static_cast<size_t>(numVertices) * 4)) {
        return bail("truncated skin matrixWeights");
    }

    out.invBoneMatrices.resize(static_cast<size_t>(out.numBones));
    for (RwInt32 i = 0; i < out.numBones; ++i) {
        if (!stream.ReadMatrix(&out.invBoneMatrices[static_cast<size_t>(i)])) {
            return bail("truncated skin inverse bone matrices");
        }
    }

    // ---- split (bone limit) block -----------------------------------------
    const RwUInt32 afterMatrices = stream.GetPosition();
    if (afterMatrices + sizeof(RwInt32) * 3 <= endPos) {
        out.hasSplitData = true;
        if (!stream.ReadInt32(&out.splitBoneLimit) ||
            !stream.ReadInt32(&out.splitNumMeshes) ||
            !stream.ReadInt32(&out.splitNumRLE)) {
            return bail("truncated skin split header");
        }

        if (out.splitNumMeshes > 0) {
            // SkinSplitDataSize(): numBones + numMeshes*2 + numRLE*2 bytes.
            const size_t remapBytes = static_cast<size_t>(out.numBones);
            const size_t rleCountBytes = static_cast<size_t>(out.splitNumMeshes) * 2;
            const size_t rleBytes = static_cast<size_t>(out.splitNumRLE) * 2;
            const size_t total = remapBytes + rleCountBytes + rleBytes;

            if (stream.GetPosition() + total > endPos) {
                return bail("skin split data exceeds chunk payload");
            }

            if (remapBytes > 0) {
                out.splitMatrixRemapIndices.resize(remapBytes);
                if (!stream.Read(out.splitMatrixRemapIndices.data(), remapBytes)) {
                    return bail("truncated skin matrixRemapIndices");
                }
            }
            if (rleCountBytes > 0) {
                out.splitMeshRLECount.resize(rleCountBytes);
                if (!stream.Read(out.splitMeshRLECount.data(), rleCountBytes)) {
                    return bail("truncated skin meshRLECount");
                }
            }
            if (rleBytes > 0) {
                out.splitMeshRLE.resize(rleBytes);
                if (!stream.Read(out.splitMeshRLE.data(), rleBytes)) {
                    return bail("truncated skin meshRLE");
                }
            }
        }
    } else {
        out.hasSplitData = false;
    }

    const RwUInt32 afterSplit = stream.GetPosition();
    if (afterSplit < endPos) {
        out.trailing.resize(endPos - afterSplit);
        if (!stream.Read(out.trailing.data(), out.trailing.size())) {
            return bail("truncated skin trailing data");
        }
    } else if (afterSplit > endPos) {
        return bail("skin payload overran its declared length");
    }

    if (!out.span.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    stream.Seek(endPos);
    return true;
}

bool WriteSkin(RwStream& stream,
               DffDiagnostics& diag,
               const DffSkin& skin,
               RwInt32 numVertices,
               RwUInt32 version)
{
    DffScope scope(diag, rwID_SKINPLUGIN);

    if (static_cast<RwInt32>(skin.vertexBoneIndices.size()) < numVertices ||
        static_cast<RwInt32>(skin.vertexBoneWeights.size()) < numVertices) {
        return diag.Fail(stream.GetPosition(),
                         "skin per-vertex arrays shorter than the geometry",
                         std::to_string(numVertices),
                         std::to_string(skin.vertexBoneIndices.size()));
    }
    if (static_cast<RwInt32>(skin.invBoneMatrices.size()) < skin.numBones) {
        return diag.Fail(stream.GetPosition(),
                         "skin inverse bone matrix array shorter than numBones",
                         std::to_string(skin.numBones),
                         std::to_string(skin.invBoneMatrices.size()));
    }

    const RwUInt32 chunkVersion = skin.chunkVersion ? skin.chunkVersion : version;
    ChunkWriter writer(stream, chunkVersion);

    if (!writer.Begin(rwID_SKINPLUGIN, skin.span, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin SKIN chunk");
    }

    const RwUInt32 skinInfo =
        ((static_cast<RwUInt32>(skin.maxWeights) << 16) & 0x00FF0000u) |
        ((static_cast<RwUInt32>(skin.numUsedBones) << 8) & 0x0000FF00u) |
        ((static_cast<RwUInt32>(skin.numBones) << 0) & 0x000000FFu);
    stream.WriteUInt32(skinInfo);

    if (skin.numUsedBones > 0) {
        if (static_cast<RwInt32>(skin.usedBoneList.size()) < skin.numUsedBones) {
            return diag.Fail(stream.GetPosition(), "skin usedBoneList shorter than numUsedBones");
        }
        stream.Write(skin.usedBoneList.data(), static_cast<size_t>(skin.numUsedBones));
    }

    for (RwInt32 i = 0; i < numVertices; ++i) {
        stream.WriteUInt32(skin.vertexBoneIndices[static_cast<size_t>(i)]);
    }
    if (numVertices > 0) {
        stream.WriteReals(reinterpret_cast<const RwReal*>(skin.vertexBoneWeights.data()),
                          static_cast<size_t>(numVertices) * 4);
    }

    for (RwInt32 i = 0; i < skin.numBones; ++i) {
        stream.WriteMatrix(skin.invBoneMatrices[static_cast<size_t>(i)]);
    }

    if (skin.hasSplitData) {
        stream.WriteInt32(skin.splitBoneLimit);
        stream.WriteInt32(skin.splitNumMeshes);
        stream.WriteInt32(skin.splitNumRLE);

        if (skin.splitNumMeshes > 0) {
            if (skin.splitMatrixRemapIndices.size() != static_cast<size_t>(skin.numBones) ||
                skin.splitMeshRLECount.size() != static_cast<size_t>(skin.splitNumMeshes) * 2 ||
                skin.splitMeshRLE.size() != static_cast<size_t>(skin.splitNumRLE) * 2) {
                return diag.Fail(stream.GetPosition(), "Skin split remap/RLE counts disagree with arrays");
            }
            stream.Write(skin.splitMatrixRemapIndices.data(), skin.splitMatrixRemapIndices.size());
            stream.Write(skin.splitMeshRLECount.data(), skin.splitMeshRLECount.size());
            stream.Write(skin.splitMeshRLE.data(), skin.splitMeshRLE.size());
        }
    }

    if (!skin.trailing.empty()) {
        stream.Write(skin.trailing.data(), skin.trailing.size());
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close SKIN chunk");
}
