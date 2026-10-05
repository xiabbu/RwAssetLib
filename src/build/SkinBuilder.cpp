#include "build/SkinBuilder.h"

#include <cassert>

namespace
{

RwReal Dot(const RwV3d& a, const RwV3d& b)
{
    return (a.x * b.x + a.y * b.y) + a.z * b.z;
}

RwMatrix Multiply(const RwMatrix& a, const RwMatrix& b)
{
    auto row = [&](const RwV3d& v) {
        return RwV3d((v.x * b.right.x + v.y * b.up.x) + v.z * b.at.x,
                     (v.x * b.right.y + v.y * b.up.y) + v.z * b.at.y,
                     (v.x * b.right.z + v.y * b.up.z) + v.z * b.at.z);
    };
    RwMatrix out;
    out.right = row(a.right);
    out.up = row(a.up);
    out.at = row(a.at);
    out.pos = row(a.pos);
    out.pos.x += b.pos.x;
    out.pos.y += b.pos.y;
    out.pos.z += b.pos.z;
    return out;
}

RwMatrix Invert(const RwMatrix& m, DffSkinInverseMode mode)
{
    RwMatrix out;
    if (mode == DffSkinInverseMode::Orthonormal) {
        out.right = RwV3d(m.right.x, m.up.x, m.at.x);
        out.up = RwV3d(m.right.y, m.up.y, m.at.y);
        out.at = RwV3d(m.right.z, m.up.z, m.at.z);
        out.pos = RwV3d(-Dot(m.pos, m.right), -Dot(m.pos, m.up), -Dot(m.pos, m.at));
    } else {
        // bamatrix.h rwMat03Inv stores the first cofactor row before the determinant.
        out.right = RwV3d(m.up.y * m.at.z - m.up.z * m.at.y,
                         -(m.right.y * m.at.z - m.right.z * m.at.y),
                          m.right.y * m.up.z - m.right.z * m.up.y);
        const RwReal determinant = (out.right.x * m.right.x + out.right.y * m.up.x)
            + out.right.z * m.at.x;
        const RwReal reciprocal = determinant != 0.0f ? 1.0f / determinant : 1.0f;
        out.right.x *= reciprocal;
        out.right.y *= reciprocal;
        out.right.z *= reciprocal;
        out.up = RwV3d(-(m.up.x * m.at.z - m.up.z * m.at.x) * reciprocal,
                       (m.right.x * m.at.z - m.right.z * m.at.x) * reciprocal,
                      -(m.right.x * m.up.z - m.right.z * m.up.x) * reciprocal);
        out.at = RwV3d((m.up.x * m.at.y - m.up.y * m.at.x) * reciprocal,
                     -(m.right.x * m.at.y - m.right.y * m.at.x) * reciprocal,
                      (m.right.x * m.up.y - m.right.y * m.up.x) * reciprocal);
        out.pos = RwV3d(-((m.pos.x * out.right.x + m.pos.y * out.up.x) + m.pos.z * out.at.x),
                       -((m.pos.x * out.right.y + m.pos.y * out.up.y) + m.pos.z * out.at.y),
                       -((m.pos.x * out.right.z + m.pos.y * out.up.z) + m.pos.z * out.at.z));
    }
    return out;
}

RwV3d Transform(const RwV3d& v, const RwMatrix& m, bool position)
{
    // ssematbl.c: (z * at + pos) + (y * up + x * right).
    const RwV3d xy(v.y * m.up.x + v.x * m.right.x,
                   v.y * m.up.y + v.x * m.right.y,
                   v.y * m.up.z + v.x * m.right.z);
    RwV3d z(v.z * m.at.x, v.z * m.at.y, v.z * m.at.z);
    if (position) {
        z.x += m.pos.x;
        z.y += m.pos.y;
        z.z += m.pos.z;
    }
    return RwV3d(z.x + xy.x, z.y + xy.y, z.z + xy.z);
}

}  // namespace

bool BuildSkinVertexBuffer(const DffClump& clump, const DffGeometry& geom,
                          RwInt32 atomicFrameIndex, DffSkinInverseMode inverseMode,
                          DffSkinVertexBuffer& out,
                          std::string& error)
{
    assert(geom.skin && geom.morphTargets.size() == 1 && geom.HasNormals());
    const auto nodeToFrame = clump.BuildHAnimNodeToFrameMap();
    if (nodeToFrame.size() != geom.skin->invBoneMatrices.size()) {
        error = "Skin binding count differs from the HAnim hierarchy";
        return false;
    }
    for (int frame : nodeToFrame) {
        if (frame < 0) {
            error = "HAnim hierarchy references a missing bone frame";
            return false;
        }
    }
    std::vector<RwMatrix> world(clump.frames.size());
    std::vector<bool> calculated(clump.frames.size());
    auto frameWorld = [&](auto&& self, size_t index) -> const RwMatrix& {
        if (!calculated[index]) {
            const auto& frame = *clump.frames[index];
            world[index] = frame.parentIndex == -1 ? frame.matrix
                : Multiply(frame.matrix, self(self, frame.parentIndex));
            calculated[index] = true;
        }
        return world[index];
    };
    const auto inverseAtomic = Invert(frameWorld(frameWorld, atomicFrameIndex), inverseMode);
    std::vector<RwMatrix> matrices(nodeToFrame.size());
    for (size_t bone = 0; bone < nodeToFrame.size(); ++bone) {
        matrices[bone] = Multiply(Multiply(geom.skin->invBoneMatrices[bone],
            frameWorld(frameWorld, nodeToFrame[bone])), inverseAtomic);
    }
    const auto& target = geom.morphTargets[0];
    out.positions.resize(target.vertices.size());
    out.normals.resize(target.normals.size());
    for (size_t vertex = 0; vertex < out.positions.size(); ++vertex) {
        const auto packed = geom.skin->vertexBoneIndices[vertex];
        RwMatrix matrix = matrices[DffSkin::DecodeBoneIndex(packed, 0)];
        const auto& weights = geom.skin->vertexBoneWeights[vertex];
        if (geom.skin->maxWeights > 1 && weights.w0 < 1.0f) {
            // SSE blends rows before transforming; weights are not normalized here.
            const RwReal slots[] = {weights.w0, weights.w1, weights.w2, weights.w3};
            RwV3d* rows[] = {&matrix.right, &matrix.up, &matrix.at, &matrix.pos};
            for (auto* row : rows) {
                row->x *= slots[0];
                row->y *= slots[0];
                row->z *= slots[0];
            }
            for (int slot = 1; slot < geom.skin->maxWeights && (slot == 1 || slots[slot] > 0.0f); ++slot) {
                const auto& bone = matrices[DffSkin::DecodeBoneIndex(packed, slot)];
                const RwV3d* boneRows[] = {&bone.right, &bone.up, &bone.at, &bone.pos};
                for (size_t row = 0; row < 4; ++row) {
                    rows[row]->x += boneRows[row]->x * slots[slot];
                    rows[row]->y += boneRows[row]->y * slots[slot];
                    rows[row]->z += boneRows[row]->z * slots[slot];
                }
            }
        }
        out.positions[vertex] = Transform(target.vertices[vertex], matrix, true);
        out.normals[vertex] = Transform(target.normals[vertex], matrix, false);
    }
    return true;
}
