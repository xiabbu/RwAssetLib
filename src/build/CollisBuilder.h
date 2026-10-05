/**
 * build/CollisBuilder.h - rebuilds the RpCollision tree from scene geometry.
 */
#pragma once

#include "world/Geometry.h"

// RpCollisionGeometryBuildData(geometry, NULL): triangles are not sorted, so the
// tree keeps its own map (every tree in the DBO corpus has rpCOLLTREE_USEMAP).
bool BuildCollisTree(const DffGeometry& geom, DffCollTree& out, std::string& error);
