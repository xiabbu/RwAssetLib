/**
 * world/Texture.h - RwTexture reference (rwID_TEXTURE, 0x06).
 *
 * Stream layout (RenderWare babintex.c, RwTextureStreamWrite):
 *
 *   STRUCT    { RwUInt32 filterAndAddress }
 *   STRING    name
 *   STRING    mask
 *   EXTENSION plugin chunks
 *
 * filterAndAddress packing (rwcore.h):
 *   bits  0..7  filter mode        (rwTEXTUREFILTERMODEMASK)
 *   bits  8..11 addressing U       (rwTEXTUREADDRESSINGUMASK)
 *   bits 12..15 addressing V       (rwTEXTUREADDRESSINGVMASK)
 *   bits 16..23 stream flags       (rwTEXTURESTREAMFLAGSUSERMIPMAPS)
 *
 * STRING chunks are padded to a 4-byte boundary by _rwStringStreamGetSize, and
 * _rwStringStreamWrite then writes the *whole* padded span straight out of the
 * texture's fixed-size name buffer. Everything after the NUL is therefore
 * uninitialised process memory (DBO files are full of the MSVC 0xCD debug fill).
 * That tail is unreproducible, so the raw bytes are kept verbatim.
 */

#pragma once

#include "core/DffError.h"
#include "core/RwChunk.h"
#include "plugin/UserData.h"
#include "world/Extension.h"

#include <string>
#include <vector>

struct DffTexture
{
    std::string name;
    std::string maskName;

    /// Raw STRING payloads including the 4-byte-aligned uninitialised tail.
    std::vector<RwUInt8> rawNameBytes;
    std::vector<RwUInt8> rawMaskBytes;

    RwUInt32 filterAndAddress = 0;

    RwUInt8 FilterMode() const   { return static_cast<RwUInt8>(filterAndAddress & 0xFF); }
    RwUInt8 AddressingU() const  { return static_cast<RwUInt8>((filterAndAddress >> 8) & 0x0F); }
    RwUInt8 AddressingV() const  { return static_cast<RwUInt8>((filterAndAddress >> 12) & 0x0F); }
    RwUInt8 StreamFlags() const  { return static_cast<RwUInt8>((filterAndAddress >> 16) & 0xFF); }

    void SetFilterMode(RwUInt8 v)  { filterAndAddress = (filterAndAddress & ~0xFFu) | v; }
    void SetAddressingU(RwUInt8 v) { filterAndAddress = (filterAndAddress & ~0x0F00u) | (static_cast<RwUInt32>(v & 0x0F) << 8); }
    void SetAddressingV(RwUInt8 v) { filterAndAddress = (filterAndAddress & ~0xF000u) | (static_cast<RwUInt32>(v & 0x0F) << 12); }

    /// Replaces the name and drops the raw buffer so it is re-encoded on write.
    void SetName(const std::string& n) { name = n; rawNameBytes.clear(); }
    void SetMaskName(const std::string& n) { maskName = n; rawMaskBytes.clear(); }

    // Extension container
    DffExtensionList             extensions;
    std::vector<DffUserDataList> userDataLists;

    // Declared-size bookkeeping for the STRUCT / STRING / TEXTURE chunks.
    RwChunkSpan textureSpan;
    RwChunkSpan structSpan;
    RwChunkSpan nameSpan;
    RwChunkSpan maskSpan;
};

/** Reads a TEXTURE payload (TEXTURE header already consumed). */
bool ReadTexture(RwStream& stream, DffDiagnostics& diag, DffTexture& tex, RwUInt32 payloadSize, RwUInt32 chunkVersion);

/** Writes a complete TEXTURE chunk (header + payload). */
bool WriteTexture(RwStream& stream, DffDiagnostics& diag, const DffTexture& tex, RwUInt32 version);

/** Encodes a RW STRING payload: NUL-terminated, padded to 4 bytes. */
std::vector<RwUInt8> EncodeRwString(const std::string& s);

// Reproduce the MSVC debug-heap tail after a texture mask's terminating NUL.
std::vector<RwUInt8> EncodeMsvcDebugRwString(const std::string& s);
