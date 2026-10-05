#include "dff/DffDocument.h"

#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <type_traits>

namespace fs = std::filesystem;

namespace
{

class DffComparer
{
public:
    DffComparer(RwUInt32 sourceVersion, RwUInt32 outputVersion)
        : m_sourceVersion(sourceVersion), m_outputVersion(outputVersion)
    {
    }

    size_t Differences() const { return m_differences; }
    bool Failed() const { return m_failed; }

    void Value(const std::string& path, long long a, long long b)
    {
        if (a != b) {
            Report(path, std::to_string(a) + " / " + std::to_string(b));
        }
    }

    template <typename T>
    void Array(const std::string& path, const std::vector<T>& a, const std::vector<T>& b)
    {
        if (a.size() != b.size()) {
            Report(path, "count " + std::to_string(a.size()) + " / " + std::to_string(b.size()));
            return;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::memcmp(&a[i], &b[i], sizeof(T)) != 0) {
                std::ostringstream detail;
                detail << "first element " << i;
                if constexpr (std::is_same_v<T, RwTexCoords>) {
                    detail << " (u " << std::setprecision(9) << a[i].u << " / " << b[i].u
                           << ", v " << a[i].v << " / " << b[i].v << ")";
                } else if constexpr (std::is_same_v<T, RwV3d>) {
                    detail << " (" << std::setprecision(9)
                           << a[i].x << "," << a[i].y << "," << a[i].z << " / "
                           << b[i].x << "," << b[i].y << "," << b[i].z << ")";
                } else if constexpr (std::is_same_v<T, RwReal>) {
                    detail << " (" << std::setprecision(9) << a[i] << " / " << b[i] << ")";
                } else if constexpr (std::is_same_v<T, RpToonEdge>) {
                    detail << " (v " << a[i].v[0] << "," << a[i].v[1] << " / "
                           << b[i].v[0] << "," << b[i].v[1] << ", face "
                           << a[i].face[0] << "," << a[i].face[1] << " / "
                           << b[i].face[0] << "," << b[i].face[1] << ")";
                }
                Report(path, detail.str());
                return;
            }
        }
    }

    // Serializes both sides with the codec's own writer and compares the bytes.
    template <typename T, typename WriteFn>
    bool Chunk(const std::string& path, const T& a, const T& b, WriteFn write)
    {
        RwStream sourceStream, outputStream;
        DffDiagnostics sourceDiag, outputDiag;
        if (!sourceStream.OpenWriteMemory() || !outputStream.OpenWriteMemory() ||
            !write(sourceStream, sourceDiag, a, m_sourceVersion) ||
            !write(outputStream, outputDiag, b, m_outputVersion)) {
            std::cerr << path << ": serialization failed: " << sourceDiag.Summary()
                      << " " << outputDiag.Summary() << "\n";
            m_failed = true;
            return false;
        }
        const auto& sourceBytes = sourceStream.GetMemoryBuffer();
        const auto& outputBytes = outputStream.GetMemoryBuffer();
        if (sourceBytes == outputBytes) {
            return true;
        }
        const auto mismatch = std::mismatch(sourceBytes.begin(), sourceBytes.end(),
                                            outputBytes.begin(), outputBytes.end());
        Report(path, "bytes " + std::to_string(sourceBytes.size()) + " / " + std::to_string(outputBytes.size())
               + " firstDiff=" + std::to_string(std::distance(sourceBytes.begin(), mismatch.first)));
        return false;
    }

    template <typename T, typename WriteFn>
    void Optional(const std::string& path, const std::shared_ptr<T>& a, const std::shared_ptr<T>& b, WriteFn write)
    {
        if (!a && !b) {
            return;
        }
        if (!a || !b) {
            Report(path, std::string("presence ") + (a ? "yes" : "no") + " / " + (b ? "yes" : "no"));
            return;
        }
        Chunk(path, *a, *b, write);
    }

    void Extensions(const std::string& path, const DffExtensionList& a, const DffExtensionList& b)
    {
        Array(path + ".order", a.order, b.order);
        if (a.raws.size() != b.raws.size()) {
            Report(path + ".raws", "count " + std::to_string(a.raws.size()) + " / " + std::to_string(b.raws.size()));
            return;
        }
        for (size_t i = 0; i < a.raws.size(); ++i) {
            if (a.raws[i].type != b.raws[i].type || a.raws[i].data != b.raws[i].data) {
                Report(path + ".raws[" + std::to_string(i) + "]", "type or payload");
            }
        }
    }

    void UserData(const std::string& path, const std::vector<DffUserDataList>& a, const std::vector<DffUserDataList>& b)
    {
        Value(path + ".count", static_cast<long long>(a.size()), static_cast<long long>(b.size()));
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            Chunk(path + "[" + std::to_string(i) + "]", a[i], b[i], WriteUserDataList);
        }
    }

