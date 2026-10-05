/**
 * build/RwTriStrip.cpp - tunnel stripifier from RenderWare bameshop.c.
 *
 * Keeps the RW graph/list traversal, cost function and strip-joining order.
 * Only allocation and list plumbing differ from the reference implementation.
 */
#include "build/RwTriStrip.h"

#include <cassert>
#include <cmath>

namespace
{

struct Link
{
    Link* next = nullptr;
    Link* prev = nullptr;
    void* owner;

    explicit Link(void* value) : owner(value) {}
    void Reset() { next = prev = nullptr; }
    bool Attached() const { return next != nullptr; }
    void Remove()
    {
        next->prev = prev;
        prev->next = next;
    }
};

struct LinkList
{
    Link head{nullptr};

    void Initialize() { head.next = head.prev = &head; }
    void Add(Link* link)
    {
        link->next = head.next;
        link->prev = &head;
        head.next->prev = link;
        head.next = link;
    }
};

struct RpTriStripEdge;
struct RpTriStripPolygon
{
    Link inEndLink{this};
    Link inUsedLink{this};
    Link inFreeLink{this};
    RwUInt32 numEdges;
    RpTriStripEdge* edges[3];
    RwUInt16 vertIndex[3];
    RwUInt32 testFrame;
};

struct RpTriStripEdge
{
    Link inUsedLink{this};
    Link inFreeLink{this};
    RpTriStripPolygon* poly1;
    RpTriStripPolygon* poly2;
    RwUInt16 vert1, vert2;
    bool strip;
};

struct RpTriStripMesh
{
    RwUInt32 numPolygons;
    LinkList polygonEndList, polygonUsedList, polygonFreeList, edgeUsedList, edgeFreeList;
    std::vector<RpTriStripPolygon> polygons;
    std::vector<RpTriStripEdge> edges;

