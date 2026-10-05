/**
 * plugin/UserData.cpp - RpUserData read/write.
 */

#include "plugin/UserData.h"

namespace
{
    std::vector<RwUInt8> EncodeNulTerminated(const std::string& s)
    {
        std::vector<RwUInt8> out(s.size() + 1, 0);
        if (!s.empty()) {
            std::memcpy(out.data(), s.data(), s.size());
        }
        return out;
    }

    std::string DecodeUpToNul(const std::vector<RwUInt8>& bytes)
    {
        size_t len = 0;
        while (len < bytes.size() && bytes[len] != 0) {
            ++len;
        }
        if (len == 0) {
            return {};
        }
        return std::string(reinterpret_cast<const char*>(bytes.data()), len);
    }
}

std::vector<RwUInt8> DffUserDataString::Encode() const
{
    if (!rawBytes.empty()) {
        return rawBytes;
    }
    if (value.empty()) {
        return {};
    }
    return EncodeNulTerminated(value);
}

std::vector<RwUInt8> DffUserDataArray::EncodeName() const
{
    if (!rawNameBytes.empty()) {
        return rawNameBytes;
    }
    if (name.empty()) {
        return {};
    }
    return EncodeNulTerminated(name);
}

RwInt32 DffUserDataArray::NumElements() const
{
    switch (format) {
        case rpINTUSERDATA:    return static_cast<RwInt32>(intData.size());
        case rpREALUSERDATA:   return static_cast<RwInt32>(realData.size());
        case rpSTRINGUSERDATA: return static_cast<RwInt32>(stringData.size());
        default:               return 0;
    }
}

const DffUserDataString* DffUserDataList::FindString(const char* arrayName) const
{
    for (const auto& arr : arrays) {
        if (arr.format != rpSTRINGUSERDATA || arr.stringData.empty()) {
            continue;
        }
        if (arrayName == nullptr || arr.name == arrayName) {
            return &arr.stringData[0];
        }
    }
    return nullptr;
}

DffUserDataString* DffUserDataList::FindOrCreateString(const char* arrayName)
{
    for (auto& arr : arrays) {
        if (arr.format == rpSTRINGUSERDATA && arr.name == arrayName) {
            if (arr.stringData.empty()) {
                arr.stringData.resize(1);
            }
            return &arr.stringData[0];
        }
    }

    DffUserDataArray arr;
    arr.SetName(arrayName ? arrayName : "");
    arr.format = rpSTRINGUSERDATA;
    arr.stringData.resize(1);

    // DBO's material/frame name array is always at index 0.
    arrays.insert(arrays.begin(), std::move(arr));
    return &arrays[0].stringData[0];
}

const DffUserDataArray* DffUserDataList::FindArray(const char* arrayName) const
{
    for (const auto& arr : arrays) {
        if (arr.name == arrayName) {
            return &arr;
        }
    }
    return nullptr;
}

DffUserDataArray* DffUserDataList::FindArray(const char* arrayName)
{
    for (auto& arr : arrays) {
        if (arr.name == arrayName) {
            return &arr;
        }
    }
    return nullptr;
}