private:
    void Report(const std::string& path, const std::string& detail)
    {
        std::cout << path << ": " << detail << "\n";
        ++m_differences;
    }

    RwUInt32 m_sourceVersion;
    RwUInt32 m_outputVersion;
    size_t   m_differences = 0;
    bool     m_failed = false;
};

void CompareMaterial(DffComparer& cmp, const std::string& path, const DffMaterial& a, const DffMaterial& b)
{
    if (cmp.Chunk(path, a, b, WriteMaterial)) {
        return;
    }
    cmp.Value(path + ".flags", a.flags, b.flags);
    cmp.Array(path + ".color", std::vector<RwRGBA>{a.color}, std::vector<RwRGBA>{b.color});
    cmp.Value(path + ".unused", a.unused, b.unused);
    cmp.Array(path + ".surfaceProps", std::vector<RwSurfaceProperties>{a.surfaceProps},
              std::vector<RwSurfaceProperties>{b.surfaceProps});
    if (a.texture && b.texture) {
        cmp.Array(path + ".texture.name", a.texture->rawNameBytes, b.texture->rawNameBytes);
        cmp.Array(path + ".texture.mask", a.texture->rawMaskBytes, b.texture->rawMaskBytes);
        cmp.Value(path + ".texture.filterAndAddress", a.texture->filterAndAddress, b.texture->filterAndAddress);
        cmp.Extensions(path + ".texture.extensions", a.texture->extensions, b.texture->extensions);
    }
    cmp.Optional(path + ".texture", a.texture, b.texture, WriteTexture);
    cmp.Optional(path + ".matfx", a.matfx, b.matfx, WriteMatFXMaterial);
    cmp.Optional(path + ".toon", a.toon, b.toon, WriteToonMaterial);
    cmp.Optional(path + ".ltmap", a.ltmap, b.ltmap, WriteLtMapMaterial);
    cmp.Optional(path + ".uvanim", a.uvanim, b.uvanim, WriteUVAnimMaterial);
    cmp.Optional(path + ".ntlMaterialExt", a.ntlMaterialExt, b.ntlMaterialExt, WriteNtlMaterialExt);
    cmp.Value(path + ".ntlWorldMaterials", static_cast<long long>(a.ntlWorldMaterials.size()),
              static_cast<long long>(b.ntlWorldMaterials.size()));
    cmp.UserData(path + ".userData", a.userDataLists, b.userDataLists);
    cmp.Extensions(path + ".extensions", a.extensions, b.extensions);
}

