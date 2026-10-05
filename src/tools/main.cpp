/**
 * tools/main.cpp - RwAssetLib command line.
 *
 *   rwasset validate      <file.dff>
 *   rwasset validate-dir  <dir> [--report <file>] [--ext dff,anm] [--quiet]
 *   rwasset info          <file.dff>
 *   rwasset scan-unknown  <dir>
 *   rwasset regen-stats   <dir>
 *   rwasset regen-check   <file|dir> [--details] [--raw-face-normals] [--vertex-extrusion-normals] [--scene-derived]
 *   rwasset compare-derived <original.dff> <exported.dff>
 *   rwasset compare-bounds <original.dff> <exported.dff>
 */

#include "anim/AnmDocument.h"
#include "anim/UvaDocument.h"
#include "dff/DffDocument.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int CmdRegenStats(const std::string& dir);
int CmdRegenCheck(int argc, char** argv);
int CmdCompareDerived(const std::string& originalPath, const std::string& exportedPath);
int CmdCompareBounds(const std::string& originalPath, const std::string& exportedPath);
int CmdCompareDff(const std::string& originalPath, const std::string& exportedPath);
int CmdToonSkinCheck(const std::string& path, int geometryIndex);

namespace
{

int PrintUsage()
{
    std::cout <<
        "RwAssetLib - RenderWare DFF/ANM/UVA tools with DBO extensions\n"
        "\n"
        "  rwasset validate      <file>              round-trip byte check for one file\n"
        "  rwasset validate-dir  <dir> [options]     round-trip byte check for a tree\n"
        "  rwasset info          <file.dff>          dump the parsed structure\n"
        "  rwasset scan-unknown  <dir>               list extension chunks kept verbatim\n"
        "  rwasset regen-stats   <dir>               histogram of BinMesh/Toon properties\n"
        "  rwasset regen-check   <file|dir> [--details] [--raw-face-normals] [--vertex-extrusion-normals] [--scene-derived]\n"
        "                                             rebuild BinMesh/Toon and compare with the file\n"
        "  rwasset compare-derived <original> <exported>   compare BinMesh/Toon bytes by geometry\n"
        "  rwasset compare-bounds  <original> <exported>   compare morph target bounding spheres\n"
        "  rwasset compare-dff     <original> <exported>   compare every modelled DFF component\n"
        "  rwasset toon-skin-check <file> <geometry>  probe bind-pose Toon normals\n"
        "\n"
        "validate-dir options:\n"
        "  --report <file>   write a full report (default: only a summary on stdout)\n"
        "  --ext <list>      comma separated extensions to scan (default: dff,anm,uva)\n"
        "  --quiet           suppress per-failure stdout lines\n";
    return 2;
}

std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> SplitCsv(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) out.push_back(ToLower(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(ToLower(cur));
    return out;
}

bool ReadWholeFile(const fs::path& path, std::vector<RwUInt8>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 0) {
        return false;
    }
    out.resize(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(out.data()), size);
        if (!in) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------------

struct FileCheck
{
    bool        ok = false;
    bool        loadFailed = false;
    std::string detail;
};

FileCheck CheckDff(const std::vector<RwUInt8>& bytes, bool deriveSizes)
{
    FileCheck fc;
    const DffRoundTripResult r = DffCheckRoundTripBytes(bytes, deriveSizes);

    if (!r.loaded) {
        fc.loadFailed = true;
        fc.detail = "load failed: " + r.error;
        return fc;
    }
    if (!r.saved) {
        fc.detail = "save failed: " + r.error;
        return fc;
    }
    if (!r.identical) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "%zu byte(s) differ; first at 0x%zX; size %zu -> %zu",
                      r.diffByteCount, r.firstDiffOffset, r.sourceSize, r.outputSize);
        fc.detail = buf;
        return fc;
    }

    fc.ok = true;
    return fc;
}

FileCheck CheckAnm(const std::vector<RwUInt8>& bytes)
{
    FileCheck fc;
    const AnmRoundTripResult r = AnmCheckRoundTripBytes(bytes);

    if (!r.loaded) {
        fc.loadFailed = true;
        fc.detail = "load failed: " + r.error;
        return fc;
    }
    if (!r.saved) {
        fc.detail = "save failed: " + r.error;
        return fc;
    }
    if (!r.identical) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "%zu byte(s) differ; first at 0x%zX; size %zu -> %zu",
                      r.diffByteCount, r.firstDiffOffset, r.sourceSize, r.outputSize);
        fc.detail = buf;
        return fc;
    }

    fc.ok = true;
    return fc;
}

