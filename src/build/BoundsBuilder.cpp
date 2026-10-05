/**
 * build/BoundsBuilder.cpp - RpMorphTargetCalcBoundingSphere (RenderWare bageomet.c).
 */

#include "build/BoundsBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

void BuildBoundingSphere(const std::vector<RwV3d>& positions, RwSphere& out)
{
    out.center = RwV3d(0, 0, 0);
    out.radius = 0.0f;
    if (positions.empty()) {
        return;
    }

    // RpMorphTargetCalcBoundingSphere: centre of the bounding box, then the
    // largest distance from it.
    RwV3d lo = positions[0];
    RwV3d hi = positions[0];
    for (const auto& p : positions) {
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
    }
    out.center = RwV3d((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);

    // The exporter ran inside 3ds Max, whose x87 control word is 53-bit: the
    // difference, dot product and running maximum stay in double registers and
    // are only rounded to RwReal when passed to rwSqrt.
    double maxDistance = 0.0;
    for (const auto& p : positions) {
        const double dx = static_cast<double>(p.x) - out.center.x;
        const double dy = static_cast<double>(p.y) - out.center.y;
        const double dz = static_cast<double>(p.z) - out.center.z;
        const double distanceSquared = dx * dx + dy * dy + dz * dz;
        if (distanceSquared > maxDistance) {
            maxDistance = distanceSquared;
        }
    }
    const RwReal maxDistSq = static_cast<RwReal>(maxDistance);
    if (maxDistSq > 0.0f) {
        // bavector.c _rwSqrt uses an 11-bit mantissa table, not CRT sqrtf.
        static const auto sqrtTable = [] {
            std::array<RwUInt32, 4096> table{};
            for (RwUInt32 i = 0; i < table.size(); ++i) {
                RwUInt32 bits = 0x3F800000u + (i << 12);
                RwReal input;
                std::memcpy(&input, &bits, sizeof(input));
                const RwReal root = static_cast<RwReal>(std::sqrt(static_cast<double>(input)));
                std::memcpy(&bits, &root, sizeof(bits));
                table[(i + 2048) % 4096] = bits - 0x20000000u + (i < 2048 ? 0x00400000u : 0);
            }
            return table;
        }();
        RwUInt32 bits;
        std::memcpy(&bits, &maxDistSq, sizeof(bits));
        bits += 1u << 11;
        bits = sqrtTable[(bits & 0x00FFFFFFu) >> 12] + ((0x7F800000u & bits) >> 1);
        RwReal root;
        std::memcpy(&root, &bits, sizeof(root));
        out.radius = root * 1.001f;
    }
}
