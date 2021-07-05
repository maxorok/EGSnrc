/*
###############################################################################
#
#  EGSnrc egs++ mesh geometry library implementation.
#
#  Copyright (C) 2020 Mevex Corporation
#
#  This file is part of EGSnrc.
#
#  Parts of this file, namely, the closest_point_triangle and
#  closest_point_tetrahedron functions, are adapted from Chapter 5 of
#  "Real-Time Collision Detection" by Christer Ericson with the consent
#  of the author and of the publisher.
#
#  EGSnrc is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Affero General Public License as published by the
#  Free Software Foundation, either version 3 of the License, or (at your
#  option) any later version.
#
#  EGSnrc is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
#  more details.
#
#  You should have received a copy of the GNU Affero General Public License
#  along with EGSnrc. If not, see <http://www.gnu.org/licenses/>.
#
###############################################################################
#
#  Authors:          Dave Macrillo,
#                    Matt Ronan,
#                    Nigel Vezeau,
#                    Lou Thompson,
#                    Max Orok
#
###############################################################################
*/

// TODO
#include "egs_input.h"
#include "egs_mesh.h"
#include "egs_vector.h"

#include "mesh_neighbours.h"
#include "msh_parser.h"

#include <cassert>
#include <chrono>
#include <deque>
#include <limits>
#include <unordered_map>

// anonymous namespace
namespace {

inline bool approx_eq(double a, double b, double eps = 1e-5) {
    return (std::abs(a - b) <= eps * (std::abs(a) + std::abs(b) + 1.0));
}

void print_egsvec(const EGS_Vector& v, std::ostream& out = std::cout) {
    out << "{\n  x: " << v.x << "\n  y: " << v.y << "\n  z: " << v.z << "\n}\n";
}

inline EGS_Float dot(const EGS_Vector &x, const EGS_Vector &y) {
    return x * y;
}

inline EGS_Vector cross(const EGS_Vector &x, const EGS_Vector &y) {
    return x.times(y);
}

inline EGS_Float distance2(const EGS_Vector &x, const EGS_Vector &y) {
    return (x - y).length2();
}

EGS_Vector closest_point_triangle(const EGS_Vector &P, const EGS_Vector &A, const EGS_Vector& B, const EGS_Vector& C)
{
    // vertex region A
    EGS_Vector ab = B - A;
    EGS_Vector ac = C - A;
    EGS_Vector ao = P - A;

    EGS_Float d1 = dot(ab, ao);
    EGS_Float d2 = dot(ac, ao);
    if (d1 <= 0.0 && d2 <= 0.0)
        return A;

    // vertex region B
    EGS_Vector bo = P - B;
    EGS_Float d3 = dot(ab, bo);
    EGS_Float d4 = dot(ac, bo);
    if (d3 >= 0.0 && d4 <= d3)
        return B;

    // edge region AB
    EGS_Float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        EGS_Float v = d1 / (d1 - d3);
        return A + v * ab;
    }

    // vertex region C
    EGS_Vector co = P - C;
    EGS_Float d5 = dot(ab, co);
    EGS_Float d6 = dot(ac, co);
    if (d6 >= 0.0 && d5 <= d6)
        return C;

    // edge region AC
    EGS_Float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        EGS_Float w = d2 / (d2 - d6);
        return A + w * ac;
    }

    // edge region BC
    EGS_Float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        EGS_Float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return B + w * (C - B);
    }

    // inside the face
    EGS_Float denom = 1.0 / (va + vb + vc);
    EGS_Float v = vb * denom;
    EGS_Float w = vc * denom;
    return A + v * ab + w * ac;
}

inline bool point_outside_of_plane(EGS_Vector P, EGS_Vector A, EGS_Vector B, EGS_Vector C, EGS_Vector D) {
    return dot(P - A, cross(B - A, C - A)) * dot(D - A, cross(B - A, C - A)) < 0.0;
}

EGS_Vector closest_point_tetrahedron(const EGS_Vector &P, const EGS_Vector &A, const EGS_Vector &B, const EGS_Vector &C, const EGS_Vector &D)
{
    EGS_Vector min_point = P;
    EGS_Float min = std::numeric_limits<EGS_Float>::max();

    auto maybe_update_min_point = [&](const EGS_Vector& A, const EGS_Vector& B, const EGS_Vector& C) {
        EGS_Vector q = closest_point_triangle(P, A, B, C);
        EGS_Float dis = distance2(q, P);
        if (dis < min) {
            min = dis;
            min_point = q;
        }
    };

    if (point_outside_of_plane(P, A, B, C, D)) {
        maybe_update_min_point(A, B, C);
    }

    if (point_outside_of_plane(P, A, C, D, B)) {
        maybe_update_min_point(A, C, D);
    }

    if (point_outside_of_plane(P, A, B, D, C)) {
        maybe_update_min_point(A, B, D);
    }
    if (point_outside_of_plane(P, B, D, C, A)) {
        maybe_update_min_point(B, D, C);
    }

    return min_point;
}

// Inputs:
// * particle position p,
// * normalized velocity v_norm
// * triangle points A, B, C (any ordering)
//
// Returns 1 if there is an intersection and 0 if not. If there is an intersection,
// the out parameter dist will be the distance along v_norm to the intersection point.
//
// Implementation of double-sided Möller-Trumbore ray-triangle intersection
// <http://www.graphics.cornell.edu/pubs/1997/MT97.pdf>
int triangle_ray_intersection(const EGS_Vector &p, const EGS_Vector &v_norm,
    const EGS_Vector& a, const EGS_Vector& b, const EGS_Vector& c, EGS_Float& dist)
{
    const EGS_Float eps = 1e-10;
    EGS_Vector ab = b - a;
    EGS_Vector ac = c - a;

    EGS_Vector pvec = cross(v_norm, ac);
    EGS_Float det = dot(ab, pvec);

    if (det > -eps && det < eps) {
        return 0;
    }
    EGS_Float inv_det = 1.0 / det;
    EGS_Vector tvec = p - a;
    EGS_Float u = dot(tvec, pvec) * inv_det;
    if (u < 0.0 || u > 1.0) {
        return 0;
    }
    EGS_Vector qvec = cross(tvec, ab);
    EGS_Float v = dot(v_norm, qvec) * inv_det;
    if (v < 0.0 || u + v > 1.0) {
        return 0;
    }
    // intersection found
    dist = dot(ac, qvec) * inv_det;
    if (dist < eps) {
        return 0;
    }
    return 1;
}