FileCheck CheckUva(const std::vector<RwUInt8>& bytes)
{
    FileCheck fc;
    const UvaRoundTripResult r = UvaCheckRoundTripBytes(bytes);

    if (!r.loaded) {
        fc.loadFailed = true;
        fc.detail = "load failed: " + r.error;
        return fc;
    }
    if (!r.saved) {
        fc.detail = "save failed: " + r.error;
        return fc;
    }
    if (!r.identical) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "%zu byte(s) differ; first at 0x%zX; size %zu -> %zu",
                      r.diffByteCount, r.firstDiffOffset, r.sourceSize, r.outputSize);
        fc.detail = buf;
        return fc;
    }

    fc.ok = true;
    return fc;
}
FileCheck CheckFile(const fs::path& path, bool deriveSizes = false)
{
    FileCheck fc;

    std::vector<RwUInt8> bytes;
    if (!ReadWholeFile(path, bytes)) {
        fc.loadFailed = true;
        fc.detail = "cannot read file";
        return fc;
    }

    const std::string ext = ToLower(path.extension().string());
    if (ext == ".anm") {
        return CheckAnm(bytes);
    }
    if (ext == ".uva") {
        return CheckUva(bytes);
    }
    return CheckDff(bytes, deriveSizes);
}

int CmdValidate(const std::string& file, bool deriveSizes)
{
    const FileCheck fc = CheckFile(file, deriveSizes);
    if (fc.ok) {
        std::cout << "OK  byte-identical: " << file << "\n";
        return 0;
    }
    std::cout << (fc.loadFailed ? "ERROR " : "FAIL  ") << file << "\n      " << fc.detail << "\n";
    return 1;
}

int CmdValidateDir(int argc, char** argv)
{
    if (argc < 3) {
        return PrintUsage();
    }

    const fs::path root = argv[2];
    std::string reportPath;
    std::vector<std::string> exts = {"dff", "anm", "uva"};
    bool quiet = false;
    bool deriveSizes = false;

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--report" && i + 1 < argc) {
            reportPath = argv[++i];
        } else if (arg == "--ext" && i + 1 < argc) {
            exts = SplitCsv(argv[++i]);
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--derive-sizes") {
            deriveSizes = true;
        } else {
            std::cerr << "unknown option: " << arg << "\n";
            return PrintUsage();
        }
    }

    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        std::cerr << "not a directory: " << root.string() << "\n";
        return 2;
    }

    auto wanted = [&](const fs::path& p) {
        std::string e = ToLower(p.extension().string());
        if (!e.empty() && e[0] == '.') {
            e.erase(0, 1);
        }
        return std::find(exts.begin(), exts.end(), e) != exts.end();
    };

    std::ofstream report;
    if (!reportPath.empty()) {
        report.open(reportPath, std::ios::binary | std::ios::trunc);
        if (!report) {
            std::cerr << "cannot write report: " << reportPath << "\n";
            return 2;
        }
    }

    size_t total = 0, ok = 0, fail = 0, error = 0;
    std::map<std::string, std::pair<size_t, size_t>> perDir;  // dir -> (ok, bad)

    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            break;
        }
        const fs::directory_entry& entry = *it;
        if (!entry.is_regular_file(ec) || !wanted(entry.path())) {
            continue;
        }

        ++total;
        const FileCheck fc = CheckFile(entry.path(), deriveSizes);
        const std::string rel = fs::relative(entry.path(), root, ec).string();
        const std::string dir = fs::path(rel).parent_path().string();

        if (fc.ok) {
            ++ok;
            perDir[dir].first++;
        } else {
            perDir[dir].second++;
            if (fc.loadFailed) {
                ++error;
            } else {
                ++fail;
            }
            const std::string line = std::string(fc.loadFailed ? "ERROR " : "FAIL  ") + rel + " : " + fc.detail;
            if (!quiet) {
                std::cout << line << "\n";
            }
            if (report) {
                report << line << "\n";
            }
        }

        if (!quiet && (total % 1000) == 0) {
            std::cout << "  ... " << total << " files checked (" << ok << " ok)\n" << std::flush;
        }
    }

    std::ostringstream summary;
    summary << "summary: total=" << total << " ok=" << ok << " fail=" << fail << " error=" << error;

    if (report) {
        report << "\n-- per directory --\n";
        for (const auto& kv : perDir) {
            report << "  " << (kv.first.empty() ? "." : kv.first)
                   << " : ok=" << kv.second.first << " bad=" << kv.second.second << "\n";
        }
        report << "\n" << summary.str() << "\n";
    }

    std::cout << summary.str() << "\n";
    return (fail == 0 && error == 0) ? 0 : 1;
}

