/**
 * core/RwTypes.h - RenderWare 3.7 base types and stream-format constants.
 *
 * Field-for-field extraction of the types used by the DFF/ANM stream formats.
 * Cross-checked against:
 *   RenderWare babinary.c   (chunk header layout, version encoding)
 *   RenderWare baclump.c         (clump/atomic/geometry stream structs)
 *   RenderWare bamateri.c        (material stream struct)
 *   RenderWare rwplcore.h      (rwID_*, MAKECHUNKID)
 *   DBO NTL Toon plugin         (DBO NTL customisation)
 */

#pragma once

#include <cstdint>
#include <cstring>

// ============================================================================
// Scalar types (RenderWare is a 32-bit little-endian x86 format)
// ============================================================================

typedef std::int8_t   RwInt8;
typedef std::uint8_t  RwUInt8;
typedef std::int16_t  RwInt16;
typedef std::uint16_t RwUInt16;
typedef std::int32_t  RwInt32;
typedef std::uint32_t RwUInt32;
typedef std::int64_t  RwInt64;
typedef std::uint64_t RwUInt64;
typedef float         RwReal;
typedef std::int32_t  RwBool;
typedef char          RwChar;

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

// ============================================================================
// Vectors / colours / matrices
// ============================================================================

struct RwV2d
{
    RwReal x = 0, y = 0;

    RwV2d() = default;
    RwV2d(RwReal x_, RwReal y_) : x(x_), y(y_) {}
};

struct RwV3d
{
    RwReal x = 0, y = 0, z = 0;

    RwV3d() = default;
    RwV3d(RwReal x_, RwReal y_, RwReal z_) : x(x_), y(y_), z(z_) {}
};

struct RwV4d
{
    RwReal x = 0, y = 0, z = 0, w = 0;

