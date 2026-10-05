/**
 * world/Geometry.h - RpGeometry (rwID_GEOMETRY, 0x0F).
 *
 * Stream layout (RenderWare baclump.c, RpGeometryStreamWrite):
 *
 *   STRUCT
 *     RwInt32 format, numTriangles, numVertices, numMorphTargets
 *     if !(format & rpGEOMETRYNATIVE):
 *       if (format & rpGEOMETRYPRELIT): RwRGBA preLitLum[numVertices]
 *       texCoords[numTexCoordSets][numVertices]      (RwTexCoords)
 *       triangles[numTriangles]:
 *         RwUInt32 { v0 << 16 | v1 }
 *         RwUInt32 { v2 << 16 | materialIndex }
 *     per morph target:
 *       RwSphere boundingSphere
 *       RwInt32  pointsPresent
 *       RwInt32  normalsPresent
 *       if !(format & rpGEOMETRYNATIVE):
 *         if pointsPresent : RwV3d verts[numVertices]
 *         if normalsPresent: RwV3d normals[numVertices]
 *   MATLIST
 *   EXTENSION
 *
 * numTexCoordSets is carried in format bits 16..23; older streams instead set
 * rpGEOMETRYTEXTURED (1 set) or rpGEOMETRYTEXTURED2 (2 sets).
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"
#include "plugin/BinMesh.h"
#include "plugin/Collis.h"
#include "plugin/Morph.h"
#include "plugin/Skin.h"
#include "plugin/Toon.h"
#include "plugin/UserData.h"
#include "world/Extension.h"
#include "world/Material.h"

#include <memory>
#include <string>
#include <vector>

struct DffTriangle
{
    RwUInt16 vertIndex[3] = {0, 0, 0};
    RwInt16  matIndex = 0;
};

struct DffMorphTarget
{
    RwSphere           boundingSphere;
    std::vector<RwV3d> vertices;
    std::vector<RwV3d> normals;
};

struct DffGeometry
{
    RwUInt32 format = 0;

    std::vector<DffTriangle>                 triangles;
    std::vector<RwRGBA>                      preLitColors;
    std::vector<std::vector<RwTexCoords>>    texCoords;
    std::vector<DffMorphTarget>              morphTargets;

    // Material list
    std::vector<RwInt32>                      materialIndices;
    std::vector<std::shared_ptr<DffMaterial>> materials;

    // ---- modelled extensions ----------------------------------------------
    std::shared_ptr<DffBinMesh>   binMesh;
    std::shared_ptr<DffSkin>      skin;
    std::shared_ptr<DffToonGeo>   toon;
    std::shared_ptr<DffMorphAnim> morph;
    std::shared_ptr<DffCollis>    collis;
    std::vector<DffUserDataList>  userDataLists;

    DffExtensionList extensions;

    RwChunkSpan geometrySpan;
    RwChunkSpan structSpan;
    RwChunkSpan matListSpan;
    RwChunkSpan matListStructSpan;

    RwInt32 NumVertices() const
    {
        return morphTargets.empty() ? 0 : static_cast<RwInt32>(morphTargets[0].vertices.size());
    }
    RwInt32 NumTexCoordSets() const { return static_cast<RwInt32>(texCoords.size()); }
    bool HasPreLitColors() const { return !preLitColors.empty(); }
    bool HasNormals() const { return !morphTargets.empty() && !morphTargets[0].normals.empty(); }
    bool IsNative() const { return (format & rpGEOMETRYNATIVE) != 0; }

    /** Geometry name as stored by DBO in the UserData "name" array. */
    std::string Name() const;

    /** Recomputes the format bits that describe the current vertex attributes. */
    void RefreshFormatFlags();

    static bool IsKnownExtension(RwUInt32 type);
};

/** Decodes the texture-coordinate set count from a geometry format word. */
RwInt32 DffGeometryTexCoordSetCount(RwUInt32 format);

bool ReadGeometry(RwStream& stream, DffDiagnostics& diag, DffGeometry& geom, RwUInt32 payloadSize, RwUInt32 chunkVersion);
bool WriteGeometry(RwStream& stream, DffDiagnostics& diag, const DffGeometry& geom, RwUInt32 version);
