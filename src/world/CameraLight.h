#pragma once

#include "plugin/UserData.h"
#include "world/Extension.h"

#include <memory>

enum RwCameraProjection
{
    rwPERSPECTIVE = 1,
    rwPARALLEL = 2
};

enum RpLightType
{
    rpLIGHTDIRECTIONAL = 1,
    rpLIGHTAMBIENT = 2,
    rpLIGHTPOINT = 0x80,
    rpLIGHTSPOT = 0x81,
    rpLIGHTSPOTSOFT = 0x82
};

struct DffCamera
{
    RwInt32 frameIndex = 0;
    RwV2d viewWindow;
    RwV2d viewOffset;
    RwReal nearPlane = 0;
    RwReal farPlane = 0;
    RwReal fogPlane = 0;
    RwUInt32 projection = rwPERSPECTIVE;
    std::shared_ptr<DffUserDataList> userData;
    DffExtensionList extensions;
    RwChunkSpan frameIndexSpan;
    RwChunkSpan span;
    RwChunkSpan structSpan;
};

struct DffLight
{
    RwInt32 frameIndex = 0;
    RwReal radius = 0;
    RwV3d color{1, 1, 1};
    // balight.c: streams before 3.3 store tan(angle), later streams -cos(angle).
    RwReal angle = 0;
    RwUInt32 typeAndFlags = (rpLIGHTPOINT << 16) | 3;
    std::shared_ptr<DffUserDataList> userData;
    DffExtensionList extensions;
    RwChunkSpan frameIndexSpan;
    RwChunkSpan span;
    RwChunkSpan structSpan;
};

bool ReadCamera(RwStream& stream, DffDiagnostics& diag, DffCamera& camera,
                RwUInt32 payloadSize, RwUInt32 version);
bool WriteCamera(RwStream& stream, DffDiagnostics& diag, const DffCamera& camera, RwUInt32 version);
bool ReadLight(RwStream& stream, DffDiagnostics& diag, DffLight& light,
               RwUInt32 payloadSize, RwUInt32 version);
bool WriteLight(RwStream& stream, DffDiagnostics& diag, const DffLight& light, RwUInt32 version);