EGS_Vector centroid(const EGS_Vector& a, const EGS_Vector& b, const EGS_Vector& c,
    const EGS_Vector& d)
{
    return EGS_Vector(
        (a.x + b.x + c.x + d.x) / 4.0,
        (a.y + b.y + c.y + d.y) / 4.0,
        (a.z + b.z + c.z + d.z) / 4.0
    );
}

/// Parse the body of a msh4.1 file into an EGS_Mesh using the msh_parser API.
///
/// Throws a std::runtime_error if parsing fails.
EGS_Mesh* parse_msh41_body(std::istream& input) {
    std::vector<msh_parser::internal::msh41::Node> nodes;
    std::vector<msh_parser::internal::msh41::MeshVolume> volumes;
    std::vector<msh_parser::internal::msh41::PhysicalGroup> groups;
    std::vector<msh_parser::internal::msh41::Tetrahedron> elements;

    std::string parse_err;
    std::string input_line;
    while (std::getline(input, input_line)) {
        msh_parser::internal::rtrim(input_line);
        // stop reading if we hit another mesh file
        if (input_line == "$MeshFormat") {
            break;
        }
        if (input_line == "$Entities") {
           volumes = msh_parser::internal::msh41::parse_entities(input);
        } else if (input_line == "$PhysicalNames") {
            groups = msh_parser::internal::msh41::parse_groups(input);
        } else if (input_line == "$Nodes") {
            nodes = msh_parser::internal::msh41::parse_nodes(input);
        } else if (input_line == "$Elements") {
            elements = msh_parser::internal::msh41::parse_elements(input);
        }
    }
    if (volumes.empty()) {
        throw std::runtime_error("No volumes were parsed from $Entities section");
    }
    if (nodes.empty()) {
        throw std::runtime_error("No nodes were parsed, missing $Nodes section");
    }
    if (groups.empty()) {
        throw std::runtime_error("No groups were parsed from $PhysicalNames section");
    }
    if (elements.empty()) {
        throw std::runtime_error("No tetrahedrons were parsed from $Elements section");
    }

    // ensure each entity has a valid group
    std::unordered_set<int> group_tags;
    group_tags.reserve(groups.size());
    for (auto g: groups) {
        group_tags.insert(g.tag);
    }
    std::unordered_map<int, int> volume_groups;
    volume_groups.reserve(volumes.size());
    for (auto v: volumes) {
        if (group_tags.find(v.group) == group_tags.end()) {
            throw std::runtime_error("volume " + std::to_string(v.tag) + " had unknown physical group tag " + std::to_string(v.group));
        }
        volume_groups.insert({ v.tag, v.group });
    }

    // ensure each element has a valid entity and therefore a valid physical group
    std::vector<int> element_groups;
    element_groups.reserve(elements.size());
    for (auto e: elements) {
        auto elt_group = volume_groups.find(e.volume);
        if (elt_group == volume_groups.end()) {
            throw std::runtime_error("tetrahedron " + std::to_string(e.tag) + " had unknown volume tag " + std::to_string(e.volume));
        }
        element_groups.push_back(elt_group->second);
    }

    std::vector<EGS_Mesh::Tetrahedron> mesh_elts;
    mesh_elts.reserve(elements.size());
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const auto& elt = elements[i];
        mesh_elts.push_back(EGS_Mesh::Tetrahedron(
            element_groups[i], elt.a, elt.b, elt.c, elt.d
        ));
    }

    std::vector<EGS_Mesh::Node> mesh_nodes;
    mesh_nodes.reserve(nodes.size());
    for (const auto& n: nodes) {
        mesh_nodes.push_back(EGS_Mesh::Node(
            n.tag, n.x, n.y, n.z
        ));
    }

    std::vector<EGS_Mesh::Medium> media;
    media.reserve(groups.size());
    for (const auto& g: groups) {
        media.push_back(EGS_Mesh::Medium(g.tag, g.name));
    }

    // TODO: check all 3d physical groups were used by elements
    // TODO: ensure all element node tags are valid
    return new EGS_Mesh(mesh_elts, mesh_nodes, media);
}
} // anonymous namespace

class EGS_Mesh_Octree {
    struct Tet {
        using Point = EGS_Vector;
        Tet(Point a, Point b, Point c, Point d) : a(a), b(b), c(c), d(d) {}
        Point a;
        Point b;
        Point c;
        Point d;
    };
    static double tet_min_x(const std::pair<int, Tet> &p) {
        const auto &t = p.second;
        return std::min(t.a.x, std::min(t.b.x, std::min(t.c.x, t.d.x)));
    }
    static double tet_max_x(const std::pair<int, Tet> &p) {
        const auto &t = p.second;
        return std::max(t.a.x, std::max(t.b.x, std::max(t.c.x, t.d.x)));
    }
    static double tet_min_y(const std::pair<int, Tet> &p) {
        const auto &t = p.second;
        return std::min(t.a.y, std::min(t.b.y, std::min(t.c.y, t.d.y)));
    }
    static double tet_max_y(const std::pair<int, Tet> &p) {
        const auto &t = p.second;
        return std::max(t.a.y, std::max(t.b.y, std::max(t.c.y, t.d.y)));
    }
    static double tet_min_z(const std::pair<int, Tet> &p) {
        const auto &t = p.second;
        return std::min(t.a.z, std::min(t.b.z, std::min(t.c.z, t.d.z)));
    }
    static double tet_max_z(const std::pair<int, Tet> &p) {
        const auto &t = p.second;
        return std::max(t.a.z, std::max(t.b.z, std::max(t.c.z, t.d.z)));
    }

