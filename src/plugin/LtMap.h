/**
 * plugin/LtMap.h - RpLtMap material extension (rwID_LTMAPPLUGIN, 0x114).
 *
 * Layout (RenderWare rpltmap.c, LtMapMaterialWrite):
 *
 *   chunk header rwID_LTMAPPLUGIN, size = LTMAPMATERIALSTREAMDATASIZE - rwCHUNKHEADERSIZE
 *   LtMapMaterialStreamData {
 *       RwUInt32 flags
 *       RwReal   lightMapDensity
 *       RwRGBA   areaLightColour
 *       RwReal   areaLightDensity
 *       RwReal   areaLightRadius
 *   }                                    // 20 bytes
 *
 * i.e. the plugin writes a *nested* chunk header purely to carry a version
 * number (the stream API does not hand the read callback its own header), so the
 * outer plugin chunk is 12 + 20 = 32 bytes.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"

constexpr RwUInt32 kLtMapMaterialDataSize = 20;
constexpr RwUInt32 kLtMapMaterialChunkSize = rwCHUNKHEADERSIZE + kLtMapMaterialDataSize;

struct DffLtMapMaterial
{
    RwUInt32 chunkVersion = 0;          ///< outer rwID_LTMAPPLUGIN header version
    RwUInt32 innerChunkVersion = 0;     ///< nested rwID_LTMAPPLUGIN header version
    RwUInt32 flags = 0;                 ///< RtLtMapMaterialFlags
    RwReal   lightMapDensity = 0;
    RwRGBA   areaLightColour;
    RwReal   areaLightDensity = 0;
    RwReal   areaLightRadius = 0;

    RwChunkSpan span;
};

bool ReadLtMapMaterial(RwStream& stream, DffDiagnostics& diag, DffLtMapMaterial& out, RwUInt32 payloadSize, RwUInt32 chunkVersion);
bool WriteLtMapMaterial(RwStream& stream, DffDiagnostics& diag, const DffLtMapMaterial& ext, RwUInt32 version);