bool ReadUserDataList(RwStream& stream,
                      DffDiagnostics& diag,
                      DffUserDataList& out,
                      RwUInt32 payloadSize,
                      RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_USERDATAPLUGIN);

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    out.chunkVersion = chunkVersion;
    out.arrays.clear();
    out.trailing.clear();

    if (payloadSize == 0) {
        if (!out.span.Record(0, 0, chunkVersion, stream)) {
            return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
        }
        return true;
    }

    RwInt32 numArrays = 0;
    if (!stream.ReadInt32(&numArrays) || numArrays < 0) {
        stream.Seek(endPos);
        return diag.Fail(startPos, "invalid UserData array count");
    }

    out.arrays.reserve(static_cast<size_t>(numArrays));

    for (RwInt32 i = 0; i < numArrays; ++i) {
        if (stream.GetPosition() >= endPos) {
            break;
        }

        DffUserDataArray arr;

        RwInt32 nameLength = 0;
        if (!stream.ReadInt32(&nameLength) || nameLength < 0) {
            stream.Seek(endPos);
            return diag.Fail(stream.GetPosition(), "invalid UserData array name length");
        }

        if (nameLength > 0) {
            arr.rawNameBytes.resize(static_cast<size_t>(nameLength));
            if (!stream.Read(arr.rawNameBytes.data(), static_cast<size_t>(nameLength))) {
                stream.Seek(endPos);
                return diag.Fail(stream.GetPosition(), "truncated UserData array name");
            }
            arr.name = DecodeUpToNul(arr.rawNameBytes);
        }

        if (!stream.ReadInt32(&arr.format)) {
            stream.Seek(endPos);
            return diag.Fail(stream.GetPosition(), "truncated UserData array format");
        }

        RwInt32 numElements = 0;
        if (!stream.ReadInt32(&numElements) || numElements < 0) {
            stream.Seek(endPos);
            return diag.Fail(stream.GetPosition(), "invalid UserData element count");
        }

        switch (arr.format) {
            case rpINTUSERDATA:
            {
                arr.intData.resize(static_cast<size_t>(numElements));
                for (RwInt32 j = 0; j < numElements; ++j) {
                    if (!stream.ReadInt32(&arr.intData[static_cast<size_t>(j)])) {
                        stream.Seek(endPos);
                        return diag.Fail(stream.GetPosition(), "truncated UserData int payload");
                    }
                }
                break;
            }

            case rpREALUSERDATA:
            {
                arr.realData.resize(static_cast<size_t>(numElements));
                if (numElements > 0 && !stream.ReadReals(arr.realData.data(), static_cast<size_t>(numElements))) {
                    stream.Seek(endPos);
                    return diag.Fail(stream.GetPosition(), "truncated UserData real payload");
                }
                break;
            }

            case rpSTRINGUSERDATA:
            {
                arr.stringData.resize(static_cast<size_t>(numElements));
                for (RwInt32 j = 0; j < numElements; ++j) {
                    RwInt32 strLength = 0;
                    if (!stream.ReadInt32(&strLength) || strLength < 0) {
                        stream.Seek(endPos);
                        return diag.Fail(stream.GetPosition(), "invalid UserData string length");
                    }

                    auto& s = arr.stringData[static_cast<size_t>(j)];
                    if (strLength > 0) {
                        s.rawBytes.resize(static_cast<size_t>(strLength));
                        if (!stream.Read(s.rawBytes.data(), static_cast<size_t>(strLength))) {
                            stream.Seek(endPos);
                            return diag.Fail(stream.GetPosition(), "truncated UserData string payload");
                        }
                        s.value = DecodeUpToNul(s.rawBytes);
                    }
                }
                break;
            }

            default:
                stream.Seek(endPos);
                return diag.Fail(stream.GetPosition(),
                                 "unknown UserData format",
                                 "1(int) | 2(real) | 3(string)",
                                 std::to_string(arr.format));
        }

        out.arrays.push_back(std::move(arr));
    }

    // Anything left inside the declared payload is preserved verbatim so the
    // chunk round-trips even if an exporter padded it.
    const RwUInt32 afterArrays = stream.GetPosition();
    if (afterArrays < endPos) {
        const RwUInt32 tail = endPos - afterArrays;
        out.trailing.resize(tail);
        if (!stream.Read(out.trailing.data(), tail)) {
            stream.Seek(endPos);
            return diag.Fail(afterArrays, "truncated UserData trailing bytes");
        }
    } else if (afterArrays > endPos) {
        return diag.Fail(startPos,
                         "UserData arrays overran the declared payload",
                         std::to_string(payloadSize),
                         std::to_string(afterArrays - startPos));
    }

    const RwUInt32 consumed = stream.GetPosition() - startPos;
    if (!out.span.Record(payloadSize, consumed, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }

    stream.Seek(endPos);
    return true;
}

bool WriteUserDataList(RwStream& stream,
                       DffDiagnostics& diag,
                       const DffUserDataList& list,
                       RwUInt32 defaultVersion)
{
    DffScope scope(diag, rwID_USERDATAPLUGIN);

    const RwUInt32 version = list.chunkVersion ? list.chunkVersion : defaultVersion;
    ChunkWriter writer(stream, version);

    if (!writer.Begin(rwID_USERDATAPLUGIN, list.span, version)) {
        return diag.Fail(stream.GetPosition(), "failed to begin USERDATA chunk");
    }

    // An empty list round-trips as a zero-length payload (DBO emits these).
    if (!list.arrays.empty() || list.span.actualSize != 0) {
        stream.WriteInt32(static_cast<RwInt32>(list.arrays.size()));

        for (const auto& arr : list.arrays) {
            const std::vector<RwUInt8> nameBytes = arr.EncodeName();
            stream.WriteInt32(static_cast<RwInt32>(nameBytes.size()));
            if (!nameBytes.empty()) {
                stream.Write(nameBytes.data(), nameBytes.size());
            }

            stream.WriteInt32(arr.format);

            const RwInt32 numElements = arr.NumElements();
            stream.WriteInt32(numElements);

            switch (arr.format) {
                case rpINTUSERDATA:
                    for (RwInt32 v : arr.intData) {
                        stream.WriteInt32(v);
                    }
                    break;

                case rpREALUSERDATA:
                    if (numElements > 0) {
                        stream.WriteReals(arr.realData.data(), static_cast<size_t>(numElements));
                    }
                    break;

                case rpSTRINGUSERDATA:
                    for (const auto& s : arr.stringData) {
                        const std::vector<RwUInt8> bytes = s.Encode();
                        stream.WriteInt32(static_cast<RwInt32>(bytes.size()));
                        if (!bytes.empty()) {
                            stream.Write(bytes.data(), bytes.size());
                        }
                    }
                    break;

                default:
                    return diag.Fail(stream.GetPosition(),
                                     "cannot write unknown UserData format",
                                     "1|2|3",
                                     std::to_string(arr.format));
            }
        }

        if (!list.trailing.empty()) {
            stream.Write(list.trailing.data(), list.trailing.size());
        }
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close USERDATA chunk");
}