int CmdInfo(const std::string& file)
{
    DffDocument doc;
    if (!doc.Load(file)) {
        std::cerr << "load failed: " << doc.GetLastError() << "\n";
        return 1;
    }
    std::cout << doc.DescribeStructure();
    return 0;
}

// ---------------------------------------------------------------------------
// scan-unknown: which extension chunks does the model still keep verbatim?
// ---------------------------------------------------------------------------

void CollectRaw(const DffExtensionList& list, const char* owner, std::map<std::string, size_t>& counts)
{
    for (const auto& raw : list.raws) {
        counts[std::string(owner) + "/" + RwChunkTypeLabel(raw.type)]++;
    }
}

// ---------------------------------------------------------------------------
// analyze: what in a DFF is NOT derivable from the semantic model?
//
// The 3ds Max exporter rebuilds a document purely from the scene, so anything
// that cannot be recomputed has to be carried as a scene attribute. This
// measures exactly which such data occurs in a corpus.
// ---------------------------------------------------------------------------

struct DerivabilityStats
{
    size_t files = 0;
    size_t loadFailed = 0;

    size_t textures = 0;
    size_t textureNamePadDirty = 0;   ///< bytes after the NUL are not all zero
    size_t textureMaskPadDirty = 0;

    size_t userDataStrings = 0;
    size_t userDataStringPadDirty = 0;

    size_t clumpsWithMultipleChunkVersions = 0;
    std::map<RwUInt32, size_t> chunkVersions;

    size_t atomics = 0;
    std::map<RwUInt32, size_t> atomicStructSizes;

    size_t clumpsWithCameraLightChunks = 0;
    size_t framesWithLegacyStringName = 0;
    std::map<std::string, size_t> frameExtensionOrders;

    /// Distinct byte patterns seen in texture mask STRING payloads.
    std::map<std::string, size_t> maskRawPatterns;
};

/// "00 cd cd cd" style dump, capped so the histogram stays readable.
std::string HexDump(const std::vector<RwUInt8>& raw, size_t maxBytes = 16)
{
    std::string out;
    char buf[8];
    const size_t n = raw.size() < maxBytes ? raw.size() : maxBytes;
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(buf, sizeof(buf), "%02x", raw[i]);
        if (i) out += ' ';
        out += buf;
    }
    if (raw.size() > n) {
        out += " ...";
    }
    return out;
}

/// True when every byte after the terminating NUL is zero.
bool PaddingIsClean(const std::vector<RwUInt8>& raw)
{
    size_t i = 0;
    while (i < raw.size() && raw[i] != 0) {
        ++i;
    }
    for (; i < raw.size(); ++i) {
        if (raw[i] != 0) {
            return false;
        }
    }
    return true;
}

void AnalyzeUserDataList(const DffUserDataList& list, DerivabilityStats& st)
{
    for (const auto& arr : list.arrays) {
        for (const auto& s : arr.stringData) {
            ++st.userDataStrings;
            if (!s.rawBytes.empty() && !PaddingIsClean(s.rawBytes)) {
                ++st.userDataStringPadDirty;
            }
        }
    }
}

