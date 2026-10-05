/**
 * build/CollisBuilder.cpp - port of RenderWare ctbuild.c (_rpCollTreeBuild).
 */

#include "build/CollisBuilder.h"

#include <cfloat>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{

// RenderWare ctdata.h
constexpr RwUInt32 kCollTreeMaxDepth = 32;
constexpr RwUInt32 kCollSectorContentsMaxCount = 0xef;
constexpr RwUInt8  kCollSectorContentsSplit = 0xff;
constexpr RwUInt8  kCollSectorTypeNeg = 0x01;
constexpr RwUInt32 kCollTreeUseMap = 0x01;

// RenderWare ctbuild.c
constexpr RwUInt32 kMinPolygonsForSplit = 4;
constexpr RwUInt32 kClipVertexLeft = 1;
constexpr RwUInt32 kClipVertexRight = 2;
constexpr RwUInt32 kClipPolygonLeft = 1;
constexpr RwUInt32 kClipPolygonRight = 2;
constexpr RwInt32  kMaxClosestCheck = 50;
constexpr RwReal   kRangeEps = 0.0001f;

struct BuildVertex
{
    const RwV3d* pos;
    RwUInt32     flags;
    RwReal       dist;
};

struct BuildPoly
{
    const DffTriangle* poly;
    RwUInt32           flags;
};

struct BuildSector
{
    RwInt32  type = -1;   // -1 for a polygon leaf, otherwise the plane's coordinate byte offset
    RwUInt16 numPolygons = 0;
    RwUInt16 firstPolygon = 0;
    RwReal   leftValue = 0;
    RwReal   rightValue = 0;
    std::unique_ptr<BuildSector> left;
    std::unique_ptr<BuildSector> right;
};

struct BuildData
{
    RwUInt32           numVertices;
    BuildVertex*       vertices;
    BuildPoly*         polygons;
    const DffTriangle* origPolys;

    RwUInt32 bspDepth;
    RwBBox   bbox;
    RwUInt32 numPolygons;
    RwUInt32 polygonOffset;

    RwUInt32      numSortVerts;
    BuildVertex** sortVerts;
};

struct ClipStats
{
    RwInt32 nLeft;
    RwInt32 nRight;
    RwReal  leftValue;
    RwReal  rightValue;
};

// GETCOORD / SETCOORD address a component by byte offset, which is also the
// value streamed as the sector type (rpCOLLSECTOR_TYPE_X/Y/Z).
RwReal GetCoord(const RwV3d& v, RwInt32 plane)
{
    return (&v.x)[plane / static_cast<RwInt32>(sizeof(RwReal))];
}

void SetCoord(RwV3d& v, RwInt32 plane, RwReal value)
{
    (&v.x)[plane / static_cast<RwInt32>(sizeof(RwReal))] = value;
}

// RenderWare Windows rwplcore.h int32fromreal: fistp in truncate mode stores the integer
// indefinite for values it cannot represent.
RwInt32 Int32FromReal(RwReal x)
{
    if (!(x > -2147483648.0f && x < 2147483648.0f)) {
        return INT32_MIN;
    }
    return static_cast<RwInt32>(x);
}

void SetClipCodes(BuildData& data, RwInt32 plane, RwReal value)
{
    for (RwUInt32 iVert = 0; iVert < data.numSortVerts; ++iVert) {
        BuildVertex* vertex = data.sortVerts[iVert];
        const RwReal test = GetCoord(*vertex->pos, plane) - value;
        vertex->flags = 0;
        // A vertex on the plane is deliberately classified as both sides.
        if (test <= 0.0f) {
            vertex->flags |= kClipVertexLeft;
        }
        if (test >= 0.0f) {
            vertex->flags |= kClipVertexRight;
        }
    }
}

