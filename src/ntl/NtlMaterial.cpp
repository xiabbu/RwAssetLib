/**
 * ntl/NtlMaterial.cpp - DBO NTL material plugins read/write.
 */

#include "ntl/NtlMaterial.h"

#include <cctype>
#include <cstring>

bool ReadNtlMaterialExt(RwStream& stream,
                        DffDiagnostics& diag,
                        DffNtlMaterialExt& out,
                        RwUInt32 payloadSize,
                        RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_NTL_MATERIAL_EXT);

    const RwUInt32 startPos = stream.GetPosition();

    out = DffNtlMaterialExt();
    out.chunkVersion = chunkVersion;

    if (!stream.ReadUInt16(&out.version)) {
        return diag.Fail(startPos, "truncated NTL material ext version");
    }

    // NtlMaterialExtStreamRead only looks for a texture at the known version.
    if (out.version == kNtlMaterialExtVersion) {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated NTL material ext TEXTURE header");
        }
        if (header.type != rwID_TEXTURE) {
            return diag.FailChunkType(headerPos, rwID_TEXTURE, header.type);
        }

        out.multiTexture = std::make_shared<DffTexture>();
        if (!ReadTexture(stream, diag, *out.multiTexture, header.size, header.version)) {
            out.multiTexture.reset();
            return false;
        }
        // The nested TEXTURE header size is trustworthy (RwTextureStreamGetSize
        // is correct); only the *outer* NTL size is wrong.
        stream.Seek(headerPos + rwCHUNKHEADERSIZE + header.size);
    }

    const RwUInt32 consumed = stream.GetPosition() - startPos;
    if (!out.span.Record(payloadSize, consumed, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }

    // Deliberately no Seek to startPos+payloadSize: the declared size is a
    // known-bad value (see header comment) and the enclosing container's loop
    // accounts for it via the declared-length arithmetic.
    return true;
}

bool WriteNtlMaterialExt(RwStream& stream,
                         DffDiagnostics& diag,
                         const DffNtlMaterialExt& ext,
                         RwUInt32 version)
{
    DffScope scope(diag, rwID_NTL_MATERIAL_EXT);

    const RwUInt32 chunkVersion = ext.chunkVersion ? ext.chunkVersion : version;

    // NtlMaterialExtStreamWrite emits nothing when there is no texture, and
    // GetSize returns 0, so the chunk is not written at all.
    if (!ext.multiTexture) {
        return true;
    }

    // NtlMaterialExtStreamGetSize reports sizeof(uiVersion) + sizeof(RwTexture)
    // no matter what the texture actually streams to, so the declared size is a
    // constant. Modelling it means a document rebuilt from scratch reproduces
    // the same header as the DBO exporter, with nothing recorded from a file.
    ChunkWriter writer(stream, chunkVersion);
    if (!writer.BeginModelledSize(rwID_NTL_MATERIAL_EXT, kNtlMaterialExtDeclaredSize, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin NTL_MATERIAL_EXT chunk");
    }

    stream.WriteUInt16(ext.version);
    if (!WriteTexture(stream, diag, *ext.multiTexture, version)) {
        return false;
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close NTL_MATERIAL_EXT chunk");
}

bool ReadNtlWorldMaterial(RwStream& stream,
                          DffDiagnostics& diag,
                          DffNtlWorldMaterial& out,
                          RwUInt32 payloadSize,
                          RwUInt32 chunkVersion)
{
    (void)diag;

    // rpNtlWorldSectorSplatStreamRead consumes nothing. The declared size is the
    // in-memory struct size (4) and there is no body to read.
    out.chunkVersion = chunkVersion;
    if (!out.span.Record(payloadSize, 0, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    (void)stream;
    return true;
}

bool WriteNtlWorldMaterial(RwStream& stream,
                           DffDiagnostics& diag,
                           const DffNtlWorldMaterial& ext,
                           RwUInt32 version)
{
    DffScope scope(diag, rwID_NTL_WORLD_MATERIAL);

    const RwUInt32 chunkVersion = ext.chunkVersion ? ext.chunkVersion : version;
    // rpNtlWorldSectorSplatStreamGetSize is sizeof(sRpNtlWorldSectorSplat) == 4,
    // a constant, so nothing needs to be recorded from the source file.
    const RwUInt32 declared = kNtlWorldMaterialDeclaredSize;

    // Header only -- matching a write callback that emits no bytes.
    if (!stream.WriteChunkHeader(rwID_NTL_WORLD_MATERIAL, declared, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to write NTL_WORLD_MATERIAL header");
    }

    // The header claims `declared` bytes that were never written. RW's reader
    // advances its container counter by the declared length, so the enclosing
    // EXTENSION must declare those phantom bytes too -- this is the origin of
    // the "+4 at every level" offset seen throughout the DBO corpus.
    stream.AddDeclaredExcess(static_cast<std::int64_t>(declared));
    return true;
}

// ============================================================================
// Runtime NTL flag derivation (NtlPLEntityRenderHelpers.cpp)
// ============================================================================

RwUInt32 DffNtlFlagsFromMaterialName(const std::string& materialName, const std::string& textureName)
{
    if (materialName.empty()) {
        return rpNTL_MATERIAL_INVALID;
    }

    auto equalsNoCase = [](const std::string& a, const char* b) {
        const size_t n = std::strlen(b);
        if (a.size() != n) {
            return false;
        }
        for (size_t i = 0; i < n; ++i) {
            const unsigned char ca = static_cast<unsigned char>(a[i]);
            const unsigned char cb = static_cast<unsigned char>(b[i]);
            if (std::tolower(ca) != std::tolower(cb)) {
                return false;
            }
        }
        return true;
    };

    // The client truncates at the first underscore before matching the prefix.
    const size_t underscore = materialName.find('_');
    const std::string prefix = (underscore == std::string::npos) ? materialName
                                                                 : materialName.substr(0, underscore);

    RwUInt32 flags = rpNTL_MATERIAL_INVALID;

    if (equalsNoCase(prefix, "skin")) {
        // Majin costumes reuse "skin_jacket" for a piece that also needs MIXED;
        // they are told apart by the "A_M" texture prefix.
        const bool majin = (textureName.compare(0, 3, "A_M") == 0);
        if (equalsNoCase(materialName, "skin_jacket") && majin) {
            flags |= rpNTL_MATERIAL_MIXED;
        }
        flags |= rpNTL_MATERIAL_SKIN_COLOR;
    } else if (equalsNoCase(prefix, "hair")) {
        flags |= rpNTL_MATERIAL_HEAD_COLOR;
    } else if (equalsNoCase(materialName, "dogi_belt")) {
        flags |= rpNTL_MATERIAL_BELT_COLOR;
    } else if (equalsNoCase(materialName, "dogi_emblem")) {
        flags |= rpNTL_MATERIAL_EMBLEM;
    } else if (equalsNoCase(materialName, "dogi_pants")) {
        flags |= rpNTL_MATERIAL_DOGI_PANTS;
    } else if (equalsNoCase(materialName, "dogi_skirt")) {
        flags |= rpNTL_MATERIAL_DOGI_SKIRT;
    } else {
        flags |= rpNTL_MATERIAL_MIXED;
    }

    return flags;
}
