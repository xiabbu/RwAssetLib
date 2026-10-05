/**
 * tools/RegenCheck.cpp - measures how DBO's derived chunks were generated.
 *
 *   rwasset regen-stats <dir>   histogram of BinMesh / Toon properties in a corpus
 *   rwasset regen-check <dir>   rebuild BinMesh / Toon from each geometry and
 *                             compare field-by-field with what the file holds
 *   rwasset compare-derived <original> <exported>  compare serialized chunks
 *   rwasset compare-bounds <original> <exported>   compare morph target bounds
 *
 * The exporter regenerates BinMesh and Toon from the mesh. These commands are
 * how the generators are proven to be the ones DBO used.
 */

#include "build/BinMeshBuilder.h"
#include "build/BoundsBuilder.h"
#include "build/CollisBuilder.h"
#include "build/ToonBuilder.h"
#include "build/SkinBuilder.h"
#include "dff/DffDocument.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace
{

std::string Hex(const void* p, size_t n)
{
    static const char* d = "0123456789abcdef";
    std::string s;
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) {
        s += d[b[i] >> 4];
        s += d[b[i] & 15];
    }
    return s;
}

std::string FixedName(const char* buf, size_t cap)
{
    size_t n = 0;
    while (n < cap && buf[n]) {
        ++n;
    }
    return std::string(buf, n);
}

void ForEachDff(const fs::path& root, const std::function<void(const fs::path&, DffDocument&)>& fn, size_t& files)
{
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".dff") {
            continue;
        }
        DffDocument doc;
        if (!doc.Load(it->path().wstring().c_str()) || !doc.GetClump()) {
            continue;
        }
        ++files;
        fn(it->path(), doc);
    }
}

void PrintHistogram(const char* title, const std::map<std::string, size_t>& h, size_t limit = 30)
{
    std::vector<std::pair<std::string, size_t>> v(h.begin(), h.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::cout << "-- " << title << " (" << v.size() << " distinct)\n";
    for (size_t i = 0; i < v.size() && i < limit; ++i) {
        std::cout << "   " << v[i].second << "  " << v[i].first << "\n";
    }
}

}  // namespace

