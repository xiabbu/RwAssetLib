/**
 * ntl/NtlAtomic.h - DBO atomic plugin (rwID_NTL_ATOMIC_EXT, 0x7FE).
 *
 * Source: DBO NtlAtomic.cpp
 *   MAKECHUNKID(rwVENDORID_CRITERIONRM = 0x07, PluginID = 0xFE)
 *
 * Plugin_StreamRead / Plugin_StreamWrite are version-gated on a leading
 * RwUInt16, and every field is streamed with a bare RwStreamRead of the member
 * (so there is no struct padding on the wire):
 *
 *   RwUInt16 uiVersion
 *   version 0 (NTL_ATOMIC_SAVE_VER0):  RwUInt32 uiFlag                                     ->  6 bytes
 *   version 1 (NTL_ATOMIC_SAVE_VER ):  + RwUInt16 UserDat, RwReal _UserDatReal             -> 12 bytes
 *   version 2 (NTL_ATOMIC_SAVE_VER1):  + RwUInt16 _EnvTexName                              -> 14 bytes
 *   any other version: the reader returns NULL (load failure)
 *
 * Note Plugin_StreamGetSize always promotes the record to version 2, so DBO's
 * own exporter writes 14-byte records. Reproducing an existing file byte for
 * byte therefore requires writing back the version that was read, not the
 * newest one.
 *
 * uiFlag holds ENtlAtomicFlag: toon edge suppression, two-sided/alpha-test/alpha
 * render state, collision and visibility switches, shadow-map participation and
 * the environment-map / emblem markers. This is the per-object render state a
 * DBO artist sets, so it is the atomic-level data a Max exporter must round-trip.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"

struct DffNtlAtomicExt
{
    /// NTL_ATOMIC_SAVE_VER* - decides which fields are present on the wire.
    RwUInt16 version = 2;

    RwUInt32 flags = 0;          ///< ENtlAtomicFlag
    RwUInt16 userData = 0;       ///< UserDat       (object shadow slot, cloned)
    RwReal   userDataReal = 0;   ///< _UserDatReal  (object shadow slot, cloned)
    RwUInt16 envTexName = 0;     ///< _EnvTexName   (valid when NTL_ENVMAP_TEX)

    RwUInt32    chunkVersion = 0;
    RwChunkSpan span;

    bool HasUserData() const { return version >= 1; }
    bool HasEnvTexName() const { return version >= 2; }

    /** Streamed payload length for `version`; 0 when the version is unsupported. */
    static RwUInt32 PayloadSizeForVersion(RwUInt16 version);
    RwUInt32 PayloadSize() const { return PayloadSizeForVersion(version); }

    bool GetFlag(ENtlAtomicFlag flag) const { return (flags & static_cast<RwUInt32>(flag)) != 0; }
    void SetFlag(ENtlAtomicFlag flag, bool on)
    {
        if (on) {
            flags |= static_cast<RwUInt32>(flag);
        } else {
            flags &= ~static_cast<RwUInt32>(flag);
        }
    }
};

bool ReadNtlAtomicExt(RwStream& stream,
                      DffDiagnostics& diag,
                      DffNtlAtomicExt& out,
                      RwUInt32 payloadSize,
                      RwUInt32 chunkVersion);

bool WriteNtlAtomicExt(RwStream& stream,
                       DffDiagnostics& diag,
                       const DffNtlAtomicExt& ext,
                       RwUInt32 version);
