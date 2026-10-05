/**
 * core/DffError.h - Chunk-path error diagnostics.
 *
 * Every read/write failure is reported as a path through the chunk tree plus the
 * stream offset and, when relevant, the expected vs. actual value. That makes a
 * corrupt or unexpected file self-describing:
 *
 *   CLUMP/GEOMETRYLIST/GEOMETRY[3]/MATLIST/MATERIAL[1]/EXTENSION @0x1A24:
 *     unexpected chunk type (expected MATERIAL(0x07), got STRUCT(0x01))
 */

#pragma once

#include "core/RwTypes.h"

#include <string>
#include <vector>

/** Human-readable chunk name for diagnostics, e.g. "GEOMETRY". */
const char* RwChunkTypeName(RwUInt32 type);

/** "GEOMETRY(0x0F)" */
std::string RwChunkTypeLabel(RwUInt32 type);

struct DffError
{
    std::string path;      ///< chunk path, "" at top level
    RwUInt32    offset = 0;///< stream offset where the problem was detected
    std::string message;
    std::string expected;  ///< optional
    std::string actual;    ///< optional

    std::string Format() const;
};

/**
 * DffDiagnostics - path stack + first-error capture.
 *
 * Readers/writers push a scope for each chunk they descend into; the first
 * failure records the full path. Subsequent failures are kept in a list so the
 * caller can show the unwind chain.
 */
class DffDiagnostics
{
public:
    void PushScope(const std::string& name);
    void PushScope(RwUInt32 chunkType);
    void PushScope(RwUInt32 chunkType, int index);
    void PopScope();

    std::string CurrentPath() const;

    /** Records an error at `offset`. Returns false so callers can `return diag.Fail(...)`. */
    bool Fail(RwUInt32 offset, const std::string& message);
    bool Fail(RwUInt32 offset, const std::string& message, const std::string& expected, const std::string& actual);
    bool FailChunkType(RwUInt32 offset, RwUInt32 expectedType, RwUInt32 actualType);

    bool HasError() const { return !m_errors.empty(); }
    const std::vector<DffError>& Errors() const { return m_errors; }
    const DffError* FirstError() const { return m_errors.empty() ? nullptr : &m_errors.front(); }

    /** Single-line summary of the first error (empty when there is none). */
    std::string Summary() const;

    void Clear();

private:
    std::vector<std::string> m_scopes;
    std::vector<DffError>    m_errors;
};

/** RAII scope helper. */
class DffScope
{
public:
    DffScope(DffDiagnostics& diag, const std::string& name) : m_diag(diag) { m_diag.PushScope(name); }
    DffScope(DffDiagnostics& diag, RwUInt32 type) : m_diag(diag) { m_diag.PushScope(type); }
    DffScope(DffDiagnostics& diag, RwUInt32 type, int index) : m_diag(diag) { m_diag.PushScope(type, index); }
    ~DffScope() { m_diag.PopScope(); }

    DffScope(const DffScope&) = delete;
    DffScope& operator=(const DffScope&) = delete;

private:
    DffDiagnostics& m_diag;
};
