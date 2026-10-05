/**
 * ntl/NtlAtomic.cpp - DBO atomic plugin read/write.
 */

#include "ntl/NtlAtomic.h"

RwUInt32 DffNtlAtomicExt::PayloadSizeForVersion(RwUInt16 version)
{
    switch (version) {
        case 0:  return sizeof(RwUInt16) + sizeof(RwUInt32);                                        //  6
        case 1:  return sizeof(RwUInt16) + sizeof(RwUInt32) + sizeof(RwUInt16) + sizeof(RwReal);    // 12
        case 2:  return sizeof(RwUInt16) + sizeof(RwUInt32) + sizeof(RwUInt16) + sizeof(RwReal) +
                        sizeof(RwUInt16);                                                            // 14
        default: return 0;
    }
}

bool ReadNtlAtomicExt(RwStream& stream,
                      DffDiagnostics& diag,
                      DffNtlAtomicExt& out,
                      RwUInt32 payloadSize,
                      RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_NTL_ATOMIC_EXT);

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    out = DffNtlAtomicExt();
    out.chunkVersion = chunkVersion;

    auto bail = [&](const char* msg) {
        stream.Seek(endPos);
        return diag.Fail(startPos, msg);
    };

    if (!stream.ReadUInt16(&out.version)) {
        return bail("truncated NTL atomic version");
    }

    // Plugin_StreamRead rejects any version it does not know.
    const RwUInt32 expected = DffNtlAtomicExt::PayloadSizeForVersion(out.version);
    if (expected == 0) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "unsupported NTL atomic plugin version",
                         "0, 1 or 2",
                         std::to_string(out.version));
    }

    if (!stream.ReadUInt32(&out.flags)) {
        return bail("truncated NTL atomic flags");
    }

    if (out.HasUserData()) {
        if (!stream.ReadUInt16(&out.userData) || !stream.ReadReal(&out.userDataReal)) {
            return bail("truncated NTL atomic user data");
        }
    }

    if (out.HasEnvTexName()) {
        if (!stream.ReadUInt16(&out.envTexName)) {
            return bail("truncated NTL atomic environment texture id");
        }
    }

    if (!out.span.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    stream.Seek(endPos);
    return true;
}

bool WriteNtlAtomicExt(RwStream& stream,
                       DffDiagnostics& diag,
                       const DffNtlAtomicExt& ext,
                       RwUInt32 version)
{
    DffScope scope(diag, rwID_NTL_ATOMIC_EXT);

    if (DffNtlAtomicExt::PayloadSizeForVersion(ext.version) == 0) {
        return diag.Fail(stream.GetPosition(),
                         "unsupported NTL atomic plugin version",
                         "0, 1 or 2",
                         std::to_string(ext.version));
    }

    const RwUInt32 chunkVersion = ext.chunkVersion ? ext.chunkVersion : version;
    ChunkWriter writer(stream, chunkVersion);

    if (!writer.Begin(rwID_NTL_ATOMIC_EXT, ext.span, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin NTL atomic chunk");
    }

    stream.WriteUInt16(ext.version);
    stream.WriteUInt32(ext.flags);

    if (ext.HasUserData()) {
        stream.WriteUInt16(ext.userData);
        stream.WriteReal(ext.userDataReal);
    }
    if (ext.HasEnvTexName()) {
        stream.WriteUInt16(ext.envTexName);
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close NTL atomic chunk");
}