void GetClipStats(BuildData& data, RwInt32 plane, RwReal value, ClipStats& stats)
{
    BuildPoly* polygons = data.polygons + data.polygonOffset;
    RwInt32 nLeft = 0;
    RwInt32 nRight = 0;
    RwReal overlapLeft = 0.0f;
    RwReal overlapRight = 0.0f;

    for (RwInt32 numPolygons = static_cast<RwInt32>(data.numPolygons); --numPolygons >= 0; ++polygons) {
        const RwUInt16* vertIndex = polygons->poly->vertIndex;
        RwUInt32 clip = kClipVertexLeft | kClipVertexRight;
        RwReal distLeft = 0.0f;
        RwReal distRight = 0.0f;

        for (int iTriVert = 0; iTriVert < 3; ++iTriVert) {
            const BuildVertex& vertex = data.vertices[vertIndex[iTriVert]];
            const RwReal dist = GetCoord(*vertex.pos, plane) - value;
            const RwReal mdist = -dist;
            clip &= vertex.flags;
            if (mdist > distLeft) {
                distLeft = mdist;
            } else if (dist > distRight) {
                distRight = dist;
            }
        }

        switch (clip & (kClipVertexLeft | kClipVertexRight)) {
            case kClipVertexLeft:
                polygons->flags = kClipPolygonLeft;
                ++nLeft;
                break;
            case kClipVertexLeft | kClipVertexRight:
            case kClipVertexRight:
                polygons->flags = kClipPolygonRight;
                ++nRight;
                break;
            case 0:
                if (distRight > distLeft) {
                    ++nRight;
                    polygons->flags = kClipPolygonRight;
                    if (distLeft > overlapRight) {
                        overlapRight = distLeft;
                    }
                } else {
                    ++nLeft;
                    polygons->flags = kClipPolygonLeft;
                    if (distRight > overlapLeft) {
                        overlapLeft = distRight;
                    }
                }
                break;
        }
    }

    stats.nLeft = nLeft;
    stats.nRight = nRight;
    stats.leftValue = value + overlapLeft;
    stats.rightValue = value - overlapRight;
}

RwReal AverageTreeDepth(RwUInt32 numLeaves)
{
    RwInt32 depth = -1;
    for (RwUInt32 temp = numLeaves; temp; temp >>= 1) {
        ++depth;
    }
    return static_cast<RwReal>(depth + 2) -
           static_cast<RwReal>(1 << (depth + 1)) / static_cast<RwReal>(numLeaves);
}

RwReal DivisionScore(const BuildData& data, const ClipStats& stats, RwInt32 plane)
{
    const RwReal sectorExtent = GetCoord(data.bbox.sup, plane) - GetCoord(data.bbox.inf, plane);
    const RwReal leftExtent = stats.leftValue - GetCoord(data.bbox.inf, plane);
    const RwReal rightExtent = GetCoord(data.bbox.sup, plane) - stats.rightValue;
    return (leftExtent * AverageTreeDepth(static_cast<RwUInt32>(stats.nLeft)) +
            rightExtent * AverageTreeDepth(static_cast<RwUInt32>(stats.nRight))) / sectorExtent;
}

void SetSortVertices(BuildData& data)
{
    const BuildPoly* polygons = data.polygons + data.polygonOffset;
    for (RwUInt32 iVert = 0; iVert < data.numVertices; ++iVert) {
        data.vertices[iVert].flags = 0;
    }
    for (RwUInt32 iPoly = 0; iPoly < data.numPolygons; ++iPoly) {
        for (int iPolyVert = 0; iPolyVert < 3; ++iPolyVert) {
            data.vertices[polygons[iPoly].poly->vertIndex[iPolyVert]].flags++;
        }
    }
    RwUInt32 iSort = 0;
    for (RwUInt32 iVert = 0; iVert < data.numVertices; ++iVert) {
        if (data.vertices[iVert].flags) {
            data.sortVerts[iSort++] = &data.vertices[iVert];
        }
    }
    data.numSortVerts = iSort;
}

RwReal FuzzyExtentScore(RwReal extent, RwReal maxExtent)
{
    RwReal value = extent / maxExtent;
    // Bias against cutting the smallest extent.
    value = value > 0.5f ? 1.0f : 0.5f + value;
    return 1.0f / value;
}