int CmdRegenStats(const std::string& dir)
{
    std::map<std::string, size_t> binFlags, binMeshOrder, toonThick, toonCrease, toonInks, toonPaint,
        toonEdgeInk, toonTriSame, toonVertSame, toonFaceIdx, toonInkRaw, toonPerm;
    auto triSameEarly = [](const DffToonGeo& t, const DffGeometry& g) {
        if (t.triangles.size() != g.triangles.size()) return false;
        for (size_t i = 0; i < t.triangles.size(); ++i)
            for (int k = 0; k < 3; ++k)
                if (t.triangles[i].vertIndex[k] != g.triangles[i].vertIndex[k]) return false;
        return true;
    };
    size_t files = 0;

    ForEachDff(dir, [&](const fs::path&, DffDocument& doc) {
        for (const auto& gp : doc.GetClump()->geometries) {
            if (!gp) continue;
            const DffGeometry& g = *gp;

            if (g.binMesh) {
                binFlags[std::to_string(g.binMesh->flags)]++;
                bool ascending = true;
                for (size_t i = 1; i < g.binMesh->meshes.size(); ++i) {
                    if (g.binMesh->meshes[i].matIndex <= g.binMesh->meshes[i - 1].matIndex) ascending = false;
                }
                binMeshOrder[ascending ? "ascending" : "not-ascending"]++;
            }

            if (g.toon) {
                const DffToonGeo& t = *g.toon;
                bool allOne = std::all_of(t.vertexThicknesses.begin(), t.vertexThicknesses.end(),
                                          [](RwReal r) { return r == 1.0f; });
                toonThick[allOne ? "all 1.0" : "varies"]++;
                toonCrease[std::to_string(t.numCreaseEdges)]++;
                std::string inks = std::to_string(t.inkIDList.size()) + ":";
                for (const auto& ink : t.inkIDList) inks += " [" + FixedName(ink.name, sizeof(ink.name)) + "]";
                toonInks[inks]++;
                toonPaint[Hex(t.defaultPaintID.name, 32)]++;
                for (const auto& ink : t.inkIDList) toonInkRaw[Hex(ink.name, 32)]++;

                // Is the toon triangle list a permutation of the geometry's, and how?
                if (!triSameEarly(t, g)) {
                    std::multimap<std::string, size_t> geomTris;
                    for (size_t i = 0; i < g.triangles.size(); ++i)
                        geomTris.emplace(Hex(g.triangles[i].vertIndex, 6), i);
                    bool perm = t.triangles.size() == g.triangles.size();
                    bool matSorted = true;
                    int prevMat = -1;
                    for (size_t i = 0; perm && i < t.triangles.size(); ++i) {
                        auto it = geomTris.find(Hex(t.triangles[i].vertIndex, 6));
                        if (it == geomTris.end()) { perm = false; break; }
                        const int m = g.triangles[it->second].matIndex;
                        if (m < prevMat) matSorted = false;
                        prevMat = m;
                        geomTris.erase(it);
                    }
                    toonPerm[perm ? (matSorted ? "permutation, grouped by ascending matIndex" : "permutation, other order") : "not a permutation"]++;
                }
                std::map<std::string, size_t> pat;
                for (const auto& e : t.edgeInkIDs) pat[std::to_string(e.inkId[0]) + "/" + std::to_string(e.inkId[1])]++;
                std::string ps;
                for (const auto& kv : pat) ps += kv.first + " ";
                toonEdgeInk[ps]++;

                bool triSame = t.triangles.size() == g.triangles.size();
                for (size_t i = 0; triSame && i < t.triangles.size(); ++i)
                    for (int k = 0; k < 3; ++k)
                        if (t.triangles[i].vertIndex[k] != g.triangles[i].vertIndex[k]) triSame = false;
                toonTriSame[triSame ? "toon tris == geometry tris" : "differ"]++;
                toonVertSame[t.NumVertices() == g.NumVertices() ? "numVerts equal" : "numVerts differ"]++;
                toonFaceIdx[g.HasNormals() ? "geometry has normals" : "no normals"]++;
            }
        }
    }, files);

    std::cout << "files: " << files << "\n";
    PrintHistogram("BinMesh flags", binFlags);
    PrintHistogram("BinMesh mesh order", binMeshOrder);
    PrintHistogram("Toon thickness", toonThick);
    PrintHistogram("Toon numCreaseEdges", toonCrease, 10);
    PrintHistogram("Toon inks", toonInks);
    PrintHistogram("Toon default paint", toonPaint);
    PrintHistogram("Toon edge ink patterns", toonEdgeInk);
    PrintHistogram("Toon ink raw", toonInkRaw);
    PrintHistogram("Toon triangles", toonTriSame);
    PrintHistogram("Toon triangle permutation", toonPerm);
    PrintHistogram("Toon vertices", toonVertSame);
    PrintHistogram("Toon geometry normals", toonFaceIdx);
    return 0;
}

