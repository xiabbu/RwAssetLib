/**
 * plugin/UVAnim.cpp - RpUVAnim material extension read/write.
 */

#include "plugin/UVAnim.h"

std::string DffUVAnimMaterial::SlotName(int slot) const
{
    if (slot < 0 || slot >= kUVAnimMaxSlots) {
        return {};
    }
    const auto& buf = names[static_cast<size_t>(slot)];
    size_t len = 0;
    while (len < buf.size() && buf[len] != 0) {
        ++len;
    }
    return std::string(buf.data(), len);
}

bool ReadUVAnimMaterial(RwStream& stream, DffDiagnostics& diag, DffUVAnimMaterial& out, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_UVANIMPLUGIN);

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    out = DffUVAnimMaterial();
    out.chunkVersion = chunkVersion;

    if (payloadSize < rwCHUNKHEADERSIZE + sizeof(RwUInt32)) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "UVAnim material payload too small",
                         ">= 16",
                         std::to_string(payloadSize));
    }

    RwChunkHeader structHeader;
    if (!stream.ReadChunkHeader(structHeader)) {
        stream.Seek(endPos);
        return diag.Fail(startPos, "truncated UVAnim STRUCT header");
    }
    if (structHeader.type != rwID_STRUCT) {
        stream.Seek(endPos);
        return diag.FailChunkType(startPos, rwID_STRUCT, structHeader.type);
    }
    if (payloadSize != rwCHUNKHEADERSIZE + structHeader.size) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "UVAnim outer/inner size mismatch",
                         std::to_string(rwCHUNKHEADERSIZE + structHeader.size),
                         std::to_string(payloadSize));
    }
    out.structChunkVersion = structHeader.version;

    if (!stream.ReadUInt32(&out.flags)) {
        stream.Seek(endPos);
        return diag.Fail(startPos, "truncated UVAnim flags");
    }

    RwUInt32 expectedStructSize = sizeof(RwUInt32);
    for (int i = 0; i < kUVAnimMaxSlots; ++i) {
        if (!out.HasSlot(i)) {
            continue;
        }
        if (!stream.Read(out.names[static_cast<size_t>(i)].data(), kUVAnimMaxName)) {
            stream.Seek(endPos);
            return diag.Fail(stream.GetPosition(), "truncated UVAnim slot name");
        }
        expectedStructSize += kUVAnimMaxName;
    }

    if (structHeader.size != expectedStructSize) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "UVAnim STRUCT size does not match flag bit count",
                         std::to_string(expectedStructSize),
                         std::to_string(structHeader.size));
    }

    if (!out.span.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    if (!out.structSpan.Record(structHeader.size, structHeader.size, structHeader.version, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    stream.Seek(endPos);
    return true;
}

bool WriteUVAnimMaterial(RwStream& stream, DffDiagnostics& diag, const DffUVAnimMaterial& ext, RwUInt32 version)
{
    DffScope scope(diag, rwID_UVANIMPLUGIN);

    const RwUInt32 outerVersion = ext.chunkVersion ? ext.chunkVersion : version;
    const RwUInt32 structVersion = ext.structChunkVersion ? ext.structChunkVersion : version;

    ChunkWriter writer(stream, outerVersion);
    if (!writer.Begin(rwID_UVANIMPLUGIN, ext.span, outerVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin UVANIM chunk");
    }

    if (!writer.Begin(rwID_STRUCT, ext.structSpan, structVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin UVANIM STRUCT");
    }
    stream.WriteUInt32(ext.flags);
    for (int i = 0; i < kUVAnimMaxSlots; ++i) {
        if (ext.HasSlot(i)) {
            stream.Write(ext.names[static_cast<size_t>(i)].data(), kUVAnimMaxName);
        }
    }
    writer.End();

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close UVANIM chunk");
}
