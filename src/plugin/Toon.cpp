/**
 * plugin/Toon.cpp - RpToon geometry/material read/write.
 */

#include "plugin/Toon.h"

#include <algorithm>
#include <cstring>

namespace
{
    std::string FixedNameToString(const char* buf, size_t cap)
    {
        size_t len = 0;
        while (len < cap && buf[len] != 0) {
            ++len;
        }
        return std::string(buf, len);
    }

    void StringToFixedName(char* buf, size_t cap, const std::string& s)
    {
        std::memset(buf, 0, cap);
        const size_t n = std::min(s.size(), cap - 1);
        if (n > 0) {
            std::memcpy(buf, s.data(), n);
        }
    }
}

std::string DffToonGeo::DefaultPaintName() const
{
    return FixedNameToString(defaultPaintID.name, sizeof(defaultPaintID.name));
}

void DffToonGeo::SetDefaultPaintName(const std::string& name)
{
    StringToFixedName(defaultPaintID.name, sizeof(defaultPaintID.name), name);
}

std::string DffToonMaterial::PaintName() const
{
    return FixedNameToString(paintName.data(), paintName.size());
}

void DffToonMaterial::SetPaintName(const std::string& name)
{
    StringToFixedName(paintName.data(), paintName.size(), name);
}

// ===========================================================================
// Geometry
// ===========================================================================

