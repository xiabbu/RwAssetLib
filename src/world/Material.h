/**
 * world/Material.h - RpMaterial (rwID_MATERIAL, 0x07).
 *
 * Stream layout (RenderWare bamateri.c, RpMaterialStreamWrite):
 *
 *   STRUCT RpMaterialChunkInfo {
 *       RwInt32             flags
 *       RwRGBA              color
 *       RwInt32             unused
 *       RwBool              textured
 *       RwSurfaceProperties surfaceProps    // ambient, specular, diffuse
 *   }                                       // 28 bytes
 *   TEXTURE     (only when textured != 0)
 *   EXTENSION   plugin chunks
 *
 * Modelled material plugins: MatFX, Toon, LtMap, UVAnim, UserData, plus DBO's
 * NTL_MATERIAL_EXT and NTL_WORLD_MATERIAL.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"
#include "ntl/NtlMaterial.h"
#include "plugin/LtMap.h"
#include "plugin/MatFX.h"
#include "plugin/Toon.h"
#include "plugin/UVAnim.h"
#include "plugin/UserData.h"
#include "world/Extension.h"
#include "world/Texture.h"

#include <memory>
#include <string>
#include <vector>

struct DffMaterial
{
    RwInt32             flags = 0;
    RwRGBA              color;
    RwInt32             unused = 0;
    RwSurfaceProperties surfaceProps;

    std::shared_ptr<DffTexture> texture;

    // ---- modelled extensions ----------------------------------------------
    std::shared_ptr<DffMatFXMaterial>   matfx;
    std::shared_ptr<DffToonMaterial>    toon;
    std::shared_ptr<DffLtMapMaterial>   ltmap;
    std::shared_ptr<DffUVAnimMaterial>  uvanim;
    std::shared_ptr<DffNtlMaterialExt>  ntlMaterialExt;
    std::vector<DffNtlWorldMaterial>    ntlWorldMaterials;
    std::vector<DffUserDataList>        userDataLists;

    DffExtensionList extensions;

    RwChunkSpan materialSpan;
    RwChunkSpan structSpan;

    /**
     * DBO derives most NTL material flags at runtime from the UserData "name"
     * string (see Helper_SetAtomicAllMaterialSkinInfo in the client), not from a
     * serialized bitfield. This returns that name when present.
     */
    const std::string* UserDataName() const;
    DffUserDataString* EnsureUserDataName();

    /** True for extension chunk IDs this library models semantically. */
    static bool IsKnownExtension(RwUInt32 type);
};

bool ReadMaterial(RwStream& stream, DffDiagnostics& diag, DffMaterial& mat, RwUInt32 payloadSize, RwUInt32 chunkVersion);
bool WriteMaterial(RwStream& stream, DffDiagnostics& diag, const DffMaterial& mat, RwUInt32 version);