    struct BoundingBox {
        double min_x = 0.0;
        double max_x = 0.0;
        double min_y = 0.0;
        double max_y = 0.0;
        double min_z = 0.0;
        double max_z = 0.0;
        BoundingBox() = default;
        BoundingBox(double min_x, double max_x, double min_y, double max_y,
            double min_z, double max_z) : min_x(min_x), max_x(max_x),
                min_y(min_y), max_y(max_y), min_z(min_z), max_z(max_z) {}
        double mid_x() const {
            return (min_x + max_x) / 2.0;
        }
        double mid_y() const {
            return (min_y + max_y) / 2.0;
        }
        double mid_z() const {
            return (min_z + max_z) / 2.0;
        }
        void print(std::ostream& out = std::cout) const {
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "min_x: " << min_x << "\n";
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "max_x: " << max_x << "\n";
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "min_y: " << min_y << "\n";
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "max_y: " << max_y << "\n";
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "min_z: " << min_z << "\n";
            std::cout <<
                std::setprecision(std::numeric_limits<double>::max_digits10) <<
                    "max_z: " << max_z << "\n";
        }
    };
    struct Node {
        std::vector<int> elts_;
        std::vector<Node> children_;
        BoundingBox bbox_;
        Node() = default;
        Node(const std::vector<std::pair<int, Tet>> &elt_pairs, const BoundingBox& bbox)
            : bbox_(bbox)
        {
            // check if we're running up against precision limits
            bool indivisible =
                approx_eq(bbox_.min_x, bbox_.mid_x()) ||
                approx_eq(bbox_.max_x, bbox_.mid_x()) ||
                approx_eq(bbox_.min_y, bbox_.mid_y()) ||
                approx_eq(bbox_.max_y, bbox_.mid_y()) ||
                approx_eq(bbox_.min_z, bbox_.mid_z()) ||
                approx_eq(bbox_.max_z, bbox_.mid_z());
            if (indivisible || elt_pairs.size() < 1000) {
                //std::cout << "obtained octant with " << elt_pairs.size() << " elts\n";
                //bbox_.print();
                elts_.reserve(elt_pairs.size());
                for (const auto &p : elt_pairs) {
                    elts_.push_back(p.first);
                }
                return;
            }
            // 0 -x-y-z
            // 1 +x-y-z
            // 2 +x+y-z
            // 3 -x+y-z
            // 4 -x-y+z
            // 5 +x-y+z
            // 6 +x+y+z
            // 7 -x+y+z
            std::vector<std::pair<int, Tet>> xlylzl;
            std::vector<std::pair<int, Tet>> xgylzl;
            std::vector<std::pair<int, Tet>> xgygzl;
            std::vector<std::pair<int, Tet>> xlygzl;
            std::vector<std::pair<int, Tet>> xlylzg;
            std::vector<std::pair<int, Tet>> xgylzg;
            std::vector<std::pair<int, Tet>> xgygzg;
            std::vector<std::pair<int, Tet>> xlygzg;

            bool found = false;
            for (const auto& e : elt_pairs) {
                if (tet_min_x(e) <= bbox_.mid_x() &&
                    tet_min_y(e) <= bbox_.mid_y() &&
                    tet_min_z(e) <= bbox_.mid_z())
                {
                    xlylzl.push_back(e);
                    found = true;
                }
                if (tet_max_x(e) > bbox_.mid_x() &&
                    tet_min_y(e) <= bbox_.mid_y() &&
                    tet_min_z(e) <= bbox_.mid_z())
                {
                    xgylzl.push_back(e);
                    found = true;
                }
                if (tet_max_x(e) > bbox_.mid_x() &&
                    tet_max_y(e) > bbox_.mid_y() &&
                    tet_min_z(e) <= bbox_.mid_z())
                {
                    xgygzl.push_back(e);
                    found = true;
                }
                if (tet_min_x(e) <= bbox_.mid_x() &&
                    tet_max_y(e) > bbox_.mid_y() &&
                    tet_min_z(e) <= bbox_.mid_z())
                {
                    xlygzl.push_back(e);
                    found = true;
                }
                if (tet_min_x(e) <= bbox_.mid_x() &&
                    tet_min_y(e) <= bbox_.mid_y() &&
                    tet_max_z(e) > bbox_.mid_z())
                {
                    xlylzg.push_back(e);
                    found = true;
                }
                if (tet_max_x(e) > bbox_.mid_x() &&
                    tet_min_y(e) <= bbox_.mid_y() &&
                    tet_max_z(e) > bbox_.mid_z())
                {
                    xgylzg.push_back(e);
                    found = true;
                }
                if (tet_max_x(e) > bbox_.mid_x() &&
                    tet_max_y(e) > bbox_.mid_y() &&
                    tet_max_z(e) > bbox_.mid_z())
                {
                    xgygzg.push_back(e);
                    found = true;
                }
                if (tet_min_x(e) <= bbox_.mid_x() &&
                    tet_max_y(e) > bbox_.mid_y() &&
                    tet_max_z(e) > bbox_.mid_z())
                {
                    xlygzg.push_back(e);
                    found = true;
                }
                if (!found) {
                    throw std::runtime_error("uncategorized tet " + std::to_string(e.first));
                }
            }
            //std::cout << "size of 0 " << xlylzl.size() << "\n";
            //std::cout << "size of 1 " << xgylzl.size() << "\n";
            //std::cout << "size of 2 " << xgygzl.size() << "\n";
            //std::cout << "size of 3 " << xlygzl.size() << "\n";
            //std::cout << "size of 4 " << xlylzg.size() << "\n";
            //std::cout << "size of 5 " << xgylzg.size() << "\n";
            //std::cout << "size of 6 " << xgygzg.size() << "\n";
            //std::cout << "size of 7 " << xlygzg.size() << "\n";
            children_ = {
                Node(xlylzl, BoundingBox(
                    bbox_.min_x, bbox_.mid_x(),
                    bbox_.min_y, bbox_.mid_y(),
                    bbox_.min_z, bbox_.mid_z()
                )),
                Node(xgylzl, BoundingBox(
                    bbox_.mid_x(), bbox_.max_x,
                    bbox_.min_y, bbox_.mid_y(),
                    bbox_.min_z, bbox_.mid_z()
                )),
                Node(xgygzl, BoundingBox(
                    bbox_.mid_x(), bbox_.max_x,
                    bbox_.mid_y(), bbox_.max_y,
                    bbox_.min_z, bbox_.mid_z()
                )),
                Node(xlygzl, BoundingBox(
                    bbox_.min_x, bbox_.mid_x(),
                    bbox_.mid_y(), bbox_.max_y,
                    bbox_.min_z, bbox_.mid_z()
                )),
                Node(xlylzg, BoundingBox(
                    bbox_.min_x, bbox_.mid_x(),
                    bbox_.min_y, bbox_.mid_y(),
                    bbox_.mid_z(), bbox_.max_z
                )),
                Node(xgylzg, BoundingBox(
                    bbox_.mid_x(), bbox_.max_x,
                    bbox_.min_y, bbox_.mid_y(),
                    bbox_.mid_z(), bbox_.max_z
                )),
                Node(xgygzg, BoundingBox(
                    bbox_.mid_x(), bbox_.max_x,
                    bbox_.mid_y(), bbox_.max_y,
                    bbox_.mid_z(), bbox_.max_z
                )),
                Node(xlygzg, BoundingBox(
                    bbox_.min_x, bbox_.mid_x(),
                    bbox_.mid_y(), bbox_.max_y,
                    bbox_.mid_z(), bbox_.max_z
                ))
            };
        }

