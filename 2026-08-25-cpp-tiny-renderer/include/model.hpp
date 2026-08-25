#pragma once

#include <array>
#include <istream>
#include <string>
#include <vector>

#include "geometry.hpp"

// A triangle mesh loaded from Wavefront .obj: just "v x y z" vertex lines
// and "f a b c" face lines (triangles only -- no quads, no texture/normal
// indices). That covers every model this project renders.
class Model {
public:
    // Returns false (leaving the model empty) if the stream has no
    // vertices, rather than throwing -- a caller that forgets to check a
    // file path typo gets an empty, harmless model instead of a crash deep
    // inside the rasterizer.
    bool load(std::istream& in);

    int num_verts() const { return static_cast<int>(verts_.size()); }
    int num_faces() const { return static_cast<int>(faces_.size()); }
    const Vec3f& vert(int i) const { return verts_[i]; }
    // The three vertex positions of face f, in file order.
    std::array<Vec3f, 3> face_verts(int f) const;

    // Outward normal via the right-hand rule over the face's winding
    // order: (v1-v0) x (v2-v0). Degenerate (zero-area) faces normalize to
    // the zero vector rather than NaN -- see Vec3f::normalized().
    Vec3f face_normal(int f) const;

private:
    std::vector<Vec3f> verts_;
    std::vector<std::array<int, 3>> faces_;  // 0-based vertex indices
};