int CmdRegenCheck(int argc, char** argv)
{
    const fs::path root(argv[2]);
    std::vector<fs::path> paths;
    std::error_code ec;
    if (fs::is_regular_file(root, ec)) {
        paths.push_back(root);
    } else {
        for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".dff") {
                paths.push_back(it->path());
            }
        }
    }
    if (ec || paths.empty()) {
        std::cerr << "cannot enumerate DFF input: " << root.string() << " " << ec.message() << "\n";
        return 2;
    }
    std::sort(paths.begin(), paths.end());
    bool details = false;
    bool rawFaceNormals = false;
    bool vertexExtrusionNormals = false;
    bool sceneDerived = false;
    for (int i = 3; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--details") {
            details = true;
        } else if (option == "--raw-face-normals") {
            rawFaceNormals = true;
        } else if (option == "--vertex-extrusion-normals") {
            vertexExtrusionNormals = true;
        } else if (option == "--scene-derived") {
            sceneDerived = true;
        } else {
            std::cerr << "unknown regen-check option: " << option << "\n";
            return 2;
        }
    }
    size_t errors = 0, binTotal = 0, binEqual = 0, toonTotal = 0, toonEqual = 0, collTotal = 0, collEqual = 0, boundsTotal = 0, boundsEqual = 0;
    std::map<std::string, size_t> differences;
    for (const auto& path : paths) {
        DffDocument doc;
        if (!doc.Load(path.wstring().c_str())) {
            std::cerr << path.string() << ": " << doc.GetLastError() << "\n";
            ++errors;
            continue;
        }
        size_t geometryIndex = 0;
        for (const auto& gp : doc.GetClump()->geometries) {
            const auto& g = *gp;
            std::vector<RwInt32> materialOrder;
            if (g.binMesh) {
                for (const auto& mesh : g.binMesh->meshes) materialOrder.push_back(mesh.matIndex);
            }
            auto record = [&](const std::string& field, bool equal) {
                if (!equal) {
                    ++differences[field];
                    if (details) std::cout << path.string() << " geometry=" << geometryIndex << " " << field << "\n";
                }
                return equal;
            };
            auto arrayEqual = [](const auto& a, const auto& b) {
                return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0);
            };
            if (g.binMesh) {
                ++binTotal;
                DffBinMesh rebuilt;
                if (!BuildBinMesh(g, rebuilt, materialOrder)) {
                    std::cerr << path.string() << " geometry=" << geometryIndex << " BinMesh build failed\n";
                    ++errors;
                } else {
                    bool equal = record("BinMesh.flags", rebuilt.flags == g.binMesh->flags);
                    equal &= record("BinMesh.meshCount", rebuilt.meshes.size() == g.binMesh->meshes.size());
                    bool order = rebuilt.meshes.size() == g.binMesh->meshes.size();
                    bool indices = order;
                    for (size_t i = 0; i < std::min(rebuilt.meshes.size(), g.binMesh->meshes.size()); ++i) {
                        order &= rebuilt.meshes[i].matIndex == g.binMesh->meshes[i].matIndex;
                        indices &= rebuilt.meshes[i].indices == g.binMesh->meshes[i].indices;
                    }
                    equal &= record("BinMesh.materialOrder", order);
                    equal &= record("BinMesh.indices", indices);
                    if (details && !indices) {
                        for (size_t meshIndex = 0; meshIndex < std::min(rebuilt.meshes.size(), g.binMesh->meshes.size()); ++meshIndex) {
                            const auto& generatedIndices = rebuilt.meshes[meshIndex].indices;
                            const auto& sourceIndices = g.binMesh->meshes[meshIndex].indices;
                            size_t first = 0;
                            while (first < generatedIndices.size() && first < sourceIndices.size()
                                   && generatedIndices[first] == sourceIndices[first]) {
                                ++first;
                            }
                            if (first == generatedIndices.size() && first == sourceIndices.size()) continue;
                            std::cout << "  BinMesh.mesh[" << meshIndex << "] material="
                                      << g.binMesh->meshes[meshIndex].matIndex
                                      << " sizes=" << generatedIndices.size() << "/" << sourceIndices.size()
                                      << " firstDiff=" << first << "\n";
                            const size_t begin = first > 6 ? first - 6 : 0;
                            const size_t end = std::min(std::max(generatedIndices.size(), sourceIndices.size()), first + 12);
                            std::cout << "    rebuilt:";
                            for (size_t i = begin; i < std::min(end, generatedIndices.size()); ++i) {
                                std::cout << " " << generatedIndices[i];
                            }
                            std::cout << "\n    original:";
                            for (size_t i = begin; i < std::min(end, sourceIndices.size()); ++i) {
                                std::cout << " " << sourceIndices[i];
                            }
                            std::cout << "\n";
                            break;
                        }
                    }
                    binEqual += equal;
                }
            }
            if (g.toon) {
                ++toonTotal;
                const auto& original = *g.toon;
                ToonBuilder builder;
                std::vector<RwInt32> toonMaterialOrder;
                if (sceneDerived) {
                    std::unordered_map<std::uint64_t, RwInt32> triangleMaterials;
                    for (const auto& triangle : g.triangles) {
                        const auto* v = triangle.vertIndex;
                        const auto key = static_cast<std::uint64_t>(v[0]) |
                                         (static_cast<std::uint64_t>(v[1]) << 16) |
                                         (static_cast<std::uint64_t>(v[2]) << 32);
                        triangleMaterials.emplace(key, triangle.matIndex);
                    }
                    for (const auto& triangle : original.triangles) {
                        const auto* v = triangle.vertIndex;
                        const auto key = static_cast<std::uint64_t>(v[0]) |
                                         (static_cast<std::uint64_t>(v[1]) << 16) |
                                         (static_cast<std::uint64_t>(v[2]) << 32);
                        const auto it = triangleMaterials.find(key);
                        if (it != triangleMaterials.end() &&
                            std::find(toonMaterialOrder.begin(), toonMaterialOrder.end(), it->second) == toonMaterialOrder.end()) {
                            toonMaterialOrder.push_back(it->second);
                        }
                    }
                    if (std::any_of(original.vertexThicknesses.begin(), original.vertexThicknesses.end(),
                                    [](RwReal thickness) { return thickness != 1.0f; })) {
                        builder.SetVertexThicknesses(original.vertexThicknesses);
                    }
                    const auto& vertexNormals = g.morphTargets[0].normals;
                    builder.SetVertexExtrusionNormals(!vertexNormals.empty() &&
                        arrayEqual(vertexNormals, original.extrusionNormals));
                } else {
                    builder.SetVertexExtrusionNormals(vertexExtrusionNormals);
                }
                builder.SetMaterialOrder(toonMaterialOrder.empty() ? materialOrder : toonMaterialOrder);
                builder.SetRawFaceNormals(sceneDerived || rawFaceNormals);
                const auto paintName = original.DefaultPaintName();
                builder.SetDefaultPaintName(paintName);
                const auto* paint = reinterpret_cast<const RwUInt8*>(original.defaultPaintID.name);
                builder.SetDefaultPaintPadding(std::vector<RwUInt8>(paint + paintName.size() + 1, paint + 32));
                if (!original.inkIDList.empty()) builder.SetDefaultInkName(FixedName(original.inkIDList[0].name, 32));
                DffToonGeo rebuilt;
                bool built = builder.Build(g, rebuilt);
                if (built && sceneDerived && (rebuilt.faceNormals.empty() ||
                    !arrayEqual(rebuilt.faceNormals, original.faceNormals))) {
                    builder.SetRawFaceNormals(false);
                    built = builder.Build(g, rebuilt);
                }
                if (built && sceneDerived && g.skin && g.morphTargets.size() == 1 && g.HasNormals() &&
                    (!arrayEqual(rebuilt.faceNormals, original.faceNormals) ||
                     !arrayEqual(rebuilt.extrusionNormals, original.extrusionNormals))) {
                    const auto& clump = *doc.GetClump();
                    const auto atomic = std::find_if(clump.atomics.begin(), clump.atomics.end(),
                        [&](const DffAtomic& a) { return a.geomIndex == geometryIndex; });
                    if (atomic == clump.atomics.end()) {
                        std::cerr << path.string() << " geometry=" << geometryIndex << " Skin atomic is missing\n";
                        built = false;
                    } else {
                        // Classify the recorded render mode by both complete arrays.
                        for (const auto mode : {DffSkinInverseMode::Generic, DffSkinInverseMode::Orthonormal}) {
                            DffSkinVertexBuffer buffer;
                            std::string error;
                            if (!BuildSkinVertexBuffer(clump, g, atomic->frameIndex, mode, buffer, error)) {
                                std::cerr << path.string() << " geometry=" << geometryIndex << " " << error << "\n";
                                built = false;
                                break;
                            }
                            ToonBuilder skinBuilder = builder;
                            skinBuilder.SetRawFaceNormals(true);
                            DffToonGeo candidate;
                            if (!skinBuilder.Build(g, candidate, &buffer)) {
                                built = false;
                                break;
                            }
                            if (arrayEqual(candidate.faceNormals, original.faceNormals) &&
                                arrayEqual(candidate.extrusionNormals, original.extrusionNormals)) {
                                rebuilt = std::move(candidate);
                                break;
                            }
                        }
                    }
                }
                if (!built) {
                    std::cerr << path.string() << " geometry=" << geometryIndex << " Toon build failed\n";
                    ++errors;
                } else {
                    bool equal = record("Toon.version", rebuilt.versionStamp == original.versionStamp);
                    equal &= record("Toon.triangles", arrayEqual(rebuilt.triangles, original.triangles));
                    equal &= record("Toon.thickness", arrayEqual(rebuilt.vertexThicknesses, original.vertexThicknesses));
                    equal &= record("Toon.creaseCount", rebuilt.numCreaseEdges == original.numCreaseEdges);
                    equal &= record("Toon.edges", arrayEqual(rebuilt.edges, original.edges));
                    equal &= record("Toon.faceNormals", arrayEqual(rebuilt.faceNormals, original.faceNormals));
                    equal &= record("Toon.extrusionNormals", arrayEqual(rebuilt.extrusionNormals, original.extrusionNormals));
                    if (details) {
                        if (!arrayEqual(rebuilt.triangles, original.triangles)) {
                            std::cout << "  Toon.triangles sizes=" << rebuilt.triangles.size()
                                      << "/" << original.triangles.size() << "\n";
                            for (size_t i = 0; i < std::min(rebuilt.triangles.size(), original.triangles.size()); ++i) {
                                const auto* generated = rebuilt.triangles[i].vertIndex;
                                const auto* source = original.triangles[i].vertIndex;
                                if (std::memcmp(generated, source, sizeof(RpToonTriangle)) == 0) continue;
                                std::cout << "  Toon.triangles[" << i << "] rebuilt=("
                                          << generated[0] << "," << generated[1] << "," << generated[2]
                                          << ") original=(" << source[0] << "," << source[1] << ","
                                          << source[2] << ")\n";
                                break;
                            }
                        }
                        auto firstNormalDifference = [&](const char* field, const auto& generated, const auto& source) {
                            for (size_t i = 0; i < std::min(generated.size(), source.size()); ++i) {
                                if (std::memcmp(&generated[i], &source[i], sizeof(RwV3d)) == 0) continue;
                                std::cout << "  " << field << "[" << i << "] rebuilt=("
                                          << std::setprecision(9) << generated[i].x << ","
                                          << generated[i].y << "," << generated[i].z << ") original=("
                                          << source[i].x << "," << source[i].y << "," << source[i].z
                                          << ") bytes=" << Hex(&generated[i], sizeof(RwV3d)) << "/"
                                          << Hex(&source[i], sizeof(RwV3d)) << "\n";
                                break;
                            }
                        };
                        firstNormalDifference("Toon.faceNormals", rebuilt.faceNormals, original.faceNormals);
                        firstNormalDifference("Toon.extrusionNormals", rebuilt.extrusionNormals, original.extrusionNormals);
                        if (!arrayEqual(rebuilt.extrusionNormals, original.extrusionNormals)) {
                            const auto& normals = g.morphTargets[0].normals;
                            size_t matchingNormals = 0;
                            for (size_t i = 0; i < std::min(normals.size(), original.extrusionNormals.size()); ++i) {
                                matchingNormals += std::memcmp(&normals[i], &original.extrusionNormals[i], sizeof(RwV3d)) == 0;
                            }
                            std::cout << "  Toon.extrusionNormals matches Geometry normals="
                                      << matchingNormals << "/" << original.extrusionNormals.size() << "\n";
                        }
                    }
                    equal &= record("Toon.inks", arrayEqual(rebuilt.inkIDList, original.inkIDList));
                    equal &= record("Toon.edgeInks", arrayEqual(rebuilt.edgeInkIDs, original.edgeInkIDs));
                    equal &= record("Toon.paint", std::memcmp(rebuilt.defaultPaintID.name, original.defaultPaintID.name, 32) == 0);
                    toonEqual += equal;
                }
            }
            for (size_t target = 0; target < g.morphTargets.size(); ++target) {
                ++boundsTotal;
                RwSphere rebuilt;
                BuildBoundingSphere(g.morphTargets[target].vertices, rebuilt);
                const bool equal = std::memcmp(&rebuilt, &g.morphTargets[target].boundingSphere, sizeof(RwSphere)) == 0;
                boundsEqual += record("MorphTarget.boundingSphere", equal);
            }
            if (g.collis) {
                ++collTotal;
                DffCollTree rebuilt;
                std::string error;
                if (!BuildCollisTree(g, rebuilt, error)) {
                    std::cerr << path.string() << " geometry=" << geometryIndex << ": " << error << "\n";
                    ++errors;
                } else {
                    const auto& original = g.collis->tree;
                    bool equal = record("Collis.header", rebuilt.flags == original.flags &&
                        std::memcmp(&rebuilt.inf, &original.inf, sizeof(RwV3d)) == 0 &&
                        std::memcmp(&rebuilt.sup, &original.sup, sizeof(RwV3d)) == 0 &&
                        rebuilt.numEntries == original.numEntries && rebuilt.numSplits == original.numSplits);
                    auto sameSector = [](const DffCollSector& a, const DffCollSector& b) {
                        return a.type == b.type && a.contents == b.contents && a.index == b.index &&
                               std::memcmp(&a.value, &b.value, sizeof(RwReal)) == 0;
                    };
                    bool splits = rebuilt.splits.size() == original.splits.size();
                    for (size_t i = 0; splits && i < rebuilt.splits.size(); ++i) {
                        splits = sameSector(rebuilt.splits[i].left, original.splits[i].left) &&
                                 sameSector(rebuilt.splits[i].right, original.splits[i].right);
                    }
                    equal &= record("Collis.splits", splits);
                    equal &= record("Collis.map", rebuilt.map == original.map);
                    collEqual += equal;
                }
            }
            ++geometryIndex;
        }
    }
    std::cout << "files=" << paths.size() << " errors=" << errors
              << " BinMesh=" << binEqual << "/" << binTotal << " Toon=" << toonEqual << "/" << toonTotal
              << " Collis=" << collEqual << "/" << collTotal << " Bounds=" << boundsEqual << "/" << boundsTotal << "\n";
    for (const auto& entry : differences) std::cout << entry.first << " differences=" << entry.second << "\n";
    return errors ? 2 : (binEqual == binTotal && toonEqual == toonTotal && collEqual == collTotal && boundsEqual == boundsTotal ? 0 : 1);
}