void CompareGeometry(DffComparer& cmp, const std::string& path, const DffGeometry& a, const DffGeometry& b)
{
    if (cmp.Chunk(path, a, b, WriteGeometry)) {
        return;
    }
    cmp.Value(path + ".format", a.format, b.format);
    cmp.Array(path + ".triangles", a.triangles, b.triangles);
    cmp.Array(path + ".preLitColors", a.preLitColors, b.preLitColors);
    cmp.Value(path + ".texCoordSets", a.NumTexCoordSets(), b.NumTexCoordSets());
    for (int i = 0; i < a.NumTexCoordSets() && i < b.NumTexCoordSets(); ++i) {
        cmp.Array(path + ".texCoords[" + std::to_string(i) + "]", a.texCoords[i], b.texCoords[i]);
    }
    cmp.Value(path + ".morphTargets", static_cast<long long>(a.morphTargets.size()),
              static_cast<long long>(b.morphTargets.size()));
    for (size_t i = 0; i < a.morphTargets.size() && i < b.morphTargets.size(); ++i) {
        const std::string target = path + ".morphTargets[" + std::to_string(i) + "]";
        cmp.Array(target + ".boundingSphere", std::vector<RwSphere>{a.morphTargets[i].boundingSphere},
                  std::vector<RwSphere>{b.morphTargets[i].boundingSphere});
        cmp.Array(target + ".vertices", a.morphTargets[i].vertices, b.morphTargets[i].vertices);
        cmp.Array(target + ".normals", a.morphTargets[i].normals, b.morphTargets[i].normals);
    }
    cmp.Array(path + ".materialIndices", a.materialIndices, b.materialIndices);
    cmp.Value(path + ".materials", static_cast<long long>(a.materials.size()),
              static_cast<long long>(b.materials.size()));
    for (size_t i = 0; i < a.materials.size() && i < b.materials.size(); ++i) {
        CompareMaterial(cmp, path + ".materials[" + std::to_string(i) + "]", *a.materials[i], *b.materials[i]);
    }
    cmp.Optional(path + ".binMesh", a.binMesh, b.binMesh, WriteBinMesh);
    if (a.skin && b.skin) {
        const RwInt32 numVertices = a.NumVertices();
        const bool equal = cmp.Chunk(path + ".skin", *a.skin, *b.skin,
            [numVertices](RwStream& s, DffDiagnostics& d, const DffSkin& skin, RwUInt32 v) {
                return WriteSkin(s, d, skin, numVertices, v);
            });
        if (!equal) {
            const auto& sa = *a.skin;
            const auto& sb = *b.skin;
            cmp.Value(path + ".skin.numBones", sa.numBones, sb.numBones);
            cmp.Value(path + ".skin.numUsedBones", sa.numUsedBones, sb.numUsedBones);
            cmp.Value(path + ".skin.maxWeights", sa.maxWeights, sb.maxWeights);
            cmp.Array(path + ".skin.usedBoneList", sa.usedBoneList, sb.usedBoneList);
            cmp.Array(path + ".skin.vertexBoneIndices", sa.vertexBoneIndices, sb.vertexBoneIndices);
            cmp.Array(path + ".skin.vertexBoneWeights", sa.vertexBoneWeights, sb.vertexBoneWeights);
            cmp.Array(path + ".skin.invBoneMatrices", sa.invBoneMatrices, sb.invBoneMatrices);
            cmp.Value(path + ".skin.splitBoneLimit", sa.splitBoneLimit, sb.splitBoneLimit);
            cmp.Value(path + ".skin.splitNumMeshes", sa.splitNumMeshes, sb.splitNumMeshes);
            cmp.Value(path + ".skin.splitNumRLE", sa.splitNumRLE, sb.splitNumRLE);
            cmp.Array(path + ".skin.splitMatrixRemapIndices", sa.splitMatrixRemapIndices, sb.splitMatrixRemapIndices);
            cmp.Array(path + ".skin.splitMeshRLECount", sa.splitMeshRLECount, sb.splitMeshRLECount);
            cmp.Array(path + ".skin.splitMeshRLE", sa.splitMeshRLE, sb.splitMeshRLE);
        }
    } else if (a.skin || b.skin) {
        cmp.Value(path + ".skin presence", a.skin != nullptr, b.skin != nullptr);
    }
    if (a.toon && b.toon) {
        if (!cmp.Chunk(path + ".toon", *a.toon, *b.toon, WriteToonGeo)) {
            cmp.Value(path + ".toon.versionStamp", a.toon->versionStamp, b.toon->versionStamp);
            cmp.Array(path + ".toon.triangles", a.toon->triangles, b.toon->triangles);
            cmp.Array(path + ".toon.vertexThicknesses", a.toon->vertexThicknesses, b.toon->vertexThicknesses);
            cmp.Array(path + ".toon.edges", a.toon->edges, b.toon->edges);
            cmp.Value(path + ".toon.numCreaseEdges", a.toon->numCreaseEdges, b.toon->numCreaseEdges);
            cmp.Array(path + ".toon.faceNormals", a.toon->faceNormals, b.toon->faceNormals);
            cmp.Array(path + ".toon.extrusionNormals", a.toon->extrusionNormals, b.toon->extrusionNormals);
            cmp.Array(path + ".toon.inkIDList", a.toon->inkIDList, b.toon->inkIDList);
            cmp.Array(path + ".toon.edgeInkIDs", a.toon->edgeInkIDs, b.toon->edgeInkIDs);
            cmp.Array(path + ".toon.defaultPaintID", std::vector<RpToonPaintID>{a.toon->defaultPaintID},
                      std::vector<RpToonPaintID>{b.toon->defaultPaintID});
        }
    } else {
        cmp.Optional(path + ".toon", a.toon, b.toon, WriteToonGeo);
    }
    cmp.Optional(path + ".morph", a.morph, b.morph, WriteMorphAnim);
    cmp.Optional(path + ".collis", a.collis, b.collis, WriteCollis);
    cmp.UserData(path + ".userData", a.userDataLists, b.userDataLists);
    cmp.Extensions(path + ".extensions", a.extensions, b.extensions);
}

}  // namespace

