/**
 * core/DffError.cpp - Chunk-path error diagnostics implementation.
 */

#include "core/DffError.h"

#include <cstdio>

const char* RwChunkTypeName(RwUInt32 type)
{
    switch (type) {
        case rwID_NAOBJECT:         return "NAOBJECT";
        case rwID_STRUCT:           return "STRUCT";
        case rwID_STRING:           return "STRING";
        case rwID_EXTENSION:        return "EXTENSION";
        case rwID_CAMERA:           return "CAMERA";
        case rwID_TEXTURE:          return "TEXTURE";
        case rwID_MATERIAL:         return "MATERIAL";
        case rwID_MATLIST:          return "MATLIST";
        case rwID_WORLD:            return "WORLD";
        case rwID_MATRIX:           return "MATRIX";
        case rwID_FRAMELIST:        return "FRAMELIST";
        case rwID_GEOMETRY:         return "GEOMETRY";
        case rwID_CLUMP:            return "CLUMP";
        case rwID_LIGHT:            return "LIGHT";
        case rwID_UNICODESTRING:    return "UNICODESTRING";
        case rwID_ATOMIC:           return "ATOMIC";
        case rwID_TEXTURENATIVE:    return "TEXTURENATIVE";
        case rwID_TEXDICTIONARY:    return "TEXDICTIONARY";
        case rwID_GEOMETRYLIST:     return "GEOMETRYLIST";
        case rwID_ANIMANIMATION:    return "ANIMANIMATION";
        case rwID_RIGHTTORENDER:    return "RIGHTTORENDER";
        case rwID_UVANIMDICT:       return "UVANIMDICT";
        case rwID_COLLTREE:         return "COLLTREE";

        case rwID_MORPHPLUGIN:      return "MORPH";
        case rwID_LTMAPPLUGIN:      return "LTMAP";
        case rwID_SKINPLUGIN:       return "SKIN";
        case rwID_COLLISPLUGIN:     return "COLLIS";
        case rwID_HANIMPLUGIN:      return "HANIM";
        case rwID_USERDATAPLUGIN:   return "USERDATA";
        case rwID_MATFXPLUGIN:      return "MATFX";
        case rwID_TOONPLUGIN:       return "TOON";
        case rwID_UVANIMPLUGIN:     return "UVANIM";
        case rwID_BINMESHPLUGIN:    return "BINMESH";
        case rwID_NABORPLUGIN:      return "NABOR";

        case rwID_NTL_MATERIAL_EXT:   return "NTL_MATERIAL_EXT";
        case rwID_NTL_WORLD_MATERIAL: return "NTL_WORLD_MATERIAL";

        default: return "UNKNOWN";
    }
}

std::string RwChunkTypeLabel(RwUInt32 type)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s(0x%02X)", RwChunkTypeName(type), static_cast<unsigned>(type));
    return buf;
}

std::string DffError::Format() const
{
    std::string out = path.empty() ? std::string("<root>") : path;

    char offsetBuf[32];
    std::snprintf(offsetBuf, sizeof(offsetBuf), " @0x%X: ", static_cast<unsigned>(offset));
    out += offsetBuf;
    out += message;

    if (!expected.empty() || !actual.empty()) {
        out += " (expected ";
        out += expected.empty() ? "?" : expected;
        out += ", got ";
        out += actual.empty() ? "?" : actual;
        out += ")";
    }
    return out;
}

void DffDiagnostics::PushScope(const std::string& name)
{
    m_scopes.push_back(name);
}

void DffDiagnostics::PushScope(RwUInt32 chunkType)
{
    m_scopes.push_back(RwChunkTypeName(chunkType));
}

void DffDiagnostics::PushScope(RwUInt32 chunkType, int index)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s[%d]", RwChunkTypeName(chunkType), index);
    m_scopes.push_back(buf);
}

void DffDiagnostics::PopScope()
{
    if (!m_scopes.empty()) {
        m_scopes.pop_back();
    }
}

std::string DffDiagnostics::CurrentPath() const
{
    std::string out;
    for (size_t i = 0; i < m_scopes.size(); ++i) {
        if (i != 0) {
            out += '/';
        }
        out += m_scopes[i];
    }
    return out;
}

bool DffDiagnostics::Fail(RwUInt32 offset, const std::string& message)
{
    DffError err;
    err.path = CurrentPath();
    err.offset = offset;
    err.message = message;
    m_errors.push_back(std::move(err));
    return false;
}

bool DffDiagnostics::Fail(RwUInt32 offset, const std::string& message, const std::string& expected, const std::string& actual)
{
    DffError err;
    err.path = CurrentPath();
    err.offset = offset;
    err.message = message;
    err.expected = expected;
    err.actual = actual;
    m_errors.push_back(std::move(err));
    return false;
}

bool DffDiagnostics::FailChunkType(RwUInt32 offset, RwUInt32 expectedType, RwUInt32 actualType)
{
    return Fail(offset, "unexpected chunk type", RwChunkTypeLabel(expectedType), RwChunkTypeLabel(actualType));
}

std::string DffDiagnostics::Summary() const
{
    if (m_errors.empty()) {
        return {};
    }
    return m_errors.front().Format();
}

void DffDiagnostics::Clear()
{
    m_scopes.clear();
    m_errors.clear();
}
