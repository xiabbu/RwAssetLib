/**
 * build/BinMeshBuilder.cpp
 */

#include "build/BinMeshBuilder.h"
#include "build/RwTriStrip.h"

#include <algorithm>
#include <cstdlib>
#include <map>

std::vector<size_t> BuildRwTriangleOrder(const DffGeometry& geom, const std::vector<RwInt32>& materialOrder)
{
    struct SortTriangle
    {
        size_t index;
        RwInt32 materialRank;
    };
    std::vector<SortTriangle> sorted;
    sorted.reserve(geom.triangles.size());
    for (size_t i = 0; i < geom.triangles.size(); ++i) {
        const auto material = geom.triangles[i].matIndex;
        RwInt32 rank = material;
        if (!materialOrder.empty()) {
            const auto it = std::find(materialOrder.begin(), materialOrder.end(), material);
            // Newly assigned material slots follow the imported draw order.
            rank = it == materialOrder.end() ? static_cast<RwInt32>(materialOrder.size()) + material
                                            : static_cast<RwInt32>(it - materialOrder.begin());
        }
        sorted.push_back({i, rank});
    }
    // RW uses CRT qsort, whose equal-key swaps affect strip and Toon face order.
    if (sorted.empty()) return {};
    std::qsort(sorted.data(), sorted.size(), sizeof(SortTriangle), [](const void* a, const void* b) {
        return static_cast<const SortTriangle*>(a)->materialRank - static_cast<const SortTriangle*>(b)->materialRank;
    });
    std::vector<size_t> result;
    result.reserve(sorted.size());
    for (const auto& triangle : sorted) result.push_back(triangle.index);
    return result;
}

bool BuildBinMesh(const DffGeometry& geom, DffBinMesh& out, const std::vector<RwInt32>& materialOrder)
{
    const RwUInt32 preservedVersion = out.chunkVersion;
    out = DffBinMesh();
    out.chunkVersion = preservedVersion;
    out.flags = (geom.format & rpGEOMETRYTRISTRIP) ? rpMESHHEADERTRISTRIP : 0;

    if (out.flags == 0) {
        std::map<RwInt32, std::vector<RwUInt32>> byMaterial;
        for (const auto& triangle : geom.triangles) {
            auto& indices = byMaterial[triangle.matIndex];
            indices.insert(indices.end(), std::begin(triangle.vertIndex), std::end(triangle.vertIndex));
        }
        for (auto& entry : byMaterial) {
            DffMesh mesh;
            mesh.matIndex = entry.first;
            mesh.indices = std::move(entry.second);
            out.meshes.push_back(std::move(mesh));
        }
        return true;
    }

    // RpBuildMeshGenerateTriStrip removes index-degenerate faces before qsort.
    DffGeometry stripGeometry;
    for (const auto& triangle : geom.triangles) {
        const auto* v = triangle.vertIndex;
        if (v[0] != v[1] && v[1] != v[2] && v[2] != v[0]) {
            stripGeometry.triangles.push_back(triangle);
        }
    }
    const auto order = BuildRwTriangleOrder(stripGeometry, materialOrder);
    for (size_t start = 0; start < order.size();) {
        const auto material = stripGeometry.triangles[order[start]].matIndex;
        std::vector<const DffTriangle*> triangles;
        size_t end = start;
        while (end < order.size() && stripGeometry.triangles[order[end]].matIndex == material) {
            triangles.push_back(&stripGeometry.triangles[order[end]]);
            ++end;
        }
        DffMesh mesh;
        mesh.matIndex = material;
        BuildRwTunnelStrip(triangles, mesh.indices);
        out.meshes.push_back(std::move(mesh));
        start = end;
    }
    return true;
}
