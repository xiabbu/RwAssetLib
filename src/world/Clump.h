/**
 * world/Clump.h - RpClump, RwFrame list and RpAtomic (rwID_CLUMP, 0x10).
 *
 * Stream layout (RenderWare baclump.c, RpClumpStreamWrite):
 *
 *   CLUMP
 *     STRUCT  { RwInt32 numAtomics, numLights, numCameras }   // 4 bytes pre-3.3
 *     FRAMELIST
 *       STRUCT { RwInt32 numFrames, rwStreamFrame frames[numFrames] }
 *       EXTENSION x numFrames
 *     GEOMETRYLIST
 *       STRUCT { RwInt32 numGeometries }
 *       GEOMETRY x numGeometries
 *     ATOMIC x numAtomics
 *     (STRUCT frameIndex + LIGHT) x numLights
 *     (STRUCT frameIndex + CAMERA) x numCameras
 *     EXTENSION
 *
 * rwStreamFrame (RenderWare babinfrm.c) is 56 bytes:
 *   RwV3d right, up, at, pos; RwInt32 parentIndex; RwInt32 flags
 *
 * RpAtomic STRUCT (_rpAtomicBinary) is 16 bytes in stock RenderWare:
 *   RwInt32 frameIndex, geomIndex, flags, unused
 * DBO extends it to 24 bytes with renderType(i16), blendMode(i16) and id(i32).
 * The recorded structSize drives which layout is written back.
 *
 * Frames carry no name in the RW core format. DBO stores it in a USERDATA
 * "name" array; some older assets use a bare STRING extension instead.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"
#include "ntl/NtlAtomic.h"
#include "plugin/HAnim.h"
#include "plugin/MatFX.h"
#include "plugin/UserData.h"
#include "world/Extension.h"
#include "world/Geometry.h"
#include "world/CameraLight.h"

#include <memory>
#include <string>
#include <vector>

/** rwID_RIGHTTORENDER payload: pluginID plus optional plugin-defined data. */
struct DffRightToRender
{
    RwUInt32    chunkVersion = 0;
    RwUInt32    pluginId = 0;
    RwUInt32    pluginData = 0;
    bool        hasPluginData = false;   ///< false when the chunk was only 4 bytes
    RwChunkSpan span;
};

struct DffFrame
{
    RwMatrix matrix;
    RwInt32  parentIndex = -1;
    RwInt32  flags = 0;

    std::shared_ptr<DffHAnim>        hanim;
    std::shared_ptr<DffUserDataList> userData;

    DffExtensionList extensions;

    /** Frame name from the USERDATA "name" array, or the legacy STRING chunk. */
    std::string Name() const;
    void SetName(const std::string& name);

    static bool IsKnownExtension(RwUInt32 type);
};

struct DffAtomic
{
    RwInt32 frameIndex = 0;
    RwInt32 geomIndex = 0;
    RwInt32 flags = 0;
    RwInt32 unused = 0;

    // DBO extension fields (present when structSize >= 24)
    RwInt16 renderType = 0;
    RwInt16 blendMode = 0;
    RwInt32 id = -1;

    /// Bytes actually occupied by the ATOMIC STRUCT payload (16 or 24 for DBO).
    RwUInt32             structSize = 24;
    /// Anything past the modelled fields, kept verbatim.
    std::vector<RwUInt8> structExtra;

    std::shared_ptr<DffRightToRender> rights;
    std::shared_ptr<DffMatFXAtomic>   matfx;
    /// DBO per-atomic render state (rwID_NTL_ATOMIC_EXT).
    std::shared_ptr<DffNtlAtomicExt>  ntlAtomic;

    DffExtensionList extensions;

    RwChunkSpan atomicSpan;
    RwChunkSpan structSpan;

    static bool IsKnownExtension(RwUInt32 type);
};

struct DffClump
{
    std::vector<std::shared_ptr<DffFrame>>    frames;
    std::vector<std::shared_ptr<DffGeometry>> geometries;
    std::vector<DffAtomic>                    atomics;

    std::vector<DffLight> lights;
    std::vector<DffCamera> cameras;

    DffExtensionList extensions;

    RwChunkSpan clumpSpan;
    RwChunkSpan structSpan;
    RwChunkSpan frameListSpan;
    RwChunkSpan frameListStructSpan;
    RwChunkSpan geometryListSpan;
    RwChunkSpan geometryListStructSpan;

    /// False when the CLUMP STRUCT only held numAtomics (pre-3.3 streams).
    bool structHasLightCameraCounts = true;

    const DffFrame* FindHAnimRoot() const;
    DffFrame* FindHAnimRoot();

    /** nodeInfos index -> frame index, via matching HAnim node IDs. */
    std::vector<int> BuildHAnimNodeToFrameMap() const;
};

bool ReadClump(RwStream& stream, DffDiagnostics& diag, DffClump& clump, RwUInt32 payloadSize, RwUInt32 chunkVersion);
bool WriteClump(RwStream& stream, DffDiagnostics& diag, const DffClump& clump, RwUInt32 version);
