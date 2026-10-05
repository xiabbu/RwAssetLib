/**
 * plugin/BinMesh.cpp - binary mesh read/write.
 */

#include "plugin/BinMesh.h"

bool ReadBinMesh(RwStream& stream, DffDiagnostics& diag, DffBinMesh& out, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_BINMESHPLUGIN);

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    out = DffBinMesh();
    out.chunkVersion = chunkVersion;

    auto bail = [&](const char* msg) {
        stream.Seek(endPos);
        return diag.Fail(startPos, msg);
    };

    RwUInt32 numMeshes = 0;
    RwUInt32 totalIndices = 0;
    if (!stream.ReadUInt32(&out.flags) ||
        !stream.ReadUInt32(&numMeshes) ||
        !stream.ReadUInt32(&totalIndices)) {
        return bail("truncated RpMeshHeader");
    }

    out.meshes.resize(numMeshes);
    for (RwUInt32 i = 0; i < numMeshes; ++i) {
        RwUInt32 numIndices = 0;
        if (!stream.ReadUInt32(&numIndices) || !stream.ReadInt32(&out.meshes[i].matIndex)) {
            return bail("truncated binary mesh header");
        }

        // Guard against a corrupt count before allocating.
        if (stream.GetPosition() + static_cast<RwUInt32>(numIndices) * 4u > endPos) {
            return bail("binary mesh index count exceeds chunk payload");
        }

        out.meshes[i].indices.resize(numIndices);
        for (RwUInt32 j = 0; j < numIndices; ++j) {
            if (!stream.ReadUInt32(&out.meshes[i].indices[j])) {
                return bail("truncated binary mesh index array");
            }
        }
    }

    if (out.TotalIndices() != totalIndices) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "RpMeshHeader totalIndicesInMesh disagrees with the mesh index arrays",
                         std::to_string(totalIndices),
                         std::to_string(out.TotalIndices()));
    }

    if (!out.span.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    stream.Seek(endPos);
    return true;
}

bool WriteBinMesh(RwStream& stream, DffDiagnostics& diag, const DffBinMesh& binMesh, RwUInt32 version)
{
    DffScope scope(diag, rwID_BINMESHPLUGIN);

    const RwUInt32 chunkVersion = binMesh.chunkVersion ? binMesh.chunkVersion : version;
    ChunkWriter writer(stream, chunkVersion);

    if (!writer.Begin(rwID_BINMESHPLUGIN, binMesh.span, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin BINMESH chunk");
    }

    stream.WriteUInt32(binMesh.flags);
    stream.WriteUInt32(static_cast<RwUInt32>(binMesh.meshes.size()));
    stream.WriteUInt32(binMesh.TotalIndices());

    for (const auto& mesh : binMesh.meshes) {
        stream.WriteUInt32(static_cast<RwUInt32>(mesh.indices.size()));
        stream.WriteInt32(mesh.matIndex);
        for (RwUInt32 idx : mesh.indices) {
            stream.WriteUInt32(idx);
        }
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close BINMESH chunk");
}
