/**
 * world/Material.cpp - RpMaterial read/write.
 */

#include "world/Material.h"

bool DffMaterial::IsKnownExtension(RwUInt32 type)
{
    switch (type) {
        case rwID_MATFXPLUGIN:
        case rwID_TOONPLUGIN:
        case rwID_LTMAPPLUGIN:
        case rwID_UVANIMPLUGIN:
        case rwID_USERDATAPLUGIN:
        case rwID_NTL_MATERIAL_EXT:
        case rwID_NTL_WORLD_MATERIAL:
            return true;
        default:
            return false;
    }
}

const std::string* DffMaterial::UserDataName() const
{
    for (const auto& list : userDataLists) {
        if (const DffUserDataString* s = list.FindString("name")) {
            if (!s->value.empty()) {
                return &s->value;
            }
        }
    }
    // Fall back to the first non-empty string array of any name.
    for (const auto& list : userDataLists) {
        if (const DffUserDataString* s = list.FindString(nullptr)) {
            if (!s->value.empty()) {
                return &s->value;
            }
        }
    }
    return nullptr;
}

DffUserDataString* DffMaterial::EnsureUserDataName()
{
    if (userDataLists.empty()) {
        userDataLists.emplace_back();
    }
    return userDataLists.front().FindOrCreateString("name");
}