int CmdCompareDerived(const std::string& originalPath, const std::string& exportedPath)
{
    DffDocument original;
    DffDocument exported;
    if (!original.Load(fs::path(originalPath).wstring().c_str())) {
        std::cerr << originalPath << ": " << original.GetLastError() << "\n";
        return 2;
    }
    if (!exported.Load(fs::path(exportedPath).wstring().c_str())) {
        std::cerr << exportedPath << ": " << exported.GetLastError() << "\n";
        return 2;
    }

    const auto& sourceGeometries = original.GetClump()->geometries;
    const auto& outputGeometries = exported.GetClump()->geometries;
    if (sourceGeometries.size() != outputGeometries.size()) {
        std::cerr << "geometry count differs: " << sourceGeometries.size()
                  << " vs " << outputGeometries.size() << "\n";
        return 1;
    }

    size_t binTotal = 0, binEqual = 0, toonTotal = 0, toonEqual = 0;
    for (size_t i = 0; i < sourceGeometries.size(); ++i) {
        const auto& a = *sourceGeometries[i];
        const auto& b = *outputGeometries[i];
        auto compare = [&](const char* name, const auto& source, const auto& output, auto write) {
            RwStream sourceStream, outputStream;
            DffDiagnostics sourceDiag, outputDiag;
            if (!sourceStream.OpenWriteMemory() || !outputStream.OpenWriteMemory() ||
                !write(sourceStream, sourceDiag, source, original.GetVersion()) ||
                !write(outputStream, outputDiag, output, exported.GetVersion())) {
                std::cerr << "geometry=" << i << " " << name << " serialization failed: "
                          << sourceDiag.Summary() << " " << outputDiag.Summary() << "\n";
                return false;
            }
            const auto& sourceBytes = sourceStream.GetMemoryBuffer();
            const auto& outputBytes = outputStream.GetMemoryBuffer();
            const auto mismatch = std::mismatch(sourceBytes.begin(), sourceBytes.end(),
                                                outputBytes.begin(), outputBytes.end());
            if (mismatch.first == sourceBytes.end() && mismatch.second == outputBytes.end()) return true;
            std::cout << "geometry=" << i << " " << name << " firstDiff="
                      << std::distance(sourceBytes.begin(), mismatch.first)
                      << " sizes=" << sourceBytes.size() << "/" << outputBytes.size() << "\n";
            return false;
        };

        if (a.binMesh || b.binMesh) {
            ++binTotal;
            if (!a.binMesh || !b.binMesh) {
                std::cout << "geometry=" << i << " BinMesh presence differs\n";
            } else if (compare("BinMesh", *a.binMesh, *b.binMesh, WriteBinMesh)) {
                ++binEqual;
            }
        }
        if (a.toon || b.toon) {
            ++toonTotal;
            if (!a.toon || !b.toon) {
                std::cout << "geometry=" << i << " Toon presence differs\n";
            } else if (compare("Toon", *a.toon, *b.toon, WriteToonGeo)) {
                ++toonEqual;
            }
        }
    }
    std::cout << "BinMesh=" << binEqual << "/" << binTotal
              << " Toon=" << toonEqual << "/" << toonTotal << "\n";
    return binEqual == binTotal && toonEqual == toonTotal ? 0 : 1;
}

