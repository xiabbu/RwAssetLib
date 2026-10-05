/**
 * build/ToonBuilder.cpp - RtToon edge generation (RenderWare rttoon.cpp).
 */

#include "build/ToonBuilder.h"
#include "build/BinMeshBuilder.h"
#include "build/SkinBuilder.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <map>

namespace
{

RwV3d Normalize(const RwV3d& v)
{
    // bavector.c InvSqrtTableCreate / _rwInvSqrt, 11-bit mantissa lookup.
    static const auto table = [] {
        std::array<RwUInt32, 4096> result{};
        for (RwUInt32 i = 0; i < 4096; ++i) {
            RwUInt32 bits = 0x3F800000u + (i << 12);
            float input;
            std::memcpy(&input, &bits, sizeof(input));
            const float reciprocal = static_cast<float>(1.0 / std::sqrt(static_cast<double>(input)));
            std::memcpy(&bits, &reciprocal, sizeof(bits));
            result[(i + 2048) % 4096] = bits - 0x20000000u + (i >= 2048 ? 0x00400000u : 0);
        }
        return result;
    }();
    const float lengthSquared = static_cast<float>(static_cast<double>(v.x) * v.x +
        static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z);
    RwUInt32 bits;
    std::memcpy(&bits, &lengthSquared, sizeof(bits));
    if (bits != 0) {
        bits += 1u << 11;
        bits = table[(bits & 0x00FFFFFFu) >> 12] + ((0x7F800000u & ~bits) >> 1);
    }
    float reciprocal;
    std::memcpy(&reciprocal, &bits, sizeof(reciprocal));
    return RwV3d(v.x * reciprocal, v.y * reciprocal, v.z * reciprocal);
}

void WriteFixedName(char* dst, size_t capacity, const std::string& name)
{
    assert(name.size() < capacity);
    std::memcpy(dst, name.c_str(), name.size() + 1);
}

}  // namespace

