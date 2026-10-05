/**
 * plugin/Collis.cpp - RpCollision geometry extension read/write.
 */

#include "plugin/Collis.h"

bool ReadCollis(RwStream& stream, DffDiagnostics& diag, DffCollis& out, RwUInt32 payloadSize, RwUInt32 chunkVersion)
{
    DffScope scope(diag, rwID_COLLISPLUGIN);

    const RwUInt32 startPos = stream.GetPosition();
    const RwUInt32 endPos = startPos + payloadSize;

    out = DffCollis();
    out.chunkVersion = chunkVersion;

    auto bail = [&](const char* msg) {
        stream.Seek(endPos);
        return diag.Fail(startPos, msg);
    };

    if (!stream.ReadUInt32(&out.internalVersion)) {
        return bail("truncated collision internal version");
    }

    RwChunkHeader treeHeader;
    if (!stream.ReadChunkHeader(treeHeader)) {
        return bail("truncated COLLTREE header");
    }
    if (treeHeader.type != rwID_COLLTREE) {
        stream.Seek(endPos);
        return diag.FailChunkType(startPos, rwID_COLLTREE, treeHeader.type);
    }
    out.tree.chunkVersion = treeHeader.version;

    if (payloadSize != sizeof(RwUInt32) + rwCHUNKHEADERSIZE + treeHeader.size) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "collision payload size disagrees with COLLTREE size",
                         std::to_string(sizeof(RwUInt32) + rwCHUNKHEADERSIZE + treeHeader.size),
                         std::to_string(payloadSize));
    }

    RwChunkHeader structHeader;
    if (!stream.ReadChunkHeader(structHeader)) {
        return bail("truncated COLLTREE STRUCT header");
    }
    if (structHeader.type != rwID_STRUCT) {
        stream.Seek(endPos);
        return diag.FailChunkType(startPos, rwID_STRUCT, structHeader.type);
    }
    if (treeHeader.size != rwCHUNKHEADERSIZE + structHeader.size) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "COLLTREE size disagrees with its STRUCT size",
                         std::to_string(rwCHUNKHEADERSIZE + structHeader.size),
                         std::to_string(treeHeader.size));
    }
    out.tree.structChunkVersion = structHeader.version;

    const bool headerOk = stream.ReadUInt32(&out.tree.flags) &&
                          stream.ReadV3d(&out.tree.inf) &&
                          stream.ReadV3d(&out.tree.sup) &&
                          stream.ReadUInt32(&out.tree.numEntries) &&
                          stream.ReadUInt32(&out.tree.numSplits);
    if (!headerOk) {
        return bail("truncated collision tree header");
    }

    RwUInt32 expectedStructSize = kCollTreeStructFixedSize + out.tree.numSplits * kCollSplitSize;
    if (out.tree.flags & kCollTreeFlagHasMap) {
        expectedStructSize += out.tree.numEntries * sizeof(RwInt16);
    }
    if (expectedStructSize != structHeader.size) {
        stream.Seek(endPos);
        return diag.Fail(startPos,
                         "collision STRUCT size disagrees with split/entry counts",
                         std::to_string(expectedStructSize),
                         std::to_string(structHeader.size));
    }

    auto readSector = [&](DffCollSector& s) {
        return stream.ReadUInt8(&s.type) && stream.ReadUInt8(&s.contents) &&
               stream.ReadUInt16(&s.index) && stream.ReadReal(&s.value);
    };

    out.tree.splits.resize(static_cast<size_t>(out.tree.numSplits));
    for (RwUInt32 i = 0; i < out.tree.numSplits; ++i) {
        auto& split = out.tree.splits[static_cast<size_t>(i)];
        if (!readSector(split.left) || !readSector(split.right)) {
            return bail("truncated collision split");
        }
    }

    if (out.tree.flags & kCollTreeFlagHasMap) {
        out.tree.map.resize(static_cast<size_t>(out.tree.numEntries));
        for (RwUInt32 i = 0; i < out.tree.numEntries; ++i) {
            if (!stream.ReadInt16(&out.tree.map[static_cast<size_t>(i)])) {
                return bail("truncated collision index map");
            }
        }
    }

    if (!out.span.Record(payloadSize, stream.GetPosition() - startPos, chunkVersion, stream)) {
        return diag.Fail(stream.GetPosition(), "failed to fingerprint chunk content");
    }
    stream.Seek(endPos);
    return true;
}

bool WriteCollis(RwStream& stream, DffDiagnostics& diag, const DffCollis& collis, RwUInt32 version)
{
    DffScope scope(diag, rwID_COLLISPLUGIN);

    if (collis.tree.splits.size() != static_cast<size_t>(collis.tree.numSplits)) {
        return diag.Fail(stream.GetPosition(), "collision split array does not match numSplits");
    }
    if ((collis.tree.flags & kCollTreeFlagHasMap) &&
        collis.tree.map.size() != static_cast<size_t>(collis.tree.numEntries)) {
        return diag.Fail(stream.GetPosition(), "collision map array does not match numEntries");
    }

    const RwUInt32 chunkVersion = collis.chunkVersion ? collis.chunkVersion : version;
    ChunkWriter writer(stream, chunkVersion);

    if (!writer.Begin(rwID_COLLISPLUGIN, collis.span, chunkVersion)) {
        return diag.Fail(stream.GetPosition(), "failed to begin COLLIS chunk");
    }

    stream.WriteUInt32(collis.internalVersion);

    if (!writer.BeginVersioned(rwID_COLLTREE, collis.tree.chunkVersion ? collis.tree.chunkVersion : version)) {
        return diag.Fail(stream.GetPosition(), "failed to begin COLLTREE chunk");
    }
    if (!writer.BeginVersioned(rwID_STRUCT, collis.tree.structChunkVersion ? collis.tree.structChunkVersion : version)) {
        return diag.Fail(stream.GetPosition(), "failed to begin COLLTREE STRUCT");
    }

    stream.WriteUInt32(collis.tree.flags);
    stream.WriteV3d(collis.tree.inf);
    stream.WriteV3d(collis.tree.sup);
    stream.WriteUInt32(collis.tree.numEntries);
    stream.WriteUInt32(collis.tree.numSplits);

    auto writeSector = [&](const DffCollSector& s) {
        stream.WriteUInt8(s.type);
        stream.WriteUInt8(s.contents);
        stream.WriteUInt16(s.index);
        stream.WriteReal(s.value);
    };

    for (const auto& split : collis.tree.splits) {
        writeSector(split.left);
        writeSector(split.right);
    }

    if (collis.tree.flags & kCollTreeFlagHasMap) {
        for (RwInt16 v : collis.tree.map) {
            stream.WriteInt16(v);
        }
    }

    writer.End();  // STRUCT
    writer.End();  // COLLTREE
    return writer.End() ? true : diag.Fail(stream.GetPosition(), "failed to close COLLIS chunk");
}
