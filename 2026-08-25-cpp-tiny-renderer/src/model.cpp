#include "model.hpp"

#include <sstream>

namespace {

// "1", "1/2", "1/2/3", and "1//3" all name vertex index 1 -- only the
// first slash-separated field (the position index) matters here since
// this renderer doesn't use texture or normal indices from the file.
int leading_index(const std::string& token) {
    return std::stoi(token.substr(0, token.find('/')));
}

}  // namespace

bool Model::load(std::istream& in) {
    verts_.clear();
    faces_.clear();
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "v") {
            float x, y, z;
            ls >> x >> y >> z;
            verts_.push_back({x, y, z});
        } else if (tag == "f") {
            std::string a, b, c;
            ls >> a >> b >> c;
            if (a.empty() || b.empty() || c.empty()) continue;
            // OBJ vertex indices are 1-based.
            faces_.push_back({leading_index(a) - 1, leading_index(b) - 1, leading_index(c) - 1});
        }
        // Every other tag (comments, vt/vn, o/g/s groups) is irrelevant
        // to a bare position-only mesh and is silently skipped.
    }
    return !verts_.empty();
}

std::array<Vec3f, 3> Model::face_verts(int f) const {
    const auto& idx = faces_[f];
    return {verts_[idx[0]], verts_[idx[1]], verts_[idx[2]]};
}

Vec3f Model::face_normal(int f) const {
    auto v = face_verts(f);
    return (v[1] - v[0]).cross(v[2] - v[0]).normalized();
}