bool ToonBuilder::Build(const DffGeometry& geom, DffToonGeo& out,
                        const DffSkinVertexBuffer* vertexBuffer) const
{
    const RwUInt32 version = out.chunkVersion;
    out = DffToonGeo();
    out.chunkVersion = version;
    if (geom.morphTargets.empty() || geom.morphTargets[0].vertices.empty()) {
        return false;
    }
    const auto& vertices = geom.morphTargets[0].vertices;
    const auto& normals = geom.morphTargets[0].normals;
    const size_t numVertices = vertices.size();
    const size_t numTriangles = geom.triangles.size();
    if (!m_vertexThicknesses.empty() && m_vertexThicknesses.size() != numVertices) return false;

    WriteFixedName(out.defaultPaintID.name, sizeof(out.defaultPaintID.name), m_defaultPaintName);
    if (!m_defaultPaintPadding.empty()) {
        const size_t offset = m_defaultPaintName.size() + 1;
        assert(offset + m_defaultPaintPadding.size() == sizeof(out.defaultPaintID.name));
        std::memcpy(out.defaultPaintID.name + offset, m_defaultPaintPadding.data(), m_defaultPaintPadding.size());
    }
    out.inkIDList.resize(2);
    // The exporter renames an existing ink with strcpy; the old suffix remains.
    WriteFixedName(out.inkIDList[0].name, 32, "Default Silhouette Ink Name");
    WriteFixedName(out.inkIDList[0].name, 32, m_defaultInkName);
    WriteFixedName(out.inkIDList[1].name, 32, m_creaseInkName);
    out.vertexThicknesses.assign(numVertices, 1.0f);
    if (!m_vertexThicknesses.empty()) out.vertexThicknesses = m_vertexThicknesses;
    out.extrusionNormals.resize(numVertices);
    out.triangles.resize(numTriangles);
    out.faceNormals.resize(numTriangles);

    // RemapTriangleIndices uses exact position equality, without an epsilon.
    std::vector<RwUInt16> remap(numVertices);
    std::multimap<float, RwUInt16> uniqueVertices;
    for (size_t i = 0; i < numVertices; ++i) {
        const auto& p = vertices[i];
        const float key = p.x + p.y + p.z;
        const auto range = uniqueVertices.equal_range(key);
        remap[i] = static_cast<RwUInt16>(i);
        for (auto it = range.first; it != range.second; ++it) {
            const auto& other = vertices[it->second];
            if (p.x == other.x && p.y == other.y && p.z == other.z) {
                remap[i] = it->second;
                break;
            }
        }
        if (remap[i] == i) uniqueVertices.emplace(key, static_cast<RwUInt16>(i));
    }
    std::vector<RpToonTriangle> welded(numTriangles);
    std::vector<std::vector<RwUInt16>> incidentFaces(numVertices);
    for (size_t i = 0; i < numTriangles; ++i) {
        for (int k = 0; k < 3; ++k) {
            const auto index = geom.triangles[i].vertIndex[k];
            if (index >= numVertices) return false;
            out.triangles[i].vertIndex[k] = index;
            welded[i].vertIndex[k] = remap[index];
        }
        const auto* v = welded[i].vertIndex;
        if (v[0] != v[1] && v[0] != v[2] && v[1] != v[2]) {
            for (int k = 0; k < 3; ++k) incidentFaces[v[k]].push_back(static_cast<RwUInt16>(i));
        }
        const auto& faceVertices = vertexBuffer ? vertexBuffer->positions : vertices;
        const auto* faceIndices = vertexBuffer ? geom.triangles[i].vertIndex : v;
        const auto& a = faceVertices[faceIndices[0]];
        const auto& b = faceVertices[faceIndices[1]];
        const auto& c = faceVertices[faceIndices[2]];
        if (m_rawFaceNormals) {
            // brutesilhouette.c recomputes skinned face normals as float cross products.
            const RwV3d e0(b.x - a.x, b.y - a.y, b.z - a.z);
            const RwV3d e1(c.x - a.x, c.y - a.y, c.z - a.z);
            out.faceNormals[i] = RwV3d(
                e0.y * e1.z - e0.z * e1.y,
                e0.z * e1.x - e0.x * e1.z,
                e0.x * e1.y - e0.y * e1.x);
        } else {
            // DBO exporter x87 evaluation: e1.x/y spill to float; the other
            // differences remain in registers until each cross-product store.
            const double e0x = static_cast<double>(b.x) - a.x;
            const double e0y = static_cast<double>(b.y) - a.y;
            const double e0z = static_cast<double>(b.z) - a.z;
            const float e1x = c.x - a.x;
            const float e1y = c.y - a.y;
            const double e1z = static_cast<double>(c.z) - a.z;
            const RwV3d faceNormal(
                static_cast<float>(e0y * e1z - e0z * e1y),
                static_cast<float>(e0z * e1x - e0x * e1z),
                static_cast<float>(e0x * e1y - e0y * e1x));
            out.faceNormals[i] = Normalize(faceNormal);
        }
    }

    if (vertexBuffer) {
        out.extrusionNormals = vertexBuffer->normals;
    } else if (m_vertexExtrusionNormals) {
        // d3d9paint.c copies vertex-buffer normals over extrusionVertexNormals.
        if (normals.size() != numVertices) return false;
        out.extrusionNormals = normals;
    } else if (!normals.empty()) {
        assert(normals.size() == numVertices);
        std::vector<std::vector<size_t>> uniqueNormals(numVertices);
        for (size_t i = 0; i < numVertices; ++i) {
            auto& list = uniqueNormals[remap[i]];
            const auto& n = normals[i];
            bool unique = true;
            for (size_t j : list) {
                if (n.x == normals[j].x && n.y == normals[j].y && n.z == normals[j].z) {
                    unique = false;
                    break;
                }
            }
            if (unique) {
                list.push_back(i);
                auto& sum = out.extrusionNormals[remap[i]];
                sum.x += n.x;
                sum.y += n.y;
                sum.z += n.z;
            }
        }
        for (size_t i = 0; i < numVertices; ++i) {
            auto& n = out.extrusionNormals[i];
            // This AND is the original RtToon rule, including axis-aligned normals.
            n = n.x != 0.0f && n.y != 0.0f && n.z != 0.0f ? Normalize(n) : normals[i];
        }
    } else {
        for (size_t i = 0; i < numVertices; ++i) {
            RwV3d sum;
            for (auto face : incidentFaces[i]) {
                const auto& n = out.faceNormals[face];
                sum.x += n.x;
                sum.y += n.y;
                sum.z += n.z;
            }
            out.extrusionNormals[i] = Normalize(sum);
        }
    }
    if (!vertexBuffer && !m_vertexExtrusionNormals) {
        for (size_t i = 0; i < numVertices; ++i) out.extrusionNormals[i] = out.extrusionNormals[remap[i]];
    }

    std::map<std::pair<RwUInt16, RwUInt16>, size_t> edgeByVertices;
    for (size_t face = 0; face < numTriangles; ++face) {
        for (int side = 0; side < 3; ++side) {
            const auto a = welded[face].vertIndex[side];
            const auto b = welded[face].vertIndex[(side + 1) % 3];
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            if (a == b || edgeByVertices.find(key) != edgeByVertices.end()) continue;
            RpToonEdge edge;
            edge.v[0] = a;
            edge.v[1] = b;
            edge.face[0] = static_cast<RwUInt16>(face);
            edge.face[1] = 0xFFFF;
            for (auto neighbour : incidentFaces[a]) {
                const auto* v = welded[neighbour].vertIndex;
                if (neighbour != face && (v[0] == b || v[1] == b || v[2] == b)) {
                    edge.face[1] = neighbour;
                    break;
                }
            }
            edgeByVertices.emplace(key, out.edges.size());
            out.edges.push_back(edge);
        }
    }

    // Explicit editing options; the DBO default does not generate crease edges
    // or infer artist-authored thickness from curvature.
    if (m_creaseAngleDegrees < 180.0f) {
        const float threshold = std::cos(m_creaseAngleDegrees * 3.14159265358979323846f / 180.0f);
        auto crease = [&](const RpToonEdge& e) {
            if (e.face[1] == 0xFFFF) return false;
            const auto& a = out.faceNormals[e.face[0]];
            const auto& b = out.faceNormals[e.face[1]];
            return a.x * b.x + a.y * b.y + a.z * b.z < threshold;
        };
        const auto end = std::stable_partition(out.edges.begin(), out.edges.end(), crease);
        out.numCreaseEdges = static_cast<RwInt32>(end - out.edges.begin());
    }
    if (m_thicknessScale != 0.0f) {
        for (size_t v = 0; v < numVertices; ++v) {
            float curvature = 0.0f;
            const auto& faces = incidentFaces[remap[v]];
            for (size_t i = 0; i < faces.size(); ++i) {
                for (size_t j = i + 1; j < faces.size(); ++j) {
                    const auto& a = out.faceNormals[faces[i]];
                    const auto& b = out.faceNormals[faces[j]];
                    curvature = std::max(curvature, 1.0f - std::clamp(a.x * b.x + a.y * b.y + a.z * b.z, -1.0f, 1.0f));
                }
            }
            out.vertexThicknesses[v] = std::clamp(1.0f + curvature * m_thicknessScale, 0.25f, 4.0f);
        }
    }

    std::vector<std::vector<RwUInt16>> incidentEdges(numVertices);
    for (size_t i = 0; i < out.edges.size(); ++i) {
        for (auto v : out.edges[i].v) incidentEdges[v].push_back(static_cast<RwUInt16>(i));
    }
    for (size_t i = 0; i < out.edges.size(); ++i) {
        auto& e = out.edges[i];
        for (int face = 0; face < 2; ++face) {
            for (int vertex = 0; vertex < 2; ++vertex) {
                e.edgefv[face][vertex] = 0xFFFF;
                if (e.face[face] == 0xFFFF) continue;
                for (auto other : incidentEdges[e.v[vertex]]) {
                    const auto& candidate = out.edges[other];
                    if (other != i && (candidate.face[0] == e.face[face] || candidate.face[1] == e.face[face])) {
                        e.edgefv[face][vertex] = other;
                        break;
                    }
                }
            }
        }
    }
    out.edgeInkIDs.resize(out.edges.size());
    for (RwInt32 i = 0; i < out.numCreaseEdges; ++i) out.edgeInkIDs[i].inkId[1] = 1;
    // RtToonGeoOptimizeEdgeInkIDs uses CRT qsort even for equal ink IDs.
    struct EdgeRemap
    {
        RwUInt16 index;
        int key;
    };
    std::vector<EdgeRemap> edgeOrder;
    for (size_t i = 0; i < out.edges.size(); ++i) {
        const int ink = out.edgeInkIDs[i].inkId[1];
        edgeOrder.push_back({static_cast<RwUInt16>(i), ink == 0 ? 100000 : ink});
    }
    if (!edgeOrder.empty()) {
        std::qsort(edgeOrder.data(), edgeOrder.size(), sizeof(EdgeRemap), [](const void* a, const void* b) {
            return static_cast<const EdgeRemap*>(a)->key - static_cast<const EdgeRemap*>(b)->key;
        });
    }
    std::vector<RwUInt16> newEdge(out.edges.size());
    for (size_t i = 0; i < edgeOrder.size(); ++i) newEdge[edgeOrder[i].index] = static_cast<RwUInt16>(i);
    const auto edges = out.edges;
    const auto edgeInks = out.edgeInkIDs;
    for (size_t i = 0; i < edgeOrder.size(); ++i) {
        auto& edge = out.edges[i];
        edge = edges[edgeOrder[i].index];
        out.edgeInkIDs[i] = edgeInks[edgeOrder[i].index];
        for (auto& face : edge.edgefv) {
            for (auto& wing : face) {
                if (wing != 0xFFFF) wing = newEdge[wing];
            }
        }
    }
    // ToonGeoPostStripifyTriangleFixUp leaves edge order/wings intact and only
    // remaps face indices, triangles and face normals.
    const auto triangleOrder = BuildRwTriangleOrder(geom, m_materialOrder);
    std::vector<RwUInt16> newFace(numTriangles);
    const auto triangles = out.triangles;
    const auto faceNormals = out.faceNormals;
    for (size_t i = 0; i < numTriangles; ++i) {
        newFace[triangleOrder[i]] = static_cast<RwUInt16>(i);
        out.triangles[i] = triangles[triangleOrder[i]];
        out.faceNormals[i] = faceNormals[triangleOrder[i]];
    }
    for (auto& edge : out.edges) {
        for (auto& face : edge.face) {
            if (face != 0xFFFF) face = newFace[face];
        }
    }
    return true;
}
