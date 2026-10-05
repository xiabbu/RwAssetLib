/**
 * world/Geometry.cpp - RpGeometry read/write.
 */

#include "world/Geometry.h"

RwInt32 DffGeometryTexCoordSetCount(RwUInt32 format)
{
    // bageomet.c: the count lives in bits 16..23; pre-3.4 streams instead use the
    // TEXTURED / TEXTURED2 flags.
    const RwInt32 declared = static_cast<RwInt32>((format >> rpGEOMETRYTEXCOORDSETS_SHIFT) & 0xFF);
    if (declared != 0) {
        return declared;
    }
    if (format & rpGEOMETRYTEXTURED2) {
        return 2;
    }
    if (format & rpGEOMETRYTEXTURED) {
        return 1;
    }
    return 0;
}

bool DffGeometry::IsKnownExtension(RwUInt32 type)
{
    switch (type) {
        case rwID_BINMESHPLUGIN:
        case rwID_SKINPLUGIN:
        case rwID_TOONPLUGIN:
        case rwID_MORPHPLUGIN:
        case rwID_COLLISPLUGIN:
        case rwID_USERDATAPLUGIN:
            return true;
        default:
            return false;
    }
}

std::string DffGeometry::Name() const
{
    for (const auto& list : userDataLists) {
        if (const DffUserDataString* s = list.FindString("name")) {
            if (!s->value.empty()) {
                return s->value;
            }
        }
    }
    return {};
}

void DffGeometry::RefreshFormatFlags()
{
    format |= rpGEOMETRYPOSITIONS;
    format &= ~static_cast<RwUInt32>(rpGEOMETRYNORMALS | rpGEOMETRYPRELIT | rpGEOMETRYTEXTURED |
                                     rpGEOMETRYTEXTURED2 | rpGEOMETRYTEXCOORDSETS_MASK);

    if (HasNormals()) {
        format |= rpGEOMETRYNORMALS;
    }
    if (!preLitColors.empty()) {
        format |= rpGEOMETRYPRELIT;
    }

    const RwUInt32 uvCount = static_cast<RwUInt32>(texCoords.size());
    if (uvCount > 0) {
        format |= (uvCount << rpGEOMETRYTEXCOORDSETS_SHIFT) & rpGEOMETRYTEXCOORDSETS_MASK;
        // RpGeometryCreate also sets the legacy flag so 3.3-era readers cope.
        format |= (uvCount == 2) ? rpGEOMETRYTEXTURED2 : rpGEOMETRYTEXTURED;
    }
}

// ============================================================================
// Read
// ============================================================================