// The comparison result is the bit pattern of the float difference, so -0.0
// orders as "less" exactly like RwSplitBits in the original.
int BuildVertexCmp(const void* a, const void* b)
{
    const RwReal diff = (*static_cast<BuildVertex* const*>(a))->dist -
                        (*static_cast<BuildVertex* const*>(b))->dist;
    RwInt32 result;
    std::memcpy(&result, &diff, sizeof(result));
    return result;
}

bool FindDividingPlane(BuildData& data, RwInt32& plane, RwReal& value)
{
    RwReal bestScore = FLT_MAX;
    RwInt32 bestPlane = 0;
    RwReal bestValue = 0.0f;

    const RwV3d vSize(data.bbox.sup.x - data.bbox.inf.x,
                      data.bbox.sup.y - data.bbox.inf.y,
                      data.bbox.sup.z - data.bbox.inf.z);

    RwInt32 maxExtentPlane = 0;
    for (RwInt32 tryPlane = 0; tryPlane < 12; tryPlane += sizeof(RwReal)) {
        if (GetCoord(vSize, tryPlane) > GetCoord(vSize, maxExtentPlane)) {
            maxExtentPlane = tryPlane;
        }
    }

    for (RwInt32 tryPlane = 0; tryPlane < static_cast<RwInt32>(3 * sizeof(RwReal)); tryPlane += sizeof(RwReal)) {
        const RwReal extentScore = FuzzyExtentScore(GetCoord(vSize, tryPlane), GetCoord(vSize, maxExtentPlane));

        // Median of the population along this axis.
        RwUInt32 nI;
        for (nI = 0; nI < data.numSortVerts; ++nI) {
            data.sortVerts[nI]->dist = GetCoord(*data.sortVerts[nI]->pos, tryPlane);
        }
        std::qsort(data.sortVerts, data.numSortVerts, sizeof(BuildVertex*), BuildVertexCmp);

        const RwReal median = data.sortVerts[nI >> 1]->dist;
        RwUInt32 lowerQuartile = nI >> 2;
        RwUInt32 upperQuartile = (nI >> 1) + (nI >> 2);
        const RwReal interQuartileRange = data.sortVerts[upperQuartile]->dist - data.sortVerts[lowerQuartile]->dist;

        RwReal quantThresh;
        if (interQuartileRange < kRangeEps * GetCoord(vSize, maxExtentPlane)) {
            lowerQuartile = upperQuartile = nI >> 1;
            quantThresh = 0.0f;
        } else {
            quantThresh = static_cast<RwReal>(kMaxClosestCheck) / interQuartileRange;
            // Sort the quartile range on absolute distance from the median.
            for (nI = lowerQuartile; nI <= upperQuartile; ++nI) {
                const RwReal dist = median - data.sortVerts[nI]->dist;
                data.sortVerts[nI]->dist = dist < 0.0f ? -dist : dist;
            }
            std::qsort(&data.sortVerts[lowerQuartile], upperQuartile - lowerQuartile + 1,
                       sizeof(BuildVertex*), BuildVertexCmp);
        }

        RwInt32 lastQuantValue = -1;
        RwInt32 numUniq = kMaxClosestCheck;
        for (nI = lowerQuartile; nI <= upperQuartile && numUniq > 0; ++nI) {
            const RwReal tryValue = GetCoord(*data.sortVerts[nI]->pos, tryPlane);
            const RwReal scaled = tryValue * quantThresh;
            const RwInt32 quantValue = Int32FromReal(scaled);
            if (quantValue != lastQuantValue) {
                ClipStats stats;
                SetClipCodes(data, tryPlane, tryValue);
                GetClipStats(data, tryPlane, tryValue, stats);
                if (stats.nLeft && stats.nRight) {
                    RwReal score = DivisionScore(data, stats, tryPlane);
                    score *= extentScore;
                    if (score < bestScore) {
                        bestScore = score;
                        bestPlane = tryPlane;
                        bestValue = tryValue;
                    }
                }
                lastQuantValue = quantValue;
                --numUniq;
            }
        }
    }

    if (bestScore < FLT_MAX) {
        plane = bestPlane;
        value = bestValue;
        return true;
    }
    return false;
}

