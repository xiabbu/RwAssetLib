/**
 * plugin/LtMap.cpp - RpLtMap material extension read/write.
 */

#include "plugin/LtMap.h"

bool ReadLtMapMaterial(RwStream& stream, DffDiagnostics& diag, DffLtMapMaterial& out, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_LTMAPPLUGIN);

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    if (payloadSize != kLtMapMaterialChunkSize) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "unexpected LtMap material payload size",
                         std::to_string(kLtMapMaterialChunkSize),
                         std::to_string(payloadSize));
    }

    out = DffLtMapMaterial();
    out.chunkVersion = chunkVersion;

    RwChunkHeader inner;
    if (!stream.ReadChunkHeader(inner)) {
        stream.Seek(endPos);
        return diag.Fail(startPos, "truncated LtMap inner header");
    }
    if (inner.type != rwID_LTMAPPLUGIN) {
        stream.Seek(endPos);
        return diag.FailChunkType(startPos, rwID_LTMAPPLUGIN, inner.type);
    }
    if (inner.size != kLtMapMaterialDataSize) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "unexpected LtMap inner payload size",
                         std::to_string(kLtMapMaterialDataSize),
                         std::to_string(inner.size));
    }
    out.innerChunkVersion = inner.version;

    const bool ok = stream.ReadUInt32(&out.flags) &&
                    stream.ReadReal(&out.lightMapDensity) &&
                    stream.Read(&out.areaLightColour, sizeof(RwRGBA)) &&
                    stream.ReadReal(&out.areaLightDensity) &&
                    stream.ReadReal(&out.areaLightRadius);
    if (!ok) {
        stream.Seek(endPos);
        return diag.Fail(startPos, "truncated LtMap material payload");
    }

    if (!out.span.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    stream.Seek(endPos);
    return true;
}

bool WriteLtMapMaterial(RwStream& stream, DffDiagnostics& diag, const DffLtMapMaterial& ext, RwUInt32 version)
{
    DffScope scope(diag, rwID_LTMAPPLUGIN);

    const RwUInt32 outerVersion = ext.chunkVersion ? ext.chunkVersion : version;
    const RwUInt32 innerVersion = ext.innerChunkVersion ? ext.innerChunkVersion : version;

    ChunkWriter writer(stream, outerVersion);
    if (!writer.Begin(rwID_LTMAPPLUGIN, ext.span, outerVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin LTMAP chunk");
    }

    stream.WriteChunkHeader(rwID_LTMAPPLUGIN, kLtMapMaterialDataSize, innerVersion);
    stream.WriteUInt32(ext.flags);
    stream.WriteReal(ext.lightMapDensity);
    stream.Write(&ext.areaLightColour, sizeof(RwRGBA));
    stream.WriteReal(ext.areaLightDensity);
    stream.WriteReal(ext.areaLightRadius);

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close LTMAP chunk");
}
