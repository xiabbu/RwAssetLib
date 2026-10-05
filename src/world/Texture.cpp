/**
 * world/Texture.cpp - RwTexture read/write.
 */

#include "world/Texture.h"

#include <cstring>

namespace
{
    std::string DecodeUpToNul(const std::vector<RwUInt8>& bytes)
    {
        size_t len = 0;
        while (len < bytes.size() && bytes[len] != 0) {
            ++len;
        }
        if (len == 0) {
            return {};
        }
        return std::string(reinterpret_cast<const char*>(bytes.data()), len);
    }

    /** Reads one STRING chunk into `raw` + decoded `text`. */
    bool ReadStringChunk(RwStream& stream,
                         DffDiagnostics& diag,
                         std::vector<RwUInt8>& raw,
                         std::string& text,
                         RwChunkSpan& span,
                         RwUInt32 defaultVersion)
    {
        const RwUInt32 headerPos = stream.GetPosition();

        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated STRING header");
        }
        if (header.type != rwID_STRING) {
            return diag.FailChunkType(headerPos, rwID_STRING, header.type);
        }

        raw.clear();
        text.clear();

        if (header.size > 0) {
            raw.resize(header.size);
            if (!stream.Read(raw.data(), header.size)) {
                return diag.Fail(headerPos, "truncated STRING payload");
            }
            text = DecodeUpToNul(raw);
        }

        if (!span.Record(header.size, header.size, header.version ? header.version : defaultVersion, stream)) {
            return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
        }
        return true;
    }
}

std::vector<RwUInt8> EncodeRwString(const std::string& s)
{
    // _rwStringStreamGetSize: ((strlen + 1) + 3) & ~3
    const size_t padded = ((s.size() + 1) + 3) & ~static_cast<size_t>(3);
    std::vector<RwUInt8> out(padded, 0);
    if (!s.empty()) {
        std::memcpy(out.data(), s.data(), s.size());
    }
    return out;
}

std::vector<RwUInt8> EncodeMsvcDebugRwString(const std::string& s)
{
    const size_t padded = (s.size() + 4) & ~static_cast<size_t>(3);
    std::vector<RwUInt8> out(padded, 0xCD);
    std::memcpy(out.data(), s.c_str(), s.size() + 1);
    return out;
}