        int findStartingOctant(const EGS_Vector &p) const {
            if (p.x <= bbox_.mid_x() && p.x >= bbox_.min_x &&
                p.y <= bbox_.mid_y() && p.y >= bbox_.min_y &&
                p.z <= bbox_.mid_z() && p.z >= bbox_.min_z) {
                //std::cout << "xlylzl\n";
                //std::cout << "selecting octant 0\n";
                return 0;
            }
            if (p.x >= bbox_.mid_x() && p.x <= bbox_.max_x &&
                p.y <= bbox_.mid_y() && p.y >= bbox_.min_y &&
                p.z <= bbox_.mid_z() && p.z >= bbox_.min_z) {
                //std::cout << "xgylzl\n";
                //std::cout << "selecting octant 1\n";
                return 1;
            }
            if (p.x >= bbox_.mid_x() && p.x <= bbox_.max_x &&
                p.y >= bbox_.mid_y() && p.y <= bbox_.max_y &&
                p.z <= bbox_.mid_z() && p.z >= bbox_.min_z) {
                //std::cout << "xgygzl\n";
                //std::cout << "selecting octant 2\n";
                return 2;
            }
            if (p.x <= bbox_.mid_x() && p.x >= bbox_.min_x &&
                p.y >= bbox_.mid_y() && p.y <= bbox_.max_y &&
                p.z <= bbox_.mid_z() && p.z >= bbox_.min_z) {
                //std::cout << "xlygzl\n";
                //std::cout << "selecting octant 3\n";
                return 3;
            }
            if (p.x <= bbox_.mid_x() && p.x >= bbox_.min_x &&
                p.y <= bbox_.mid_y() && p.y >= bbox_.min_y &&
                p.z >= bbox_.mid_z() && p.z <= bbox_.max_z) {
                //std::cout << "xlylzg\n";
                //std::cout << "selecting octant 4\n";
                return 4;
            }
            if (p.x >= bbox_.mid_x() && p.x <= bbox_.max_x &&
                p.y <= bbox_.mid_y() && p.y >= bbox_.min_y &&
                p.z >= bbox_.mid_z() && p.z <= bbox_.max_z) {
                //std::cout << "xgylzg\n";
                //std::cout << "selecting octant 5\n";
                return 5;
            }
            if (p.x >= bbox_.mid_x() && p.x <= bbox_.max_x &&
                p.y >= bbox_.mid_y() && p.y <= bbox_.max_y &&
                p.z >= bbox_.mid_z() && p.z <= bbox_.max_z) {
                //std::cout << "xgygzg\n";
                //std::cout << "selecting octant 6\n";
                return 6;
            }
            if (p.x <= bbox_.mid_x() && p.x >= bbox_.min_x &&
                p.y >= bbox_.mid_y() && p.y <= bbox_.max_y &&
                p.z >= bbox_.mid_z() && p.z <= bbox_.max_z) {
                //std::cout << "xlygzg\n";
                //std::cout << "selecting octant 7\n";
                return 7;
            }
            return -1;
            //std::ostringstream oss;
            //print_egsvec(p, oss);
            //throw std::runtime_error("findTetrahedron: uncategorizable point " + oss.str());
        }

