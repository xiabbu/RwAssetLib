/**
 * plugin/UVAnim.h - RpUVAnim material extension (rwID_UVANIMPLUGIN, 0x135).
 *
 * Layout (RenderWare rpuvanim.c, UVAnimWrite):
 *
 *   STRUCT {
 *       RwUInt32 flags                       // bit i set => slot i has an animation
 *       char     name[32] per set bit        // RP_UVANIM_MAXNAME
 *   }
 *
 * The DFF only stores animation *names*; the animations themselves live in a
 * separate UV-anim dictionary, so nothing else is required for a faithful
 * round-trip.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"

#include <array>
#include <string>

constexpr int kUVAnimMaxSlots = 8;   // RP_UVANIM_MAXSLOTS
constexpr int kUVAnimMaxName  = 32;  // RP_UVANIM_MAXNAME

struct DffUVAnimMaterial
{
    RwUInt32 chunkVersion = 0;
    RwUInt32 structChunkVersion = 0;
    RwUInt32 flags = 0;
    std::array<std::array<char, kUVAnimMaxName>, kUVAnimMaxSlots> names{};

    RwChunkSpan span;
    RwChunkSpan structSpan;

    bool HasSlot(int slot) const { return (flags & (1u << slot)) != 0; }
    std::string SlotName(int slot) const;
};

bool ReadUVAnimMaterial(RwStream& stream, DffDiagnostics& diag, DffUVAnimMaterial& out, RwUInt32 payloadSize, RwUInt32 chunkVersion);
bool WriteUVAnimMaterial(RwStream& stream, DffDiagnostics& diag, const DffUVAnimMaterial& ext, RwUInt32 version);
