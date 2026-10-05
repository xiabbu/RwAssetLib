#include "build/SkinBuilder.h"
#include "dff/DffDocument.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>

int CmdToonSkinCheck(const std::string& path, int geometryIndex)
{
    DffDocument doc;
    if (!doc.Load(std::filesystem::path(path).wstring().c_str())) {
        std::cerr << path << ": " << doc.GetLastError() << "\n";
        return 2;
    }
    const auto& clump = *doc.GetClump();
    if (geometryIndex < 0 || static_cast<size_t>(geometryIndex) >= clump.geometries.size()) {
        std::cerr << "geometry index is outside the input geometry list\n";
        return 2;
    }
    const auto& geom = *clump.geometries[geometryIndex];
    if (!geom.skin || !geom.toon || geom.morphTargets.size() != 1 || !geom.HasNormals()) {
        std::cerr << "toon-skin-check requires a single-morph Skin/Toon geometry with normals\n";
        return 2;
    }
    const auto atomic = std::find_if(clump.atomics.begin(), clump.atomics.end(),
        [&](const DffAtomic& a) { return a.geomIndex == geometryIndex; });
    if (atomic == clump.atomics.end()) {
        std::cerr << "geometry has no atomic frame\n";
        return 2;
    }
    const auto& toon = *geom.toon;
    bool matched = false;
    for (const auto mode : {DffSkinInverseMode::Generic, DffSkinInverseMode::Orthonormal}) {
        DffSkinVertexBuffer buffer;
        std::string error;
        if (!BuildSkinVertexBuffer(clump, geom, atomic->frameIndex, mode, buffer, error)) {
            std::cerr << path << ": " << error << "\n";
            return 2;
        }
        size_t faceMatches = 0, normalMatches = 0;
        for (size_t vertex = 0; vertex < buffer.normals.size(); ++vertex) {
            normalMatches += std::memcmp(&buffer.normals[vertex], &toon.extrusionNormals[vertex], sizeof(RwV3d)) == 0;
        }
        for (size_t face = 0; face < toon.triangles.size(); ++face) {
            const auto* v = toon.triangles[face].vertIndex;
            const auto& a = buffer.positions[v[0]];
            const auto& b = buffer.positions[v[1]];
            const auto& c = buffer.positions[v[2]];
            const RwV3d e0(b.x - a.x, b.y - a.y, b.z - a.z);
            const RwV3d e1(c.x - a.x, c.y - a.y, c.z - a.z);
            const RwV3d normal(e0.y * e1.z - e0.z * e1.y,
                               e0.z * e1.x - e0.x * e1.z,
                               e0.x * e1.y - e0.y * e1.x);
            faceMatches += std::memcmp(&normal, &toon.faceNormals[face], sizeof(RwV3d)) == 0;
        }
        std::cout << "inverse=" << (mode == DffSkinInverseMode::Generic ? "generic" : "orthonormal")
                  << " faceNormals=" << faceMatches << "/" << toon.faceNormals.size()
                  << " extrusionNormals=" << normalMatches << "/" << toon.extrusionNormals.size() << "\n";
        matched |= faceMatches == toon.faceNormals.size() && normalMatches == toon.extrusionNormals.size();
    }
    return matched ? 0 : 1;
}
