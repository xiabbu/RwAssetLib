/**
 * dff/DffDocument.cpp - Load/Save facade implementation.
 */

#include "dff/DffDocument.h"

#include <cstdio>
#include <sstream>

DffDocument::DffDocument() = default;

DffClump& DffDocument::CreateClump()
{
    m_clump = std::make_unique<DffClump>();
    return *m_clump;
}

std::string DffDocument::GetLastError() const
{
    return m_diag.Summary();
}

// ============================================================================
// Load
// ============================================================================

bool DffDocument::Load(RwStream& stream)
{
    m_clump.reset();
    m_diag.Clear();

    const RwUInt32 headerPos = stream.GetPosition();
    RwChunkHeader header;
    if (!stream.ReadChunkHeader(header)) {
        return m_diag.Fail(headerPos, "truncated file: no chunk header");
    }
    if (header.type != rwID_CLUMP) {
        return m_diag.FailChunkType(headerPos, rwID_CLUMP, header.type);
    }

    m_version = header.version;

    auto clump = std::make_unique<DffClump>();
    if (!ReadClump(stream, m_diag, *clump, header.size, header.version)) {
        return false;
    }

    m_clump = std::move(clump);
    return true;
}

bool DffDocument::Load(const std::string& filename)
{
    RwStream stream;
    if (!stream.OpenRead(filename)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open file: " + filename);
    }
    return Load(stream);
}

bool DffDocument::LoadFromMemory(const void* data, size_t size)
{
    RwStream stream;
    if (!stream.OpenReadMemory(data, size)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open memory buffer");
    }
    return Load(stream);
}

#ifdef _WIN32
bool DffDocument::Load(const wchar_t* filename)
{
    RwStream stream;
    if (!stream.OpenRead(filename)) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open file");
    }
    return Load(stream);
}
#endif

// ============================================================================
// Save
// ============================================================================

bool DffDocument::Save(RwStream& stream)
{
    m_diag.Clear();

    if (!m_clump) {
        return m_diag.Fail(0, "no clump to save");
    }
    if (!stream.IsOpen() || !stream.IsWriteMode()) {
        return m_diag.Fail(0, "stream is not open for writing");
    }

    stream.SetDeriveAllChunkSizes(m_deriveAllChunkSizes);
    return WriteClump(stream, m_diag, *m_clump, m_version);
}

// Serialize before opening the destination so format failures do not truncate it.
bool DffDocument::Save(const std::string& filename)
{
    std::vector<RwUInt8> bytes;
    if (!SaveToMemory(bytes)) {
        return false;
    }
    RwStream stream;
    if (!stream.OpenWrite(filename)) {
        return m_diag.Fail(0, "failed to create file: " + filename);
    }
    if (!stream.Write(bytes.data(), bytes.size())) {
        return m_diag.Fail(0, "failed to write file: " + filename);
    }
    if (!stream.Close()) {
        return m_diag.Fail(0, stream.GetLastError() + ": " + filename);
    }
    return true;
}

bool DffDocument::SaveToMemory(std::vector<RwUInt8>& out)
{
    out.clear();

    RwStream stream;
    if (!stream.OpenWriteMemory()) {
        m_diag.Clear();
        return m_diag.Fail(0, "failed to open memory write stream");
    }
    if (!Save(stream)) {
        return false;
    }
    out = stream.GetMemoryBuffer();
    return true;
}

#ifdef _WIN32
bool DffDocument::Save(const wchar_t* filename)
{
    std::vector<RwUInt8> bytes;
    if (!SaveToMemory(bytes)) {
        return false;
    }
    RwStream stream;
    if (!stream.OpenWrite(filename)) {
        return m_diag.Fail(0, "failed to create file");
    }
    if (!stream.Write(bytes.data(), bytes.size())) {
        return m_diag.Fail(0, "failed to write file");
    }
    if (!stream.Close()) {
        return m_diag.Fail(0, stream.GetLastError());
    }
    return true;
}
#endif

// ============================================================================
// Structure dump
// ============================================================================

