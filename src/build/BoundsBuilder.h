/**
 * build/BoundsBuilder.h - morph target bounding sphere from its vertices.
 */
#pragma once

#include "core/RwTypes.h"

#include <vector>

void BuildBoundingSphere(const std::vector<RwV3d>& positions, RwSphere& out);