bool ReadMaterial(RwStream& stream, DffDiagnostics& diag, DffMaterial& mat, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    const RwUInt32 startPos = stream.GetPosition();

    mat.texture.reset();
    mat.matfx.reset();
    mat.toon.reset();
    mat.ltmap.reset();
    mat.uvanim.reset();
    mat.ntlMaterialExt.reset();
    mat.ntlWorldMaterials.clear();
    mat.userDataLists.clear();
    mat.extensions.Clear();

    // ---- STRUCT ------------------------------------------------------------
    RwBool textured = 0;
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated MATERIAL STRUCT header");
        }
        if (header.type != rwID_STRUCT) {
            return diag.FailChunkType(headerPos, rwID_STRUCT, header.type);
        }

        const RwUInt32 dataStart = stream.GetPosition();
        const bool ok = stream.ReadInt32(&mat.flags) &&
                        stream.Read(&mat.color, sizeof(RwRGBA)) &&
                        stream.ReadInt32(&mat.unused) &&
                        stream.ReadInt32(&textured) &&
                        stream.ReadReal(&mat.surfaceProps.ambient) &&
                        stream.ReadReal(&mat.surfaceProps.specular) &&
                        stream.ReadReal(&mat.surfaceProps.diffuse);
        if (!ok) {
            return diag.Fail(dataStart, "truncated RpMaterialChunkInfo");
        }

        if (!mat.structSpan.Record(header.size, header.size, header.version ? header.version : chunkVersion, stream)) {
            return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
        }
        stream.Seek(dataStart + header.size);
    }

    // ---- TEXTURE -----------------------------------------------------------
    if (textured) {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated MATERIAL TEXTURE header");
        }
        if (header.type != rwID_TEXTURE) {
            return diag.FailChunkType(headerPos, rwID_TEXTURE, header.type);
        }

        const RwUInt32 texEnd = stream.GetPosition() + header.size;
        mat.texture = std::make_shared<DffTexture>();
        if (!ReadTexture(stream, diag, *mat.texture, header.size, header.version)) {
            return false;
        }
        stream.Seek(texEnd);
    }

    // ---- EXTENSION ---------------------------------------------------------
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated MATERIAL EXTENSION header");
        }
        if (header.type != rwID_EXTENSION) {
            return diag.FailChunkType(headerPos, rwID_EXTENSION, header.type);
        }

        const RwUInt32 extVersion = header.version ? header.version : chunkVersion;

        const bool ok = ReadExtensionContainer(
            stream, diag, header.size, extVersion, mat.extensions,
            [&](const RwChunkHeader& sub, bool& handled) -> bool {
                switch (sub.type) {
                    case rwID_NTL_WORLD_MATERIAL:
                    {
                        DffNtlWorldMaterial ext;
                        if (!ReadNtlWorldMaterial(stream, diag, ext, sub.size, sub.version)) {
                            return false;
                        }
                        mat.ntlWorldMaterials.push_back(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_NTL_MATERIAL_EXT:
                    {
                        auto ext = std::make_shared<DffNtlMaterialExt>();
                        if (!ReadNtlMaterialExt(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        mat.ntlMaterialExt = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_MATFXPLUGIN:
                    {
                        auto ext = std::make_shared<DffMatFXMaterial>();
                        if (!ReadMatFXMaterial(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        mat.matfx = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_TOONPLUGIN:
                    {
                        auto ext = std::make_shared<DffToonMaterial>();
                        if (!ReadToonMaterial(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        mat.toon = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_LTMAPPLUGIN:
                    {
                        auto ext = std::make_shared<DffLtMapMaterial>();
                        if (!ReadLtMapMaterial(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        mat.ltmap = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_UVANIMPLUGIN:
                    {
                        auto ext = std::make_shared<DffUVAnimMaterial>();
                        if (!ReadUVAnimMaterial(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        mat.uvanim = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_USERDATAPLUGIN:
                    {
                        DffUserDataList ud;
                        if (!ReadUserDataList(stream, diag, ud, sub.size, sub.version)) {
                            return false;
                        }
                        mat.userDataLists.push_back(std::move(ud));
                        handled = true;
                        return true;
                    }

                    default:
                        return true;  // keep verbatim
                }
            });
        if (!ok) {
            return false;
        }
    }

    const RwUInt32 consumed = stream.GetPosition() - startPos;
    if (!mat.materialSpan.Record(payloadSize, consumed, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}

bool WriteMaterial(RwStream& stream, DffDiagnostics& diag, const DffMaterial& mat, RwUInt32 version)
{
    ChunkWriter writer(stream, version);

    if (!writer.Begin(rwID_MATERIAL, mat.materialSpan, mat.materialSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin MATERIAL chunk");
    }

    // STRUCT
    if (!writer.Begin(rwID_STRUCT, mat.structSpan, mat.structSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin MATERIAL STRUCT");
    }
    stream.WriteInt32(mat.flags);
    stream.Write(&mat.color, sizeof(RwRGBA));
    stream.WriteInt32(mat.unused);
    stream.WriteInt32(mat.texture ? 1 : 0);
    stream.WriteReal(mat.surfaceProps.ambient);
    stream.WriteReal(mat.surfaceProps.specular);
    stream.WriteReal(mat.surfaceProps.diffuse);
    writer.End();

    // TEXTURE
    if (mat.texture) {
        if (!WriteTexture(stream, diag, *mat.texture, version)) {
            return false;
        }
    }

    // EXTENSION
    if (!writer.Begin(rwID_EXTENSION, mat.extensions.span, mat.extensions.span.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin MATERIAL EXTENSION");
    }

    if (!mat.extensions.order.empty()) {
        size_t ntlWorldIdx = 0;
        size_t userDataIdx = 0;
        RawChunkCursor rawCursor(mat.extensions.raws);
        bool wroteMatfx = false;
        bool wroteToon = false;
        bool wroteLtMap = false;
        bool wroteUvAnim = false;
        bool wroteNtlExt = false;

        auto writeRaw = [&](RwUInt32 type) -> bool {
            const RawChunk* raw = rawCursor.Next(type);
            if (!raw) {
                return diag.Fail(stream.GetPosition(),
                                 "material extension order/raw mismatch",
                                 RwChunkTypeLabel(type),
                                 "<missing>");
            }
            writer.WriteRaw(*raw);
            return true;
        };

        for (RwUInt32 type : mat.extensions.order) {
            switch (type) {
                case rwID_NTL_WORLD_MATERIAL:
                    if (ntlWorldIdx < mat.ntlWorldMaterials.size()) {
                        if (!WriteNtlWorldMaterial(stream, diag, mat.ntlWorldMaterials[ntlWorldIdx++], version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                case rwID_NTL_MATERIAL_EXT:
                    if (mat.ntlMaterialExt && !wroteNtlExt) {
                        wroteNtlExt = true;
                        if (!WriteNtlMaterialExt(stream, diag, *mat.ntlMaterialExt, version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                case rwID_MATFXPLUGIN:
                    if (mat.matfx && !wroteMatfx) {
                        wroteMatfx = true;
                        if (!WriteMatFXMaterial(stream, diag, *mat.matfx, version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                case rwID_TOONPLUGIN:
                    if (mat.toon && !wroteToon) {
                        wroteToon = true;
                        if (!WriteToonMaterial(stream, diag, *mat.toon, version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                case rwID_LTMAPPLUGIN:
                    if (mat.ltmap && !wroteLtMap) {
                        wroteLtMap = true;
                        if (!WriteLtMapMaterial(stream, diag, *mat.ltmap, version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                case rwID_UVANIMPLUGIN:
                    if (mat.uvanim && !wroteUvAnim) {
                        wroteUvAnim = true;
                        if (!WriteUVAnimMaterial(stream, diag, *mat.uvanim, version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                case rwID_USERDATAPLUGIN:
                    if (userDataIdx < mat.userDataLists.size()) {
                        if (!WriteUserDataList(stream, diag, mat.userDataLists[userDataIdx++], version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                default:
                    if (!writeRaw(type)) {
                        return false;
                    }
                    break;
            }
        }

        if (ntlWorldIdx != mat.ntlWorldMaterials.size() ||
            userDataIdx != mat.userDataLists.size() ||
            !rawCursor.Exhausted()) {
            return diag.Fail(stream.GetPosition(), "material extension list not fully consumed");
        }
    } else {
        // Freshly built material: emit in RW plugin registration order.
        if (mat.matfx && !WriteMatFXMaterial(stream, diag, *mat.matfx, version)) {
            return false;
        }
        if (mat.toon && !WriteToonMaterial(stream, diag, *mat.toon, version)) {
            return false;
        }
        if (mat.ltmap && !WriteLtMapMaterial(stream, diag, *mat.ltmap, version)) {
            return false;
        }
        if (mat.uvanim && !WriteUVAnimMaterial(stream, diag, *mat.uvanim, version)) {
            return false;
        }
        for (const auto& ud : mat.userDataLists) {
            if (!WriteUserDataList(stream, diag, ud, version)) {
                return false;
            }
        }
        if (mat.ntlMaterialExt && !WriteNtlMaterialExt(stream, diag, *mat.ntlMaterialExt, version)) {
            return false;
        }
        for (const auto& nw : mat.ntlWorldMaterials) {
            if (!WriteNtlWorldMaterial(stream, diag, nw, version)) {
                return false;
            }
        }
        for (const auto& raw : mat.extensions.raws) {
            writer.WriteRaw(raw);
        }
    }

    writer.End();  // EXTENSION
    writer.End();  // MATERIAL
    return true;
}
