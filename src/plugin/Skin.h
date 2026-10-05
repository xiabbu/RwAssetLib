/**
 * plugin/Skin.h - RpSkin geometry extension (rwID_SKINPLUGIN, 0x116).
 *
 * Stream layout (RenderWare rpskin.c, SkinGeometryWrite,
 * plus RenderWare bsplit.c for the split block):
 *
 *   RwUInt32 skinInfo                       // packed counters, see below
 *   RwUInt8  usedBoneList[numUsedBones]
 *   RwUInt32 matrixIndices[numVertices]     // 4 packed bone indices per vertex
 *   RwReal   matrixWeights[numVertices * 4]
 *   RwMatrix invBoneToSkinMat[numBones]     // 64 bytes each (pads included)
 *   RwInt32  boneLimit
 *   RwInt32  numMeshes
 *   RwInt32  numRLE
 *   if numMeshes > 0:
 *     RwUInt8 matrixRemapIndices[numBones]
 *     RwUInt8 meshRLECount[numMeshes * 2]
 *     RwUInt8 meshRLE[numRLE * 2]
 *
 * skinInfo packing (SkinGeometryWrite):
 *   bits  0..7  numBones
 *   bits  8..15 numUsedBones
 *   bits 16..23 maxWeights
 *
 * Note on index space: DBO stores *global* bone indices (indices into the HAnim
 * hierarchy's nodeInfos array) directly in matrixIndices; usedBoneList is only
 * the set of bones this geometry references, not a remap table.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"

#include <vector>

struct DffSkin
{
    RwInt32 numBones = 0;
    RwInt32 numUsedBones = 0;
    RwInt32 maxWeights = 0;

    std::vector<RwUInt8>         usedBoneList;
    std::vector<RwUInt32>        vertexBoneIndices;   ///< 4 x uint8 packed per vertex
    std::vector<RwMatrixWeights> vertexBoneWeights;
    std::vector<RwMatrix>        invBoneMatrices;

    // Split (bone-limit) block
    bool                 hasSplitData = true;
    RwInt32              splitBoneLimit = 0;
    RwInt32              splitNumMeshes = 0;
    RwInt32              splitNumRLE = 0;
    std::vector<RwUInt8> splitMatrixRemapIndices;
    std::vector<RwUInt8> splitMeshRLECount;
    std::vector<RwUInt8> splitMeshRLE;
    /// Bytes past the modelled payload; empty for every file in the DBO corpus.
    std::vector<RwUInt8> trailing;

    RwUInt32    chunkVersion = 0;
    RwChunkSpan span;

    static int DecodeBoneIndex(RwUInt32 packed, int slot)
    {
        return static_cast<int>((packed >> (slot * 8)) & 0xFFu);
    }

    static RwUInt32 EncodeBoneIndices(const int idx[4])
    {
        RwUInt32 packed = 0;
        for (int s = 0; s < 4; ++s) {
            packed |= (static_cast<RwUInt32>(idx[s] & 0xFF) << (s * 8));
        }
        return packed;
    }
};

bool ReadSkin(RwStream& stream,
              DffDiagnostics& diag,
              DffSkin& out,
              RwInt32 numVertices,
              RwUInt32 payloadSize,
              RwUInt32 chunkVersion);

bool WriteSkin(RwStream& stream,
               DffDiagnostics& diag,
               const DffSkin& skin,
               RwInt32 numVertices,
               RwUInt32 version);