// Moves only the triangle pointers, as the original does; flags stay in place.
void SortPolygons(BuildData& data)
{
    BuildPoly* polygons = data.polygons + data.polygonOffset;
    RwUInt32 leftCount = 0;
    for (RwUInt32 iPoly = 0; iPoly < data.numPolygons; ++iPoly) {
        if (polygons[iPoly].flags == kClipPolygonLeft) {
            if (iPoly > leftCount) {
                const DffTriangle* swap = polygons[leftCount].poly;
                polygons[leftCount].poly = polygons[iPoly].poly;
                polygons[iPoly].poly = swap;
            }
            ++leftCount;
        }
    }
}

std::unique_ptr<BuildSector> PolySector(const BuildData& data)
{
    auto sector = std::make_unique<BuildSector>();
    sector->numPolygons = static_cast<RwUInt16>(data.numPolygons);
    sector->firstPolygon = static_cast<RwUInt16>(data.polygonOffset);
    return sector;
}

std::unique_ptr<BuildSector> BuildTreeGenerate(BuildData& data, std::string& error)
{
    RwInt32 plane = 0;
    RwReal value = 0.0f;
    ClipStats stats;

    if (data.numPolygons < kMinPolygonsForSplit) {
        return PolySector(data);
    }
    if (data.bspDepth < kCollTreeMaxDepth) {
        SetSortVertices(data);
        if (FindDividingPlane(data, plane, value)) {
            SetClipCodes(data, plane, value);
            GetClipStats(data, plane, value, stats);
            SortPolygons(data);
        } else if (data.numPolygons <= kCollSectorContentsMaxCount) {
            // Coincident triangles that no plane separates share a leaf.
            return PolySector(data);
        } else {
            // Too many for one leaf: a fully overlapping pseudo split.
            plane = 0;
            stats.leftValue = data.bbox.sup.x;
            stats.rightValue = data.bbox.inf.x;
            stats.nLeft = static_cast<RwInt32>(data.numPolygons >> 1);
            stats.nRight = static_cast<RwInt32>(data.numPolygons) - stats.nLeft;
        }
    } else if (data.numPolygons <= kCollSectorContentsMaxCount) {
        return PolySector(data);
    } else {
        error = "collision tree reached its maximum depth with too many triangles for a leaf";
        return nullptr;
    }

    ++data.bspDepth;
    auto sector = std::make_unique<BuildSector>();
    sector->type = plane;
    sector->leftValue = stats.leftValue;
    sector->rightValue = stats.rightValue;

    BuildData subdata = data;
    subdata.numPolygons = static_cast<RwUInt32>(stats.nLeft);
    SetCoord(subdata.bbox.sup, plane, stats.leftValue);
    sector->left = BuildTreeGenerate(subdata, error);
    if (!sector->left) {
        return nullptr;
    }

    subdata = data;
    subdata.numPolygons = static_cast<RwUInt32>(stats.nRight);
    subdata.polygonOffset = data.polygonOffset + static_cast<RwUInt32>(stats.nLeft);
    SetCoord(subdata.bbox.inf, plane, stats.rightValue);
    sector->right = BuildTreeGenerate(subdata, error);
    if (!sector->right) {
        return nullptr;
    }
    return sector;
}

RwUInt32 CountLeafNodes(const BuildSector& sector)
{
    return sector.type < 0 ? 1 : CountLeafNodes(*sector.left) + CountLeafNodes(*sector.right);
}

}  // namespace