int CmdCompareBounds(const std::string& originalPath, const std::string& exportedPath)
{
    DffDocument original;
    DffDocument exported;
    if (!original.Load(fs::path(originalPath).wstring().c_str())) {
        std::cerr << originalPath << ": " << original.GetLastError() << "\n";
        return 2;
    }
    if (!exported.Load(fs::path(exportedPath).wstring().c_str())) {
        std::cerr << exportedPath << ": " << exported.GetLastError() << "\n";
        return 2;
    }

    const auto& sourceGeometries = original.GetClump()->geometries;
    const auto& outputGeometries = exported.GetClump()->geometries;
    if (sourceGeometries.size() != outputGeometries.size()) {
        std::cerr << "geometry count differs: " << sourceGeometries.size()
                  << " vs " << outputGeometries.size() << "\n";
        return 1;
    }

    size_t total = 0, equal = 0;
    for (size_t geometryIndex = 0; geometryIndex < sourceGeometries.size(); ++geometryIndex) {
        const auto& sourceTargets = sourceGeometries[geometryIndex]->morphTargets;
        const auto& outputTargets = outputGeometries[geometryIndex]->morphTargets;
        if (sourceTargets.size() != outputTargets.size()) {
            std::cout << "geometry=" << geometryIndex << " morph target count differs: "
                      << sourceTargets.size() << " vs " << outputTargets.size() << "\n";
        }
        total += std::max(sourceTargets.size(), outputTargets.size());
        for (size_t targetIndex = 0; targetIndex < std::min(sourceTargets.size(), outputTargets.size()); ++targetIndex) {
            const auto& a = sourceTargets[targetIndex].boundingSphere;
            const auto& b = outputTargets[targetIndex].boundingSphere;
            const RwReal sourceValues[] = {a.center.x, a.center.y, a.center.z, a.radius};
            const RwReal outputValues[] = {b.center.x, b.center.y, b.center.z, b.radius};
            if (std::memcmp(sourceValues, outputValues, sizeof(sourceValues)) == 0) {
                ++equal;
                continue;
            }
            const char* fields[] = {"center.x", "center.y", "center.z", "radius"};
            for (size_t field = 0; field < 4; ++field) {
                if (std::memcmp(&sourceValues[field], &outputValues[field], sizeof(RwReal)) != 0) {
                    std::cout << "geometry=" << geometryIndex << " morph=" << targetIndex
                              << " " << fields[field] << " original=" << sourceValues[field]
                              << " (" << Hex(&sourceValues[field], sizeof(RwReal)) << ") exported="
                              << outputValues[field] << " (" << Hex(&outputValues[field], sizeof(RwReal)) << ")\n";
                }
            }
        }
    }
    std::cout << "bounds=" << equal << "/" << total << "\n";
    return equal == total ? 0 : 1;
}