        // Does not mutate the EGS_Mesh.
        int findTetrahedron(const EGS_Vector &p, /*const*/ EGS_Mesh &mesh) const {
            //for (const auto &e: elts_) {
            //    std::cout << e << "\n";
            //}
            if (children_.empty()) {
                for (const auto &e: elts_) {
                 //   std::cout << "looking at element " << e << "\n";
                    if (mesh.insideElement(e, p)) {
                        return e;
                    }
                }
                return -1;
            }
            int octant = findStartingOctant(p);
            if (octant == -1) {
                return -1;
            }
            //std::cout << "starting in octant " << octant << "\n";
            for (int count = 0; count < 8; count++) {
                //std::cout << "looking in octant " << octant << " \n";
                //children_.at(octant).bbox_.print();
                auto elt = children_.at(octant).findTetrahedron(p, mesh);
                if (elt != -1) {
                 //   std::cout << "found elt " << elt << "\n";
                    return elt;
                }
                octant = (octant + 1) % 8;
            }
            return -1;
        }
    };
    Node root_;
public:
    EGS_Mesh_Octree() = default;
    EGS_Mesh_Octree(const std::vector<EGS_Vector> &points) {
        if (points.empty()) {
            throw std::runtime_error("EGS_Mesh_Octree: empty points vector");
        }
        auto num_elts = points.size() / 4;
        if (num_elts > std::numeric_limits<int>::max()) {
            throw std::runtime_error("EGS_Mesh_Octree: num elts must fit into an int");
        }
        std::vector<std::pair<int, Tet>> element_offsets;
        element_offsets.reserve(num_elts);
        for (std::size_t i = 0; i < num_elts; i++) {
            element_offsets.push_back({static_cast<int>(i), Tet(
                points.at(4*i),
                points.at(4*i+1),
                points.at(4*i+2),
                points.at(4*i+3))
            });
        }
        BoundingBox b;
        b.min_x = tet_min_x(*std::min_element(element_offsets.begin(), element_offsets.end(),
            [&](const std::pair<int, Tet> &a, const std::pair<int, Tet> &b) {
                return tet_min_x(a) < tet_min_x(b); }));
        b.max_x = tet_max_x(*std::max_element(element_offsets.begin(), element_offsets.end(),
            [&](const std::pair<int, Tet> &a, const std::pair<int, Tet> &b) {
                return tet_max_x(a) < tet_max_x(b); }));
        b.min_y = tet_min_y(*std::min_element(element_offsets.begin(), element_offsets.end(),
            [&](const std::pair<int, Tet> &a, const std::pair<int, Tet> &b) {
                return tet_min_y(a) < tet_min_y(b); }));
        b.max_y = tet_max_y(*std::max_element(element_offsets.begin(), element_offsets.end(),
            [&](const std::pair<int, Tet> &a, const std::pair<int, Tet> &b) {
                return tet_max_y(a) < tet_max_y(b); }));
        b.min_z = tet_min_z(*std::min_element(element_offsets.begin(), element_offsets.end(),
            [&](const std::pair<int, Tet> &a, const std::pair<int, Tet> &b) {
                return tet_min_z(a) < tet_min_z(b); }));
        b.max_z = tet_max_z(*std::max_element(element_offsets.begin(), element_offsets.end(),
            [&](const std::pair<int, Tet> &a, const std::pair<int, Tet> &b) {
                return tet_max_z(a) < tet_max_z(b); }));
        root_ = Node(element_offsets, b);
    }
    int findTetrahedron(const EGS_Vector& p, /*const*/ EGS_Mesh& mesh) const {
        //std::cout << "looking for point:\n";
        //print_egsvec(p);
        if (p.x < root_.bbox_.min_x || p.x > root_.bbox_.max_x ||
            p.y < root_.bbox_.min_y || p.y > root_.bbox_.max_y ||
            p.z < root_.bbox_.min_z || p.z > root_.bbox_.max_z) {
        //    std::cout << "outside point\n";
            return -1;
        }
        return root_.findTetrahedron(p, mesh);
    }
};

// msh4.1 parsing
EGS_Mesh* EGS_Mesh::parse_msh_file(std::istream& input) {
    auto version = msh_parser::internal::parse_msh_version(input);
    // TODO auto mesh_data;
    switch(version) {
        case msh_parser::internal::MshVersion::v41:
            try {
                return parse_msh41_body(input);
            } catch (const std::runtime_error& err) {
                throw std::runtime_error("msh 4.1 parsing failed\n" + std::string(err.what()));
            }
            break;
    }
    throw std::runtime_error("couldn't parse msh file");
}

