#pragma once

#include "world/Clump.h"

struct DffSkinVertexBuffer
{
    std::vector<RwV3d> positions;
    std::vector<RwV3d> normals;
};

enum class DffSkinInverseMode
{
    Generic,
    Orthonormal
};

// Reconstructs the D3D9 SSE Skin buffer from binding and current frame pose.
bool BuildSkinVertexBuffer(const DffClump& clump, const DffGeometry& geom,
                          RwInt32 atomicFrameIndex, DffSkinInverseMode inverseMode,
                          DffSkinVertexBuffer& out,
                          std::string& error);
