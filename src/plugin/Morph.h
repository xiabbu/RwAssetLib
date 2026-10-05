/**
 * plugin/Morph.h - RpMorph geometry extension (rwID_MORPHPLUGIN, 0x105).
 *
 * Stream layout (RenderWare rpmorph.c, GeomAnimWriteStream):
 *
 *   RwInt32 numInterpolators
 *   per interpolator (_rpBinaryAnimInterpolator, 20 bytes):
 *     RwInt32 flags
 *     RwInt32 startMorphTarget
 *     RwInt32 endMorphTarget
 *     RwReal  time
 *     RwInt32 nextInterpIndex        // index of `next`, i.e. next - allInterps
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"

#include <vector>

constexpr RwUInt32 kMorphInterpolatorSize = 20;

struct DffMorphInterpolator
{
    RwInt32 flags = 0;
    RwInt32 startMorphTarget = 0;
    RwInt32 endMorphTarget = 0;
    RwReal  time = 0;
    RwInt32 nextInterpIndex = 0;
};

struct DffMorphAnim
{
    std::vector<DffMorphInterpolator> interpolators;

    RwUInt32    chunkVersion = 0;
    RwChunkSpan span;
};

bool ReadMorphAnim(RwStream& stream, DffDiagnostics& diag, DffMorphAnim& out, RwUInt32 payloadSize, RwUInt32 chunkVersion);
bool WriteMorphAnim(RwStream& stream, DffDiagnostics& diag, const DffMorphAnim& morph, RwUInt32 version);