EGS_Mesh::EGS_Mesh(std::vector<EGS_Mesh::Tetrahedron> elements,
    std::vector<EGS_Mesh::Node> nodes, std::vector<EGS_Mesh::Medium> materials) :
    EGS_BaseGeometry("EGS_Mesh"), _elements(std::move(elements)), _nodes(std::move(nodes)), _materials(std::move(materials))
{
    std::size_t max_elts = std::numeric_limits<int>::max();
    if (_elements.size() >= max_elts) {
        throw std::runtime_error("maximum number of elements (" +
            std::to_string(max_elts) + ") exceeded (" + std::to_string(_elements.size()) + ")");
    }
    EGS_BaseGeometry::nreg = _elements.size();

    _elt_tags.reserve(_elements.size());
    _elt_points.reserve(_elements.size() * 4);

    std::unordered_map<int, EGS_Mesh::Node> node_map;
    node_map.reserve(_nodes.size());
    for (const auto& n : _nodes) {
        node_map.insert({n.tag, n});
    }
    if (node_map.size() != _nodes.size()) {
        throw std::runtime_error("duplicate nodes in node list");
    }
    // Find the matching nodes for every tetrahedron
    auto find_node = [&](int node_tag) -> EGS_Mesh::Node {
        auto node_it = node_map.find(node_tag);
        if (node_it == node_map.end()) {
            throw std::runtime_error("No mesh node with tag: " + std::to_string(node_tag));
        }
        return node_it->second;
    };
    for (int i = 0; i < _elements.size(); i++) {
        _elt_tags.push_back(i);
        const auto& e = _elements[i];
        auto a = find_node(e.a);
        auto b = find_node(e.b);
        auto c = find_node(e.c);
        auto d = find_node(e.d);
        _elt_points.emplace_back(EGS_Vector(a.x, a.y, a.z));
        _elt_points.emplace_back(EGS_Vector(b.x, b.y, b.z));
        _elt_points.emplace_back(EGS_Vector(c.x, c.y, c.z));
        _elt_points.emplace_back(EGS_Vector(d.x, d.y, d.z));
    }

    std::vector<mesh_neighbours::Tetrahedron> neighbour_elts;
    neighbour_elts.reserve(_elements.size());
    for (const auto& e: _elements) {
        neighbour_elts.emplace_back(mesh_neighbours::Tetrahedron(e.a, e.b, e.c, e.d));
    }
    this->_neighbours = mesh_neighbours::tetrahedron_neighbours(neighbour_elts);

    _boundary_faces.reserve(_elements.size() * 4);
    _boundary_elts.reserve(_elements.size());
    for (const auto& ns: _neighbours) {
        _boundary_elts.push_back(std::any_of(begin(ns), end(ns),
            [](int n){ return n == mesh_neighbours::NONE; }
        ));
        for (const auto& n: ns) {
            _boundary_faces.push_back(n == mesh_neighbours::NONE);
        }
    }

    // TODO figure out materials setup (override setMedia?) with egsinp

    // map from medium tags to offsets
    std::unordered_map<int, int> medium_offsets;
    for (std::size_t i = 0; i < _materials.size(); i++) {
        // TODO use EGS_BaseGeometry tracker
        // auto med = EGS_BaseGeometry::addMedium(m.medium_name);
        _medium_names.push_back(_materials[i].medium_name);
        auto material_tag = _materials[i].tag;
        bool inserted = medium_offsets.insert({material_tag, i}).second;
        if (!inserted) {
            throw std::runtime_error("duplicate medium tag: " + std::to_string(material_tag));
        }
    }

    _medium_indices.reserve(_elements.size());
    for (const auto& e: _elements) {
        // TODO handle vacuum tag (-1)?
        _medium_indices.push_back(medium_offsets.at(e.medium_tag));
    }

    //std::vector<EGS_Vector> centroids;
    //centroids.reserve(num_elements());
    //for (int i = 0; i < num_elements(); i++) {
    //    auto n = element_nodes(i);
    //    centroids.push_back(centroid(n.A, n.B, n.C, n.D));
    //}
    std::cout << "before making octree\n";
    _lookup_tree = std::unique_ptr<EGS_Mesh_Octree>(new EGS_Mesh_Octree(_elt_points));
    std::cout << "after making octree\n";
}

bool EGS_Mesh::isInside(const EGS_Vector &x) {
    return isWhere(x) != -1;
}

int EGS_Mesh::inside(const EGS_Vector &x) {
    return isInside(x) ? 0 : -1;
}

int EGS_Mesh::medium(int ireg) const {
    return _medium_indices.at(ireg);
}

bool EGS_Mesh::insideElement(int i, const EGS_Vector &x) /* const */ {
    const auto& n = element_nodes(i);
    if (point_outside_of_plane(x, n.A, n.B, n.C, n.D)) {
        return false;
    }
    if (point_outside_of_plane(x, n.A, n.C, n.D, n.B)) {
        return false;
    }
    if (point_outside_of_plane(x, n.A, n.B, n.D, n.C)) {
        return false;
    }
    if (point_outside_of_plane(x, n.B, n.C, n.D, n.A)) {
        return false;
    }
    return true;
}

std::vector<int> EGS_Mesh::findNeighbourhood(int elt) {
    auto sz = 128;
    std::vector<int> hood;
    hood.reserve(sz);
    std::deque<int> to_search;
    to_search.push_back(elt);
    while (!to_search.empty() && hood.size() <= 128) {
        int elt = to_search.front();
        to_search.pop_front();
        std::array<int, 4> neighbours = _neighbours[elt];
        for (auto n : neighbours) {
            if (n == mesh_neighbours::NONE) {
                continue;
            }
            if (std::find(begin(hood), end(hood), n) == hood.end()) {
                hood.push_back(n);
                to_search.push_back(n);
            }
        }
    }
    return hood;
}

int EGS_Mesh::isWhere(const EGS_Vector &x) {
    //static int num_calls = 0;
    //static int num_hits = 0;
    //num_calls++;
    //if (num_calls && num_calls % 1000 == 0) {
    //    egsInformation("\n%d of %d\n", num_hits, num_calls);
    //}
    if (!_lookup_tree) {
        std::cerr << "lookup tree is null!\n";
    }

    //std::vector<EGS_Vector> centroids;
    //centroids.reserve(num_elements());
    //for (int i = 0; i < num_elements(); i++) {
    //    auto n = element_nodes(i);
    //    centroids.push_back(centroid(n.A, n.B, n.C, n.D));
    //}

    //int elt = -1;
    //{
    //auto start = std::chrono::steady_clock::now();
    //for (auto i = 0; i < num_elements(); i++) {
    //    if (insideElement(i, x)) {
    //        elt = i;
    //    }
    //}
    //auto end = std::chrono::steady_clock::now();
    //std::cout << "Brute search: "
    //    << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
    //    << " us\n";
    //}
    //std::cout << "elt should be " << elt << "\n";

    //return _lookup_tree->findTetrahedron(x, *this);
    //auto start = std::chrono::steady_clock::now();
    //auto closest_elt = _lookup_tree->findTetrahedron(x, *this);
    //auto end = std::chrono::steady_clock::now();
    //std::cout << "Octree search: "
    //    << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()
    //    << " us\n";
    //std::cout << "found elt " << closest_elt << "\n";
    //if (closest_elt != -1) {
    //    //num_hits++;
    //    return closest_elt;
    //}

    //std::cout << "got closest elt " << closest_elt << "\n";
    //auto neighbourhood = findNeighbourhood(closest_elt);
    //std::array<int, 5> neighbourhood { closest_elt,
    //    _neighbours[closest_elt][0], _neighbours[closest_elt][1],
    //    _neighbours[closest_elt][2], _neighbours[closest_elt][3]
    //};
    //for (auto n : neighbourhood) {
    //    if (n != mesh_neighbours::NONE && insideElement(n, x)) {
    ////        num_hits++;
    //        return n;
    //    }
    //}
    for (auto i = 0; i < num_elements(); i++) {
        if (insideElement(i, x)) {
            return i;
        }
    }
    return -1;
}

