/**
 * plugin/Toon.h - RpToon (rwID_TOONPLUGIN, 0x12E).
 *
 * The plugin registers on both RpMaterial and RpGeometry, so the same chunk ID
 * means two different payloads depending on which EXTENSION container it sits in.
 *
 * Material payload (RenderWare toonmaterial.c, ToonMaterialStreamWrite;
 * identical in DBO toonmaterial.c):
 *   RwInt32 versionStamp                (rpToonMaterialVersionStamp = 0x67890002)
 *   RwInt32 overrideGeometryPaint
 *   char    paintName[32]               (rwTEXTUREBASENAMELENGTH)
 *   RwInt32 silhouetteInkID
 *   => 44 bytes
 * The reader is version-gated: paintName is only present when
 * overrideGeometryPaint != 0 or version >= ...WithSilhouetteInkIDs, and
 * silhouetteInkID only for the latter.
 *
 * Geometry payload (RenderWare toongeo.c, _rpToonGeoWrite):
 *   RwInt32          version            (RPTOON_INTERNALTOONGEOSTREAMVERSION = 0xabcd0002)
 *   RwInt32          numVerts
 *   RwInt32          numTriangles
 *   rpToonTriangle   triangles[numTriangles]        (3 x RwUInt16)
 *   RwReal           vertexThicknesses[numVerts]
 *   RwInt32          numEdges
 *   RwInt32          numCreaseEdges
 *   RpToonEdge       edges[numEdges]                (8 x RwUInt16)
 *   RwV3d            faceNormals[numTriangles]
 *   RwV3d            extrusionVertexNormals[numVerts]
 *   RwInt32          inkIDCount
 *   rpToonInkID      inkIDList[inkIDCount]          (char[32])
 *   rpToonEdgeInkID  edgeInkIDs[numEdges]           (2 x RwUInt8)
 *   rpToonPaintID    defaultPaintID                 (char[32])
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"

#include <array>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Geometry-side toon data
// ---------------------------------------------------------------------------

struct RpToonTriangle
{
    RwUInt16 vertIndex[3] = {0, 0, 0};
};

struct RpToonEdge
{
    RwUInt16 v[2] = {0, 0};
    RwUInt16 face[2] = {0, 0};
    RwUInt16 edgefv[2][2] = {{0, 0}, {0, 0}};
};

struct RpToonInkID
{
    char name[rpTOONINKNAMELEN] = {};
};

struct RpToonEdgeInkID
{
    RwUInt8 inkId[2] = {0, 0};
};

struct RpToonPaintID
{
    char name[rpTOONPAINTNAMELEN] = {};
};

struct DffToonGeo
{
    RwInt32 versionStamp = rpTOONGEOSTREAMVERSION;

    std::vector<RpToonTriangle>  triangles;
    std::vector<RwReal>          vertexThicknesses;
    std::vector<RpToonEdge>      edges;
    RwInt32                      numCreaseEdges = 0;
    std::vector<RwV3d>           faceNormals;
    std::vector<RwV3d>           extrusionNormals;

    std::vector<RpToonInkID>     inkIDList;
    std::vector<RpToonEdgeInkID> edgeInkIDs;
    RpToonPaintID                defaultPaintID;

    RwUInt32    chunkVersion = 0;
    RwChunkSpan span;

    RwInt32 NumVertices() const { return static_cast<RwInt32>(vertexThicknesses.size()); }
    RwInt32 NumTriangles() const { return static_cast<RwInt32>(triangles.size()); }
    RwInt32 NumEdges() const { return static_cast<RwInt32>(edges.size()); }

    std::string DefaultPaintName() const;
    void SetDefaultPaintName(const std::string& name);
};

// ---------------------------------------------------------------------------
// Material-side toon data
// ---------------------------------------------------------------------------

struct DffToonMaterial
{
    RwUInt32                          chunkVersion = 0;
    RwInt32                           versionStamp = rpTOONMATERIALVERSIONSTAMP;
    RwInt32                           overrideGeometryPaint = 0;
    std::array<char, rpTOONPAINTNAMELEN> paintName{};
    RwInt32                           silhouetteInkID = 0;

    /// True when the source stream carried the paintName field.
    bool hasPaintName = true;
    /// True when the source stream carried the silhouetteInkID field.
    bool hasSilhouetteInkID = true;

    RwChunkSpan span;

    std::string PaintName() const;
    void SetPaintName(const std::string& name);
};

bool ReadToonGeo(RwStream& stream, DffDiagnostics& diag, DffToonGeo& out, RwUInt32 payloadSize, RwUInt32 chunkVersion);
bool WriteToonGeo(RwStream& stream, DffDiagnostics& diag, const DffToonGeo& toon, RwUInt32 version);

bool ReadToonMaterial(RwStream& stream, DffDiagnostics& diag, DffToonMaterial& out, RwUInt32 payloadSize, RwUInt32 chunkVersion);
bool WriteToonMaterial(RwStream& stream, DffDiagnostics& diag, const DffToonMaterial& toon, RwUInt32 version);
