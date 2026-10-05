/**
 * plugin/MatFX.cpp - RpMatFX material/atomic read/write.
 */

#include "plugin/MatFX.h"

void DffMatFXTextureRef::Set(const std::string& name)
{
    present = 1;
    if (!texture) {
        texture = std::make_shared<DffTexture>();
    }
    if (texture->name != name) {
        texture->SetName(name);
    }
}

void DffMatFXMaterial::ApplyEffectLayout(RpMatFXMaterialFlags newFlags)
{
    flags = newFlags;

    switch (newFlags) {
        case rpMATFXEFFECTNULL:
            passes[0].effect = rpMATFXEFFECTNULL;
            passes[1].effect = rpMATFXEFFECTNULL;
            break;

        case rpMATFXEFFECTBUMPMAP:
            passes[0].effect = rpMATFXEFFECTBUMPMAP;
            passes[1].effect = rpMATFXEFFECTNULL;
            break;

        case rpMATFXEFFECTENVMAP:
            passes[0].effect = rpMATFXEFFECTENVMAP;
            passes[1].effect = rpMATFXEFFECTNULL;
            break;

        case rpMATFXEFFECTBUMPENVMAP:
            passes[0].effect = rpMATFXEFFECTBUMPMAP;
            passes[1].effect = rpMATFXEFFECTENVMAP;
            break;

        case rpMATFXEFFECTDUAL:
            passes[0].effect = rpMATFXEFFECTDUAL;
            passes[1].effect = rpMATFXEFFECTNULL;
            break;

        case rpMATFXEFFECTUVTRANSFORM:
            passes[0].effect = rpMATFXEFFECTUVTRANSFORM;
            passes[1].effect = rpMATFXEFFECTNULL;
            break;

        case rpMATFXEFFECTDUALUVTRANSFORM:
            passes[0].effect = rpMATFXEFFECTUVTRANSFORM;
            passes[1].effect = rpMATFXEFFECTDUAL;
            break;
    }
}

namespace
{
    bool ReadTextureRef(RwStream& stream, DffDiagnostics& diag, DffMatFXTextureRef& ref, RwUInt32 version)
    {
        const RwUInt32 pos = stream.GetPosition();

        if (!stream.ReadInt32(&ref.present)) {
            return diag.Fail(pos, "truncated MatFX texture presence flag");
        }

        ref.texture.reset();
        if (!ref.present) {
            return true;
        }

        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated MatFX TEXTURE header");
        }
        if (header.type != rwID_TEXTURE) {
            return diag.FailChunkType(headerPos, rwID_TEXTURE, header.type);
        }

        const RwUInt32 texEnd = stream.GetPosition() + header.size;
        ref.texture = std::make_shared<DffTexture>();
        if (!ReadTexture(stream, diag, *ref.texture, header.size, header.version)) {
            ref.texture.reset();
            stream.Seek(texEnd);
            return false;
        }
        stream.Seek(texEnd);
        return true;
    }

    bool WriteTextureRef(RwStream& stream, DffDiagnostics& diag, const DffMatFXTextureRef& ref, RwUInt32 version)
    {
        stream.WriteInt32(ref.present);
        if (!ref.present) {
            return true;
        }
        if (!ref.texture) {
            return diag.Fail(stream.GetPosition(), "MatFX texture marked present but missing");
        }
        return WriteTexture(stream, diag, *ref.texture, version);
    }
}

bool ReadMatFXMaterial(RwStream& stream,
                       DffDiagnostics& diag,
                       DffMatFXMaterial& out,
                       RwUInt32 payloadSize,
                       RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_MATFXPLUGIN);

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    out = DffMatFXMaterial();
    out.chunkVersion = chunkVersion;

    RwInt32 flags = 0;
    if (!stream.ReadInt32(&flags)) {
        return diag.Fail(startPos, "truncated MatFX material flags");
    }
    out.flags = static_cast<RpMatFXMaterialFlags>(flags);

    for (int pass = 0; pass < kMatFXMaxPass; ++pass) {
        const RwUInt32 passPos = stream.GetPosition();

        RwInt32 effect = 0;
        if (!stream.ReadInt32(&effect)) {
            return diag.Fail(passPos, "truncated MatFX pass effect");
        }
        out.passes[pass].effect = static_cast<RpMatFXMaterialFlags>(effect);

        switch (static_cast<RpMatFXMaterialFlags>(effect)) {
            case rpMATFXEFFECTNULL:
            case rpMATFXEFFECTUVTRANSFORM:
                break;

            case rpMATFXEFFECTBUMPMAP:
            {
                auto& bump = out.passes[pass].bumpMap;
                if (!stream.ReadReal(&bump.coef)) {
                    return diag.Fail(stream.GetPosition(), "truncated MatFX bumpMap coefficient");
                }
                if (!ReadTextureRef(stream, diag, bump.texture, chunkVersion)) {
                    return false;
                }
                if (!ReadTextureRef(stream, diag, bump.bumpTexture, chunkVersion)) {
                    return false;
                }
                break;
            }

            case rpMATFXEFFECTENVMAP:
            {
                auto& env = out.passes[pass].envMap;
                if (!stream.ReadReal(&env.coef)) {
                    return diag.Fail(stream.GetPosition(), "truncated MatFX envMap coefficient");
                }
                if (!stream.ReadInt32(&env.useFrameBufferAlpha)) {
                    return diag.Fail(stream.GetPosition(), "truncated MatFX envMap useFrameBufferAlpha");
                }
                if (!ReadTextureRef(stream, diag, env.texture, chunkVersion)) {
                    return false;
                }
                break;
            }

            case rpMATFXEFFECTDUAL:
            {
                auto& dual = out.passes[pass].dual;
                if (!stream.ReadInt32(&dual.srcBlendMode) || !stream.ReadInt32(&dual.dstBlendMode)) {
                    return diag.Fail(stream.GetPosition(), "truncated MatFX dual blend modes");
                }
                if (!ReadTextureRef(stream, diag, dual.texture, chunkVersion)) {
                    return false;
                }
                break;
            }

            default:
                return diag.Fail(passPos,
                                 "unknown MatFX pass effect",
                                 "0..6",
                                 std::to_string(effect));
        }
    }

    const RwUInt32 consumed = stream.GetPosition() - startPos;
    if (!out.span.Record(payloadSize, consumed, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }

    stream.Seek(endPos);
    return true;
}