    RwV4d() = default;
    RwV4d(RwReal x_, RwReal y_, RwReal z_, RwReal w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct RwRGBA
{
    RwUInt8 red = 255, green = 255, blue = 255, alpha = 255;

    RwRGBA() = default;
    RwRGBA(RwUInt8 r, RwUInt8 g, RwUInt8 b, RwUInt8 a = 255) : red(r), green(g), blue(b), alpha(a) {}
};

struct RwRGBAReal
{
    RwReal red = 1.0f, green = 1.0f, blue = 1.0f, alpha = 1.0f;
};

/**
 * RwMatrix - RenderWare 4x3 affine transform.
 *
 * Streamed layout is the full in-memory struct (64 bytes): each 3-float axis is
 * followed by a 4-byte pad word. RpSkin's inverse-bone-matrix array relies on
 * that exact layout, so the pads are preserved verbatim.
 */
struct RwMatrix
{
    RwV3d    right;
    RwUInt32 flags = 0;
    RwV3d    up;
    RwUInt32 pad1 = 0;
    RwV3d    at;
    RwUInt32 pad2 = 0;
    RwV3d    pos;
    RwUInt32 pad3 = 0;

    RwMatrix() { SetIdentity(); }

    void SetIdentity()
    {
        right = RwV3d(1, 0, 0);
        up    = RwV3d(0, 1, 0);
        at    = RwV3d(0, 0, 1);
        pos   = RwV3d(0, 0, 0);
        flags = pad1 = pad2 = pad3 = 0;
    }
};

struct RwSphere
{
    RwV3d  center;
    RwReal radius = 0;
};

struct RwBBox
{
    RwV3d sup;
    RwV3d inf;
};

struct RwSurfaceProperties
{
    RwReal ambient  = 1.0f;
    RwReal specular = 1.0f;
    RwReal diffuse  = 1.0f;
};

struct RwTexCoords
{
    RwReal u = 0, v = 0;

    RwTexCoords() = default;
    RwTexCoords(RwReal u_, RwReal v_) : u(u_), v(v_) {}
};

struct RwMatrixWeights
{
    RwReal w0 = 0, w1 = 0, w2 = 0, w3 = 0;
};

// ============================================================================
// Chunk IDs (rwplcore.h)
// ============================================================================

enum RwCoreChunkID : RwUInt32
{
    rwID_NAOBJECT         = 0x00,
    rwID_STRUCT           = 0x01,
    rwID_STRING           = 0x02,
    rwID_EXTENSION        = 0x03,
    rwID_CAMERA           = 0x05,
    rwID_TEXTURE          = 0x06,
    rwID_MATERIAL         = 0x07,
    rwID_MATLIST          = 0x08,
    rwID_ATOMICSECT       = 0x09,
    rwID_PLANESECT        = 0x0A,
    rwID_WORLD            = 0x0B,
    rwID_SPLINE           = 0x0C,
    rwID_MATRIX           = 0x0D,
    rwID_FRAMELIST        = 0x0E,
    rwID_GEOMETRY         = 0x0F,
    rwID_CLUMP            = 0x10,
    rwID_LIGHT            = 0x12,
    rwID_UNICODESTRING    = 0x13,
    rwID_ATOMIC           = 0x14,
    rwID_TEXTURENATIVE    = 0x15,
    rwID_TEXDICTIONARY    = 0x16,
    rwID_ANIMDATABASE     = 0x17,
    rwID_IMAGE            = 0x18,
    rwID_SKINANIMATION    = 0x19,
    rwID_GEOMETRYLIST     = 0x1A,
    rwID_ANIMANIMATION    = 0x1B,
    rwID_HANIMANIMATION   = 0x1B,
    rwID_TEAM             = 0x1C,
    rwID_CROWD            = 0x1D,
    rwID_DMORPHANIMATION  = 0x1E,
    rwID_RIGHTTORENDER    = 0x1F,
    rwID_MTEFFECTNATIVE   = 0x20,
    rwID_MTEFFECTDICT     = 0x21,
    rwID_TEAMDICT         = 0x22,
    rwID_PITEXDICTIONARY  = 0x23,
    rwID_TOC              = 0x24,
    rwID_PRTSTDGLOBALDATA = 0x25,
    rwID_ALTPIPE          = 0x26,
    rwID_PIPEDS           = 0x27,
    rwID_PATCHMESH        = 0x28,
    rwID_CHUNKGROUPSTART  = 0x29,
    rwID_CHUNKGROUPEND    = 0x2A,
    rwID_UVANIMDICT       = 0x2B,
    rwID_COLLTREE         = 0x2C,
};

/**
 * Plugin chunk IDs.
 *
 * All of these are MAKECHUNKID(rwVENDORID_CRITERIONTK=0x01, id), i.e. 0x100 | id.
 * DBO reuses the same vendor space for its two custom plugins
 * (see DBO NtlMaterialExt.h).
 */
enum RwPluginChunkID : RwUInt32
{
    rwID_MORPHPLUGIN    = 0x105,
    rwID_LTMAPPLUGIN    = 0x114,
    rwID_SKINPLUGIN     = 0x116,
    rwID_COLLISPLUGIN   = 0x11D,
    rwID_HANIMPLUGIN    = 0x11E,
    rwID_USERDATAPLUGIN = 0x11F,
    rwID_MATFXPLUGIN    = 0x120,
    rwID_TOONPLUGIN     = 0x12E,
    rwID_UVANIMPLUGIN   = 0x135,

    rwID_BINMESHPLUGIN  = 0x50E,
    rwID_NABORPLUGIN    = 0x50F,

    // DBO custom (Ntl_Plugin_Toon)
    rwID_NTL_MATERIAL_EXT   = 0x1FC,
    rwID_NTL_WORLD_MATERIAL = 0x177,

    // DBO custom atomic plugin (DBO NtlAtomic.cpp):
    // MAKECHUNKID(rwVENDORID_CRITERIONRM = 0x07, PluginID = 0xFE).
    rwID_NTL_ATOMIC_EXT     = 0x7FE,
};

/**
 * ENtlAtomicFlag - per-atomic render flags carried by rwID_NTL_ATOMIC_EXT.
 *
 * Verbatim from DBO NtlAtomic.h.
 */
enum ENtlAtomicFlag : RwUInt32
{
    NTL_ATOMIC_FLAG_INVALID = 0x00000000,

    NTL_TOON_NOT_EDGE       = 0x00000001,  ///< silhouette edges are not rendered
    NTL_TOON_DETAIL_EDGE    = 0x00000002,  ///< render the detailed per-vertex edge set

    NTL_TWOSIDE             = 0x00000010,
    NTL_ALPHATEST           = 0x00000020,
    NTL_ALPHA               = 0x00000040,

    NTL_COLLISION           = 0x00001000,
    NTL_NOT_VISIBLE         = 0x00002000,
    NTL_DECAL_VISIBLE       = 0x00004000,
    NTL_SHADOW_MAP          = 0x00008000,
    NTL_RUNTIME_ALPHA       = 0x00010000,
    NTL_CAMERA_COLLISION    = 0x00020000,  ///< only meaningful with NTL_NOT_VISIBLE
    NTL_TOON_EDGE_OFF       = 0x00040000,
    NTL_ENVMAP_TEX          = 0x00080000,  ///< _EnvTexName holds an environment map id
    NTL_EMBLEM_MARK         = 0x00100000,

    NTL_FINAL_SORT          = (NTL_ALPHA | NTL_RUNTIME_ALPHA),
};

// ============================================================================
// RpGeometry format flags (rpworld.h)
// ============================================================================

enum RpGeometryFlag : RwUInt32
{
    rpGEOMETRYTRISTRIP              = 0x00000001,
    rpGEOMETRYPOSITIONS             = 0x00000002,
    rpGEOMETRYTEXTURED              = 0x00000004,
    rpGEOMETRYPRELIT                = 0x00000008,
    rpGEOMETRYNORMALS               = 0x00000010,
    rpGEOMETRYLIGHT                 = 0x00000020,
    rpGEOMETRYMODULATEMATERIALCOLOR = 0x00000040,
    rpGEOMETRYTEXTURED2             = 0x00000080,
    rpGEOMETRYNATIVE                = 0x01000000,
    rpGEOMETRYNATIVEINSTANCE        = 0x02000000,

    // Number of texture-coordinate sets lives in bits 16..23.
    rpGEOMETRYTEXCOORDSETS_MASK  = 0x00FF0000,
    rpGEOMETRYTEXCOORDSETS_SHIFT = 16,
};

// ============================================================================
// RpHAnim (rphanim.h)
// ============================================================================

enum RpHAnimHierarchyFlag : RwUInt32
{
    rpHANIMHIERARCHYSUBHIERARCHY            = 0x0001,
    rpHANIMHIERARCHYNOMATRICES              = 0x0002,
    rpHANIMHIERARCHYUPDATEMODELLINGMATRICES = 0x1000,
    rpHANIMHIERARCHYUPDATELTMS              = 0x2000,
    rpHANIMHIERARCHYLOCALSPACEMATRICES      = 0x4000,
};

enum RpHAnimNodeFlag : RwUInt32
{
    rpHANIMPOPPARENTMATRIX  = 0x01,
    rpHANIMPUSHPARENTMATRIX = 0x02,
};

// ============================================================================
// RpMatFX (rpmatfx.h)
// ============================================================================

enum RpMatFXMaterialFlags : RwInt32
{
    rpMATFXEFFECTNULL            = 0,
    rpMATFXEFFECTBUMPMAP         = 1,
    rpMATFXEFFECTENVMAP          = 2,
    rpMATFXEFFECTBUMPENVMAP      = 3,
    rpMATFXEFFECTDUAL            = 4,
    rpMATFXEFFECTUVTRANSFORM     = 5,
    rpMATFXEFFECTDUALUVTRANSFORM = 6,
};

// ============================================================================
// RpMeshHeader (bamesh.h)
// ============================================================================

enum RpMeshHeaderFlags : RwUInt32
{
    rpMESHHEADERTRISTRIP  = 0x0001,
    rpMESHHEADERTRIFAN    = 0x0002,
    rpMESHHEADERLINELIST  = 0x0004,
    rpMESHHEADERPOLYLINE  = 0x0008,
    rpMESHHEADERPOINTLIST = 0x0010,
    rpMESHHEADERPRIMMASK  = 0x00FF,
    rpMESHHEADERUNINDEXED = 0x0100,
};

// ============================================================================
// RpUserData formats (rpuserdt.h)
// ============================================================================

enum RpUserDataFormat : RwInt32
{
    rpNAUSERDATAFORMAT     = 0,
    rpINTUSERDATA          = 1,
    rpREALUSERDATA         = 2,
    rpSTRINGUSERDATA       = 3,
    rpUSERDATAFORMATCOUNT  = 4,
};

// ============================================================================
// DBO NTL material flags (NtlMaterialExt.h)
// ============================================================================

enum RpNtlMatFlag : RwUInt32
{
    rpNTL_MATERIAL_INVALID          = 0x00000000,
    rpNTL_MATERIAL_SKIN_COLOR       = 0x00000001,
    rpNTL_MATERIAL_HEAD_COLOR       = 0x00000002,
    rpNTL_MATERIAL_ADD_COLOR        = 0x00000004,
    rpNTL_MATERIAL_BELT_COLOR       = 0x00000008,
    rpNTL_MATERIAL_UV_MATRIX        = 0x00000010,
    rpNTL_MATERIAL_ENV_MAP          = 0x00000020,
    rpNTL_MATERIAL_EMUV             = 0x00000040,
    rpNTL_MATERIAL_MIXED            = 0x00000080,
    rpNTL_MATERIAL_EMBLEM           = 0x00000100,
    rpNTL_MATERIAL_SIMPLEMATERIAL   = 0x00000200,
    rpNTL_MATERIAL_DXT1_ALPHA_CHECK = 0x00000400,
    rpNTL_MATERIAL_DOGI_PANTS       = 0x00000800,
    rpNTL_MATERIAL_PETRIFY          = 0x00001000,
    rpNTL_MATERIAL_DOGI_SKIRT       = 0x00002000,
};

// ============================================================================
// Toon (rptoon.h)
// ============================================================================

// RPTOON_INTERNALTOONGEOSTREAMVERSION (toongeo.c) - identical in RW 3.7 and in
// DBO's Ntl_Plugin_Toon fork.
constexpr RwInt32 rpTOONGEOSTREAMVERSION = static_cast<RwInt32>(0xabcd0002u);
// rpToonMaterialVersionStamp (toonmaterial.c). Note this differs from the
// geometry stamp; the material reader is version-gated on it.
constexpr RwInt32 rpTOONMATERIALVERSIONSTAMP = static_cast<RwInt32>(0x67890002u);
constexpr RwInt32 rpTOONMATERIALVERSIONSTAMP_SILHOUETTEINKIDS = static_cast<RwInt32>(0x67890002u);

// rwTEXTUREBASENAMELENGTH - the fixed field width used for toon ink/paint names.
constexpr int rpTOONINKNAMELEN   = 32;
constexpr int rpTOONPAINTNAMELEN = 32;

// ============================================================================
// Version helpers (RenderWare rwversion.h)
// ============================================================================
//
// The 32-bit word in every chunk header is a *library ID stamp*, not a version.
// Bit layout (MSB first) per rwversion.h:
//
//     2  SDK version (zero is RW3)
//     4  major revision
//     4  minor revision
//     6  binary format revision
//    16  library build number
//
// RWLIBRARYIDPACK / RWLIBRARYIDUNPACKVERSION convert between that stamp and the
// packed version word (0x37002 == RW 3.7.0.2).

/** RWLIBRARYIDUNPACKVERSION: library ID stamp -> packed version word. */
inline RwUInt32 RwLibraryIDUnpackVersion(RwUInt32 libraryID)
{
    return (((libraryID >> 14) & 0x3FF00u) + 0x30000u) | ((libraryID >> 16) & 0x3Fu);
}

/** RWLIBRARYIDUNPACKBUILDNUM. */
inline RwUInt32 RwLibraryIDUnpackBuildNum(RwUInt32 libraryID)
{
    return libraryID & 0xFFFFu;
}

/** RWLIBRARYIDPACK: packed version word + build number -> library ID stamp. */
inline RwUInt32 RwLibraryIDPack(RwUInt32 version, RwUInt32 buildNum)
{
    return (((version - 0x30000u) & 0x3FF00u) << 14) | ((version & 0x3Fu) << 16) | (buildNum & 0xFFFFu);
}

// Accessors on the *unpacked* version word (0x37002 -> 3, 7, 0, 2).
inline RwUInt32 RwVersionGetMajor(RwUInt32 version) { return (version >> 16) & 0xFu; }
inline RwUInt32 RwVersionGetMinor(RwUInt32 version) { return (version >> 12) & 0xFu; }
inline RwUInt32 RwVersionGetRevision(RwUInt32 version) { return (version >> 8) & 0xFu; }
inline RwUInt32 RwVersionGetBinaryFormat(RwUInt32 version) { return version & 0x3Fu; }

/// Unpacked version word for RW 3.7.0.2, the release DBO shipped against.
constexpr RwUInt32 rwLIBRARYVERSION37002 = 0x37002;

/// Library ID stamp DBO writes into every chunk header: RW 3.7.0.2, build 0x65.
/// Used only for chunks this library creates from scratch; chunks that came from
/// a file always re-emit the stamp they were read with.
constexpr RwUInt32 rwLIBRARYIDDEFAULT = 0x1C020065;

/// Backwards-compatible alias used as the default `version` argument throughout
/// the codec (it is a library ID stamp, matching the chunk header field).
constexpr RwUInt32 rwLIBRARYVERSION37 = rwLIBRARYIDDEFAULT;

// Size of a chunk header on the wire: type + size + version.
constexpr RwUInt32 rwCHUNKHEADERSIZE = 12;
