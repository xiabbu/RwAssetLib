#pragma once

#include "world/Geometry.h"

// Input is one nonempty material group of nondegenerate triangles, in RW sort order.
void BuildRwTunnelStrip(const std::vector<const DffTriangle*>& triangles, std::vector<RwUInt32>& indices);