void AnalyzeTexture(const DffTexture& tex, DerivabilityStats& st)
{
    ++st.textures;
    if (!tex.rawNameBytes.empty() && !PaddingIsClean(tex.rawNameBytes)) {
        ++st.textureNamePadDirty;
    }
    if (!tex.rawMaskBytes.empty() && !PaddingIsClean(tex.rawMaskBytes)) {
        ++st.textureMaskPadDirty;
    }
    if (!tex.rawMaskBytes.empty()) {
        st.maskRawPatterns[HexDump(tex.rawMaskBytes)]++;
    }
    for (const auto& ud : tex.userDataLists) {
        AnalyzeUserDataList(ud, st);
    }
}

int CmdAnalyze(const std::string& dir)
{
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        std::cerr << "not a directory: " << dir << "\n";
        return 2;
    }

    DerivabilityStats st;

    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            break;
        }
        const fs::directory_entry& entry = *it;
        if (!entry.is_regular_file(ec) || ToLower(entry.path().extension().string()) != ".dff") {
            continue;
        }

        ++st.files;
        DffDocument doc;
        if (!doc.Load(entry.path().string())) {
            ++st.loadFailed;
            continue;
        }
        const DffClump* clump = doc.GetClump();
        if (!clump) {
            continue;
        }

        st.chunkVersions[doc.GetVersion()]++;

        if (!clump->lights.empty() || !clump->cameras.empty()) {
            ++st.clumpsWithCameraLightChunks;
        }

        for (const auto& f : clump->frames) {
            if (!f) continue;
            std::string order;
            for (RwUInt32 t : f->extensions.order) {
                if (!order.empty()) order += ",";
                order += RwChunkTypeName(t);
            }
            st.frameExtensionOrders[order]++;
            for (const auto& raw : f->extensions.raws) {
                if (raw.type == rwID_STRING) {
                    ++st.framesWithLegacyStringName;
                }
            }
            if (f->userData) {
                AnalyzeUserDataList(*f->userData, st);
            }
        }

        for (const auto& a : clump->atomics) {
            ++st.atomics;
            st.atomicStructSizes[a.structSize]++;
        }

        for (const auto& g : clump->geometries) {
            if (!g) continue;
            for (const auto& ud : g->userDataLists) {
                AnalyzeUserDataList(ud, st);
            }
            for (const auto& m : g->materials) {
                if (!m) continue;
                for (const auto& ud : m->userDataLists) {
                    AnalyzeUserDataList(ud, st);
                }
                if (m->texture) AnalyzeTexture(*m->texture, st);
                if (m->ntlMaterialExt && m->ntlMaterialExt->multiTexture) {
                    AnalyzeTexture(*m->ntlMaterialExt->multiTexture, st);
                }
            }
        }
    }

    std::cout << "files scanned            : " << st.files << " (" << st.loadFailed << " failed)\n";
    std::cout << "\n-- chunk header library ids --\n";
    for (const auto& kv : st.chunkVersions) {
        std::printf("  0x%08X : %zu file(s)\n", kv.first, kv.second);
    }

    std::cout << "\n-- string padding (non-derivable if dirty) --\n";
    std::cout << "  textures                 : " << st.textures << "\n";
    std::cout << "  texture name pad dirty   : " << st.textureNamePadDirty << "\n";
    std::cout << "  texture mask pad dirty   : " << st.textureMaskPadDirty << "\n";
    std::cout << "  userdata strings         : " << st.userDataStrings << "\n";
    std::cout << "  userdata pad dirty       : " << st.userDataStringPadDirty << "\n";

    std::cout << "\n-- atomic STRUCT sizes --\n";
    for (const auto& kv : st.atomicStructSizes) {
        std::cout << "  " << kv.first << " bytes : " << kv.second << " atomic(s)\n";
    }

    std::cout << "\n-- frame extension order --\n";
    for (const auto& kv : st.frameExtensionOrders) {
        std::cout << "  [" << kv.first << "] : " << kv.second << " frame(s)\n";
    }

    std::cout << "\n-- texture mask STRING byte patterns (top 12) --\n";
    {
        std::vector<std::pair<std::string, size_t>> byCount(st.maskRawPatterns.begin(), st.maskRawPatterns.end());
        std::sort(byCount.begin(), byCount.end(),
                  [](const std::pair<std::string, size_t>& a, const std::pair<std::string, size_t>& b) {
                      return a.second > b.second;
                  });
        std::cout << "  distinct patterns : " << byCount.size() << "\n";
        for (size_t i = 0; i < byCount.size() && i < 12; ++i) {
            std::cout << "  " << byCount[i].second << " x  [" << byCount[i].first << "]\n";
        }
    }

    std::cout << "\nclumps with camera/light chunks : " << st.clumpsWithCameraLightChunks << "\n";
    std::cout << "frames with legacy STRING name  : " << st.framesWithLegacyStringName << "\n";
    return 0;
}