EGS_Float EGS_Mesh::hownear(int ireg, const EGS_Vector& x) {
    if (ireg > 0 && ireg > num_elements() - 1) {
        throw std::runtime_error("ireg " + std::to_string(ireg) + " out of bounds for mesh with " + std::to_string(num_elements()) + " regions");
    }
    // inside
    if (ireg >= 0) {
        return min_interior_face_dist(ireg, x);
    }
    // outside
    return min_exterior_face_dist(ireg, x);
}

EGS_Float EGS_Mesh::min_interior_face_dist(int ireg, const EGS_Vector& x) {
    assert(ireg >= 0);
    EGS_Float min2 = std::numeric_limits<EGS_Float>::max();

    auto maybe_update_min = [&](const EGS_Vector& A, const EGS_Vector& B, const EGS_Vector& C) {
        EGS_Float dis = distance2(closest_point_triangle(x, A, B, C), x);
        if (dis < min2) {
            min2 = dis;
        }
    };

    const auto& n = element_nodes(ireg);
    maybe_update_min(n.A, n.B, n.C);
    maybe_update_min(n.A, n.C, n.D);
    maybe_update_min(n.A, n.B, n.D);
    maybe_update_min(n.B, n.C, n.D);

    return std::sqrt(min2);
}

EGS_Float EGS_Mesh::min_exterior_face_dist(int ireg, const EGS_Vector& x) {
    assert(ireg < 0);
    // loop over all boundary tetrahedrons and find the closest point to the tetrahedron
    EGS_Float min2 = std::numeric_limits<EGS_Float>::max();
    for (auto i = 0; i < num_elements(); i++) {
        // TODO check for guaranteed exterior face, not just exterior element?
        if (!is_boundary(i)) {
            continue;
        }
        const auto& n = element_nodes(i);
        EGS_Float dis = distance2(x,
            closest_point_tetrahedron(x, n.A, n.B, n.C, n.D));
        if (dis < min2) {
            min2 = dis;
        }
    }
    return std::sqrt(min2);
}

int EGS_Mesh::howfar(int ireg, const EGS_Vector &x, const EGS_Vector &u,
    EGS_Float &t, int *newmed /* =0 */, EGS_Vector *normal /* =0 */)
{
    if (ireg < 0) {
        return howfar_exterior(ireg, x, u, t, newmed, normal);
    }
    return howfar_interior(ireg, x, u, t, newmed, normal);
}

int EGS_Mesh::howfar_interior(int ireg, const EGS_Vector &x, const EGS_Vector &u,
    EGS_Float &t, int *newmed, EGS_Vector *normal)
{
    assert(ireg >= 0 && ireg < num_elements());
    EGS_Vector u_norm = u;
    u_norm.normalize();

    auto update_media_and_normal = [&](const EGS_Vector &A, const EGS_Vector &B,
        const EGS_Vector &C, int new_reg)
    {
        if (newmed) {
            if (new_reg == -1) {
                *newmed = -1; // vacuum
            } else {
                *newmed = medium(new_reg);
            }
        }
        if (normal) {
            EGS_Vector ab = B - A;
            EGS_Vector ac = C - A;
            *normal = cross(ab, ac);
        }
    };

    EGS_Float dist = 1e30;
    const auto& n = element_nodes(ireg);
    if (triangle_ray_intersection(x, u_norm, n.A, n.B, n.C, dist)) {
        // too far away to intersect
        if (dist > t) {
            return ireg;
        }
        t = dist;
        // index 3 = excluding last point D = face ABC
        auto new_reg = _neighbours[ireg][3];
        update_media_and_normal(n.A, n.B, n.C, new_reg);
        return new_reg;
    }
    if (triangle_ray_intersection(x, u_norm, n.A, n.C, n.D, dist)) {
        // too far away to intersect
        if (dist > t) {
            return ireg;
        }
        t = dist;
        // index 1 = excluding point B = face ACD
        auto new_reg = _neighbours[ireg][1];
        update_media_and_normal(n.A, n.C, n.D, new_reg);
        return new_reg;
    }
    if (triangle_ray_intersection(x, u_norm, n.A, n.B, n.D, dist)) {
        // too far away to intersect
        if (dist > t) {
            return ireg;
        }
        t = dist;
        // index 2 = excluding point C = face ABD
        auto new_reg = _neighbours[ireg][2];
        update_media_and_normal(n.A, n.B, n.D, new_reg);
        return new_reg;
    }
    if (triangle_ray_intersection(x, u_norm, n.B, n.C, n.D, dist)) {
        if (dist > t) {
            return ireg;
        }
        t = dist;
        // index 0 = excluding point A = face BCD
        auto new_reg = _neighbours[ireg][0];
        update_media_and_normal(n.B, n.C, n.D, new_reg);
        return new_reg;
    }

    return ireg;
}

EGS_Mesh::Intersection EGS_Mesh::closest_boundary_face(int ireg, const EGS_Vector &x,
    const EGS_Vector &u)
{
    assert(is_boundary(ireg));
    EGS_Float min_dist = std::numeric_limits<EGS_Float>::max();

    auto dist = min_dist;
    auto closest_face = -1;

    auto check_face_intersection = [&](int face, const  EGS_Vector& p1, const EGS_Vector& p2,
            const EGS_Vector& p3)
    {
        if (_boundary_faces[4*ireg + face] &&
            triangle_ray_intersection(x, u, p1, p2, p3, dist) && dist < min_dist)
        {
            min_dist = dist;
            closest_face = face;
        }
    };


    const auto& n = element_nodes(ireg);
    // face 0 (BCD), face 1 (ACD) etc.
    check_face_intersection(0, n.B, n.C, n.D);
    check_face_intersection(1, n.A, n.C, n.D);
    check_face_intersection(2, n.A, n.B, n.D);
    check_face_intersection(3, n.A, n.B, n.C);

    return EGS_Mesh::Intersection(min_dist, closest_face);
}