bool WriteMatFXMaterial(RwStream& stream,
                        DffDiagnostics& diag,
                        const DffMatFXMaterial& matfx,
                        RwUInt32 version)
{
    DffScope scope(diag, rwID_MATFXPLUGIN);

    const RwUInt32 chunkVersion = matfx.chunkVersion ? matfx.chunkVersion : version;
    ChunkWriter writer(stream, chunkVersion);

    if (!writer.Begin(rwID_MATFXPLUGIN, matfx.span, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin MATFX chunk");
    }

    stream.WriteInt32(static_cast<RwInt32>(matfx.flags));

    for (int pass = 0; pass < kMatFXMaxPass; ++pass) {
        const auto& p = matfx.passes[pass];
        stream.WriteInt32(static_cast<RwInt32>(p.effect));

        switch (p.effect) {
            case rpMATFXEFFECTNULL:
            case rpMATFXEFFECTUVTRANSFORM:
                break;

            case rpMATFXEFFECTBUMPMAP:
                stream.WriteReal(p.bumpMap.coef);
                if (!WriteTextureRef(stream, diag, p.bumpMap.texture, version) ||
                    !WriteTextureRef(stream, diag, p.bumpMap.bumpTexture, version)) {
                    return false;
                }
                break;

            case rpMATFXEFFECTENVMAP:
                stream.WriteReal(p.envMap.coef);
                stream.WriteInt32(p.envMap.useFrameBufferAlpha);
                if (!WriteTextureRef(stream, diag, p.envMap.texture, version)) {
                    return false;
                }
                break;

            case rpMATFXEFFECTDUAL:
                stream.WriteInt32(p.dual.srcBlendMode);
                stream.WriteInt32(p.dual.dstBlendMode);
                if (!WriteTextureRef(stream, diag, p.dual.texture, version)) {
                    return false;
                }
                break;

            default:
                return diag.Fail(stream.GetPosition(),
                                 "cannot write unknown MatFX pass effect",
                                 "0..6",
                                 std::to_string(static_cast<int>(p.effect)));
        }
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close MATFX chunk");
}

// ============================================================================
// Atomic payload (rpmatfx.c: MatFXAtomicStreamRead / MatFXAtomicStreamWrite)
// ============================================================================

bool ReadMatFXAtomic(RwStream& stream,
                     DffDiagnostics& diag,
                     DffMatFXAtomic& out,
                     RwUInt32 payloadSize,
                     RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_MATFXPLUGIN);

    const RwUInt32 startPos = stream.GetPosition();
    out = DffMatFXAtomic();
    out.chunkVersion = chunkVersion;

    if (payloadSize != sizeof(RwInt32)) {
        stream.Seek(startPos + payloadSize);
        return diag.Fail(startPos,
                         "MATFX atomic payload is not a single RwInt32",
                         std::to_string(sizeof(RwInt32)),
                         std::to_string(payloadSize));
    }

    if (!stream.ReadInt32(&out.enabled)) {
        return diag.Fail(startPos, "truncated MATFX atomic payload");
    }

    if (!out.span.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}

bool WriteMatFXAtomic(RwStream& stream,
                      DffDiagnostics& diag,
                      const DffMatFXAtomic& matfx,
                      RwUInt32 version)
{
    DffScope scope(diag, rwID_MATFXPLUGIN);

    const RwUInt32 chunkVersion = matfx.chunkVersion ? matfx.chunkVersion : version;
    ChunkWriter writer(stream, chunkVersion);

    if (!writer.Begin(rwID_MATFXPLUGIN, matfx.span, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin MATFX atomic chunk");
    }
    stream.WriteInt32(matfx.enabled);
    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close MATFX atomic chunk");
}
