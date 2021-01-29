#include "egs_mesh.h"
#include "egs_vector.h"

#include <algorithm>
#include <cassert>
#include <fstream>

#define RUN_TEST(test_fn) \
    std::cerr << "starting test " << #test_fn << std::endl; \
    err = test_fn; \
    num_total++; \
    if (err) { \
        std::cerr << "test FAILED" << std::endl; \
        num_failed++; \
    } else { \
        std::cerr << "test passed" << std::endl; \
    }

#define EXPECT_ERROR(stmt, err_msg) \
    try { \
        stmt; \
        std::cerr << "expected exception with message: \"" << err_msg << "\"\n"; \
        return 1; \
    } catch (const std::runtime_error& err) { \
        if (err.what() != std::string(err_msg)) { \
            std::cerr << "got error message: \"" \
                << err.what() << "\"\nbut expected: \"" << err_msg << "\"\n"; \
            return 1; \
        } \
    }

// we'll use a simple five-element mesh for smoke testing
static EGS_Mesh test_mesh = [](){
    std::ifstream input("five-tet.msh");
    return EGS_Mesh::parse_msh_file(input);
}();

class Tet {
public:
    Tet(EGS_Vector a, EGS_Vector b, EGS_Vector c, EGS_Vector d)
        : a(a), b(b), c(c), d(d) {}
    EGS_Vector centroid() const {
        return EGS_Vector (
            (a.x + b.x + c.x + d.x) / 4.0,
            (a.y + b.y + c.y + d.y) / 4.0,
            (a.z + b.z + c.z + d.z) / 4.0
        );
    }
private:
    EGS_Vector a;
    EGS_Vector b;
    EGS_Vector c;
    EGS_Vector d;
};

int test_unknown_node() {
    std::vector<EGS_Mesh::Tetrahedron> elt { EGS_Mesh::Tetrahedron(0, 0, 1, 2, 100) };
    // no node 100 in nodes vector
    std::vector<EGS_Mesh::Node> nodes {
        EGS_Mesh::Node(0, 1.0, 1.0, -1.0),
        EGS_Mesh::Node(1, -1.0, 1.0, -1.0),
        EGS_Mesh::Node(2, 0.0, -1.0, -1.0),
        EGS_Mesh::Node(3, 0.0, 0.0, 1.0)
    };
    std::vector<EGS_Mesh::Medium> media { EGS_Mesh::Medium(1, "") };
    EXPECT_ERROR(EGS_Mesh mesh(elt, nodes, media), "No mesh node with tag: 100");
    return 0;
}

int test_boundary() {
    // element 0 is surrounded by the other four elements
    if (test_mesh.is_boundary() != std::vector<bool>{false, true, true, true, true}) {
        return 1;
    }
    return 0;
}

int test_neighbours() {
    // element 0 is neighbours with the other four elements
    auto neighbours = test_mesh.neighbours();
    auto n0 = neighbours.at(0);
    assert(std::count(n0.begin(), n0.end(), 1) == 1);
    assert(std::count(n0.begin(), n0.end(), 2) == 1);
    assert(std::count(n0.begin(), n0.end(), 3) == 1);
    assert(std::count(n0.begin(), n0.end(), 4) == 1);

    for (auto ns = neighbours.begin() + 1; ns != neighbours.end(); ns++) {
        assert(std::count(ns->begin(), ns->end(), 0) == 1);
        assert(std::count(ns->begin(), ns->end(), -1) == 3);
    }

    return 0;
}

int test_isWhere() {
    // test the centroid of each tetrahedron is inside the tetrahedron
    auto points = test_mesh.points();
    for (int i = 0; i < (int)test_mesh.num_elements(); i++) {
        auto tet = Tet(points[4*i], points[4*i+1], points[4*i+2], points[4*i+3]);
        auto c = tet.centroid();
        auto in_tet = test_mesh.isWhere(c);
        if (in_tet != i) {
            std::cerr << "expected point to be in tetrahedron " << i << " got: " << in_tet << "\n";
            return 1;
        }
    }
    EGS_Vector out(10e10, 0, 0);
    if (test_mesh.isWhere(out) != -1) {
        std::cerr << "expected point to be outside (-1), got: " << test_mesh.isWhere(out) << "\n";
        return 1;
    }

    return 0;
}

int test_hownear() {
    return 1;
}

int main() {
    int num_failed = 0;
    int num_total = 0;
    int err = 0;

    RUN_TEST(test_unknown_node());
    RUN_TEST(test_isWhere());
    RUN_TEST(test_boundary());
    RUN_TEST(test_neighbours());
    RUN_TEST(test_hownear());

    std::cerr << num_total - num_failed << " out of " << num_total << " tests passed\n";
    return num_failed;
}
