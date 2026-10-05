/**
 * world/Clump.cpp - RpClump / frame list / RpAtomic read/write.
 */

#include "world/Clump.h"

#include <unordered_map>

// ============================================================================
// Frame
// ============================================================================

bool DffFrame::IsKnownExtension(RwUInt32 type)
{
    switch (type) {
        case rwID_HANIMPLUGIN:
        case rwID_USERDATAPLUGIN:
            return true;
        default:
            return false;
    }
}

std::string DffFrame::Name() const
{
    if (userData) {
        if (const DffUserDataString* s = userData->FindString("name")) {
            if (!s->value.empty()) {
                return s->value;
            }
        }
    }

    // Legacy assets store the name in a bare STRING extension.
    for (const auto& raw : extensions.raws) {
        if (raw.type != rwID_STRING || raw.data.empty()) {
            continue;
        }
        size_t len = 0;
        while (len < raw.data.size() && raw.data[len] != 0) {
            ++len;
        }
        if (len > 0) {
            return std::string(reinterpret_cast<const char*>(raw.data.data()), len);
        }
    }
    return {};
}

void DffFrame::SetName(const std::string& name)
{
    if (!userData) {
        userData = std::make_shared<DffUserDataList>();
    }
    DffUserDataString* s = userData->FindOrCreateString("name");
    if (s) {
        s->SetValue(name);
    }
    extensions.EnsurePresent(rwID_USERDATAPLUGIN, &DffFrame::IsKnownExtension);
}

// ============================================================================
// Atomic
// ============================================================================

bool DffAtomic::IsKnownExtension(RwUInt32 type)
{
    switch (type) {
        case rwID_RIGHTTORENDER:
        case rwID_MATFXPLUGIN:
        case rwID_NTL_ATOMIC_EXT:
            return true;
        default:
            return false;
    }
}

// ============================================================================
// Clump helpers
// ============================================================================

const DffFrame* DffClump::FindHAnimRoot() const
{
    for (const auto& f : frames) {
        if (f && f->hanim && f->hanim->IsHierarchyRoot()) {
            return f.get();
        }
    }
    return nullptr;
}

DffFrame* DffClump::FindHAnimRoot()
{
    for (auto& f : frames) {
        if (f && f->hanim && f->hanim->IsHierarchyRoot()) {
            return f.get();
        }
    }
    return nullptr;
}

std::vector<int> DffClump::BuildHAnimNodeToFrameMap() const
{
    std::vector<int> map;
    const DffFrame* root = FindHAnimRoot();
    if (!root || !root->hanim) {
        return map;
    }

    std::unordered_map<RwInt32, int> idToFrame;
    idToFrame.reserve(frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        if (frames[i] && frames[i]->hanim) {
            idToFrame[frames[i]->hanim->nodeId] = static_cast<int>(i);
        }
    }

    const auto& nodeInfos = root->hanim->nodeInfos;
    map.assign(nodeInfos.size(), -1);
    for (size_t i = 0; i < nodeInfos.size(); ++i) {
        auto it = idToFrame.find(nodeInfos[i].nodeID);
        if (it != idToFrame.end()) {
            map[i] = it->second;
        }
    }
    return map;
}

// ============================================================================
// RIGHTTORENDER
// ============================================================================

static bool ReadRightToRender(RwStream& stream,
                              DffDiagnostics& diag,
                              DffRightToRender& out,
                              RwUInt32 payloadSize,
                              RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_RIGHTTORENDER);

    const RwUInt32 startPos = stream.GetPosition();
    out = DffRightToRender();
    out.chunkVersion = chunkVersion;

    if (payloadSize != 4 && payloadSize != 8) {
        stream.Seek(startPos + payloadSize);
        return diag.Fail(startPos,
                         "RIGHTTORENDER payload is neither 4 nor 8 bytes",
                         "4 or 8",
                         std::to_string(payloadSize));
    }

    if (!stream.ReadUInt32(&out.pluginId)) {
        return diag.Fail(startPos, "truncated RIGHTTORENDER pluginId");
    }
    if (payloadSize == 8) {
        if (!stream.ReadUInt32(&out.pluginData)) {
            return diag.Fail(startPos, "truncated RIGHTTORENDER pluginData");
        }
        out.hasPluginData = true;
    }

    if (!out.span.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}

