/**
 * plugin/Collis.h - RpCollision geometry extension (rwID_COLLISPLUGIN, 0x11D).
 *
 * Stream layout (RenderWare collision plugin):
 *
 *   RwUInt32 internalVersion
 *   COLLTREE chunk (rwID_COLLTREE, 0x2C)
 *     STRUCT chunk
 *       RwUInt32 flags                 // bit 0: an index map follows
 *       RwV3d    inf
 *       RwV3d    sup
 *       RwUInt32 numEntries
 *       RwUInt32 numSplits
 *       per split (16 bytes):
 *         RpCollSector left  { RwUInt8 type, RwUInt8 contents, RwUInt16 index, RwReal value }
 *         RpCollSector right { ... }
 *       if (flags & 1): RwInt16 map[numEntries]
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"

#include <vector>

constexpr RwUInt32 kCollTreeStructFixedSize = 36;  // flags + inf + sup + numEntries + numSplits
constexpr RwUInt32 kCollSplitSize = 16;
constexpr RwUInt32 kCollTreeFlagHasMap = 0x1;

struct DffCollSector
{
    RwUInt8  type = 0;
    RwUInt8  contents = 0;
    RwUInt16 index = 0;
    RwReal   value = 0;
};

struct DffCollSplit
{
    DffCollSector left;
    DffCollSector right;
};

struct DffCollTree
{
    RwUInt32 chunkVersion = 0;
    RwUInt32 structChunkVersion = 0;
    RwUInt32 flags = 0;
    RwV3d    inf;
    RwV3d    sup;
    RwUInt32 numEntries = 0;
    RwUInt32 numSplits = 0;
    std::vector<DffCollSplit> splits;
    std::vector<RwInt16>      map;
};

struct DffCollis
{
    RwUInt32    chunkVersion = 0;
    RwUInt32    internalVersion = 0;
    DffCollTree tree;
    RwChunkSpan span;
};

bool ReadCollis(RwStream& stream, DffDiagnostics& diag, DffCollis& out, RwUInt32 payloadSize, RwUInt32 chunkVersion);
bool WriteCollis(RwStream& stream, DffDiagnostics& diag, const DffCollis& collis, RwUInt32 version);
