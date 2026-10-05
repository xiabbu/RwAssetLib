/**
 * plugin/BinMesh.h - RpMeshHeader / binary mesh (rwID_BINMESHPLUGIN, 0x50E).
 *
 * Stream layout (RenderWare bamesh.c):
 *
 *   RwUInt32 flags                  // RpMeshHeaderFlags (prim type + UNINDEXED)
 *   RwUInt32 numMeshes
 *   RwUInt32 totalIndicesInMesh
 *   per mesh:
 *     RwUInt32 numIndices
 *     RwInt32  materialIndex
 *     RwUInt32 indices[numIndices]
 *
 * This is the render-ready index buffer; it is derived data (the triangle list
 * in the geometry STRUCT is authoritative) and must be regenerated whenever the
 * mesh topology or material assignment changes.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"

#include <vector>

struct DffMesh
{
    RwInt32               matIndex = 0;
    std::vector<RwUInt32> indices;
};

struct DffBinMesh
{
    RwUInt32             flags = 0;
    std::vector<DffMesh> meshes;

    RwUInt32    chunkVersion = 0;
    RwChunkSpan span;

    RwUInt32 TotalIndices() const
    {
        RwUInt32 total = 0;
        for (const auto& m : meshes) {
            total += static_cast<RwUInt32>(m.indices.size());
        }
        return total;
    }
};

bool ReadBinMesh(RwStream& stream, DffDiagnostics& diag, DffBinMesh& out, RwUInt32 payloadSize, RwUInt32 chunkVersion);
bool WriteBinMesh(RwStream& stream, DffDiagnostics& diag, const DffBinMesh& binMesh, RwUInt32 version);
