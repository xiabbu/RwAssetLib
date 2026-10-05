/**
 * plugin/MatFX.h - RpMatFX (rwID_MATFXPLUGIN, 0x120).
 *
 * Material payload (RenderWare rpmatfx.c,
 * MatFXMaterialStreamWrite):
 *
 *   RwInt32 flags                       (RpMatFXMaterialFlags for the material)
 *   per pass (rpMAXPASS == 2):
 *     RwInt32 effect
 *     rpMATFXEFFECTNULL / UVTRANSFORM : no payload
 *     rpMATFXEFFECTBUMPMAP            : RwReal coef, texture, bumpTexture
 *     rpMATFXEFFECTENVMAP             : RwReal coef, RwInt32 useFrameBufferAlpha, texture
 *     rpMATFXEFFECTDUAL               : RwInt32 srcBlend, RwInt32 dstBlend, texture
 *
 * where "texture" is _rpMatFXStreamWriteTexture: RwInt32 present, then a full
 * TEXTURE chunk when present.
 *
 * Note: the bump-map writer stores the *negated* coefficient (`coef = -coef`),
 * after SetBumpMapCoefficient already negates it internally. The streamed
 * value here equals RpMatFXMaterialGetBumpMapCoefficient.
 *
 * Atomic payload (MatFXAtomicStreamWrite): a single RwInt32 "effects enabled".
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"
#include "world/Texture.h"

#include <memory>

struct DffMatFXTextureRef
{
    RwInt32                     present = 0;
    std::shared_ptr<DffTexture> texture;

    void Set(const std::string& name);
    void Clear()
    {
        present = 0;
        texture.reset();
    }
};

struct DffMatFXBumpMap
{
    RwReal             coef = 0;   ///< streamed value == user coefficient
    DffMatFXTextureRef texture;
    DffMatFXTextureRef bumpTexture;
};

struct DffMatFXEnvMap
{
    RwReal             coef = 0;
    RwInt32            useFrameBufferAlpha = 0;
    DffMatFXTextureRef texture;
};

struct DffMatFXDual
{
    RwInt32            srcBlendMode = 0;
    RwInt32            dstBlendMode = 0;
    DffMatFXTextureRef texture;
};

struct DffMatFXPass
{
    RpMatFXMaterialFlags effect = rpMATFXEFFECTNULL;
    DffMatFXBumpMap      bumpMap;
    DffMatFXEnvMap       envMap;
    DffMatFXDual         dual;
};

/** rpMAXPASS in rpmatfx.h. */
constexpr int kMatFXMaxPass = 2;

struct DffMatFXMaterial
{
    RpMatFXMaterialFlags flags = rpMATFXEFFECTNULL;
    RwUInt32             chunkVersion = 0;
    DffMatFXPass         passes[kMatFXMaxPass];
    RwChunkSpan          span;

    /** Rebuilds `passes` to the canonical layout for `flags`, keeping textures. */
    void ApplyEffectLayout(RpMatFXMaterialFlags newFlags);
};

struct DffMatFXAtomic
{
    RwUInt32    chunkVersion = 0;
    RwInt32     enabled = 0;
    RwChunkSpan span;
};

/** Reads the MATFX material payload (header already consumed). */
bool ReadMatFXMaterial(RwStream& stream,
                       DffDiagnostics& diag,
                       DffMatFXMaterial& out,
                       RwUInt32 payloadSize,
                       RwUInt32 chunkVersion);

/** Writes a complete MATFX material chunk. */
bool WriteMatFXMaterial(RwStream& stream,
                        DffDiagnostics& diag,
                        const DffMatFXMaterial& matfx,
                        RwUInt32 version);

/** Reads the MATFX atomic payload (header already consumed). */
bool ReadMatFXAtomic(RwStream& stream,
                     DffDiagnostics& diag,
                     DffMatFXAtomic& out,
                     RwUInt32 payloadSize,
                     RwUInt32 chunkVersion);

/** Writes a complete MATFX atomic chunk. */
bool WriteMatFXAtomic(RwStream& stream,
                      DffDiagnostics& diag,
                      const DffMatFXAtomic& matfx,
                      RwUInt32 version);