int CmdCompareDff(const std::string& originalPath, const std::string& exportedPath)
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
    const auto& a = *original.GetClump();
    const auto& b = *exported.GetClump();
    DffComparer cmp(original.GetVersion(), exported.GetVersion());

    cmp.Value("version", original.GetVersion(), exported.GetVersion());
    cmp.Value("clump.lights", a.lights.size(), b.lights.size());
    cmp.Value("clump.cameras", a.cameras.size(), b.cameras.size());
    cmp.Value("clump.structHasLightCameraCounts", a.structHasLightCameraCounts, b.structHasLightCameraCounts);
    for (size_t i = 0; i < a.lights.size() && i < b.lights.size(); ++i) {
        const std::string path = "clump.lights[" + std::to_string(i) + "]";
        cmp.Value(path + ".frameIndex", a.lights[i].frameIndex, b.lights[i].frameIndex);
        cmp.Chunk(path, a.lights[i], b.lights[i], WriteLight);
    }
    for (size_t i = 0; i < a.cameras.size() && i < b.cameras.size(); ++i) {
        const std::string path = "clump.cameras[" + std::to_string(i) + "]";
        cmp.Value(path + ".frameIndex", a.cameras[i].frameIndex, b.cameras[i].frameIndex);
        cmp.Chunk(path, a.cameras[i], b.cameras[i], WriteCamera);
    }
    cmp.Extensions("clump.extensions", a.extensions, b.extensions);

    cmp.Value("frames", static_cast<long long>(a.frames.size()), static_cast<long long>(b.frames.size()));
    for (size_t i = 0; i < a.frames.size() && i < b.frames.size(); ++i) {
        const auto& fa = *a.frames[i];
        const auto& fb = *b.frames[i];
        const std::string path = "frames[" + std::to_string(i) + "]";
        cmp.Array(path + ".matrix", std::vector<RwMatrix>{fa.matrix}, std::vector<RwMatrix>{fb.matrix});
        cmp.Value(path + ".parentIndex", fa.parentIndex, fb.parentIndex);
        cmp.Value(path + ".flags", fa.flags, fb.flags);
        cmp.Optional(path + ".hanim", fa.hanim, fb.hanim, WriteHAnim);
        cmp.Optional(path + ".userData", fa.userData, fb.userData, WriteUserDataList);
        cmp.Extensions(path + ".extensions", fa.extensions, fb.extensions);
    }

    cmp.Value("geometries", static_cast<long long>(a.geometries.size()), static_cast<long long>(b.geometries.size()));
    for (size_t i = 0; i < a.geometries.size() && i < b.geometries.size(); ++i) {
        CompareGeometry(cmp, "geometries[" + std::to_string(i) + "]", *a.geometries[i], *b.geometries[i]);
    }

    cmp.Value("atomics", static_cast<long long>(a.atomics.size()), static_cast<long long>(b.atomics.size()));
    for (size_t i = 0; i < a.atomics.size() && i < b.atomics.size(); ++i) {
        const auto& aa = a.atomics[i];
        const auto& ab = b.atomics[i];
        const std::string path = "atomics[" + std::to_string(i) + "]";
        cmp.Value(path + ".frameIndex", aa.frameIndex, ab.frameIndex);
        cmp.Value(path + ".geomIndex", aa.geomIndex, ab.geomIndex);
        cmp.Value(path + ".flags", aa.flags, ab.flags);
        cmp.Value(path + ".unused", aa.unused, ab.unused);
        cmp.Value(path + ".renderType", aa.renderType, ab.renderType);
        cmp.Value(path + ".blendMode", aa.blendMode, ab.blendMode);
        cmp.Value(path + ".id", aa.id, ab.id);
        cmp.Value(path + ".structSize", aa.structSize, ab.structSize);
        cmp.Array(path + ".structExtra", aa.structExtra, ab.structExtra);
        if (aa.rights && ab.rights) {
            cmp.Value(path + ".rights.pluginId", aa.rights->pluginId, ab.rights->pluginId);
            cmp.Value(path + ".rights.pluginData", aa.rights->pluginData, ab.rights->pluginData);
            cmp.Value(path + ".rights.hasPluginData", aa.rights->hasPluginData, ab.rights->hasPluginData);
        } else {
            cmp.Value(path + ".rights presence", aa.rights != nullptr, ab.rights != nullptr);
        }
        cmp.Optional(path + ".matfx", aa.matfx, ab.matfx, WriteMatFXAtomic);
        cmp.Optional(path + ".ntlAtomic", aa.ntlAtomic, ab.ntlAtomic, WriteNtlAtomicExt);
        cmp.Extensions(path + ".extensions", aa.extensions, ab.extensions);
    }

    std::vector<RwUInt8> sourceBytes, outputBytes;
    if (!original.SaveToMemory(sourceBytes) || !exported.SaveToMemory(outputBytes)) {
        std::cerr << "re-serialization failed\n";
        return 2;
    }
    if (cmp.Failed()) {
        return 2;
    }
    const bool identical = sourceBytes == outputBytes;
    std::cout << "differences=" << cmp.Differences() << " file=" << (identical ? "identical" : "different") << "\n";
    return identical && cmp.Differences() == 0 ? 0 : 1;
}
