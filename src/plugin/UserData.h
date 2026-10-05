/**
 * plugin/UserData.h - RpUserData (rwID_USERDATAPLUGIN, 0x11F).
 *
 * Stream layout (RenderWare rpuserdt.c,
 * _rpUserDataStreamRead / _rpUserDataStreamWrite):
 *
 *   RwInt32 numArrays
 *   per array:
 *     RwInt32 nameLength
 *     char    name[nameLength]        (NUL-terminated, length includes the NUL)
 *     RwInt32 format                  (rpINTUSERDATA / rpREALUSERDATA / rpSTRINGUSERDATA)
 *     RwInt32 numElements
 *     payload:
 *       int    : RwInt32 x numElements
 *       real   : RwReal  x numElements
 *       string : per element -> RwInt32 length + char[length]
 *
 * DBO stores frame names, geometry names and material names here. The name and
 * string buffers frequently contain trailing garbage after the NUL (uninitialised
 * stack in the exporter), so the raw bytes are kept alongside the decoded text.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"

#include <string>
#include <vector>

struct DffUserDataString
{
    std::string          value;    ///< decoded text up to the first NUL
    std::vector<RwUInt8> rawBytes; ///< exact bytes as streamed (may hold garbage past the NUL)

    /// Bytes to emit; regenerated from `value` when the raw buffer was dropped.
    std::vector<RwUInt8> Encode() const;
    void SetValue(const std::string& v)
    {
        value = v;
        rawBytes.clear();
    }
};

struct DffUserDataArray
{
    std::string          name;
    std::vector<RwUInt8> rawNameBytes;
    RwInt32              format = rpNAUSERDATAFORMAT;

    std::vector<RwInt32>           intData;
    std::vector<RwReal>            realData;
    std::vector<DffUserDataString> stringData;

    std::vector<RwUInt8> EncodeName() const;
    RwInt32 NumElements() const;

    void SetName(const std::string& n)
    {
        name = n;
        rawNameBytes.clear();
    }
};

struct DffUserDataList
{
    RwUInt32                      chunkVersion = 0;
    std::vector<DffUserDataArray> arrays;
    /// Bytes between the end of the last array and the chunk's declared end.
    /// Always empty in the DBO corpus; kept so an odd stream still round-trips.
    std::vector<RwUInt8>          trailing;
    RwChunkSpan                   span;

    /** First string element of the array called `arrayName` (usually "name"). */
    const DffUserDataString* FindString(const char* arrayName) const;
    DffUserDataString* FindOrCreateString(const char* arrayName);

    /** First array with the given name, or nullptr. */
    const DffUserDataArray* FindArray(const char* arrayName) const;
    DffUserDataArray* FindArray(const char* arrayName);
};

/** Reads the USERDATA payload (header already consumed). */
bool ReadUserDataList(RwStream& stream,
                      DffDiagnostics& diag,
                      DffUserDataList& out,
                      RwUInt32 payloadSize,
                      RwUInt32 chunkVersion);

/** Writes a complete USERDATA chunk (header + payload). */
bool WriteUserDataList(RwStream& stream,
                       DffDiagnostics& diag,
                       const DffUserDataList& list,
                       RwUInt32 defaultVersion);
