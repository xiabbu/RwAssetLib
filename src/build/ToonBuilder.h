/**
 * build/ToonBuilder.h - regenerates RpToonGeo data from a geometry.
 *
 * Follows RenderWare rttoon.cpp: exact position welding, incident-edge order,
 * extrusion normals and post-stripify triangle/face remapping. Creases and
 * curvature thickness are explicit editing options, disabled by default.
 */

#pragma once

#include "world/Geometry.h"

#include <string>

struct DffSkinVertexBuffer;

class ToonBuilder
{
public:
    /** Dihedral angle above which an edge is treated as a crease. */
    void SetCreaseAngleDegrees(float degrees) { m_creaseAngleDegrees = degrees; }
    /** Scales the curvature-derived per-vertex ink thickness. */
    void SetThicknessScale(float scale) { m_thicknessScale = scale; }

    void SetDefaultInkName(const std::string& name) { m_defaultInkName = name; }
    void SetCreaseInkName(const std::string& name) { m_creaseInkName = name; }
    void SetDefaultPaintName(const std::string& name) { m_defaultPaintName = name; }

    // Bytes after the paint-name NUL are historical allocation residue.
    void SetDefaultPaintPadding(const std::vector<RwUInt8>& padding) { m_defaultPaintPadding = padding; }
    void SetMaterialOrder(const std::vector<RwInt32>& order) { m_materialOrder = order; }
    void SetVertexThicknesses(const std::vector<RwReal>& thicknesses) { m_vertexThicknesses = thicknesses; }
    void SetRawFaceNormals(bool raw) { m_rawFaceNormals = raw; }
    void SetVertexExtrusionNormals(bool useVertexNormals) { m_vertexExtrusionNormals = useVertexNormals; }

    /**
     * Builds toon data for `geom`. Returns false when the geometry has no
     * usable base morph target, a triangle references a missing vertex, or
     * authored thicknesses do not match the vertex count, or vertex-buffer
     * extrusion mode has no per-vertex normals.
     */
    bool Build(const DffGeometry& geom, DffToonGeo& out,
               const DffSkinVertexBuffer* vertexBuffer = nullptr) const;

private:
    std::vector<RwInt32> m_materialOrder;
    std::vector<RwUInt8> m_defaultPaintPadding;
    std::vector<RwReal> m_vertexThicknesses;
    float       m_creaseAngleDegrees = 180.0f;
    float       m_thicknessScale = 0.0f;
    bool        m_rawFaceNormals = false;
    bool        m_vertexExtrusionNormals = false;
    std::string m_defaultInkName = "silhouette";
    std::string m_creaseInkName = "Default Crease Ink Name";
    std::string m_defaultPaintName = "default paint name";
};
