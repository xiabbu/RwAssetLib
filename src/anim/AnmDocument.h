/**
 * anim/AnmDocument.h - RtAnimAnimation stream (.anm, rwID_ANIMANIMATION 0x1B).
 *
 * Stream layout (RenderWare rtanim.c, RtAnimAnimationStreamWrite):
 *
 *   ANIMANIMATION
 *     RwInt32 streamVersion            (rtANIMSTREAMCURRENTVERSION = 0x100)
 *     RwInt32 typeID                   (interpolator scheme)
 *     RwInt32 numFrames
 *     RwInt32 flags
 *     RwReal  duration
 *     keyframes[numFrames]
 *     [interpolator custom data]
 *
 * Two interpolator schemes appear in DBO content:
 *
 *   rpHANIMSTDKEYFRAMETYPEID (1)  - RpHAnimKeyFrame, 36 bytes streamed:
 *       RwReal time; RtQuat q (x,y,z,w); RwV3d t; RwInt32 prevFrameOffset
 *
 *   rtANIMCOMPRESSEDKEYFRAMETYPEID (2) - RtCompressedKeyFrame, 22 bytes streamed:
 *       RwReal time; RwUInt16 qx,qy,qz,qr,tx,ty,tz; RwInt32 prevFrameOffset
 *     followed by RtCompressedKeyFrameCustomData: RwV3d offset, RwV3d scalar
 *
 * prevFrameOffset is a *byte* offset into the in-memory keyframe array, so the
 * stride used to turn it into an index is sizeof(the in-memory keyframe), which
 * is 36 for the standard scheme and 24 (not 22) for the compressed one.
 *
 * RtAnimAnimationGetNumNodes recovers the node count by walking the leading
 * keyframes until one points back at keyframe 0 - that is node 0's second key,
 * so everything before it is one initial key per node.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwStream.h"
#include "world/Clump.h"

#include <memory>
#include <string>
#include <vector>

constexpr RwInt32 rtANIMSTREAMCURRENTVERSION = 0x100;
constexpr RwInt32 rpHANIMSTDKEYFRAMETYPEID = 0x1;
constexpr RwInt32 rtANIMCOMPRESSEDKEYFRAMETYPEID = 0x2;

/// Bytes each keyframe occupies in the stream.
constexpr RwUInt32 kStdKeyFrameStreamSize = 36;
constexpr RwUInt32 kCompressedKeyFrameStreamSize = 22;
/// sizeof() of the in-memory keyframe, the unit of prevFrameOffset.
constexpr RwInt32 kStdKeyFrameStride = 36;
constexpr RwInt32 kCompressedKeyFrameStride = 24;

struct RtQuat
{
    RwReal x = 0, y = 0, z = 0, w = 1;
};

struct AnmKeyFrame
{
    RwReal  time = 0;
    RtQuat  q;
    RwV3d   t;
    RwInt32 prevFrameOffset = -1;
};

struct AnmCompressedKeyFrame
{
    RwReal   time = 0;
    RwUInt16 qx = 0, qy = 0, qz = 0, qr = 0;
    RwUInt16 tx = 0, ty = 0, tz = 0;
    RwInt32  prevFrameOffset = -1;
};

/// RtCompressedKeyFrameCustomData - translation dequantisation parameters.
struct AnmCompressedCustomData
{
    RwV3d offset{0, 0, 0};
    RwV3d scalar{1, 1, 1};
};

struct AnmNodeTrack
{
    RwInt32                  nodeID = 0;   ///< HAnim node ID once mapped
    std::vector<AnmKeyFrame> keyframes;
};

struct AnmAnimation
{
    RwInt32 streamVersion = rtANIMSTREAMCURRENTVERSION;
    RwInt32 typeID = rpHANIMSTDKEYFRAMETYPEID;
    RwInt32 numFrames = 0;
    RwInt32 flags = 0;
    RwReal  duration = 0;

    /// Always populated; compressed animations are decompressed into this.
    std::vector<AnmKeyFrame> keyframes;

    /// Only for typeID == rtANIMCOMPRESSEDKEYFRAMETYPEID.
    std::vector<AnmCompressedKeyFrame> compressedKeyframes;
    bool                               hasCompressedCustomData = false;
    AnmCompressedCustomData            compressedCustomData;

    /// Bytes past the modelled payload, kept verbatim.
    std::vector<RwUInt8> trailing;

    /// Keyframes grouped per animated node, in node order.
    std::vector<AnmNodeTrack> nodeTracks;

    RwChunkSpan span;

    bool IsCompressed() const { return typeID == rtANIMCOMPRESSEDKEYFRAMETYPEID; }
    int NumNodes() const { return static_cast<int>(nodeTracks.size()); }
    RwInt32 KeyFrameStride() const { return IsCompressed() ? kCompressedKeyFrameStride : kStdKeyFrameStride; }

    /**
     * Rebuilds `keyframes` (and numFrames, duration) from `nodeTracks` in the
     * order of the RW 3ds Max exporter (hanim.cpp,
     * GenerateHAnimationFromKeyFrameList): each node's first key, then each
     * node's second key, then repeatedly the next key of the node whose last
     * emitted key is earliest, lowest node first on ties. Every track needs a
     * key at time 0 and at least two keys, as that function assumes.
     *
     * Initial keys have a NULL prevFrame, which RpHAnimKeyFrameStreamWrite
     * streams as `0 - (RwInt32)pFrames`; that heap address is supplied as
     * `initialPrevFrameOffset`. Compressed clips keep their scheme and supplied
     * compression parameters; absent parameters are built from the translation
     * bounds as in RtCompressedKeyConvertFromStdKey.
     */
    bool RebuildFromNodeTracks(RwInt32 initialPrevFrameOffset, std::string& outError);

    /** Regroups `keyframes` into `nodeTracks` using the prevFrameOffset chain. */
    bool BuildNodeTracks(std::string& outError);
};

