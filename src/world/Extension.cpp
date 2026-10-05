/**
 * world/Extension.cpp - EXTENSION container traversal.
 */

#include "world/Extension.h"

bool ReadExtensionContainer(RwStream& stream,
                            DffDiagnostics& diag,
                            RwUInt32 declaredLength,
                            RwUInt32 containerVersion,
                            DffExtensionList& list,
                            const DffExtensionChunkHandler& handler)
{
    DffScope scope(diag, rwID_EXTENSION);

    const RwUInt32 containerStart = stream.GetPosition();

    list.order.clear();
    list.raws.clear();

    // _rwPluginRegistryReadDataChunks: while (length > 0) { ... length -= readLength + rwCHUNKHEADERSIZE; }
    RwUInt32 remaining = declaredLength;

    while (remaining > 0) {
        if (remaining < rwCHUNKHEADERSIZE) {
            // RW would underflow its unsigned counter here; a well-formed stream never does.
            return diag.Fail(stream.GetPosition(),
                             "extension container length does not cover a chunk header",
                             ">= 12",
                             std::to_string(remaining));
        }

        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated extension sub-chunk header");
        }

        const RwUInt32 step = header.size + rwCHUNKHEADERSIZE;
        if (step > remaining) {
            return diag.Fail(headerPos,
                             "extension sub-chunk declared length exceeds container remainder",
                             std::to_string(remaining),
                             std::to_string(step));
        }

        list.Append(header.type);

        bool handled = false;
        if (handler && !handler(header, handled)) {
            return false;
        }

        if (!handled) {
            // Unregistered plugin: RwStreamSkip(readLength). Keep the bytes so the
            // chunk can be re-emitted verbatim.
            RawChunk raw;
            raw.type = header.type;
            raw.version = header.version;
            if (header.size > 0) {
                raw.data.resize(header.size);
                if (!stream.Read(raw.data.data(), header.size)) {
                    return diag.Fail(headerPos,
                                     "truncated extension sub-chunk payload",
                                     std::to_string(header.size),
                                     "<eof>");
                }
            }
            list.raws.push_back(std::move(raw));
        }

        remaining -= step;
    }

    const RwUInt32 consumed = stream.GetPosition() - containerStart;
    if (!list.span.Record(declaredLength, consumed, containerVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}
