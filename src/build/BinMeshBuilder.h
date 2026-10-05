/**
 * build/BinMeshBuilder.h - rebuilds mesh indices from scene geometry.
 */
#pragma once

#include "world/Geometry.h"

// Render order is a scene attribute: old raster/texture pointer order cannot be
// inferred from vertex data. An empty order uses material slot order.
std::vector<size_t> BuildRwTriangleOrder(const DffGeometry& geom, const std::vector<RwInt32>& materialOrder);
bool BuildBinMesh(const DffGeometry& geom, DffBinMesh& out, const std::vector<RwInt32>& materialOrder = {});