int EGS_Mesh::howfar_exterior(int ireg, const EGS_Vector &x, const EGS_Vector &u,
    EGS_Float &t, int *newmed, EGS_Vector *normal)
{
    assert(ireg == -1);

    // loop over all boundary tetrahedrons and find the closest point to the tetrahedron
    EGS_Float min_dist = 1e30;
    int min_reg = -1;
    int min_reg_face = -1;

    for (auto i = 0; i < num_elements(); i++) {
        if (!is_boundary(i)) {
            continue;
        }
        auto intersection = closest_boundary_face(i, x, u);
        if (intersection.dist < min_dist) {
            min_dist = intersection.dist;
            min_reg_face = intersection.face_index;
            min_reg = i;
        }
    }
    // no intersection
    if (min_dist > t) {
        return -1;
    }
    // intersection found, update out parameters
    t = min_dist;
    if (newmed) {
        *newmed = medium(min_reg);
    }
    if (normal) {
        const auto& n = element_nodes(ireg);
        switch(min_reg_face) {
            case 0: *normal = cross(n.C - n.B, n.D - n.B); break;
            case 1: *normal = cross(n.C - n.A, n.D - n.A); break;
            case 2: *normal = cross(n.B - n.A, n.D - n.A); break;
            case 3: *normal = cross(n.B - n.A, n.C - n.A); break;
            default: throw std::runtime_error("Bad intersection, got face index: " +
                std::to_string(min_reg_face));
        }
    }
    //out << "got min_reg: " << min_reg << "\n";
    //egsWarning("%s", out.str().c_str());
    return min_reg;
}

// TODO
static char EGS_MESH_LOCAL geom_class_msg[] = "createGeometry(Mesh): %s\n";

void EGS_Mesh::printInfo() const {
    EGS_BaseGeometry::printInfo();
    std::ostringstream oss;
    printElement(0, oss);
    egsInformation(oss.str().c_str());
}

extern "C" {
    EGS_MESH_EXPORT EGS_BaseGeometry *createGeometry(EGS_Input *input) {
        if (!input) {
            egsWarning(geom_class_msg, "null input");
            return nullptr;
        }
        std::string mesh_file;
        int err = input->getInput("file", mesh_file);
        if (err) {
            egsWarning(geom_class_msg, "no mesh file specified in input");
            return nullptr;
        }
        if (mesh_file.length() >= 4 && mesh_file.rfind(".msh") == mesh_file.length() - 4)
        {
            std::ifstream input_file(mesh_file);
            if (!input_file) {
                egsWarning("unable to open file: `%s`", mesh_file.c_str());
                return nullptr;
            }
            EGS_Mesh* mesh = EGS_Mesh::parse_msh_file(input_file);
            if (!mesh) {
                egsWarning("EGS_Mesh::from_file: Gmsh msh file parsing failed\n");
                return nullptr;
            }
            mesh->setFilename(mesh_file);
            mesh->setName(input);
            for (const auto& medium: mesh->medium_names()) {
                mesh->addMedium(medium);
            }
            return mesh;
        }
        egsWarning("EGS_Mesh::from_file: unknown file extension for file `%s`,"
            "only `.msh` is allowed\n", mesh_file.c_str());
        return nullptr;
    }
}

void EGS_Mesh::reorderMesh(const EGS_Vector &x) {
    std::vector<std::pair<int, EGS_Vector>> elt_centroids;
    elt_centroids.reserve(num_elements());
    for (int i = 0; i < num_elements(); i++) {
        auto n = element_nodes(i);
        elt_centroids.push_back(std::make_pair(i, centroid(n.A, n.B, n.C, n.D)));
    }
    std::sort(begin(elt_centroids), end(elt_centroids),
        [&](const std::pair<int, EGS_Vector>& A, const std::pair<int, EGS_Vector>& B) {
        return (A.second - x).length2() < (B.second - x).length2();
    });
    std::vector<int> reordered_tags;
    reordered_tags.reserve(num_elements());
    for (const auto& pair: elt_centroids) {
        reordered_tags.push_back(pair.first);
    }
    //for (int i = 0; i < 5; i++) {
    //    std::cout << "tet " << elt_centroids[i].first << "\nwith centroid:\n";
    //    print_egsvec(elt_centroids[i].second);
    //}
    renumberMesh(reordered_tags);
}

void EGS_Mesh::renumberMesh(const std::vector<int>& reordered_tags) {
    if (reordered_tags.size() != _elt_tags.size()) {
        throw std::runtime_error("renumberMesh: tag vector length mismatch");
    }

    // map from old numbering to new numbering
    std::unordered_map<int, int> renum;
    renum.reserve(num_elements());
    for (int i = 0; i < num_elements(); i++) {
        renum.insert({reordered_tags[i], i});
    }

    auto old_tags = _elt_tags;
    auto old_points = _elt_points;
    auto old_b_faces = _boundary_faces;
    auto old_b_elts = _boundary_elts;
    auto old_media = _medium_indices;
    auto old_neighbours = _neighbours;

    for (std::size_t i = 0; i < reordered_tags.size(); i++) {
        auto old = reordered_tags.at(i);
        _elt_tags.at(i) = old;
        for (int j = 0; j < 4; j++) {
            _elt_points.at(4*i+j) = old_points.at(4*old+j);
            _boundary_faces.at(4*i+j) = old_b_faces.at(4*old+j);
        }
        _boundary_elts.at(i) = old_b_elts.at(old);
        _medium_indices.at(i) = old_media.at(old);
        for (int j = 0; j < 4; j++) {
            auto old_n = old_neighbours.at(old).at(j);
            if (old_n == -1) {
                _neighbours.at(i).at(j) = -1;
            } else {
                _neighbours.at(i).at(j) = renum.at(old_n);
            }
        }
    }
}
