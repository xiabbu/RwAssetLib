/**
 * plugin/Morph.cpp - RpMorph geometry extension read/write.
 */

#include "plugin/Morph.h"

bool ReadMorphAnim(RwStream& stream, DffDiagnostics& diag, DffMorphAnim& out, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_MORPHPLUGIN);

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    out = DffMorphAnim();
    out.chunkVersion = chunkVersion;

    RwInt32 numInterpolators = 0;
    if (!stream.ReadInt32(&numInterpolators) || numInterpolators < 0) {
        stream.Seek(endPos);
        return diag.Fail(startPos, "invalid morph interpolator count");
    }

    const RwUInt32 expected = sizeof(RwInt32) + static_cast<RwUInt32>(numInterpolators) * kMorphInterpolatorSize;
    if (expected != payloadSize) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "morph payload size disagrees with interpolator count",
                         std::to_string(expected),
                         std::to_string(payloadSize));
    }

    out.interpolators.resize(static_cast<size_t>(numInterpolators));
    for (RwInt32 i = 0; i < numInterpolators; ++i) {
        auto& interp = out.interpolators[static_cast<size_t>(i)];
        if (!stream.ReadInt32(&interp.flags) ||
            !stream.ReadInt32(&interp.startMorphTarget) ||
            !stream.ReadInt32(&interp.endMorphTarget) ||
            !stream.ReadReal(&interp.time) ||
            !stream.ReadInt32(&interp.nextInterpIndex)) {
            stream.Seek(endPos);
            return diag.Fail(startPos, "truncated morph interpolator");
        }
    }

    if (!out.span.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    stream.Seek(endPos);
    return true;
}

bool WriteMorphAnim(RwStream& stream, DffDiagnostics& diag, const DffMorphAnim& morph, RwUInt32 version)
{
    DffScope scope(diag, rwID_MORPHPLUGIN);

    const RwUInt32 chunkVersion = morph.chunkVersion ? morph.chunkVersion : version;
    ChunkWriter writer(stream, chunkVersion);

    if (!writer.Begin(rwID_MORPHPLUGIN, morph.span, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin MORPH chunk");
    }

    stream.WriteInt32(static_cast<RwInt32>(morph.interpolators.size()));
    for (const auto& interp : morph.interpolators) {
        stream.WriteInt32(interp.flags);
        stream.WriteInt32(interp.startMorphTarget);
        stream.WriteInt32(interp.endMorphTarget);
        stream.WriteReal(interp.time);
        stream.WriteInt32(interp.nextInterpIndex);
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close MORPH chunk");
}
