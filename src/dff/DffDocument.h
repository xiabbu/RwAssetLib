/**
 * dff/DffDocument.h - Load/Save facade for a whole .dff file.
 *
 * A DFF is a single rwID_CLUMP chunk. Everything needed to reproduce the file
 * byte-for-byte lives in the model (declared-size spans, extension order, raw
 * pass-through chunks), so Save() after Load() with no edits is 1:1 by
 * construction -- there is no dirty flag to set.
 */

#pragma once

#include "core/DffError.h"
#include "world/Clump.h"

#include <memory>
#include <string>
#include <vector>

class DffDocument
{
public:
    DffDocument();

    // -- load ---------------------------------------------------------------
    bool Load(const std::string& filename);
    bool LoadFromMemory(const void* data, size_t size);
    bool Load(RwStream& stream);
#ifdef _WIN32
    bool Load(const wchar_t* filename);
#endif

    // -- save ---------------------------------------------------------------
    bool Save(const std::string& filename);
    bool SaveToMemory(std::vector<RwUInt8>& out);
    bool Save(RwStream& stream);
#ifdef _WIN32
    bool Save(const wchar_t* filename);
#endif

    // -- model --------------------------------------------------------------
    DffClump* GetClump() { return m_clump.get(); }
    const DffClump* GetClump() const { return m_clump.get(); }
    DffClump& CreateClump();

    RwUInt32 GetVersion() const { return m_version; }
    void SetVersion(RwUInt32 version) { m_version = version; }

    /**
     * Derive every chunk's declared size from the modelled StreamGetSize
     * semantics instead of replaying what was read.
     *
     * A document rebuilt from a 3ds Max scene has nothing recorded, so this is
     * the mode that must reproduce the original bytes. Enabling it for a
     * freshly loaded document proves the semantics are complete.
     */
    void SetDeriveAllChunkSizes(bool derive) { m_deriveAllChunkSizes = derive; }
    bool DeriveAllChunkSizes() const { return m_deriveAllChunkSizes; }

    // -- diagnostics --------------------------------------------------------
    DffDiagnostics& Diagnostics() { return m_diag; }
    const DffDiagnostics& Diagnostics() const { return m_diag; }
    std::string GetLastError() const;

    /** Multi-line dump of the parsed structure (for `info`). */
    std::string DescribeStructure() const;

private:
    std::unique_ptr<DffClump> m_clump;
    RwUInt32                  m_version = rwLIBRARYVERSION37;
    DffDiagnostics            m_diag;
    bool                      m_deriveAllChunkSizes = false;
};

/** Result of a load -> save -> compare cycle against the source bytes. */
struct DffRoundTripResult
{
    bool        loaded = false;
    bool        saved = false;
    bool        identical = false;
    size_t      sourceSize = 0;
    size_t      outputSize = 0;
    size_t      firstDiffOffset = 0;   ///< valid when !identical && sizes overlap
    size_t      diffByteCount = 0;
    std::string error;

    bool Ok() const { return loaded && saved && identical; }
};

/** Loads `filename`, saves to memory and compares with the original bytes. */
DffRoundTripResult DffCheckRoundTrip(const std::string& filename, bool deriveAllChunkSizes = false);

/** Same, for bytes already in memory. */
DffRoundTripResult DffCheckRoundTripBytes(const std::vector<RwUInt8>& source, bool deriveAllChunkSizes = false);