    explicit RpTriStripMesh(RwUInt32 count) : numPolygons(count), polygons(count), edges(3 * count) {}
};

struct RpTriStripTunnelData
{
    RwReal quality;
    RwUInt32 lengthLimit;
    RwUInt32 (*costCB)(RpTriStripPolygon*, RwUInt32, void*);
    void* data;
    RwUInt32 testFrame = 1;
};

struct RpTriStripTunnel
{
    RwUInt32 length, maxLength;
    RpTriStripEdge** edges;
    RpTriStripPolygon** polygons;
};

struct _rpTriStripTunnelStack
{
    RpTriStripEdge* edge;
    RpTriStripPolygon* polygon;
    RwUInt32 length;
};

struct Vertex
{
    LinkList triangleList;
};

struct VertexEdge
{
    RpTriStripEdge* edge;
    Link inVertexLink{this};
};

struct RpMesh
{
    RwUInt16* indices;
    RwUInt32 numIndices;
};

struct RpTriStripData
{
    bool ignoreWinding;
};

using RpBuildMeshTriangle = DffTriangle;
RpTriStripPolygon* RpTriStripPolygonFollowStrip(RpTriStripPolygon* curr, RpTriStripPolygon* prev);

static bool
_rpTriStripPolygonSharedVertex4(RpTriStripPolygon *poly1,
                                RpTriStripPolygon *poly2,
                                RpTriStripPolygon *poly3,
                                RpTriStripPolygon *poly4)
{
    RwUInt32 i, j, k, l;

    for (i = 0; i < poly1->numEdges; ++i) {
        for (j = 0; j < poly2->numEdges; ++j) {
            if (poly1->vertIndex[i] == poly2->vertIndex[j]) {
                break;
            }
        }

        if (j < poly2->numEdges) {
            for (k = 0; k < poly3->numEdges; ++k) {
                if (poly1->vertIndex[i] == poly3->vertIndex[k]) {
                    break;
                }
            }

            if (k < poly3->numEdges) {
                for (l = 0; l < poly4->numEdges; ++l) {
                    if (poly1->vertIndex[i] == poly4->vertIndex[l]) {
                        break;
                    }
                }

                if (l < poly3->numEdges) {
                    return  true ;
                }
            }
        }
    }

    return  false ;
}

static RwUInt16
_rpTriStripPolygonSharedVertex3(RpTriStripPolygon *polygon1,
                                RpTriStripPolygon *polygon2,
                                RpTriStripPolygon *polygon3)
{
    RwUInt32 i, j, k;

    assert(polygon1 && polygon2 && polygon3);

    for (i = 0; i < polygon1->numEdges; ++i) {
        for (j = 0; j < polygon2->numEdges; ++j) {
            if (polygon1->vertIndex[i] == polygon2->vertIndex[j]) {
                for (k = 0; k < polygon3->numEdges; ++k) {
                    if (polygon1->vertIndex[i] == polygon3->vertIndex[k]) {
                        goto found;
                    }
                }
            }
        }
    }

found:
    assert(i < polygon1->numEdges);
    return polygon1->vertIndex[i];
}

static RwUInt16
_rpTriStripPolygonSharedVertex2(RpTriStripPolygon *polygon1,
                                RpTriStripPolygon *polygon2)
{
    RwUInt32 i, j;

    assert(polygon1 && polygon2);

    for (i = 0; i < polygon1->numEdges; ++i) {
        for (j = 0; j < polygon2->numEdges; ++j) {
            if (polygon1->vertIndex[i] == polygon2->vertIndex[j]) {
                goto found;
            }
        }
    }

found:
    assert(i < polygon1->numEdges);
    return polygon1->vertIndex[i];
}

static RwUInt16
_rpTriStripPolygonOtherSharedVertex2(RpTriStripPolygon *polygon1,
                                RpTriStripPolygon *polygon2,
                                RwUInt16 index)
{
    RwUInt32 i, j;

    assert(polygon1 && polygon2);

    for (i = 0; i < polygon1->numEdges; ++i) {
        if (index != polygon1->vertIndex[i]) {
            for (j = 0; j < polygon2->numEdges; ++j) {
                if (polygon1->vertIndex[i] == polygon2->vertIndex[j]) {
                    goto found;
                }
            }
        }
    }

found:
    assert(i < polygon1->numEdges);
    return polygon1->vertIndex[i];
}

static RwUInt16
_rpTriStripPolygonOtherVertex(RpTriStripPolygon *polygon,
                              RwUInt16 index1,
                              RwUInt16 index2)
{
    RwUInt32 i;

    assert(polygon);

    for (i = 0; i < polygon->numEdges; ++i) {
        if (index1 != polygon->vertIndex[i]
            && index2 != polygon->vertIndex[i]) {
            break;
        }
    }

    assert(i < polygon->numEdges);
    return polygon->vertIndex[i];
}

static void
_rpTriStripTunnelEdges(RpTriStripTunnel *tunnel)
{
    RwUInt32 i;

    if (tunnel->length == 6) {
        i = 0;
    }

    for (i = 0; i < tunnel->length; ++i) {
        tunnel->edges[i]->strip = !tunnel->edges[i]->strip;
    }

    return;
}

static RwUInt32
_rpTriStripPolygonNumStripEdges(RpTriStripPolygon *polygon)
{
    RwUInt32 numStripEdges;
    RwUInt32 i;

    numStripEdges = 0;
    for (i = 0; i < polygon->numEdges; ++i) {
        if (polygon->edges[i]->strip) {
            ++numStripEdges;
        }
    }

    assert(numStripEdges < 3);
    return numStripEdges;
}

static void
_rpTriStripMeshComplementTunnel(RpTriStripMesh *mesh,
                                RpTriStripTunnel *tunnel)
{
    RpTriStripPolygon *polygon;
    RwUInt32 numStripEdges;
    RwUInt32 i;

    _rpTriStripTunnelEdges(tunnel);

    for (i = 0; i <= tunnel->length; ++i) {
        polygon = tunnel->polygons[i];
        numStripEdges = _rpTriStripPolygonNumStripEdges(polygon);

        if (polygon->inEndLink.Attached()) {
            if (numStripEdges > 1) {
                polygon->inEndLink.Remove();
                polygon->inEndLink.Reset();
                assert(&mesh->polygonEndList.head
                    != mesh->polygonEndList.head.next);
            }
        } else {
            if (numStripEdges < 2) {
                mesh->polygonEndList.Add(&polygon->inEndLink);
            }
        }
    }

    return;
}

static bool
_rpTriStripTunnelCheckStrips(RpTriStripTunnel *tunnel)
{
    RpTriStripPolygon *polygon, *curr, *next, *temp;
    RwUInt32 i;

    for (i = 0; i < tunnel->length; i += 2) {
        polygon = tunnel->polygons[i];
        curr = tunnel->polygons[i+1];
        next = RpTriStripPolygonFollowStrip(curr, polygon);

        while (nullptr != next && polygon != next) {
            temp = RpTriStripPolygonFollowStrip(next, curr);
            curr = next;
            next = temp;
        }

        if (nullptr != next) {
            return false;
        }
    }

    return true;
}

static RwUInt32
_rpTriStripTunnelCost(RpTriStripTunnel *tunnel,
                      RpTriStripTunnelData *tunnelData)
{
    RwUInt32 i;
    RwUInt32 cost;
    RpTriStripPolygon *polygon, *next, *next2;
    RwUInt32& testFrame = tunnelData->testFrame;

    cost = 0;
    for (i = 0; i <= tunnel->length; ++i) {

        polygon = tunnel->polygons[i];

        if (testFrame != polygon->testFrame) {
            next = RpTriStripPolygonFollowStrip(polygon, nullptr);
            while (nullptr != next) {
                next2 = RpTriStripPolygonFollowStrip(next, polygon);
                polygon = next;
                next = next2;
            }
            cost += tunnelData->costCB(polygon, testFrame, tunnelData->data);
        }
    }
    ++testFrame;

    return cost;
}

static bool
_rpTriStripTunnelValid(RpTriStripTunnel *tunnel,
                       RpTriStripTunnelData *tunnelData)
{
    bool result;

    assert(tunnel);
    assert(tunnelData);

    if (tunnel->length & 1
        && !tunnel->polygons[tunnel->length]->inEndLink.Attached()) {
        return false;
    }

    _rpTriStripTunnelEdges(tunnel);
    result = _rpTriStripTunnelCheckStrips(tunnel);

    if (!result || tunnel->length < 2) {
        _rpTriStripTunnelEdges(tunnel);
    } else {
        RwUInt32 oldCost, newCost;

        newCost = _rpTriStripTunnelCost(tunnel, tunnelData);
        _rpTriStripTunnelEdges(tunnel);
        oldCost = _rpTriStripTunnelCost(tunnel, tunnelData);

        result = oldCost > newCost;
    }

    return result;
}

static bool
_rpTriStripTunnelFind(RpTriStripTunnel *tunnel,
                      RpTriStripTunnelData *tunnelData,
                      _rpTriStripTunnelStack *stack)
{
    _rpTriStripTunnelStack *top;
    RpTriStripPolygon *polygon;
    RpTriStripEdge *edge;
    bool strip;
    RwUInt32 i, j;

    assert(tunnel);
    assert(tunnelData);
    assert(stack);

    top = stack;
    for (i = 0; i < tunnel->polygons[0]->numEdges; ++i) {
        edge = tunnel->polygons[0]->edges[i];

        if (!edge->strip) {
            polygon = tunnel->polygons[0] == edge->poly1
                ? edge->poly2 : edge->poly1;

            if (nullptr != polygon) {
                top->edge = edge;
                top->polygon = polygon;
                top->length = 1;
                ++top;
            }
        }
    }

    while (stack < top) {

        --top;
        assert(top->polygon);
        assert(top->edge);
        tunnel->length = top->length;
        tunnel->edges[tunnel->length - 1] = top->edge;
        tunnel->polygons[tunnel->length] = top->polygon;
        strip = tunnel->length & 1;

        if (_rpTriStripTunnelValid(tunnel, tunnelData)) {
            return true;
        }

        if (tunnel->length < tunnel->maxLength) {
            polygon = tunnel->polygons[tunnel->length];

            for (i = 0; i < polygon->numEdges; ++i) {
                edge = polygon->edges[i];

                if (strip == edge->strip
                    && edge != tunnel->edges[tunnel->length - 1]) {
                    top->polygon = polygon == edge->poly1
                        ? edge->poly2 : edge->poly1;
                    if (nullptr != top->polygon) {

                        for (j = 0; j <= tunnel->length; ++j) {
                            if (top->polygon == tunnel->polygons[j]) {
                                break;
                            }
                        }

                        if (j > tunnel->length) {
                            top->length = tunnel->length + 1;
                            top->edge = edge;
                            ++top;
                        }
                    }
                }
            }
        }
    }

    return false;
}

static bool
_rpTriStripPolygonWindingCorrect(RpTriStripPolygon *polygon,
                                 RwUInt16 index1,
                                 RwUInt16 index2,
                                 RwUInt32 winding)
{
    RwUInt32 i;

    assert(polygon);

    for (i = 0; i < polygon->numEdges; ++i) {
        if (index1 == polygon->vertIndex[i]) {
            break;
        }
    }
    assert(i < polygon->numEdges);

    if (index2 == polygon->vertIndex[(i + 1) % polygon->numEdges]) {
        return 1 == winding;
    } else {
        return 2 == winding;
    }
}

static bool
_rpTriStripPolygonStripIndex(RpTriStripPolygon *polygon,
                             RwUInt16 index1,
                             RwUInt16 index2,
                             RwUInt16 index3,
                             RwUInt16 *index)
{
    RwUInt32 i;
    bool tristrip = false;

    assert(polygon);
    assert(3 == polygon->numEdges);
    assert(index);
    assert(index1 != index2 && index2 != index3 && index3 != index1);
#ifndef NDEBUG
    {
        RwUInt32 numShared;

        numShared = 0;
        for (i = 0; i < polygon->numEdges; ++i) {
            if (index1 == polygon->vertIndex[i]
                || index2 == polygon->vertIndex[i]
                || index3 == polygon->vertIndex[i]) {
                ++numShared;
            }
        }
        assert(2 <= numShared);
    }
#endif

    *index = index1;
    for (i = 0; i < polygon->numEdges; ++i) {
        if (index1 != polygon->vertIndex[i]
            && index2 != polygon->vertIndex[i]
            && index3 != polygon->vertIndex[i]) {
            *index = polygon->vertIndex[i];
        } else if (index2 == polygon->vertIndex[i]) {
            tristrip = true;
        }
    }

    return tristrip;
}

static RwUInt32
_rpTriStripPolygonSharedVertices(RpTriStripPolygon *polygon1,
                                 RpTriStripPolygon *polygon2)
{
    RwUInt32 numShared;
    RwUInt32 i, j;

    assert(polygon1);
    assert(polygon2);

    numShared = 0;
    for (i = 0; i < polygon1->numEdges; ++i) {
        for (j = 0; j < polygon2->numEdges; ++j) {
            if (polygon1->vertIndex[i] == polygon2->vertIndex[j]) {
                ++numShared;
                break;
            }
        }
    }

    return numShared;
}

static void
_rpTriStripMeshCreate(const RpBuildMeshTriangle* const* triangles, RwUInt32 numTriangles, RpTriStripMesh* mesh)
{
    RwUInt32 i, j;
    RpTriStripEdge *edge, *testEdge;
    RpTriStripPolygon *poly;
    Vertex *vertices;
    VertexEdge *edges, *nextEdge;
    RwUInt32 numVertices;
    assert(numTriangles);

    numVertices = 0;
    for (i = 0; i < numTriangles; ++i) {
        for (j = 0; j < 3; ++j) {
            if (triangles[i]->vertIndex[j] > numVertices) {
                numVertices = triangles[i]->vertIndex[j];
            }
        }
    }

    std::vector<Vertex> vertexStorage(numVertices);
    vertices = vertexStorage.data();
    for (i = 0; i < numVertices; ++i) {
        vertices[i].triangleList.Initialize();
    }
    std::vector<VertexEdge> edgeStorage(3 * numTriangles);
    nextEdge = edges = edgeStorage.data();

    mesh->numPolygons = numTriangles;
    mesh->polygonEndList.Initialize();
    mesh->polygonFreeList.Initialize();
    mesh->polygonUsedList.Initialize();
    mesh->edgeFreeList.Initialize();
    mesh->edgeUsedList.Initialize();

    poly = mesh->polygons.data();
    edge = mesh->edges.data();

    for (i = 0; i < numTriangles; ++i) {

        poly->vertIndex[0] = triangles[i]->vertIndex[0];
        poly->vertIndex[1] = triangles[i]->vertIndex[1];
        poly->vertIndex[2] = triangles[i]->vertIndex[2];

        assert(poly->vertIndex[0] != poly->vertIndex[1]
            && poly->vertIndex[1] != poly->vertIndex[2]
            && poly->vertIndex[2] != poly->vertIndex[0]);

        mesh->polygonUsedList.Add(&poly->inUsedLink);
        mesh->polygonEndList.Add(&poly->inEndLink);
        poly->numEdges = 3;
        poly->testFrame = 0;

        for (j = 0; j < 3; ++j) {
            RwUInt16 index1, index2;
            Vertex *vertex;
            Link *curr, *end;

            index1 = poly->vertIndex[j];
            index2 = poly->vertIndex[(j + 1) % 3];
            if (index1 > index2) {
                RwUInt16 temp;

                temp = index1;
                index1 = index2;
                index2 = temp;
            }
            vertex = vertices + index1;

            curr = vertex->triangleList.head.next;
            end = &vertex->triangleList.head;

            while (curr != end) {
                testEdge = static_cast<VertexEdge*>(curr->owner)->edge;

                if (index2 == testEdge->vert2
                    && nullptr == testEdge->poly2
                    && 2 == _rpTriStripPolygonSharedVertices(poly, testEdge->poly1)) {
                    break;
                }

                curr = curr->next;
            }

            if (curr != end) {
                testEdge = static_cast<VertexEdge*>(curr->owner)->edge;
                poly->edges[j] = testEdge;
                testEdge->poly2 = poly;
            } else {
                mesh->edgeUsedList.Add(&edge->inUsedLink);
                poly->edges[j] = edge;
                edge->poly1 = poly;
                edge->poly2 = nullptr;
                edge->vert1 = index1;
                edge->vert2 = index2;
                edge->strip = false;
                nextEdge->edge = edge;
                vertex->triangleList.Add(&nextEdge->inVertexLink);
                ++nextEdge;
                ++edge;
            }

        }

        ++poly;
    }

}

static void
_rpTriStripGetStrip(Link *link,
                    RpTriStripPolygon **polygon1,
                    RpTriStripPolygon **polygon2,
                    RpTriStripPolygon **polygon3,
                    RwUInt16 *index1,
                    RwUInt16 *index2,
                    RwUInt16 *index3)
{

    *polygon1 = static_cast<RpTriStripPolygon*>(link->owner);
    assert(3 == (*polygon1)->numEdges);

    *polygon2 = RpTriStripPolygonFollowStrip(*polygon1, nullptr);
    if (nullptr == *polygon2) {
        *index1 = (*polygon1)->vertIndex[0];
        *index2 = (*polygon1)->vertIndex[1];
        *index3 = (*polygon1)->vertIndex[2];
        *polygon3 = nullptr;
    } else {
        assert(3 == (*polygon2)->numEdges);
        *polygon3 = RpTriStripPolygonFollowStrip(*polygon2, *polygon1);
        if (nullptr == *polygon3) {
            *index3 = _rpTriStripPolygonSharedVertex2(*polygon1, *polygon2);
        } else {
            assert(3 == (*polygon3)->numEdges);
            *index3 = _rpTriStripPolygonSharedVertex3(*polygon1, *polygon2,
                *polygon3);
        }
        *index2 = _rpTriStripPolygonOtherSharedVertex2(*polygon1, *polygon2,
            *index3);
        *index1 = _rpTriStripPolygonOtherVertex(*polygon1, *index2, *index3);
    }

    return;
}

static void
_rpTriStripMeshCreateOutput(RpTriStripMesh *inMesh, RpMesh *outMesh,
                            RpTriStripData *data)
{
    Link *curr, *end;
    RpTriStripPolygon *polygon1, *polygon2, *polygon3;
    RwUInt32 winding;
    RwUInt16 *indices;
    RwUInt16 index1, index2, index3;
    RwUInt16 index, testIndex;

    winding = data->ignoreWinding ? 0 : 1;
    indices = outMesh->indices;
    curr = end = &inMesh->polygonEndList.head;
    _rpTriStripGetStrip(inMesh->polygonEndList.head.next,
        &polygon1, &polygon2, &polygon3, &index1, &index2, &index3);

    do {
        if (curr == end) {
            curr = inMesh->polygonEndList.head.next;
            while (curr != end) {
                _rpTriStripGetStrip(curr, &polygon1, &polygon2, &polygon3, &index1, &index2, &index3);

                if (!winding || _rpTriStripPolygonWindingCorrect(polygon1, index1, index2, winding)) {
                    if (winding) {
                        winding ^= 3;
                    }
                    if (indices != outMesh->indices) {
                        indices[0] = indices[-1];
                        indices[1] = index1;
                        indices += 2;
                    }
                    break;
                }

                curr = curr->next;
            }
        }

        if (curr == end) {
            curr = inMesh->polygonEndList.head.next;

            if (curr == end) {
                break;
            }

            _rpTriStripGetStrip(curr, &polygon1, &polygon2, &polygon3, &index1, &index2, &index3);

            if (indices != outMesh->indices) {
                indices[0] = indices[-1];
                indices[1] = index1;
                indices += 2;
            }
            *indices++ = index1;
        }

        *indices++ = index1;
        *indices++ = index2;
        *indices++ = index3;
        assert(indices[-3] != indices[-2]
            && indices[-2] != indices[-1]
            && indices[-1] != indices[-3]);
        assert(!winding || _rpTriStripPolygonWindingCorrect(
            polygon1, indices[-3], indices[-2], winding ^ 3));

        while (nullptr != polygon2) {
            polygon1 = polygon2;
            polygon2 = polygon3;
            polygon3 = nullptr != polygon2
                ? RpTriStripPolygonFollowStrip(polygon2, polygon1) : nullptr;

            if (polygon1->inEndLink.Attached()) {
                polygon1->inEndLink.Remove();
                polygon1->inEndLink.Reset();
            }

            if (_rpTriStripPolygonStripIndex(polygon1, indices[-3],
                indices[-2], indices[-1], &index)) {
                if (winding) {
                    if (_rpTriStripPolygonWindingCorrect(polygon1, indices[-2],
                        indices[-1], winding)) {
                        winding ^= 3;
                    } else {
                        indices[0] = indices[-2];
                        indices[1] = indices[-2];
                        indices[2] = indices[-1];
                        indices += 3;
                    }
                }
                *indices++ = index;
            }

            else if (nullptr == polygon2 || _rpTriStripPolygonStripIndex(
                polygon2, indices[-3], indices[-1], index, &testIndex)) {
                indices[0] = indices[-1];
                indices[-1] = indices[-3];
                ++indices;
                if (winding) {
                    if (!_rpTriStripPolygonWindingCorrect(polygon1,
                        indices[-2], indices[-1], winding ^ 3)) {
                        indices[1] = indices[-2];
                        indices[2] = indices[-1];
                        indices[0] = indices[-1];
                        indices[-2] = indices[-1];
                        indices += 3;
                        winding ^= 3;
                    }
                }
                *indices++ = index;
            }

            else if (nullptr == polygon3 || _rpTriStripPolygonStripIndex(
                polygon3, indices[-3], index, testIndex, &testIndex)) {
                index1 = indices[-3];
                indices[0] = indices[-1];
                ++indices;
                if (winding) {
                    if (_rpTriStripPolygonWindingCorrect(polygon1, indices[-1],
                        index1, winding)) {
                        winding ^= 3;
                    } else {
                        indices[0] = indices[-1];
                        indices++;
                    }
                }
                *indices++ = index1;
                *indices++ = index;
            } else {
                index1 = indices[-3];
                indices[0] = indices[-1];
                ++indices;
                if (winding) {
                    if (_rpTriStripPolygonWindingCorrect(polygon1, indices[-1],
                        index, winding)) {
                        winding ^= 3;
                    } else {
                        indices[0] = indices[-1];
                        indices++;
                    }
                }
                *indices++ = index;
                *indices++ = index1;
            }

            assert(indices[-3] != indices[-2]
                && indices[-2] != indices[-1]
                && indices[-1] != indices[-3]);
            assert(!winding || _rpTriStripPolygonWindingCorrect(
                polygon1, indices[-3], indices[-2], winding ^ 3));
        }

        curr->Remove();
        curr->Reset();

        curr = inMesh->polygonEndList.head.next;
        while (curr != end) {
            _rpTriStripGetStrip(curr, &polygon1, &polygon2, &polygon3, &index1, &index2, &index3);

            if (index1 == indices[-1]) {
                if (!winding || _rpTriStripPolygonWindingCorrect(polygon1, index1, index2, winding)) {
                    if (winding) {
                        winding ^= 3;
                    }
                    break;
                }
            }

            curr = curr->next;
        }

        if (winding && curr == end) {
            curr = inMesh->polygonEndList.head.next;
            while (curr != end) {
                _rpTriStripGetStrip(curr, &polygon1, &polygon2, &polygon3, &index1, &index2, &index3);

                if (index1 == indices[-1]) {
                    *indices++ = index1;
                    break;
                }

                curr = curr->next;
            }
        }

        if (curr == end) {
            curr = inMesh->polygonEndList.head.next;
            while (curr != end) {
                _rpTriStripGetStrip(curr, &polygon1, &polygon2, &polygon3, &index1, &index2, &index3);

                if (index1 == indices[-2]) {
                    if (!winding || _rpTriStripPolygonWindingCorrect(polygon1, index1, index2, winding ^ 3)) {
                        *indices++ = index1;
                        break;
                    }
                }
                if (index2 == indices[-1]) {
                    if (!winding || _rpTriStripPolygonWindingCorrect(polygon1, index1, index2, winding ^ 3)) {
                        *indices++ = index2;
                        break;
                    }
                }

                curr = curr->next;
            }
        }

        if (winding && curr == end) {
            curr = inMesh->polygonEndList.head.next;
            while (curr != end) {
                _rpTriStripGetStrip(curr, &polygon1, &polygon2, &polygon3, &index1, &index2, &index3);

                if (index1 == indices[-2]) {
                    winding ^= 3;
                    *indices++ = index1;
                    *indices++ = index1;
                    break;
                }

                if (index2 == indices[-1]) {
                    winding ^= 3;
                    *indices++ = index2;
                    *indices++ = index2;
                    break;
                }

                curr = curr->next;
            }
        }
    } while (1);

    outMesh->numIndices = static_cast<RwUInt32>(indices - outMesh->indices);
    assert(outMesh->numIndices);

    return;
}

RwUInt32
RpTriStripDefaultCost(RpTriStripPolygon *startPolygon, RwUInt32 testFrame,
                      void *data )
{
    RwUInt32 cost;
    RpTriStripPolygon *polygon, *next, *next2, *next3;
    RwUInt32 beforeFan, afterFan;

    cost = 40000;
    beforeFan = 1;
    afterFan = 0;
    polygon = startPolygon;
    polygon->testFrame = testFrame;
    next = RpTriStripPolygonFollowStrip(polygon, nullptr);

    if (nullptr != next) {
        next->testFrame = testFrame;
        next2 = RpTriStripPolygonFollowStrip(next, polygon);

        if (nullptr != next2) {
            next2->testFrame = testFrame;
            next3 = RpTriStripPolygonFollowStrip(next2, next);
        } else {
            next3 = nullptr;
        }
    } else {
        next2 = nullptr;
        next3 = nullptr;
    }

    while (nullptr != next3) {
        next3->testFrame = testFrame;

        if (_rpTriStripPolygonSharedVertex4(polygon, next, next2, next3)) {
            afterFan = 1;
            cost += 10000;
        } else if (afterFan) {
            ++afterFan;
        } else {
            ++beforeFan;
        }

        polygon = next;
        next = next2;
        next2 = next3;
        next3 = RpTriStripPolygonFollowStrip(next2, next);
    }

    if (afterFan > beforeFan) {
        return cost + beforeFan;
    } else {
        return cost + afterFan;
    }
}

RpTriStripMesh *
RpTriStripMeshTunnel(RpTriStripMesh *mesh, void *data)
{
    RpTriStripTunnelData *tunnelData = static_cast<RpTriStripTunnelData*>(data);
    RpTriStripTunnel *tunnel;
    Link *curr, *end;
    _rpTriStripTunnelStack *stack;
    RwUInt32 numTunnels;
    RwReal coeff;
    RwReal minCoeff;

    if (tunnelData->lengthLimit) {

        RpTriStripTunnel tunnelStorage;
        std::vector<RpTriStripEdge*> tunnelEdges(tunnelData->lengthLimit);
        std::vector<RpTriStripPolygon*> tunnelPolygons(1 + tunnelData->lengthLimit);
        std::vector<_rpTriStripTunnelStack> stackStorage(3 * tunnelData->lengthLimit);
        tunnel = &tunnelStorage;
        tunnel->edges = tunnelEdges.data();
        tunnel->polygons = tunnelPolygons.data();
        tunnel->length = 0;
        tunnel->maxLength = 1;
        stack = stackStorage.data();
        numTunnels = 0;
        coeff = 0.0f;
        minCoeff = static_cast<RwReal>(std::pow(0.001f, tunnelData->quality));

        do {
            end = &mesh->polygonEndList.head;
            curr = mesh->polygonEndList.head.next;
            assert(curr != end);

            while (curr != end) {
                tunnel->polygons[0] = static_cast<RpTriStripPolygon*>(curr->owner);

                if (_rpTriStripTunnelFind(tunnel, tunnelData, stack)) {
                    break;
                }

                curr = curr->next;
            }

            if (curr == end) {
                coeff = 0.5f * coeff + static_cast<RwReal>(numTunnels) / mesh->numPolygons;
                ++tunnel->maxLength;
                numTunnels = 0;

                if (coeff < minCoeff
                    || tunnel->maxLength >= tunnelData->lengthLimit) {
                    break;
                }
            } else {
                _rpTriStripMeshComplementTunnel(mesh, tunnel);
                tunnel->length = 0;
                ++numTunnels;
            }
        } while (1);

    }

    return mesh;
}

RpTriStripPolygon *
RpTriStripPolygonFollowStrip(RpTriStripPolygon *curr, RpTriStripPolygon *prev)
{

    RwUInt32 i;
    RpTriStripPolygon *next;

    next = nullptr;
    for (i = 0; i < curr->numEdges; ++i) {
        if (curr->edges[i]->strip && prev != curr->edges[i]->poly1
            && prev != curr->edges[i]->poly2) {
            next = curr == curr->edges[i]->poly1
                ? curr->edges[i]->poly2 : curr->edges[i]->poly1;
            break;
        }
    }

    return next;
}

}  // namespace

void BuildRwTunnelStrip(const std::vector<const DffTriangle*>& triangles, std::vector<RwUInt32>& indices)
{
    // Tunnel settings verified against the DBO sample corpus; preserve winding.
    RpTriStripTunnelData settings{0.5f, 10000, RpTriStripDefaultCost, nullptr};
    RpTriStripData outputSettings{false};
    RpTriStripMesh mesh(static_cast<RwUInt32>(triangles.size()));
    _rpTriStripMeshCreate(triangles.data(), static_cast<RwUInt32>(triangles.size()), &mesh);
    RpTriStripMeshTunnel(&mesh, &settings);
    std::vector<RwUInt16> buffer(6 * triangles.size());
    RpMesh output{buffer.data(), 0};
    _rpTriStripMeshCreateOutput(&mesh, &output, &outputSettings);
    indices.assign(buffer.begin(), buffer.begin() + output.numIndices);
}
