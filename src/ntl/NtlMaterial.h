/**
 * ntl/NtlMaterial.h - DBO's two custom RpMaterial plugins.
 *
 * ---------------------------------------------------------------------------
 * rwID_NTL_MATERIAL_EXT (0x1FC = MAKECHUNKID(rwVENDORID_CRITERIONTK, 0xFC))
 * DBO NtlMaterialExt.c
 *
 *   NtlMaterialExtStreamWrite:
 *       if (pTexture) { write RwUInt16 uiVersion; RwTextureStreamWrite(pTexture); }
 *   NtlMaterialExtStreamRead:
 *       read RwUInt16 uiVersion
 *       if (uiVersion == NTL_MATERIAL_EXT_VER) { RwStreamFindChunk(rwID_TEXTURE); RwTextureStreamRead() }
 *   NtlMaterialExtStreamGetSize:
 *       nSize = sizeof(uiVersion) + sizeof(RwTexture)      <-- BUG
 *
 * GetSize adds `sizeof(RwTexture)` (the in-memory struct, 88 bytes on x86)
 * instead of `RwTextureStreamGetSize(tex) + rwCHUNKHEADERSIZE`. So the header
 * always declares 2 + 88 = 90 while the real payload is 2 + 12 + streamSize,
 * which ranges from 86 to 138 across the DBO corpus depending on the texture
 * name length and whether the texture carries a USERDATA extension.
 *
 * The declared size is therefore meaningless and the *reader must parse
 * semantically* -- which is exactly what DBO itself does, since the read
 * callback ignores its `length` argument. Nothing else is needed: RW's
 * container loop is driven by declared lengths and stays in step regardless.
 *
 * ---------------------------------------------------------------------------
 * rwID_NTL_WORLD_MATERIAL (0x177 = MAKECHUNKID(rwVENDORID_CRITERIONTK, 0x77))
 * DBO NtlWorldMaterialPlugin.c
 *
 *   rpNtlWorldSectorSplatStreamWrite   -> writes nothing, returns stream
 *   rpNtlWorldSectorSplatStreamGetSize -> sizeof(sRpNtlWorldSectorSplat)
 *   struct sRpNtlWorldSectorSplat { CNtlWorldSector *pNtlWorldSector; }   // 4 bytes
 *
 * The plugin declares 4 bytes and writes none: a header with an empty body.
 * This single fact explains two things seen throughout the corpus --
 *   * the "dangling" 0x177 header whose apparent payload is really the next
 *     chunk's type field, and
 *   * the ubiquitous +4 inflation of every enclosing container's declared size
 *     (RwMaterial EXTENSION, MATERIAL, MATLIST, GEOMETRY, GEOMETRYLIST, CLUMP),
 *     because _rwPluginRegistryGetSize counts those 4 phantom bytes.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"
#include "world/Texture.h"

#include <memory>
#include <string>

/** NTL_MATERIAL_EXT_VER in NtlMaterialExt.c. */
constexpr RwUInt16 kNtlMaterialExtVersion = 0;

/** sizeof(RwTexture) on x86, as used by the buggy GetSize. */
constexpr RwUInt32 kNtlMaterialExtSizeofRwTexture = 88;

/** The size NtlMaterialExtStreamGetSize always reports for a textured material. */
constexpr RwUInt32 kNtlMaterialExtDeclaredSize =
    static_cast<RwUInt32>(sizeof(RwUInt16)) + kNtlMaterialExtSizeofRwTexture;

struct DffNtlMaterialExt
{
    RwUInt32                    chunkVersion = 0;
    RwUInt16                    version = kNtlMaterialExtVersion;
    std::shared_ptr<DffTexture> multiTexture;
    RwChunkSpan                 span;
};

/** sizeof(sRpNtlWorldSectorSplat) -- what the plugin declares but never writes. */
constexpr RwUInt32 kNtlWorldMaterialDeclaredSize = 4;

struct DffNtlWorldMaterial
{
    RwUInt32    chunkVersion = 0;
    RwChunkSpan span;   ///< declaredSize is the phantom body length; actualSize is 0
};

bool ReadNtlMaterialExt(RwStream& stream,
                        DffDiagnostics& diag,
                        DffNtlMaterialExt& out,
                        RwUInt32 payloadSize,
                        RwUInt32 chunkVersion);

bool WriteNtlMaterialExt(RwStream& stream,
                         DffDiagnostics& diag,
                         const DffNtlMaterialExt& ext,
                         RwUInt32 version);

bool ReadNtlWorldMaterial(RwStream& stream,
                          DffDiagnostics& diag,
                          DffNtlWorldMaterial& out,
                          RwUInt32 payloadSize,
                          RwUInt32 chunkVersion);

bool WriteNtlWorldMaterial(RwStream& stream,
                           DffDiagnostics& diag,
                           const DffNtlWorldMaterial& ext,
                           RwUInt32 version);

/**
 * The NTL material flags DBO derives at runtime from the material's UserData
 * name, ported from Helper_SetAtomicAllMaterialSkinInfo
 * (DBO NtlPLEntityRenderHelpers.cpp).
 *
 * The client takes the name up to the first '_' and matches case-insensitively:
 *
 *   "skin"        -> SKIN_COLOR   (plus MIXED for "skin_jacket" on a texture
 *                                  whose name starts with "A_M", the majin set)
 *   "hair"        -> HEAD_COLOR
 *   "dogi_belt"   -> BELT_COLOR
 *   "dogi_emblem" -> EMBLEM
 *   "dogi_pants"  -> DOGI_PANTS
 *   "dogi_skirt"  -> DOGI_SKIRT
 *   anything else -> MIXED
 *
 * These flags are never serialized, so reproducing them is the only way a tool
 * can show what the client will actually do with a material.
 */
RwUInt32 DffNtlFlagsFromMaterialName(const std::string& materialName, const std::string& textureName);
