/**
 * anim/UvaDocument.h - RpUVAnim dictionary stream (.uva, rwID_UVANIMDICT 0x2B).
 *
 * A DFF's UVANIM material extension stores only animation *names*; the curves
 * live in this external dictionary, which the runtime resolves the names
 * against (NtlPLUVAnim.cpp: UVAnimDictLoad -> RtDictSchemaSetCurrentDict).
 *
 * Layout (RenderWare rtdict.c RtDictStreamWrite,
 * RenderWare stdkey.c RpUVAnimKeyFrameStreamWrite):
 *
 *   UVANIMDICT
 *     STRUCT { RwInt32 count }
 *     ANIMANIMATION[count]
 *
 *   ANIMANIMATION payload (rtanim.c RtAnimAnimationStreamWrite, then the
 *   UVAnim interpolator's keyframe callback):
 *     RwInt32 streamVersion         rtANIMSTREAMCURRENTVERSION
 *     RwInt32 typeID                rwID_UVANIMLINEAR / rwID_UVANIMPARAM
 *     RwInt32 numFrames
 *     RwInt32 flags
 *     RwReal  duration
 *     RwInt32 uvStreamVersion
 *     char    name[32]              _rpUVAnimCustomData
 *     RwInt32 nodeToUVChannelMap[8]
 *     keyframes[numFrames] { RwReal time; RwReal values[6]; RwInt32 prevFrameIndex }
 *
 * `values` is the keyframe data union: RpUVAnimLinearKeyFrameData.uv[6] for the
 * linear scheme, RpUVAnimParamKeyFrameData {theta, s0, s1, skew, x, y} for the
 * parametric one. `prevFrameIndex` is streamed as an index into the keyframe
 * array; RW derives it from an in-memory pointer, so an initial key carries a
 * heap-address-derived value that is preserved verbatim rather than recomputed.
 *
 * The dictionary is read and written as parsed: nothing here regenerates an
 * animation, so no curve is resampled or re-ordered.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"
#include "core/RwStream.h"

#include <array>
#include <string>
#include <vector>

/// RpUVAnim interpolator schemes (rpcriter.h: MAKECHUNKID(CRITERIONTK, 0xc0/0xc1)).
constexpr RwInt32 rwID_UVANIMLINEAR = 0x1C0;
constexpr RwInt32 rwID_UVANIMPARAM  = 0x1C1;

constexpr int      kUvaMaxName      = 32;  // RP_UVANIM_MAXNAME
constexpr int      kUvaMaxSlots     = 8;   // RP_UVANIM_MAXSLOTS
/// 7 RwReal (time + keyframe data) + RwInt32 prevFrameIndex.
constexpr RwUInt32 kUvaKeyFrameSize = 32;
/// streamVersion, typeID, numFrames, flags, duration, uvStreamVersion.
constexpr RwUInt32 kUvaAnimHeaderSize = 24;

struct UvaKeyFrame
{
    RwReal                time = 0;
    std::array<RwReal, 6> values{};
    RwInt32               prevFrameIndex = 0;
};

struct UvaAnimation
{
    RwUInt32 chunkVersion = 0;
    RwInt32  streamVersion = 0;
    RwInt32  typeID = 0;
    RwInt32  numFrames = 0;
    RwInt32  flags = 0;
    RwReal   duration = 0;
    RwInt32  uvStreamVersion = 0;

    std::array<char, kUvaMaxName>     name{};
    std::array<RwInt32, kUvaMaxSlots> channelMap{};
    std::vector<UvaKeyFrame>          keyframes;

    RwChunkSpan span;

    std::string Name() const;
    bool IsParam() const { return typeID == rwID_UVANIMPARAM; }
};

class UvaDocument
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

    std::vector<UvaAnimation>&       Animations() { return m_animations; }
    const std::vector<UvaAnimation>& Animations() const { return m_animations; }

    RwUInt32 GetVersion() const { return m_version; }
    void     SetVersion(RwUInt32 v) { m_version = v; }
    RwUInt32 GetStructVersion() const { return m_structVersion; }
    void     SetStructVersion(RwUInt32 v) { m_structVersion = v; }

    DffDiagnostics& Diagnostics() { return m_diag; }
    std::string     GetLastError() const { return m_diag.Summary(); }

private:
    std::vector<UvaAnimation> m_animations;
    RwUInt32                  m_version = rwLIBRARYVERSION37;
    /// 0 until a file supplies one; Save then follows the document version.
    RwUInt32                  m_structVersion = 0;
    DffDiagnostics            m_diag;
};

struct UvaRoundTripResult
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

UvaRoundTripResult UvaCheckRoundTripBytes(const std::vector<RwUInt8>& source);
UvaRoundTripResult UvaCheckRoundTrip(const std::string& filename);