/** RtCompressedKeyFrameDecompressFloat (rtcmpkey.h). */
RwReal AnmDecompressFloat(RwUInt16 v);
/** Inverse of AnmDecompressFloat, matching RtCompressedKeyFrameCompressFloat. */
RwUInt16 AnmCompressFloat(RwReal v);

class AnmDocument
{
public:
    bool Load(const std::string& filename);
    bool LoadFromMemory(const void* data, size_t size);
    bool Load(RwStream& stream);
#ifdef _WIN32
    bool Load(const wchar_t* filename);
#endif

    bool Save(const std::string& filename);
    bool SaveToMemory(std::vector<RwUInt8>& out);
    bool Save(RwStream& stream);
#ifdef _WIN32
    bool Save(const wchar_t* filename);
#endif

    AnmAnimation* GetAnimation() { return m_anim.get(); }
    const AnmAnimation* GetAnimation() const { return m_anim.get(); }
    AnmAnimation& CreateAnimation();

    RwUInt32 GetVersion() const { return m_version; }
    void SetVersion(RwUInt32 v) { m_version = v; }

    /**
     * Assigns HAnim node IDs to the tracks using a reference clump's hierarchy.
     * ANM streams carry no node IDs: track order *is* the nodeInfos order.
     */
    bool ApplyHAnimIds(const DffClump& refClump);

    DffDiagnostics& Diagnostics() { return m_diag; }
    std::string GetLastError() const { return m_diag.Summary(); }

    std::string DescribeStructure() const;

private:
    std::unique_ptr<AnmAnimation> m_anim;
    RwUInt32                      m_version = rwLIBRARYVERSION37;
    DffDiagnostics                m_diag;
};

struct AnmRoundTripResult
{
    bool        loaded = false;
    bool        saved = false;
    bool        identical = false;
    size_t      sourceSize = 0;
    size_t      outputSize = 0;
    size_t      firstDiffOffset = 0;
    size_t      diffByteCount = 0;
    std::string error;

    bool Ok() const { return loaded && saved && identical; }
};

AnmRoundTripResult AnmCheckRoundTripBytes(const std::vector<RwUInt8>& source);
AnmRoundTripResult AnmCheckRoundTrip(const std::string& filename);
