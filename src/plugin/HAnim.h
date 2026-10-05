/**
 * plugin/HAnim.h - RpHAnim frame extension (rwID_HANIMPLUGIN, 0x11E).
 *
 * Stream layout (RenderWare rphanim.c, HAnimWrite):
 *
 *   RwInt32 streamVersion            (rpHANIMSTREAMCURRENTVERSION = 0x100)
 *   RwInt32 id                       (this frame's HAnim node ID)
 *   RwInt32 numNodes                 (0 unless this frame owns the hierarchy)
 *   if numNodes > 0:
 *     RwInt32 flags                  (RpHAnimHierarchyFlag)
 *     RwInt32 maxInterpKeyFrameSize
 *     per node: RwInt32 nodeID, RwInt32 nodeIndex, RwInt32 flags
 *
 * Only the hierarchy root carries nodeInfos; every other bone frame writes just
 * its own id with numNodes = 0. nodeInfos order defines the bone index space
 * used by RpSkin and by ANM keyframe ordering.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"

#include <vector>

constexpr RwInt32 rpHANIMSTREAMCURRENTVERSION = 0x100;

struct RpHAnimNodeInfo
{
    RwInt32 nodeID = 0;
    RwInt32 nodeIndex = 0;
    RwInt32 flags = 0;   ///< rpHANIMPUSHPARENTMATRIX / rpHANIMPOPPARENTMATRIX
};

struct DffHAnim
{
    RwInt32 streamVersion = rpHANIMSTREAMCURRENTVERSION;
    RwInt32 nodeId = 0;
    RwInt32 flags = 0;
    RwInt32 maxInterpKeyFrameSize = 0;
    std::vector<RpHAnimNodeInfo> nodeInfos;

    RwUInt32    chunkVersion = 0;
    RwChunkSpan span;

    bool IsHierarchyRoot() const { return !nodeInfos.empty(); }

    /**
     * Rebuilds parent indices from the push/pop flags, exactly as
     * RpHAnimHierarchy traversal does.
     * Returns a vector parallel to nodeInfos; -1 means "no parent".
     */
    std::vector<int> BuildParentIndices() const;
};

bool ReadHAnim(RwStream& stream, DffDiagnostics& diag, DffHAnim& out, RwUInt32 payloadSize, RwUInt32 chunkVersion);
bool WriteHAnim(RwStream& stream, DffDiagnostics& diag, const DffHAnim& hanim, RwUInt32 version);