bool ReadTexture(RwStream& stream, DffDiagnostics& diag, DffTexture& tex, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_TEXTURE);

    const RwUInt32 startPos = stream.GetPosition();

    tex.extensions.Clear();
    tex.userDataLists.clear();

    // ---- STRUCT { filterAndAddress } --------------------------------------
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated TEXTURE STRUCT header");
        }
        if (header.type != rwID_STRUCT) {
            return diag.FailChunkType(headerPos, rwID_STRUCT, header.type);
        }

        const RwUInt32 dataStart = stream.GetPosition();
        RwUInt32 filterAndAddress = 0;

        if (header.size >= 4) {
            if (!stream.ReadUInt32(&filterAndAddress)) {
                return diag.Fail(dataStart, "truncated TEXTURE filterAndAddress");
            }
        } else if (header.size >= 2) {
            // Pre-3.04 streams packed filter/addressing into 16 bits.
            RwUInt16 packed = 0;
            if (!stream.ReadUInt16(&packed)) {
                return diag.Fail(dataStart, "truncated TEXTURE filterAndAddress (16-bit form)");
            }
            filterAndAddress = packed;
        }

        tex.filterAndAddress = filterAndAddress;
        if (!tex.structSpan.Record(header.size, header.size, header.version ? header.version : chunkVersion, stream)) {
            return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
        }
        stream.Seek(dataStart + header.size);
    }

    // ---- STRING name / STRING mask ----------------------------------------
    if (!ReadStringChunk(stream, diag, tex.rawNameBytes, tex.name, tex.nameSpan, chunkVersion)) {
        return false;
    }
    if (!ReadStringChunk(stream, diag, tex.rawMaskBytes, tex.maskName, tex.maskSpan, chunkVersion)) {
        return false;
    }

    // ---- EXTENSION ---------------------------------------------------------
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated TEXTURE EXTENSION header");
        }
        if (header.type != rwID_EXTENSION) {
            return diag.FailChunkType(headerPos, rwID_EXTENSION, header.type);
        }

        const bool ok = ReadExtensionContainer(
            stream, diag, header.size, header.version ? header.version : chunkVersion, tex.extensions,
            [&](const RwChunkHeader& sub, bool& handled) -> bool {
                if (sub.type != rwID_USERDATAPLUGIN) {
                    return true;  // keep verbatim
                }
                DffUserDataList ud;
                if (!ReadUserDataList(stream, diag, ud, sub.size, sub.version)) {
                    return false;
                }
                tex.userDataLists.push_back(std::move(ud));
                handled = true;
                return true;
            });
        if (!ok) {
            return false;
        }
    }

    const RwUInt32 consumed = stream.GetPosition() - startPos;
    if (!tex.textureSpan.Record(payloadSize, consumed, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}

bool WriteTexture(RwStream& stream, DffDiagnostics& diag, const DffTexture& tex, RwUInt32 version)
{
    DffScope scope(diag, rwID_TEXTURE);

    ChunkWriter writer(stream, version);
    if (!writer.Begin(rwID_TEXTURE, tex.textureSpan, tex.textureSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin TEXTURE chunk");
    }

    // STRUCT
    if (!writer.Begin(rwID_STRUCT, tex.structSpan, tex.structSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin TEXTURE STRUCT");
    }
    stream.WriteUInt32(tex.filterAndAddress);
    writer.End();

    // STRING name
    {
        const std::vector<RwUInt8> bytes = tex.rawNameBytes.empty() ? EncodeRwString(tex.name) : tex.rawNameBytes;
        if (!writer.Begin(rwID_STRING, tex.nameSpan, tex.nameSpan.ResolveVersion(version))) {
            return diag.Fail(stream.GetPosition(), "failed to begin TEXTURE name STRING");
        }
        if (!bytes.empty()) {
            stream.Write(bytes.data(), bytes.size());
        }
        writer.End();
    }

    // STRING mask
    {
        std::vector<RwUInt8> bytes = tex.rawMaskBytes;
        if (bytes.empty()) {
            // RW always writes at least the 4-byte padded empty string.
            bytes = EncodeRwString(tex.maskName);
        }
        if (!writer.Begin(rwID_STRING, tex.maskSpan, tex.maskSpan.ResolveVersion(version))) {
            return diag.Fail(stream.GetPosition(), "failed to begin TEXTURE mask STRING");
        }
        if (!bytes.empty()) {
            stream.Write(bytes.data(), bytes.size());
        }
        writer.End();
    }

    // EXTENSION
    if (!writer.Begin(rwID_EXTENSION, tex.extensions.span, tex.extensions.span.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin TEXTURE EXTENSION");
    }

    if (!tex.extensions.order.empty()) {
        size_t userDataIdx = 0;
        RawChunkCursor rawCursor(tex.extensions.raws);

        for (RwUInt32 type : tex.extensions.order) {
            if (type == rwID_USERDATAPLUGIN) {
                if (userDataIdx >= tex.userDataLists.size()) {
                    return diag.Fail(stream.GetPosition(), "texture UserData order/content mismatch");
                }
                if (!WriteUserDataList(stream, diag, tex.userDataLists[userDataIdx++], version)) {
                    return false;
                }
                continue;
            }

            const RawChunk* raw = rawCursor.Next(type);
            if (!raw) {
                return diag.Fail(stream.GetPosition(),
                                 "texture extension order/raw mismatch",
                                 RwChunkTypeLabel(type),
                                 "<missing>");
            }
            writer.WriteRaw(*raw);
        }

        if (userDataIdx != tex.userDataLists.size() || !rawCursor.Exhausted()) {
            return diag.Fail(stream.GetPosition(), "texture extension list not fully consumed");
        }
    } else {
        for (const auto& ud : tex.userDataLists) {
            if (!WriteUserDataList(stream, diag, ud, version)) {
                return false;
            }
        }
        for (const auto& raw : tex.extensions.raws) {
            writer.WriteRaw(raw);
        }
    }

    writer.End();  // EXTENSION
    writer.End();  // TEXTURE
    return true;
}