bool BuildCollisTree(const DffGeometry& geom, DffCollTree& out, std::string& error)
{
    const auto& vertices = geom.morphTargets[0].vertices;
    const RwUInt32 numVertices = static_cast<RwUInt32>(vertices.size());
    const RwUInt32 numPolygons = static_cast<RwUInt32>(geom.triangles.size());
    if (numPolygons > 0xffff) {
        error = "collision trees index at most 65535 triangles";
        return false;
    }

    std::vector<BuildVertex> buildVertices(numVertices);
    std::vector<BuildVertex*> sortVerts(numVertices);
    std::vector<BuildPoly> buildPolys(numPolygons);

    BuildData data;
    data.numVertices = numVertices;
    data.vertices = buildVertices.data();
    data.polygons = buildPolys.data();
    data.origPolys = geom.triangles.data();
    data.bspDepth = 0;
    data.numPolygons = numPolygons;
    data.polygonOffset = 0;
    data.numSortVerts = 0;
    data.sortVerts = sortVerts.data();

    // RwBBoxInitialize + RwBBoxAddPoint.
    data.bbox.inf = vertices[0];
    data.bbox.sup = vertices[0];
    for (RwUInt32 iVert = 0; iVert < numVertices; ++iVert) {
        const RwV3d& v = vertices[iVert];
        if (data.bbox.inf.x > v.x) data.bbox.inf.x = v.x;
        if (data.bbox.inf.y > v.y) data.bbox.inf.y = v.y;
        if (data.bbox.inf.z > v.z) data.bbox.inf.z = v.z;
        if (data.bbox.sup.x < v.x) data.bbox.sup.x = v.x;
        if (data.bbox.sup.y < v.y) data.bbox.sup.y = v.y;
        if (data.bbox.sup.z < v.z) data.bbox.sup.z = v.z;
        buildVertices[iVert].pos = &v;
    }
    for (RwUInt32 iPoly = 0; iPoly < numPolygons; ++iPoly) {
        buildPolys[iPoly].poly = &geom.triangles[iPoly];
    }

    BuildData rootData = data;
    const auto root = BuildTreeGenerate(rootData, error);
    if (!root) {
        return false;
    }

    out = DffCollTree();
    out.flags = kCollTreeUseMap;
    out.inf = data.bbox.inf;
    out.sup = data.bbox.sup;
    out.numEntries = numPolygons;
    out.numSplits = CountLeafNodes(*root) - 1;
    out.map.resize(numPolygons);
    for (RwUInt32 iTri = 0; iTri < numPolygons; ++iTri) {
        out.map[iTri] = static_cast<RwInt16>(buildPolys[iTri].poly - geom.triangles.data());
    }
    if (root->type < 0) {
        return true;
    }

    // CreateCollTreeFromBuildTree: depth-first, left subtree before right.
    out.splits.resize(out.numSplits);
    struct StackEntry
    {
        RwUInt16*          ref;
        const BuildSector* sector;
    };
    StackEntry stack[kCollTreeMaxDepth + 1];
    RwUInt16 dummy = 0;
    stack[0] = {&dummy, root.get()};
    RwInt32 nStack = 0;
    RwUInt32 iSplit = 0;
    while (nStack >= 0) {
        DffCollSplit& split = out.splits[iSplit];
        const BuildSector& planeSector = *stack[nStack].sector;
        *stack[nStack].ref = static_cast<RwUInt16>(iSplit);

        split.right.type = static_cast<RwUInt8>(planeSector.type);
        split.left.type = static_cast<RwUInt8>(planeSector.type | kCollSectorTypeNeg);
        split.left.value = planeSector.leftValue;
        split.right.value = planeSector.rightValue;

        if (planeSector.right->type < 0) {
            split.right.contents = static_cast<RwUInt8>(planeSector.right->numPolygons);
            split.right.index = planeSector.right->firstPolygon;
        } else {
            split.right.contents = kCollSectorContentsSplit;
            stack[nStack] = {&split.right.index, planeSector.right.get()};
            ++nStack;
        }
        if (planeSector.left->type < 0) {
            split.left.contents = static_cast<RwUInt8>(planeSector.left->numPolygons);
            split.left.index = planeSector.left->firstPolygon;
        } else {
            split.left.contents = kCollSectorContentsSplit;
            stack[nStack] = {&split.left.index, planeSector.left.get()};
            ++nStack;
        }
        ++iSplit;
        --nStack;
    }
    return true;
}