int CmdScanUnknown(const std::string& dir)
{
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        std::cerr << "not a directory: " << dir << "\n";
        return 2;
    }

    std::map<std::string, size_t> counts;
    size_t files = 0, failed = 0;

    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            break;
        }
        const fs::directory_entry& entry = *it;
        if (!entry.is_regular_file(ec) || ToLower(entry.path().extension().string()) != ".dff") {
            continue;
        }

        ++files;
        DffDocument doc;
        if (!doc.Load(entry.path().string())) {
            ++failed;
            continue;
        }

        const DffClump* clump = doc.GetClump();
        if (!clump) {
            continue;
        }

        CollectRaw(clump->extensions, "clump", counts);
        for (const auto& camera : clump->cameras) {
            CollectRaw(camera.extensions, "camera", counts);
        }
        for (const auto& light : clump->lights) {
            CollectRaw(light.extensions, "light", counts);
        }
        for (const auto& f : clump->frames) {
            if (f) CollectRaw(f->extensions, "frame", counts);
        }
        for (const auto& a : clump->atomics) {
            CollectRaw(a.extensions, "atomic", counts);
        }
        for (const auto& g : clump->geometries) {
            if (!g) continue;
            CollectRaw(g->extensions, "geometry", counts);
            for (const auto& m : g->materials) {
                if (!m) continue;
                CollectRaw(m->extensions, "material", counts);
                if (m->texture) CollectRaw(m->texture->extensions, "texture", counts);
            }
        }
    }

    std::cout << "scanned " << files << " dff files (" << failed << " failed to load)\n";
    if (counts.empty()) {
        std::cout << "no verbatim extension chunks: every extension is modelled semantically\n";
        return 0;
    }
    std::cout << "extension chunks kept verbatim:\n";
    for (const auto& kv : counts) {
        std::cout << "  " << kv.first << " : " << kv.second << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        return PrintUsage();
    }

    const std::string cmd = argv[1];

    if (cmd == "validate") {
        if (argc < 3) return PrintUsage();
        bool deriveSizes = false;
        for (int i = 3; i < argc; ++i) {
            if (std::strcmp(argv[i], "--derive-sizes") == 0) deriveSizes = true;
        }
        return CmdValidate(argv[2], deriveSizes);
    }
    if (cmd == "validate-dir") {
        return CmdValidateDir(argc, argv);
    }
    if (cmd == "info") {
        if (argc < 3) return PrintUsage();
        return CmdInfo(argv[2]);
    }
    if (cmd == "scan-unknown") {
        if (argc < 3) return PrintUsage();
        return CmdScanUnknown(argv[2]);
    }
    if (cmd == "analyze") {
        if (argc < 3) return PrintUsage();
        return CmdAnalyze(argv[2]);
    }
    if (cmd == "regen-stats") {
        if (argc < 3) return PrintUsage();
        return CmdRegenStats(argv[2]);
    }
    if (cmd == "regen-check") {
        if (argc < 3) return PrintUsage();
        return CmdRegenCheck(argc, argv);
    }
    if (cmd == "compare-derived") {
        if (argc != 4) return PrintUsage();
        return CmdCompareDerived(argv[2], argv[3]);
    }
    if (cmd == "compare-bounds") {
        if (argc != 4) return PrintUsage();
        return CmdCompareBounds(argv[2], argv[3]);
    }
    if (cmd == "compare-dff") {
        if (argc != 4) return PrintUsage();
        return CmdCompareDff(argv[2], argv[3]);
    }
    if (cmd == "toon-skin-check") {
        if (argc != 4) return PrintUsage();
        char* end;
        const long index = std::strtol(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || index < 0 || index > INT_MAX) return PrintUsage();
        return CmdToonSkinCheck(argv[2], static_cast<int>(index));
    }

    return PrintUsage();
}