bool ReadGeometry(RwStream& stream, DffDiagnostics& diag, DffGeometry& geom, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    const RwUInt32 startPos = stream.GetPosition();

    geom.triangles.clear();
    geom.preLitColors.clear();
    geom.texCoords.clear();
    geom.morphTargets.clear();
    geom.materialIndices.clear();
    geom.materials.clear();
    geom.binMesh.reset();
    geom.skin.reset();
    geom.toon.reset();
    geom.morph.reset();
    geom.collis.reset();
    geom.userDataLists.clear();
    geom.extensions.Clear();

    RwInt32 numTriangles = 0;
    RwInt32 numVertices = 0;
    RwInt32 numMorphTargets = 0;

    // ---- STRUCT ------------------------------------------------------------
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated GEOMETRY STRUCT header");
        }
        if (header.type != rwID_STRUCT) {
            return diag.FailChunkType(headerPos, rwID_STRUCT, header.type);
        }

        const RwUInt32 dataStart = stream.GetPosition();
        const RwUInt32 structEnd = dataStart + header.size;

        RwInt32 format = 0;
        if (!stream.ReadInt32(&format) ||
            !stream.ReadInt32(&numTriangles) ||
            !stream.ReadInt32(&numVertices) ||
            !stream.ReadInt32(&numMorphTargets)) {
            return diag.Fail(dataStart, "truncated _rpGeometry header");
        }
        geom.format = static_cast<RwUInt32>(format);

        if (numTriangles < 0 || numVertices < 0 || numMorphTargets < 0) {
            return diag.Fail(dataStart, "negative count in _rpGeometry header");
        }

        const bool native = geom.IsNative();

        if (!native) {
            if (geom.format & rpGEOMETRYPRELIT) {
                geom.preLitColors.resize(static_cast<size_t>(numVertices));
                if (numVertices > 0 &&
                    !stream.Read(geom.preLitColors.data(), static_cast<size_t>(numVertices) * sizeof(RwRGBA))) {
                    return diag.Fail(stream.GetPosition(), "truncated prelit colour array");
                }
            }

            const RwInt32 numTexCoordSets = DffGeometryTexCoordSetCount(geom.format);
            geom.texCoords.resize(static_cast<size_t>(numTexCoordSets));
            for (RwInt32 i = 0; i < numTexCoordSets; ++i) {
                geom.texCoords[static_cast<size_t>(i)].resize(static_cast<size_t>(numVertices));
                if (numVertices > 0 &&
                    !stream.ReadReals(reinterpret_cast<RwReal*>(geom.texCoords[static_cast<size_t>(i)].data()),
                                      static_cast<size_t>(numVertices) * 2)) {
                    return diag.Fail(stream.GetPosition(), "truncated texture coordinate set");
                }
            }

            geom.triangles.resize(static_cast<size_t>(numTriangles));
            for (RwInt32 i = 0; i < numTriangles; ++i) {
                RwUInt32 vertex01 = 0;
                RwUInt32 vertex2Mat = 0;
                if (!stream.ReadUInt32(&vertex01) || !stream.ReadUInt32(&vertex2Mat)) {
                    return diag.Fail(stream.GetPosition(), "truncated triangle array");
                }

                DffTriangle& tri = geom.triangles[static_cast<size_t>(i)];
                tri.vertIndex[0] = static_cast<RwUInt16>((vertex01 >> 16) & 0xFFFF);
                tri.vertIndex[1] = static_cast<RwUInt16>(vertex01 & 0xFFFF);
                tri.vertIndex[2] = static_cast<RwUInt16>((vertex2Mat >> 16) & 0xFFFF);
                tri.matIndex     = static_cast<RwInt16>(vertex2Mat & 0xFFFF);
            }
        }

        geom.morphTargets.resize(static_cast<size_t>(numMorphTargets));
        for (RwInt32 i = 0; i < numMorphTargets; ++i) {
            DffMorphTarget& mt = geom.morphTargets[static_cast<size_t>(i)];

            RwInt32 pointsPresent = 0;
            RwInt32 normalsPresent = 0;
            if (!stream.ReadV3d(&mt.boundingSphere.center) ||
                !stream.ReadReal(&mt.boundingSphere.radius) ||
                !stream.ReadInt32(&pointsPresent) ||
                !stream.ReadInt32(&normalsPresent)) {
                return diag.Fail(stream.GetPosition(), "truncated _rpMorphTarget header");
            }

            if (native) {
                continue;
            }

            if (pointsPresent) {
                mt.vertices.resize(static_cast<size_t>(numVertices));
                if (numVertices > 0 &&
                    !stream.ReadReals(reinterpret_cast<RwReal*>(mt.vertices.data()),
                                      static_cast<size_t>(numVertices) * 3)) {
                    return diag.Fail(stream.GetPosition(), "truncated morph target vertex array");
                }
            }
            if (normalsPresent) {
                mt.normals.resize(static_cast<size_t>(numVertices));
                if (numVertices > 0 &&
                    !stream.ReadReals(reinterpret_cast<RwReal*>(mt.normals.data()),
                                      static_cast<size_t>(numVertices) * 3)) {
                    return diag.Fail(stream.GetPosition(), "truncated morph target normal array");
                }
            }
        }

        if (!geom.structSpan.Record(header.size, stream.GetPosition() - dataStart, header.version ? header.version : chunkVersion, stream)) {
            return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
        }
        stream.Seek(structEnd);
    }

    // ---- MATLIST -----------------------------------------------------------
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated MATLIST header");
        }
        if (header.type != rwID_MATLIST) {
            return diag.FailChunkType(headerPos, rwID_MATLIST, header.type);
        }

        const RwUInt32 matListStart = stream.GetPosition();
        const RwUInt32 matListVersion = header.version ? header.version : chunkVersion;
        DffScope matScope(diag, rwID_MATLIST);

        // MATLIST STRUCT: numMaterials + the shared-material index table
        // (bamatlst.c: -1 means "a MATERIAL chunk follows", >= 0 aliases an earlier one).
        RwInt32 numMaterials = 0;
        {
            const RwUInt32 structHeaderPos = stream.GetPosition();
            RwChunkHeader structHeader;
            if (!stream.ReadChunkHeader(structHeader)) {
                return diag.Fail(structHeaderPos, "truncated MATLIST STRUCT header");
            }
            if (structHeader.type != rwID_STRUCT) {
                return diag.FailChunkType(structHeaderPos, rwID_STRUCT, structHeader.type);
            }

            const RwUInt32 structDataStart = stream.GetPosition();
            if (!stream.ReadInt32(&numMaterials) || numMaterials < 0) {
                return diag.Fail(structDataStart, "invalid material count");
            }

            geom.materialIndices.resize(static_cast<size_t>(numMaterials));
            for (RwInt32 i = 0; i < numMaterials; ++i) {
                if (!stream.ReadInt32(&geom.materialIndices[static_cast<size_t>(i)])) {
                    return diag.Fail(stream.GetPosition(), "truncated material index table");
                }
            }

            if (!geom.matListStructSpan.Record(structHeader.size,
                                          stream.GetPosition() - structDataStart,
                                          structHeader.version ? structHeader.version : matListVersion, stream)) {
                return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
            }
            stream.Seek(structDataStart + structHeader.size);
        }

        geom.materials.resize(static_cast<size_t>(numMaterials));
        for (RwInt32 i = 0; i < numMaterials; ++i) {
            DffScope entryScope(diag, rwID_MATERIAL, i);
            const RwInt32 index = geom.materialIndices[static_cast<size_t>(i)];
            if (index >= 0) {
                if (index >= i) {
                    return diag.Fail(stream.GetPosition(), "MATLIST reference must identify an earlier material",
                                     "< " + std::to_string(i), std::to_string(index));
                }
                geom.materials[static_cast<size_t>(i)] = geom.materials[static_cast<size_t>(index)];
                continue;
            }

            const RwUInt32 matHeaderPos = stream.GetPosition();
            RwChunkHeader matHeader;
            if (!stream.ReadChunkHeader(matHeader)) {
                return diag.Fail(matHeaderPos, "truncated MATERIAL header");
            }
            if (matHeader.type != rwID_MATERIAL) {
                return diag.FailChunkType(matHeaderPos, rwID_MATERIAL, matHeader.type);
            }

            geom.materials[static_cast<size_t>(i)] = std::make_shared<DffMaterial>();
            if (!ReadMaterial(stream, diag, *geom.materials[static_cast<size_t>(i)], matHeader.size,
                              matHeader.version ? matHeader.version : matListVersion)) {
                return false;
            }
        }

        if (!geom.matListSpan.Record(header.size, stream.GetPosition() - matListStart, matListVersion, stream)) {
            return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
        }
    }

    // ---- EXTENSION ---------------------------------------------------------
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated GEOMETRY EXTENSION header");
        }
        if (header.type != rwID_EXTENSION) {
            return diag.FailChunkType(headerPos, rwID_EXTENSION, header.type);
        }

        const RwUInt32 extVersion = header.version ? header.version : chunkVersion;
        const RwInt32 vertexCount = numVertices;

        const bool ok = ReadExtensionContainer(
            stream, diag, header.size, extVersion, geom.extensions,
            [&](const RwChunkHeader& sub, bool& handled) -> bool {
                switch (sub.type) {
                    case rwID_BINMESHPLUGIN:
                    {
                        auto ext = std::make_shared<DffBinMesh>();
                        if (!ReadBinMesh(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        geom.binMesh = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_SKINPLUGIN:
                    {
                        auto ext = std::make_shared<DffSkin>();
                        if (!ReadSkin(stream, diag, *ext, vertexCount, sub.size, sub.version)) {
                            return false;
                        }
                        geom.skin = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_TOONPLUGIN:
                    {
                        auto ext = std::make_shared<DffToonGeo>();
                        if (!ReadToonGeo(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        geom.toon = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_MORPHPLUGIN:
                    {
                        auto ext = std::make_shared<DffMorphAnim>();
                        if (!ReadMorphAnim(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        geom.morph = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_COLLISPLUGIN:
                    {
                        auto ext = std::make_shared<DffCollis>();
                        if (!ReadCollis(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        geom.collis = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_USERDATAPLUGIN:
                    {
                        DffUserDataList ud;
                        if (!ReadUserDataList(stream, diag, ud, sub.size, sub.version)) {
                            return false;
                        }
                        geom.userDataLists.push_back(std::move(ud));
                        handled = true;
                        return true;
                    }

                    default:
                        return true;  // keep verbatim
                }
            });
        if (!ok) {
            return false;
        }
    }

    if (!geom.geometrySpan.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}

// ============================================================================
// Write
// ============================================================================

static bool WriteMaterialList(RwStream& stream, DffDiagnostics& diag, const DffGeometry& geom, RwUInt32 version)
{
    DffScope scope(diag, rwID_MATLIST);
    ChunkWriter writer(stream, version);

    if (!writer.Begin(rwID_MATLIST, geom.matListSpan, geom.matListSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin MATLIST chunk");
    }

    const RwInt32 numMaterials = static_cast<RwInt32>(geom.materials.size());

    if (!writer.Begin(rwID_STRUCT, geom.matListStructSpan, geom.matListStructSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin MATLIST STRUCT");
    }
    stream.WriteInt32(numMaterials);
    std::vector<RwInt32> indices(static_cast<size_t>(numMaterials), -1);
    for (RwInt32 i = 0; i < numMaterials; ++i) {
        // _rpMaterialListStreamWrite references the nearest earlier instance.
        for (RwInt32 j = i - 1; j >= 0; --j) {
            if (geom.materials[static_cast<size_t>(j)] == geom.materials[static_cast<size_t>(i)]) {
                indices[static_cast<size_t>(i)] = j;
                break;
            }
        }
        stream.WriteInt32(indices[static_cast<size_t>(i)]);
    }
    writer.End();

    for (RwInt32 i = 0; i < numMaterials; ++i) {
        if (indices[static_cast<size_t>(i)] >= 0) {
            continue;
        }
        DffScope entryScope(diag, rwID_MATERIAL, i);
        if (!geom.materials[static_cast<size_t>(i)]) {
            return diag.Fail(stream.GetPosition(), "null material in material list");
        }
        if (!WriteMaterial(stream, diag, *geom.materials[static_cast<size_t>(i)], version)) {
            return false;
        }
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close MATLIST chunk");
}

bool WriteGeometry(RwStream& stream, DffDiagnostics& diag, const DffGeometry& geom, RwUInt32 version)
{
    ChunkWriter writer(stream, version);

    if (!writer.Begin(rwID_GEOMETRY, geom.geometrySpan, geom.geometrySpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin GEOMETRY chunk");
    }

    const RwInt32 numVertices = geom.NumVertices();
    const RwInt32 numTriangles = static_cast<RwInt32>(geom.triangles.size());
    const RwInt32 numMorphTargets = static_cast<RwInt32>(geom.morphTargets.size());
    const bool native = geom.IsNative();

    // ---- STRUCT ------------------------------------------------------------
    if (!writer.Begin(rwID_STRUCT, geom.structSpan, geom.structSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin GEOMETRY STRUCT");
    }

    stream.WriteInt32(static_cast<RwInt32>(geom.format));
    stream.WriteInt32(numTriangles);
    stream.WriteInt32(numVertices);
    stream.WriteInt32(numMorphTargets);

    if (!native) {
        if (geom.format & rpGEOMETRYPRELIT) {
            if (static_cast<RwInt32>(geom.preLitColors.size()) != numVertices) {
                return diag.Fail(stream.GetPosition(),
                                 "prelit colour count does not match vertex count",
                                 std::to_string(numVertices),
                                 std::to_string(geom.preLitColors.size()));
            }
            if (numVertices > 0) {
                stream.Write(geom.preLitColors.data(), static_cast<size_t>(numVertices) * sizeof(RwRGBA));
            }
        }

        for (const auto& uvSet : geom.texCoords) {
            if (static_cast<RwInt32>(uvSet.size()) != numVertices) {
                return diag.Fail(stream.GetPosition(),
                                 "texture coordinate count does not match vertex count",
                                 std::to_string(numVertices),
                                 std::to_string(uvSet.size()));
            }
            if (numVertices > 0) {
                stream.WriteReals(reinterpret_cast<const RwReal*>(uvSet.data()),
                                  static_cast<size_t>(numVertices) * 2);
            }
        }

        for (const auto& tri : geom.triangles) {
            const RwUInt32 vertex01 = (static_cast<RwUInt32>(tri.vertIndex[0]) << 16) |
                                      static_cast<RwUInt32>(tri.vertIndex[1]);
            const RwUInt32 vertex2Mat = (static_cast<RwUInt32>(tri.vertIndex[2]) << 16) |
                                        (static_cast<RwUInt32>(tri.matIndex) & 0xFFFFu);
            stream.WriteUInt32(vertex01);
            stream.WriteUInt32(vertex2Mat);
        }
    }

    for (const auto& mt : geom.morphTargets) {
        stream.WriteV3d(mt.boundingSphere.center);
        stream.WriteReal(mt.boundingSphere.radius);
        stream.WriteInt32(mt.vertices.empty() ? 0 : 1);
        stream.WriteInt32(mt.normals.empty() ? 0 : 1);

        if (native) {
            continue;
        }
        if (!mt.vertices.empty()) {
            if (static_cast<RwInt32>(mt.vertices.size()) != numVertices) {
                return diag.Fail(stream.GetPosition(), "morph target vertex count mismatch");
            }
            stream.WriteReals(reinterpret_cast<const RwReal*>(mt.vertices.data()),
                              static_cast<size_t>(numVertices) * 3);
        }
        if (!mt.normals.empty()) {
            if (static_cast<RwInt32>(mt.normals.size()) != numVertices) {
                return diag.Fail(stream.GetPosition(), "morph target normal count mismatch");
            }
            stream.WriteReals(reinterpret_cast<const RwReal*>(mt.normals.data()),
                              static_cast<size_t>(numVertices) * 3);
        }
    }

    writer.End();  // STRUCT

    // ---- MATLIST -----------------------------------------------------------
    if (!WriteMaterialList(stream, diag, geom, version)) {
        return false;
    }

    // ---- EXTENSION ---------------------------------------------------------
    if (!writer.Begin(rwID_EXTENSION, geom.extensions.span, geom.extensions.span.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin GEOMETRY EXTENSION");
    }

    if (!geom.extensions.order.empty()) {
        size_t userDataIdx = 0;
        RawChunkCursor rawCursor(geom.extensions.raws);
        bool wroteBinMesh = false;
        bool wroteSkin = false;
        bool wroteToon = false;
        bool wroteMorph = false;
        bool wroteCollis = false;

        auto writeRaw = [&](RwUInt32 type) -> bool {
            const RawChunk* raw = rawCursor.Next(type);
            if (!raw) {
                return diag.Fail(stream.GetPosition(),
                                 "geometry extension order/raw mismatch",
                                 RwChunkTypeLabel(type),
                                 "<missing>");
            }
            writer.WriteRaw(*raw);
            return true;
        };

        for (RwUInt32 type : geom.extensions.order) {
            switch (type) {
                case rwID_BINMESHPLUGIN:
                    if (geom.binMesh && !wroteBinMesh) {
                        wroteBinMesh = true;
                        if (!WriteBinMesh(stream, diag, *geom.binMesh, version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                case rwID_SKINPLUGIN:
                    if (geom.skin && !wroteSkin) {
                        wroteSkin = true;
                        if (!WriteSkin(stream, diag, *geom.skin, numVertices, version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                case rwID_TOONPLUGIN:
                    if (geom.toon && !wroteToon) {
                        wroteToon = true;
                        if (!WriteToonGeo(stream, diag, *geom.toon, version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                case rwID_MORPHPLUGIN:
                    if (geom.morph && !wroteMorph) {
                        wroteMorph = true;
                        if (!WriteMorphAnim(stream, diag, *geom.morph, version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                case rwID_COLLISPLUGIN:
                    if (geom.collis && !wroteCollis) {
                        wroteCollis = true;
                        if (!WriteCollis(stream, diag, *geom.collis, version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                case rwID_USERDATAPLUGIN:
                    if (userDataIdx < geom.userDataLists.size()) {
                        if (!WriteUserDataList(stream, diag, geom.userDataLists[userDataIdx++], version)) {
                            return false;
                        }
                    } else if (!writeRaw(type)) {
                        return false;
                    }
                    break;

                default:
                    if (!writeRaw(type)) {
                        return false;
                    }
                    break;
            }
        }

        if (userDataIdx != geom.userDataLists.size() || !rawCursor.Exhausted()) {
            return diag.Fail(stream.GetPosition(), "geometry extension list not fully consumed");
        }
    } else {
        if (geom.binMesh && !WriteBinMesh(stream, diag, *geom.binMesh, version)) {
            return false;
        }
        if (geom.skin && !WriteSkin(stream, diag, *geom.skin, numVertices, version)) {
            return false;
        }
        if (geom.toon && !WriteToonGeo(stream, diag, *geom.toon, version)) {
            return false;
        }
        if (geom.morph && !WriteMorphAnim(stream, diag, *geom.morph, version)) {
            return false;
        }
        if (geom.collis && !WriteCollis(stream, diag, *geom.collis, version)) {
            return false;
        }
        for (const auto& ud : geom.userDataLists) {
            if (!WriteUserDataList(stream, diag, ud, version)) {
                return false;
            }
        }
        for (const auto& raw : geom.extensions.raws) {
            writer.WriteRaw(raw);
        }
    }

    writer.End();  // EXTENSION
    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close GEOMETRY chunk");
}