bool ReadToonGeo(RwStream& stream, DffDiagnostics& diag, DffToonGeo& out, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, "TOON(geometry)");

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    out = DffToonGeo();
    out.chunkVersion = chunkVersion;

    auto bail = [&](const char* msg) {
        stream.Seek(endPos);
        return diag.Fail(stream.GetPosition(), msg);
    };

    if (!stream.ReadInt32(&out.versionStamp)) {
        return bail("truncated toon geometry version stamp");
    }
    if (out.versionStamp != rpTOONGEOSTREAMVERSION) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "incompatible toon geometry stream version",
                         "0xABCD0002",
                         std::to_string(static_cast<unsigned>(out.versionStamp)));
    }

    RwInt32 numVertices = 0;
    RwInt32 numTriangles = 0;
    if (!stream.ReadInt32(&numVertices) || !stream.ReadInt32(&numTriangles) ||
        numVertices < 0 || numTriangles < 0) {
        return bail("invalid toon geometry vertex/triangle count");
    }

    out.triangles.resize(static_cast<size_t>(numTriangles));
    for (RwInt32 i = 0; i < numTriangles; ++i) {
        auto& tri = out.triangles[static_cast<size_t>(i)];
        if (!stream.ReadUInt16(&tri.vertIndex[0]) ||
            !stream.ReadUInt16(&tri.vertIndex[1]) ||
            !stream.ReadUInt16(&tri.vertIndex[2])) {
            return bail("truncated toon triangle list");
        }
    }

    out.vertexThicknesses.resize(static_cast<size_t>(numVertices));
    if (numVertices > 0 && !stream.ReadReals(out.vertexThicknesses.data(), static_cast<size_t>(numVertices))) {
        return bail("truncated toon vertex thickness array");
    }

    RwInt32 numEdges = 0;
    if (!stream.ReadInt32(&numEdges) || numEdges < 0) {
        return bail("invalid toon edge count");
    }
    if (!stream.ReadInt32(&out.numCreaseEdges)) {
        return bail("truncated toon crease-edge count");
    }

    out.edges.resize(static_cast<size_t>(numEdges));
    for (RwInt32 i = 0; i < numEdges; ++i) {
        auto& e = out.edges[static_cast<size_t>(i)];
        if (!stream.ReadUInt16(&e.v[0]) || !stream.ReadUInt16(&e.v[1]) ||
            !stream.ReadUInt16(&e.face[0]) || !stream.ReadUInt16(&e.face[1]) ||
            !stream.ReadUInt16(&e.edgefv[0][0]) || !stream.ReadUInt16(&e.edgefv[0][1]) ||
            !stream.ReadUInt16(&e.edgefv[1][0]) || !stream.ReadUInt16(&e.edgefv[1][1])) {
            return bail("truncated toon edge connectivity");
        }
    }

    out.faceNormals.resize(static_cast<size_t>(numTriangles));
    if (numTriangles > 0 &&
        !stream.ReadReals(reinterpret_cast<RwReal*>(out.faceNormals.data()), static_cast<size_t>(numTriangles) * 3)) {
        return bail("truncated toon face normals");
    }

    out.extrusionNormals.resize(static_cast<size_t>(numVertices));
    if (numVertices > 0 &&
        !stream.ReadReals(reinterpret_cast<RwReal*>(out.extrusionNormals.data()), static_cast<size_t>(numVertices) * 3)) {
        return bail("truncated toon extrusion normals");
    }

    RwInt32 inkIDCount = 0;
    if (!stream.ReadInt32(&inkIDCount) || inkIDCount < 0) {
        return bail("invalid toon ink ID count");
    }

    out.inkIDList.resize(static_cast<size_t>(inkIDCount));
    if (inkIDCount > 0 &&
        !stream.Read(out.inkIDList.data(), static_cast<size_t>(inkIDCount) * sizeof(RpToonInkID))) {
        return bail("truncated toon ink ID list");
    }

    out.edgeInkIDs.resize(static_cast<size_t>(numEdges));
    if (numEdges > 0 &&
        !stream.Read(out.edgeInkIDs.data(), static_cast<size_t>(numEdges) * sizeof(RpToonEdgeInkID))) {
        return bail("truncated toon edge ink IDs");
    }

    if (!stream.Read(out.defaultPaintID.name, sizeof(out.defaultPaintID.name))) {
        return bail("truncated toon default paint ID");
    }

    const RwUInt32 consumed = stream.GetPosition() - startPos;
    if (consumed != payloadSize) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "toon geometry payload length mismatch",
                         std::to_string(payloadSize),
                         std::to_string(consumed));
    }

    if (!out.span.Record(payloadSize, consumed, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}

bool WriteToonGeo(RwStream& stream, DffDiagnostics& diag, const DffToonGeo& toon, RwUInt32 version)
{
    DffScope scope(diag, "TOON(geometry)");

    const RwInt32 numVertices = toon.NumVertices();
    const RwInt32 numTriangles = toon.NumTriangles();
    const RwInt32 numEdges = toon.NumEdges();

    if (static_cast<RwInt32>(toon.faceNormals.size()) != numTriangles) {
        return diag.Fail(stream.GetPosition(),
                         "toon faceNormals count must match triangle count",
                         std::to_string(numTriangles),
                         std::to_string(toon.faceNormals.size()));
    }
    if (static_cast<RwInt32>(toon.extrusionNormals.size()) != numVertices) {
        return diag.Fail(stream.GetPosition(),
                         "toon extrusionNormals count must match vertex count",
                         std::to_string(numVertices),
                         std::to_string(toon.extrusionNormals.size()));
    }
    if (static_cast<RwInt32>(toon.edgeInkIDs.size()) != numEdges) {
        return diag.Fail(stream.GetPosition(),
                         "toon edgeInkIDs count must match edge count",
                         std::to_string(numEdges),
                         std::to_string(toon.edgeInkIDs.size()));
    }

    const RwUInt32 chunkVersion = toon.chunkVersion ? toon.chunkVersion : version;
    ChunkWriter writer(stream, chunkVersion);
    if (!writer.Begin(rwID_TOONPLUGIN, toon.span, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin TOON geometry chunk");
    }

    stream.WriteInt32(toon.versionStamp);
    stream.WriteInt32(numVertices);
    stream.WriteInt32(numTriangles);

    for (const auto& tri : toon.triangles) {
        stream.WriteUInt16(tri.vertIndex[0]);
        stream.WriteUInt16(tri.vertIndex[1]);
        stream.WriteUInt16(tri.vertIndex[2]);
    }

    if (numVertices > 0) {
        stream.WriteReals(toon.vertexThicknesses.data(), static_cast<size_t>(numVertices));
    }

    stream.WriteInt32(numEdges);
    stream.WriteInt32(toon.numCreaseEdges);

    for (const auto& e : toon.edges) {
        stream.WriteUInt16(e.v[0]);
        stream.WriteUInt16(e.v[1]);
        stream.WriteUInt16(e.face[0]);
        stream.WriteUInt16(e.face[1]);
        stream.WriteUInt16(e.edgefv[0][0]);
        stream.WriteUInt16(e.edgefv[0][1]);
        stream.WriteUInt16(e.edgefv[1][0]);
        stream.WriteUInt16(e.edgefv[1][1]);
    }

    if (numTriangles > 0) {
        stream.WriteReals(reinterpret_cast<const RwReal*>(toon.faceNormals.data()),
                          static_cast<size_t>(numTriangles) * 3);
    }
    if (numVertices > 0) {
        stream.WriteReals(reinterpret_cast<const RwReal*>(toon.extrusionNormals.data()),
                          static_cast<size_t>(numVertices) * 3);
    }

    stream.WriteInt32(static_cast<RwInt32>(toon.inkIDList.size()));
    if (!toon.inkIDList.empty()) {
        stream.Write(toon.inkIDList.data(), toon.inkIDList.size() * sizeof(RpToonInkID));
    }
    if (numEdges > 0) {
        stream.Write(toon.edgeInkIDs.data(), toon.edgeInkIDs.size() * sizeof(RpToonEdgeInkID));
    }
    stream.Write(toon.defaultPaintID.name, sizeof(toon.defaultPaintID.name));

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close TOON geometry chunk");
}

// ===========================================================================
// Material
// ===========================================================================

bool ReadToonMaterial(RwStream& stream, DffDiagnostics& diag, DffToonMaterial& out, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, "TOON(material)");

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    out = DffToonMaterial();
    out.chunkVersion = chunkVersion;

    if (!stream.ReadInt32(&out.versionStamp)) {
        stream.Seek(endPos);
        return diag.Fail(startPos, "truncated toon material version stamp");
    }
    if (out.versionStamp > rpTOONMATERIALVERSIONSTAMP) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "incompatible toon material stream version",
                         "<= 0x67890002",
                         std::to_string(static_cast<unsigned>(out.versionStamp)));
    }

    if (!stream.ReadInt32(&out.overrideGeometryPaint)) {
        stream.Seek(endPos);
        return diag.Fail(stream.GetPosition(), "truncated toon material overrideGeometryPaint");
    }

    // ToonMaterialStreamRead gates the remaining fields on the version stamp.
    out.hasPaintName = (out.overrideGeometryPaint != 0) ||
                       (out.versionStamp >= rpTOONMATERIALVERSIONSTAMP_SILHOUETTEINKIDS);
    out.hasSilhouetteInkID = (out.versionStamp >= rpTOONMATERIALVERSIONSTAMP_SILHOUETTEINKIDS);

    if (out.hasPaintName) {
        if (!stream.Read(out.paintName.data(), out.paintName.size())) {
            stream.Seek(endPos);
            return diag.Fail(stream.GetPosition(), "truncated toon material paint name");
        }
    }
    if (out.hasSilhouetteInkID) {
        if (!stream.ReadInt32(&out.silhouetteInkID)) {
            stream.Seek(endPos);
            return diag.Fail(stream.GetPosition(), "truncated toon material silhouette ink ID");
        }
    }

    const RwUInt32 consumed = stream.GetPosition() - startPos;
    if (consumed != payloadSize) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "toon material payload length mismatch",
                         std::to_string(payloadSize),
                         std::to_string(consumed));
    }

    if (!out.span.Record(payloadSize, consumed, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}

bool WriteToonMaterial(RwStream& stream, DffDiagnostics& diag, const DffToonMaterial& toon, RwUInt32 version)
{
    DffScope scope(diag, "TOON(material)");

    const RwUInt32 chunkVersion = toon.chunkVersion ? toon.chunkVersion : version;
    ChunkWriter writer(stream, chunkVersion);

    if (!writer.Begin(rwID_TOONPLUGIN, toon.span, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin TOON material chunk");
    }

    stream.WriteInt32(toon.versionStamp);
    stream.WriteInt32(toon.overrideGeometryPaint);
    if (toon.hasPaintName) {
        stream.Write(toon.paintName.data(), toon.paintName.size());
    }
    if (toon.hasSilhouetteInkID) {
        stream.WriteInt32(toon.silhouetteInkID);
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close TOON material chunk");
}