static bool WriteRightToRender(RwStream& stream,
                               DffDiagnostics& diag,
                               const DffRightToRender& rights,
                               RwUInt32 version)
{
    DffScope scope(diag, rwID_RIGHTTORENDER);

    const RwUInt32 chunkVersion = rights.chunkVersion ? rights.chunkVersion : version;
    ChunkWriter writer(stream, chunkVersion);

    if (!writer.Begin(rwID_RIGHTTORENDER, rights.span, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin RIGHTTORENDER chunk");
    }
    stream.WriteUInt32(rights.pluginId);
    if (rights.hasPluginData) {
        stream.WriteUInt32(rights.pluginData);
    }
    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close RIGHTTORENDER chunk");
}

// ============================================================================
// Frame list
// ============================================================================

static bool ReadFrameList(RwStream& stream, DffDiagnostics& diag, DffClump& clump, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_FRAMELIST);
    const RwUInt32 startPos = stream.GetPosition();

    RwInt32 numFrames = 0;
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated FRAMELIST STRUCT header");
        }
        if (header.type != rwID_STRUCT) {
            return diag.FailChunkType(headerPos, rwID_STRUCT, header.type);
        }

        const RwUInt32 dataStart = stream.GetPosition();
        if (!stream.ReadInt32(&numFrames) || numFrames < 0) {
            return diag.Fail(dataStart, "invalid frame count");
        }

        clump.frames.resize(static_cast<size_t>(numFrames));
        for (RwInt32 i = 0; i < numFrames; ++i) {
            auto frame = std::make_shared<DffFrame>();

            // rwStreamFrame: right, up, at, pos, parentIndex, flags
            const bool ok = stream.ReadV3d(&frame->matrix.right) &&
                            stream.ReadV3d(&frame->matrix.up) &&
                            stream.ReadV3d(&frame->matrix.at) &&
                            stream.ReadV3d(&frame->matrix.pos) &&
                            stream.ReadInt32(&frame->parentIndex) &&
                            stream.ReadInt32(&frame->flags);
            if (!ok) {
                return diag.Fail(stream.GetPosition(), "truncated rwStreamFrame");
            }

            clump.frames[static_cast<size_t>(i)] = std::move(frame);
        }

        if (!clump.frameListStructSpan.Record(header.size,
                                         stream.GetPosition() - dataStart,
                                         header.version ? header.version : chunkVersion, stream)) {
            return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
        }
        stream.Seek(dataStart + header.size);
    }

    // One EXTENSION per frame, in frame order.
    for (RwInt32 i = 0; i < numFrames; ++i) {
        DffScope frameScope(diag, "FRAME[" + std::to_string(i) + "]");
        DffFrame& frame = *clump.frames[static_cast<size_t>(i)];

        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated FRAME EXTENSION header");
        }
        if (header.type != rwID_EXTENSION) {
            return diag.FailChunkType(headerPos, rwID_EXTENSION, header.type);
        }

        const RwUInt32 extVersion = header.version ? header.version : chunkVersion;
        const bool ok = ReadExtensionContainer(
            stream, diag, header.size, extVersion, frame.extensions,
            [&](const RwChunkHeader& sub, bool& handled) -> bool {
                switch (sub.type) {
                    case rwID_HANIMPLUGIN:
                    {
                        auto ext = std::make_shared<DffHAnim>();
                        if (!ReadHAnim(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        frame.hanim = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_USERDATAPLUGIN:
                    {
                        auto ext = std::make_shared<DffUserDataList>();
                        if (!ReadUserDataList(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        frame.userData = std::move(ext);
                        handled = true;
                        return true;
                    }

                    default:
                        return true;  // keep verbatim (e.g. legacy STRING names)
                }
            });
        if (!ok) {
            return false;
        }
    }

    if (!clump.frameListSpan.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}

static bool WriteFrameList(RwStream& stream, DffDiagnostics& diag, const DffClump& clump, RwUInt32 version)
{
    DffScope scope(diag, rwID_FRAMELIST);
    ChunkWriter writer(stream, version);

    if (!writer.Begin(rwID_FRAMELIST, clump.frameListSpan, clump.frameListSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin FRAMELIST chunk");
    }

    if (!writer.Begin(rwID_STRUCT, clump.frameListStructSpan, clump.frameListStructSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin FRAMELIST STRUCT");
    }
    stream.WriteInt32(static_cast<RwInt32>(clump.frames.size()));
    for (const auto& frame : clump.frames) {
        if (!frame) {
            return diag.Fail(stream.GetPosition(), "null frame in frame list");
        }
        stream.WriteV3d(frame->matrix.right);
        stream.WriteV3d(frame->matrix.up);
        stream.WriteV3d(frame->matrix.at);
        stream.WriteV3d(frame->matrix.pos);
        stream.WriteInt32(frame->parentIndex);
        stream.WriteInt32(frame->flags);
    }
    writer.End();

    for (size_t i = 0; i < clump.frames.size(); ++i) {
        DffScope frameScope(diag, "FRAME[" + std::to_string(i) + "]");
        const DffFrame& frame = *clump.frames[i];

        if (!writer.Begin(rwID_EXTENSION, frame.extensions.span, frame.extensions.span.ResolveVersion(version))) {
            return diag.Fail(stream.GetPosition(), "failed to begin FRAME EXTENSION");
        }

        if (!frame.extensions.order.empty()) {
            RawChunkCursor rawCursor(frame.extensions.raws);
            bool wroteHAnim = false;
            bool wroteUserData = false;

            for (RwUInt32 type : frame.extensions.order) {
                if (type == rwID_HANIMPLUGIN && frame.hanim && !wroteHAnim) {
                    wroteHAnim = true;
                    if (!WriteHAnim(stream, diag, *frame.hanim, version)) {
                        return false;
                    }
                } else if (type == rwID_USERDATAPLUGIN && frame.userData && !wroteUserData) {
                    wroteUserData = true;
                    if (!WriteUserDataList(stream, diag, *frame.userData, version)) {
                        return false;
                    }
                } else {
                    const RawChunk* raw = rawCursor.Next(type);
                    if (!raw) {
                        return diag.Fail(stream.GetPosition(),
                                         "frame extension order/raw mismatch",
                                         RwChunkTypeLabel(type),
                                         "<missing>");
                    }
                    writer.WriteRaw(*raw);
                }
            }

            if (!rawCursor.Exhausted()) {
                return diag.Fail(stream.GetPosition(), "frame extension list not fully consumed");
            }
        } else {
            if (frame.userData && !WriteUserDataList(stream, diag, *frame.userData, version)) {
                return false;
            }
            if (frame.hanim && !WriteHAnim(stream, diag, *frame.hanim, version)) {
                return false;
            }
            for (const auto& raw : frame.extensions.raws) {
                writer.WriteRaw(raw);
            }
        }

        writer.End();  // EXTENSION
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close FRAMELIST chunk");
}

// ============================================================================
// Geometry list
// ============================================================================

static bool ReadGeometryList(RwStream& stream, DffDiagnostics& diag, DffClump& clump, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_GEOMETRYLIST);
    const RwUInt32 startPos = stream.GetPosition();

    RwInt32 numGeometries = 0;
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated GEOMETRYLIST STRUCT header");
        }
        if (header.type != rwID_STRUCT) {
            return diag.FailChunkType(headerPos, rwID_STRUCT, header.type);
        }

        const RwUInt32 dataStart = stream.GetPosition();
        if (!stream.ReadInt32(&numGeometries) || numGeometries < 0) {
            return diag.Fail(dataStart, "invalid geometry count");
        }

        if (!clump.geometryListStructSpan.Record(header.size,
                                            stream.GetPosition() - dataStart,
                                            header.version ? header.version : chunkVersion, stream)) {
            return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
        }
        stream.Seek(dataStart + header.size);
    }

    clump.geometries.resize(static_cast<size_t>(numGeometries));
    for (RwInt32 i = 0; i < numGeometries; ++i) {
        DffScope geomScope(diag, rwID_GEOMETRY, i);

        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated GEOMETRY header");
        }
        if (header.type != rwID_GEOMETRY) {
            return diag.FailChunkType(headerPos, rwID_GEOMETRY, header.type);
        }

        clump.geometries[static_cast<size_t>(i)] = std::make_shared<DffGeometry>();
        if (!ReadGeometry(stream, diag, *clump.geometries[static_cast<size_t>(i)], header.size,
                          header.version ? header.version : chunkVersion)) {
            return false;
        }
    }

    if (!clump.geometryListSpan.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}

static bool WriteGeometryList(RwStream& stream, DffDiagnostics& diag, const DffClump& clump, RwUInt32 version)
{
    DffScope scope(diag, rwID_GEOMETRYLIST);
    ChunkWriter writer(stream, version);

    if (!writer.Begin(rwID_GEOMETRYLIST, clump.geometryListSpan, clump.geometryListSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin GEOMETRYLIST chunk");
    }

    if (!writer.Begin(rwID_STRUCT, clump.geometryListStructSpan, clump.geometryListStructSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin GEOMETRYLIST STRUCT");
    }
    stream.WriteInt32(static_cast<RwInt32>(clump.geometries.size()));
    writer.End();

    for (size_t i = 0; i < clump.geometries.size(); ++i) {
        DffScope geomScope(diag, rwID_GEOMETRY, static_cast<int>(i));
        if (!clump.geometries[i]) {
            return diag.Fail(stream.GetPosition(), "null geometry in geometry list");
        }
        if (!WriteGeometry(stream, diag, *clump.geometries[i], version)) {
            return false;
        }
    }

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close GEOMETRYLIST chunk");
}

// ============================================================================
// Atomic
// ============================================================================

static bool ReadAtomic(RwStream& stream, DffDiagnostics& diag, DffAtomic& atomic, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    const RwUInt32 startPos = stream.GetPosition();

    atomic.rights.reset();
    atomic.matfx.reset();
    atomic.extensions.Clear();
    atomic.structExtra.clear();

    // ---- STRUCT ------------------------------------------------------------
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated ATOMIC STRUCT header");
        }
        if (header.type != rwID_STRUCT) {
            return diag.FailChunkType(headerPos, rwID_STRUCT, header.type);
        }

        const RwUInt32 dataStart = stream.GetPosition();
        atomic.structSize = header.size;

        if (header.size < 16) {
            return diag.Fail(dataStart,
                             "ATOMIC STRUCT smaller than _rpAtomicBinary",
                             ">= 16",
                             std::to_string(header.size));
        }

        const bool ok = stream.ReadInt32(&atomic.frameIndex) &&
                        stream.ReadInt32(&atomic.geomIndex) &&
                        stream.ReadInt32(&atomic.flags) &&
                        stream.ReadInt32(&atomic.unused);
        if (!ok) {
            return diag.Fail(dataStart, "truncated _rpAtomicBinary");
        }

        RwUInt32 consumed = 16;
        if (header.size >= 24) {
            if (!stream.ReadInt16(&atomic.renderType) ||
                !stream.ReadInt16(&atomic.blendMode) ||
                !stream.ReadInt32(&atomic.id)) {
                return diag.Fail(stream.GetPosition(), "truncated DBO atomic extension fields");
            }
            consumed = 24;
        }

        if (header.size > consumed) {
            atomic.structExtra.resize(header.size - consumed);
            if (!stream.Read(atomic.structExtra.data(), atomic.structExtra.size())) {
                return diag.Fail(stream.GetPosition(), "truncated ATOMIC STRUCT tail");
            }
        }

        if (!atomic.structSpan.Record(header.size, header.size, header.version ? header.version : chunkVersion, stream)) {
            return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
        }
        stream.Seek(dataStart + header.size);
    }

    // ---- EXTENSION ---------------------------------------------------------
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated ATOMIC EXTENSION header");
        }
        if (header.type != rwID_EXTENSION) {
            return diag.FailChunkType(headerPos, rwID_EXTENSION, header.type);
        }

        const RwUInt32 extVersion = header.version ? header.version : chunkVersion;
        const bool ok = ReadExtensionContainer(
            stream, diag, header.size, extVersion, atomic.extensions,
            [&](const RwChunkHeader& sub, bool& handled) -> bool {
                switch (sub.type) {
                    case rwID_RIGHTTORENDER:
                    {
                        auto ext = std::make_shared<DffRightToRender>();
                        if (!ReadRightToRender(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        atomic.rights = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_MATFXPLUGIN:
                    {
                        auto ext = std::make_shared<DffMatFXAtomic>();
                        if (!ReadMatFXAtomic(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        atomic.matfx = std::move(ext);
                        handled = true;
                        return true;
                    }

                    case rwID_NTL_ATOMIC_EXT:
                    {
                        auto ext = std::make_shared<DffNtlAtomicExt>();
                        if (!ReadNtlAtomicExt(stream, diag, *ext, sub.size, sub.version)) {
                            return false;
                        }
                        atomic.ntlAtomic = std::move(ext);
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

    if (!atomic.atomicSpan.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}

static bool WriteAtomic(RwStream& stream, DffDiagnostics& diag, const DffAtomic& atomic, RwUInt32 version)
{
    ChunkWriter writer(stream, version);

    if (!writer.Begin(rwID_ATOMIC, atomic.atomicSpan, atomic.atomicSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin ATOMIC chunk");
    }

    // ---- STRUCT ------------------------------------------------------------
    if (!writer.Begin(rwID_STRUCT, atomic.structSpan, atomic.structSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin ATOMIC STRUCT");
    }

    RwUInt32 structSize = atomic.structSize < 16 ? 16u : atomic.structSize;
    RwUInt32 written = 0;

    stream.WriteInt32(atomic.frameIndex);
    stream.WriteInt32(atomic.geomIndex);
    stream.WriteInt32(atomic.flags);
    stream.WriteInt32(atomic.unused);
    written += 16;

    if (structSize >= 24) {
        stream.WriteInt16(atomic.renderType);
        stream.WriteInt16(atomic.blendMode);
        stream.WriteInt32(atomic.id);
        written += 8;
    }

    if (structSize > written) {
        const RwUInt32 remaining = structSize - written;
        const size_t toWrite = (atomic.structExtra.size() < remaining) ? atomic.structExtra.size() : remaining;
        if (toWrite > 0) {
            stream.Write(atomic.structExtra.data(), toWrite);
        }
        if (toWrite < remaining) {
            stream.WritePadding(remaining - toWrite, 0);
        }
    }

    writer.End();  // STRUCT

    // ---- EXTENSION ---------------------------------------------------------
    if (!writer.Begin(rwID_EXTENSION, atomic.extensions.span, atomic.extensions.span.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin ATOMIC EXTENSION");
    }

    if (!atomic.extensions.order.empty()) {
        RawChunkCursor rawCursor(atomic.extensions.raws);
        bool wroteRights = false;
        bool wroteMatfx = false;
        bool wroteNtl = false;

        for (RwUInt32 type : atomic.extensions.order) {
            if (type == rwID_RIGHTTORENDER && atomic.rights && !wroteRights) {
                wroteRights = true;
                if (!WriteRightToRender(stream, diag, *atomic.rights, version)) {
                    return false;
                }
            } else if (type == rwID_MATFXPLUGIN && atomic.matfx && !wroteMatfx) {
                wroteMatfx = true;
                if (!WriteMatFXAtomic(stream, diag, *atomic.matfx, version)) {
                    return false;
                }
            } else if (type == rwID_NTL_ATOMIC_EXT && atomic.ntlAtomic && !wroteNtl) {
                wroteNtl = true;
                if (!WriteNtlAtomicExt(stream, diag, *atomic.ntlAtomic, version)) {
                    return false;
                }
            } else {
                const RawChunk* raw = rawCursor.Next(type);
                if (!raw) {
                    return diag.Fail(stream.GetPosition(),
                                     "atomic extension order/raw mismatch",
                                     RwChunkTypeLabel(type),
                                     "<missing>");
                }
                writer.WriteRaw(*raw);
            }
        }

        if (!rawCursor.Exhausted()) {
            return diag.Fail(stream.GetPosition(), "atomic extension list not fully consumed");
        }
    } else {
        if (atomic.rights && !WriteRightToRender(stream, diag, *atomic.rights, version)) {
            return false;
        }
        if (atomic.matfx && !WriteMatFXAtomic(stream, diag, *atomic.matfx, version)) {
            return false;
        }
        if (atomic.ntlAtomic && !WriteNtlAtomicExt(stream, diag, *atomic.ntlAtomic, version)) {
            return false;
        }
        for (const auto& raw : atomic.extensions.raws) {
            writer.WriteRaw(raw);
        }
    }

    writer.End();  // EXTENSION
    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close ATOMIC chunk");
}

// ============================================================================
// Clump
// ============================================================================

bool ReadClump(RwStream& stream, DffDiagnostics& diag, DffClump& clump, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_CLUMP);
    const RwUInt32 startPos = stream.GetPosition();

    clump.frames.clear();
    clump.geometries.clear();
    clump.atomics.clear();
    clump.lights.clear();
    clump.cameras.clear();
    clump.extensions.Clear();

    // ---- STRUCT ------------------------------------------------------------
    RwInt32 numAtomics = 0;
    RwInt32 numLights = 0;
    RwInt32 numCameras = 0;
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated CLUMP STRUCT header");
        }
        if (header.type != rwID_STRUCT) {
            return diag.FailChunkType(headerPos, rwID_STRUCT, header.type);
        }

        const RwUInt32 dataStart = stream.GetPosition();
        if (!stream.ReadInt32(&numAtomics) || numAtomics < 0) {
            return diag.Fail(dataStart, "invalid atomic count");
        }

        // RW 3.3 added the light/camera counts (baclump.c ClumpStreamGetSize).
        clump.structHasLightCameraCounts = (header.size >= 12);
        if (clump.structHasLightCameraCounts) {
            if (!stream.ReadInt32(&numLights) || !stream.ReadInt32(&numCameras) || numLights < 0 || numCameras < 0) {
                return diag.Fail(stream.GetPosition(), "truncated CLUMP light/camera counts");
            }
        }

        if (!clump.structSpan.Record(header.size, stream.GetPosition() - dataStart, header.version ? header.version : chunkVersion, stream)) {
            return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
        }
        stream.Seek(dataStart + header.size);
    }

    // ---- FRAMELIST ---------------------------------------------------------
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated FRAMELIST header");
        }
        if (header.type != rwID_FRAMELIST) {
            return diag.FailChunkType(headerPos, rwID_FRAMELIST, header.type);
        }
        if (!ReadFrameList(stream, diag, clump, header.size, header.version ? header.version : chunkVersion)) {
            return false;
        }
    }

    // ---- GEOMETRYLIST ------------------------------------------------------
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated GEOMETRYLIST header");
        }
        if (header.type != rwID_GEOMETRYLIST) {
            return diag.FailChunkType(headerPos, rwID_GEOMETRYLIST, header.type);
        }
        if (!ReadGeometryList(stream, diag, clump, header.size, header.version ? header.version : chunkVersion)) {
            return false;
        }
    }

    // ---- ATOMICS -----------------------------------------------------------
    clump.atomics.resize(static_cast<size_t>(numAtomics));
    for (RwInt32 i = 0; i < numAtomics; ++i) {
        DffScope atomicScope(diag, rwID_ATOMIC, i);

        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated ATOMIC header");
        }
        if (header.type != rwID_ATOMIC) {
            return diag.FailChunkType(headerPos, rwID_ATOMIC, header.type);
        }
        if (!ReadAtomic(stream, diag, clump.atomics[static_cast<size_t>(i)], header.size,
                        header.version ? header.version : chunkVersion)) {
            return false;
        }
    }

    // ---- CAMERA / LIGHT chunks --------------------------------------------
    // baclump.c writes a frame-index STRUCT before each light, then each camera.
    auto readObjects = [&](auto& objects, RwInt32 count, RwUInt32 type, auto read) {
        objects.resize(static_cast<size_t>(count));
        for (auto& object : objects) {
            RwUInt32 position = stream.GetPosition();
            RwChunkHeader header;
            if (!stream.ReadChunkHeader(header)) {
                return diag.Fail(position, "truncated camera/light frame-index header");
            }
            if (header.type != rwID_STRUCT) {
                return diag.FailChunkType(position, rwID_STRUCT, header.type);
            }
            if (header.size != 4 || !stream.ReadInt32(&object.frameIndex) || object.frameIndex < 0 ||
                static_cast<size_t>(object.frameIndex) >= clump.frames.size()) {
                return diag.Fail(position, "invalid camera/light frame index");
            }
            if (!object.frameIndexSpan.Record(header.size, 4, header.version ? header.version : chunkVersion, stream)) {
                return diag.Fail(stream.GetPosition(), "failed to fingerprint camera/light frame index");
            }
            position = stream.GetPosition();
            if (!stream.ReadChunkHeader(header)) {
                return diag.Fail(position, "truncated camera/light header");
            }
            if (header.type != type) {
                return diag.FailChunkType(position, type, header.type);
            }
            if (!read(stream, diag, object, header.size, header.version ? header.version : chunkVersion)) {
                return false;
            }
        }
        return true;
    };
    if (!readObjects(clump.lights, numLights, rwID_LIGHT, ReadLight) ||
        !readObjects(clump.cameras, numCameras, rwID_CAMERA, ReadCamera)) {
        return false;
    }

    // ---- EXTENSION ---------------------------------------------------------
    {
        const RwUInt32 headerPos = stream.GetPosition();
        RwChunkHeader header;
        if (!stream.ReadChunkHeader(header)) {
            return diag.Fail(headerPos, "truncated CLUMP EXTENSION header");
        }
        if (header.type != rwID_EXTENSION) {
            return diag.FailChunkType(headerPos, rwID_EXTENSION, header.type);
        }
        const RwUInt32 extVersion = header.version ? header.version : chunkVersion;
        if (!ReadExtensionContainer(
                stream, diag, header.size, extVersion, clump.extensions,
                [](const RwChunkHeader&, bool&) -> bool { return true; })) {
            return false;
        }
    }

    if (!clump.clumpSpan.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    return true;
}

bool WriteClump(RwStream& stream, DffDiagnostics& diag, const DffClump& clump, RwUInt32 version)
{
    DffScope scope(diag, rwID_CLUMP);
    if (!clump.structHasLightCameraCounts && (!clump.lights.empty() || !clump.cameras.empty())) {
        return diag.Fail(stream.GetPosition(), "pre-3.3 CLUMP cannot contain cameras/lights");
    }
    ChunkWriter writer(stream, version);

    if (!writer.Begin(rwID_CLUMP, clump.clumpSpan, clump.clumpSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin CLUMP chunk");
    }

    // ---- STRUCT ------------------------------------------------------------
    if (!writer.Begin(rwID_STRUCT, clump.structSpan, clump.structSpan.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin CLUMP STRUCT");
    }
    stream.WriteInt32(static_cast<RwInt32>(clump.atomics.size()));
    if (clump.structHasLightCameraCounts) {
        stream.WriteInt32(static_cast<RwInt32>(clump.lights.size()));
        stream.WriteInt32(static_cast<RwInt32>(clump.cameras.size()));
    }
    writer.End();

    if (!WriteFrameList(stream, diag, clump, version)) {
        return false;
    }
    if (!WriteGeometryList(stream, diag, clump, version)) {
        return false;
    }

    for (size_t i = 0; i < clump.atomics.size(); ++i) {
        DffScope atomicScope(diag, rwID_ATOMIC, static_cast<int>(i));
        if (!WriteAtomic(stream, diag, clump.atomics[i], version)) {
            return false;
        }
    }

    auto writeObjects = [&](const auto& objects, auto write) {
        for (const auto& object : objects) {
            if (!writer.Begin(rwID_STRUCT, object.frameIndexSpan, object.frameIndexSpan.ResolveVersion(version)) ||
                !stream.WriteInt32(object.frameIndex) || !writer.End()) {
                return diag.Fail(stream.GetPosition(), "failed to write camera/light frame index");
            }
            if (!write(stream, diag, object, version)) {
                return false;
            }
        }
        return true;
    };
    if (!writeObjects(clump.lights, WriteLight) || !writeObjects(clump.cameras, WriteCamera)) {
        return false;
    }

    // ---- EXTENSION ---------------------------------------------------------
    if (!writer.Begin(rwID_EXTENSION, clump.extensions.span, clump.extensions.span.ResolveVersion(version))) {
        return diag.Fail(stream.GetPosition(), "failed to begin CLUMP EXTENSION");
    }
    {
        RawChunkCursor rawCursor(clump.extensions.raws);
        for (RwUInt32 type : clump.extensions.order) {
            const RawChunk* raw = rawCursor.Next(type);
            if (!raw) {
                return diag.Fail(stream.GetPosition(),
                                 "clump extension order/raw mismatch",
                                 RwChunkTypeLabel(type),
                                 "<missing>");
            }
            writer.WriteRaw(*raw);
        }
        if (clump.extensions.order.empty()) {
            for (const auto& raw : clump.extensions.raws) {
                writer.WriteRaw(raw);
            }
        } else if (!rawCursor.Exhausted()) {
            return diag.Fail(stream.GetPosition(), "clump extension list not fully consumed");
        }
    }
    writer.End();  // EXTENSION

    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close CLUMP chunk");
}
