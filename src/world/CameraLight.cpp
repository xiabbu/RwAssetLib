#include "world/CameraLight.h"

namespace
{

bool ReadObjectExtensions(RwStream& stream, DffDiagnostics& diag, DffExtensionList& extensions,
                           std::shared_ptr<DffUserDataList>& userData, RwUInt32 version)
{
    const RwUInt32 position = stream.GetPosition();
    RwChunkHeader header;
    if (!stream.ReadChunkHeader(header)) {
        return diag.Fail(position, "truncated camera/light EXTENSION header");
    }
    if (header.type != rwID_EXTENSION) {
        return diag.FailChunkType(position, rwID_EXTENSION, header.type);
    }
    return ReadExtensionContainer(stream, diag, header.size, header.version ? header.version : version,
        extensions, [&](const RwChunkHeader& sub, bool& handled) {
            if (sub.type == rwID_USERDATAPLUGIN) {
                userData = std::make_shared<DffUserDataList>();
                handled = true;
                return ReadUserDataList(stream, diag, *userData, sub.size, sub.version);
            }
            return true;
        });
}

bool WriteObjectExtensions(RwStream& stream, DffDiagnostics& diag, const DffExtensionList& extensions,
                            const std::shared_ptr<DffUserDataList>& userData, RwUInt32 version)
{
    ChunkWriter writer(stream, version);
    if (!writer.Begin(rwID_EXTENSION, extensions.span, extensions.span.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin camera/light EXTENSION");
    }
    if (extensions.order.empty()) {
        if (userData && !WriteUserDataList(stream, diag, *userData, version)) {
            return false;
        }
        for (const auto& raw : extensions.raws) {
            if (!writer.WriteRaw(raw)) {
                return diag.Fail(stream.GetPosition(), "failed to write camera/light extension");
            }
        }
    } else {
        RawChunkCursor raws(extensions.raws);
        for (RwUInt32 type : extensions.order) {
            if (type == rwID_USERDATAPLUGIN) {
                if (!userData) {
                    return diag.Fail(stream.GetPosition(), "camera/light UserData order/content mismatch");
                }
                if (!WriteUserDataList(stream, diag, *userData, version)) {
                    return false;
                }
            } else {
                const RawChunk* raw = raws.Next(type);
                if (!raw || !writer.WriteRaw(*raw)) {
                    return diag.Fail(stream.GetPosition(), "camera/light extension order/content mismatch");
                }
            }
        }
        if (!raws.Exhausted()) {
            return diag.Fail(stream.GetPosition(), "camera/light extension list not fully consumed");
        }
    }
    if (!writer.End()) {
        return diag.Fail(stream.GetPosition(), "failed to finish camera/light EXTENSION");
    }
    return true;
}

}

bool ReadCamera(RwStream& stream, DffDiagnostics& diag, DffCamera& camera,
                RwUInt32 payloadSize, RwUInt32 version)
{
    DffScope scope(diag, rwID_CAMERA);
    const RwUInt32 start = stream.GetPosition();
    RwChunkHeader header;
    if (!stream.ReadChunkHeader(header)) {
        return diag.Fail(start, "truncated CAMERA STRUCT header");
    }
    if (header.type != rwID_STRUCT) {
        return diag.FailChunkType(start, rwID_STRUCT, header.type);
    }
    if (header.size != 32) {
        return diag.Fail(start, "unsupported CAMERA STRUCT size", "32", std::to_string(header.size));
    }
    if (!stream.ReadReal(&camera.viewWindow.x) || !stream.ReadReal(&camera.viewWindow.y) ||
        !stream.ReadReal(&camera.viewOffset.x) || !stream.ReadReal(&camera.viewOffset.y) ||
        !stream.ReadReal(&camera.nearPlane) || !stream.ReadReal(&camera.farPlane) ||
        !stream.ReadReal(&camera.fogPlane) || !stream.ReadUInt32(&camera.projection)) {
        return diag.Fail(stream.GetPosition(), "truncated CAMERA STRUCT");
    }
    if (!camera.structSpan.Record(header.size, 32, header.version ? header.version : version, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint CAMERA STRUCT");
    }
    if (!ReadObjectExtensions(stream, diag, camera.extensions, camera.userData, version)) {
        return false;
    }
    if (!camera.span.Record(payloadSize, stream.GetPosition() - start, version, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint CAMERA");
    }
    return true;
}

bool WriteCamera(RwStream& stream, DffDiagnostics& diag, const DffCamera& camera, RwUInt32 version)
{
    DffScope scope(diag, rwID_CAMERA);
    ChunkWriter writer(stream, version);
    if (!writer.Begin(rwID_CAMERA, camera.span, camera.span.ResolveVersion(version)) ||
        !writer.Begin(rwID_STRUCT, camera.structSpan, camera.structSpan.ResolveVersion(version)) ||
        !stream.WriteReal(camera.viewWindow.x) || !stream.WriteReal(camera.viewWindow.y) ||
        !stream.WriteReal(camera.viewOffset.x) || !stream.WriteReal(camera.viewOffset.y) ||
        !stream.WriteReal(camera.nearPlane) || !stream.WriteReal(camera.farPlane) ||
        !stream.WriteReal(camera.fogPlane) || !stream.WriteUInt32(camera.projection) || !writer.End()) {
        return diag.Fail(stream.GetPosition(), "failed to write CAMERA STRUCT");
    }
    if (!WriteObjectExtensions(stream, diag, camera.extensions, camera.userData, version) || !writer.End()) {
        return diag.Fail(stream.GetPosition(), "failed to finish CAMERA");
    }
    return true;
}

bool ReadLight(RwStream& stream, DffDiagnostics& diag, DffLight& light,
               RwUInt32 payloadSize, RwUInt32 version)
{
    DffScope scope(diag, rwID_LIGHT);
    const RwUInt32 start = stream.GetPosition();
    RwChunkHeader header;
    if (!stream.ReadChunkHeader(header)) {
        return diag.Fail(start, "truncated LIGHT STRUCT header");
    }
    if (header.type != rwID_STRUCT) {
        return diag.FailChunkType(start, rwID_STRUCT, header.type);
    }
    if (header.size != 24) {
        return diag.Fail(start, "unsupported LIGHT STRUCT size", "24", std::to_string(header.size));
    }
    if (!stream.ReadReal(&light.radius) || !stream.ReadV3d(&light.color) ||
        !stream.ReadReal(&light.angle) || !stream.ReadUInt32(&light.typeAndFlags)) {
        return diag.Fail(stream.GetPosition(), "truncated LIGHT STRUCT");
    }
    if (!light.structSpan.Record(header.size, 24, header.version ? header.version : version, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint LIGHT STRUCT");
    }
    if (!ReadObjectExtensions(stream, diag, light.extensions, light.userData, version)) {
        return false;
    }
    if (!light.span.Record(payloadSize, stream.GetPosition() - start, version, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint LIGHT");
    }
    return true;
}

bool WriteLight(RwStream& stream, DffDiagnostics& diag, const DffLight& light, RwUInt32 version)
{
    DffScope scope(diag, rwID_LIGHT);
    ChunkWriter writer(stream, version);
    if (!writer.Begin(rwID_LIGHT, light.span, light.span.ResolveVersion(version)) ||
        !writer.Begin(rwID_STRUCT, light.structSpan, light.structSpan.ResolveVersion(version)) ||
        !stream.WriteReal(light.radius) || !stream.WriteV3d(light.color) ||
        !stream.WriteReal(light.angle) || !stream.WriteUInt32(light.typeAndFlags) || !writer.End()) {
        return diag.Fail(stream.GetPosition(), "failed to write LIGHT STRUCT");
    }
    if (!WriteObjectExtensions(stream, diag, light.extensions, light.userData, version) || !writer.End()) {
        return diag.Fail(stream.GetPosition(), "failed to finish LIGHT");
    }
    return true;
}