std::string DffDocument::DescribeStructure() const
{
    std::ostringstream os;

    if (!m_clump) {
        os << "no clump loaded\n";
        return os.str();
    }

    char versionBuf[96];
    const RwUInt32 unpacked = RwLibraryIDUnpackVersion(m_version);
    std::snprintf(versionBuf, sizeof(versionBuf), "0x%08X (RW %u.%u.%u.%u build %u)",
                  m_version,
                  RwVersionGetMajor(unpacked),
                  RwVersionGetMinor(unpacked),
                  RwVersionGetRevision(unpacked),
                  RwVersionGetBinaryFormat(unpacked),
                  RwLibraryIDUnpackBuildNum(m_version));

    os << "=== DFF structure ===\n";
    os << "version : " << versionBuf << "\n";
    os << "frames  : " << m_clump->frames.size() << "\n";
    os << "geoms   : " << m_clump->geometries.size() << "\n";
    os << "atomics : " << m_clump->atomics.size() << "\n";
    os << "lights  : " << m_clump->lights.size() << "   cameras: " << m_clump->cameras.size() << "\n";

    os << "\n-- frames --\n";
    for (size_t i = 0; i < m_clump->frames.size(); ++i) {
        const auto& f = m_clump->frames[i];
        if (!f) {
            continue;
        }
        os << "  [" << i << "] parent=" << f->parentIndex << " name='" << f->Name() << "'";
        if (f->hanim) {
            os << " hanim(id=" << f->hanim->nodeId;
            if (f->hanim->IsHierarchyRoot()) {
                os << ", ROOT, nodes=" << f->hanim->nodeInfos.size();
            }
            os << ")";
        }
        os << " ext=[";
        for (size_t e = 0; e < f->extensions.order.size(); ++e) {
            os << (e ? "," : "") << RwChunkTypeName(f->extensions.order[e]);
        }
        os << "]\n";
    }

    os << "\n-- geometries --\n";
    for (size_t i = 0; i < m_clump->geometries.size(); ++i) {
        const auto& g = m_clump->geometries[i];
        if (!g) {
            continue;
        }
        os << "  [" << i << "] '" << g->Name() << "'"
           << " verts=" << g->NumVertices()
           << " tris=" << g->triangles.size()
           << " mats=" << g->materials.size()
           << " uvSets=" << g->NumTexCoordSets()
           << " morphTargets=" << g->morphTargets.size();
        if (g->HasNormals()) os << " +normals";
        if (g->HasPreLitColors()) os << " +prelit";
        if (g->skin) {
            os << " +skin(bones=" << g->skin->numBones
               << ",used=" << g->skin->numUsedBones
               << ",maxW=" << g->skin->maxWeights
               << ",split=" << (g->skin->hasSplitData ? 1 : 0)
               << ",limit=" << g->skin->splitBoneLimit
               << ",meshes=" << g->skin->splitNumMeshes
               << ",rle=" << g->skin->splitNumRLE
               << ",remap=" << g->skin->splitMatrixRemapIndices.size() << ")";
        }
        if (g->binMesh) {
            os << " +binmesh(" << g->binMesh->meshes.size()
               << ",idx=" << g->binMesh->TotalIndices()
               << ",flags=0x" << std::hex << g->binMesh->flags << std::dec << ")";
        }
        if (g->toon) {
            os << " +toon(edges=" << g->toon->NumEdges()
               << ",crease=" << g->toon->numCreaseEdges
               << ",inks=" << g->toon->inkIDList.size()
               << ",paint='" << g->toon->DefaultPaintName() << "')";
        }
        if (g->morph) os << " +morph";
        if (g->collis) os << " +collis";
        os << "\n";

        for (size_t m = 0; m < g->materials.size(); ++m) {
            const auto& mat = g->materials[m];
            if (!mat) {
                continue;
            }
            os << "      mat[" << m << "]";
            if (const std::string* n = mat->UserDataName()) {
                os << " name='" << *n << "'";
            }
            os << " rgba=(" << int(mat->color.red) << "," << int(mat->color.green) << ","
               << int(mat->color.blue) << "," << int(mat->color.alpha) << ")";
            if (mat->texture) {
                os << " tex='" << mat->texture->name << "'";
            }
            if (mat->ntlMaterialExt) os << " +ntlExt";
            if (!mat->ntlWorldMaterials.empty()) os << " +ntlWorld(" << mat->ntlWorldMaterials.size() << ")";
            if (mat->matfx) os << " +matfx";
            if (mat->toon) os << " +toonMat";
            if (mat->uvanim) os << " +uvanim";
            if (mat->ltmap) os << " +ltmap";
            os << "\n";
        }
    }

    os << "\n-- atomics --\n";
    for (size_t i = 0; i < m_clump->atomics.size(); ++i) {
        const auto& a = m_clump->atomics[i];
        os << "  [" << i << "] frame=" << a.frameIndex
           << " geom=" << a.geomIndex
           << " flags=0x" << std::hex << a.flags << std::dec
           << " structSize=" << a.structSize;
        if (a.structSize >= 24) {
            os << " renderType=" << a.renderType << " blendMode=" << a.blendMode << " id=" << a.id;
        }
        if (a.rights) os << " +rights";
        if (a.matfx) os << " +matfx";
        os << "\n";
    }

    return os.str();
}

// ============================================================================
// Round-trip check
// ============================================================================

DffRoundTripResult DffCheckRoundTripBytes(const std::vector<RwUInt8>& source, bool deriveAllChunkSizes)
{
    DffRoundTripResult result;
    result.sourceSize = source.size();

    DffDocument doc;
    doc.SetDeriveAllChunkSizes(deriveAllChunkSizes);
    if (!doc.LoadFromMemory(source.data(), source.size())) {
        result.error = doc.GetLastError();
        return result;
    }
    result.loaded = true;

    std::vector<RwUInt8> output;
    if (!doc.SaveToMemory(output)) {
        result.error = doc.GetLastError();
        return result;
    }
    result.saved = true;
    result.outputSize = output.size();

    const size_t common = (source.size() < output.size()) ? source.size() : output.size();
    size_t firstDiff = common;
    size_t diffCount = 0;
    for (size_t i = 0; i < common; ++i) {
        if (source[i] != output[i]) {
            if (diffCount == 0) {
                firstDiff = i;
            }
            ++diffCount;
        }
    }
    diffCount += (source.size() > output.size()) ? (source.size() - output.size())
                                                 : (output.size() - source.size());

    result.firstDiffOffset = firstDiff;
    result.diffByteCount = diffCount;
    result.identical = (diffCount == 0);
    return result;
}

DffRoundTripResult DffCheckRoundTrip(const std::string& filename, bool deriveAllChunkSizes)
{
    DffRoundTripResult result;

    std::FILE* f = std::fopen(filename.c_str(), "rb");
    if (!f) {
        result.error = "failed to open file: " + filename;
        return result;
    }

    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(f);
        result.error = "failed to determine file size: " + filename;
        return result;
    }

    std::vector<RwUInt8> source(static_cast<size_t>(size));
    if (size > 0 && std::fread(source.data(), 1, source.size(), f) != source.size()) {
        std::fclose(f);
        result.error = "failed to read file: " + filename;
        return result;
    }
    std::fclose(f);

    return DffCheckRoundTripBytes(source, deriveAllChunkSizes);
}
