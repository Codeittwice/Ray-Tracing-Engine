#include <doctest/doctest.h>
#include "scrt/core/AABB.hpp"
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/materials/PerfectMirror.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/surfaces/CylindricalParaboloid.hpp"
#include "scrt/surfaces/FresnelZoneLens.hpp"
#include "scrt/surfaces/GeneralQuadric.hpp"
#include "scrt/surfaces/Paraboloid.hpp"
#include "scrt/surfaces/Plane.hpp"
#include "scrt/surfaces/Sphere.hpp"
#include "scrt/surfaces/TriangleMesh.hpp"
#include <cmath>
#include <cstdint>
#include <vector>

using namespace scrt::core;
using namespace scrt::math;
using namespace scrt::surfaces;

namespace {

/// Downward ray along -z starting high above the local origin.
Ray down_ray(vec3 origin) {
    Ray r;
    r.origin    = origin;
    r.direction = {0.0, 0.0, -1.0};
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Transform: scale factories, decomposition, singular values
// ---------------------------------------------------------------------------

TEST_CASE("Scale: from_scale scales points and leaves the origin fixed") {
    auto xf = Transform::from_scale({2.0, 3.0, 0.5});
    vec3 p = xf.point_to_world({1.0, 1.0, 1.0});
    CHECK(p.x == doctest::Approx(2.0).epsilon(1e-14));
    CHECK(p.y == doctest::Approx(3.0).epsilon(1e-14));
    CHECK(p.z == doctest::Approx(0.5).epsilon(1e-14));

    vec3 o = xf.point_to_world({0.0, 0.0, 0.0});
    CHECK(glm::length(o) == doctest::Approx(0.0).epsilon(1e-14));
}

TEST_CASE("Scale: max_scale_factor is the largest singular value") {
    CHECK(Transform().max_scale_factor() == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(Transform::from_scale({2.0, 3.0, 0.5}).max_scale_factor()
          == doctest::Approx(3.0).epsilon(1e-12));

    // Rotation must not change the singular values.
    auto rot    = Transform::from_euler_xyz({0.3, -0.7, 1.1});
    auto rs     = rot.compose(Transform::from_scale({2.0, 3.0, 0.5}));
    CHECK(rs.max_scale_factor() == doctest::Approx(3.0).epsilon(1e-12));

    // Pure rotation: all singular values are 1.
    CHECK(rot.max_scale_factor() == doctest::Approx(1.0).epsilon(1e-12));
}

TEST_CASE("Scale: is_uniform_scale distinguishes uniform from anisotropic") {
    CHECK(Transform().is_uniform_scale());
    CHECK(Transform::from_scale({2.0, 2.0, 2.0}).is_uniform_scale());
    CHECK(Transform::from_euler_xyz({0.3, -0.7, 1.1})
              .compose(Transform::from_scale({2.5, 2.5, 2.5}))
              .is_uniform_scale());
    CHECK_FALSE(Transform::from_scale({2.0, 2.0, 2.5}).is_uniform_scale());
    CHECK_FALSE(Transform::from_scale({1.0, 1.0, 1.0 + 1e-6}).is_uniform_scale());
}

TEST_CASE("Scale: decompose_trs round-trips from_trs") {
    const vec3 t_in{1.0, -2.0, 3.5};
    const vec3 e_in{0.3, -0.4, 1.1};
    const vec3 s_in{2.0, 3.0, 0.5};

    auto xf = Transform::from_trs(t_in, e_in, s_in);
    vec3 t_out, e_out, s_out;
    REQUIRE(xf.decompose_trs(t_out, e_out, s_out));

    CHECK(t_out.x == doctest::Approx(t_in.x).epsilon(1e-12));
    CHECK(t_out.y == doctest::Approx(t_in.y).epsilon(1e-12));
    CHECK(t_out.z == doctest::Approx(t_in.z).epsilon(1e-12));
    CHECK(e_out.x == doctest::Approx(e_in.x).epsilon(1e-12));
    CHECK(e_out.y == doctest::Approx(e_in.y).epsilon(1e-12));
    CHECK(e_out.z == doctest::Approx(e_in.z).epsilon(1e-12));
    CHECK(s_out.x == doctest::Approx(s_in.x).epsilon(1e-12));
    CHECK(s_out.y == doctest::Approx(s_in.y).epsilon(1e-12));
    CHECK(s_out.z == doctest::Approx(s_in.z).epsilon(1e-12));

    // Recomposing the recovered factors must reproduce the matrix.
    auto again = Transform::from_trs(t_out, e_out, s_out);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            CHECK(again.matrix()[c][r] == doctest::Approx(xf.matrix()[c][r]).epsilon(1e-12));
}

TEST_CASE("Scale: decompose_trs rejects a sheared matrix") {
    mat4 shear(1.0);
    shear[1][0] = 0.5;  // x += 0.5*y  --> columns are no longer orthogonal
    auto xf = Transform::from_matrix(shear);
    vec3 t, e, s;
    CHECK_FALSE(xf.decompose_trs(t, e, s));
}

// ---------------------------------------------------------------------------
// Affine invariance of t
//
// p_world(t) = M * p_local(t) for an affine M, and ray_to_local deliberately
// does NOT renormalise the local direction. So the SAME parameter t solves both
// the world and the local intersection, whatever the scale. What changes under a
// scale of s is the world geometry: the surface sits s times further out, so the
// ray reaches it at a different t -- but that t is still measured in world units
// along the original (unit) world direction.
// ---------------------------------------------------------------------------

TEST_CASE("Scale: Sphere under uniform scale 2") {
    Sphere s(1.0);
    Ray r;
    r.origin    = {0.0, 0.0, -10.0};
    r.direction = {0.0, 0.0, 1.0};

    Hit h;
    REQUIRE(s.intersect(r, 1e-6, 1e9, h));
    CHECK(h.t == doctest::Approx(9.0).epsilon(1e-12));
    CHECK(h.position.z == doctest::Approx(-1.0).epsilon(1e-12));

    // Scaled 2x the world sphere has radius 2, so the near surface is at z = -2.
    s.set_transform(Transform::from_scale({2.0, 2.0, 2.0}));
    Hit h2;
    REQUIRE(s.intersect(r, 1e-6, 1e9, h2));
    CHECK(h2.t == doctest::Approx(8.0).epsilon(1e-12));
    CHECK(h2.position.z == doctest::Approx(-2.0).epsilon(1e-12));
    CHECK(h2.normal.z == doctest::Approx(-1.0).epsilon(1e-12));
}

TEST_CASE("Scale: Plane extent grows and t follows the moved plane") {
    Plane p(1.0, 1.0);

    // A ray outside the unit half-extent misses at identity...
    Ray outside = down_ray({1.5, 1.5, 5.0});
    Hit h;
    CHECK_FALSE(p.intersect(outside, 1e-6, 1e9, h));

    // ...and hits once the plane is scaled to half-extent 2. The plane still lies
    // in z=0, so t is unchanged at 5.
    p.set_transform(Transform::from_scale({2.0, 2.0, 2.0}));
    REQUIRE(p.intersect(outside, 1e-6, 1e9, h));
    CHECK(h.t == doctest::Approx(5.0).epsilon(1e-12));
    CHECK(h.position.x == doctest::Approx(1.5).epsilon(1e-12));
    CHECK(h.position.y == doctest::Approx(1.5).epsilon(1e-12));
    CHECK(h.position.z == doctest::Approx(0.0).epsilon(1e-12));

    // Offset the plane to local z=1, then scale 2x: the world plane sits at z=2,
    // so a ray from z=5 reaches it at t=3 rather than t=4.
    Plane q(1.0, 1.0);
    q.set_transform(Transform::from_translation({0.0, 0.0, 1.0}));
    Ray axial = down_ray({0.0, 0.0, 5.0});
    Hit hq;
    REQUIRE(q.intersect(axial, 1e-6, 1e9, hq));
    CHECK(hq.t == doctest::Approx(4.0).epsilon(1e-12));

    q.set_transform(Transform::from_scale({2.0, 2.0, 2.0})
                        .compose(Transform::from_translation({0.0, 0.0, 1.0})));
    Hit hq2;
    REQUIRE(q.intersect(axial, 1e-6, 1e9, hq2));
    CHECK(hq2.t == doctest::Approx(3.0).epsilon(1e-12));
    CHECK(hq2.position.z == doctest::Approx(2.0).epsilon(1e-12));
}

TEST_CASE("Scale: Paraboloid under uniform scale 2") {
    // x^2 + y^2 = 4fz with f = 0.5: local hit at x=0.4 is z = 0.16/2 = 0.08.
    Paraboloid par(0.5, 0.45);
    Ray r = down_ray({0.4, 0.0, 100.0});
    Hit h;
    REQUIRE(par.intersect(r, 1e-6, 1e9, h));
    CHECK(h.t == doctest::Approx(99.92).epsilon(1e-12));

    // Scaled 2x, the same local point (0.4, 0, 0.08) sits at world (0.8, 0, 0.16),
    // so the matching world ray is at x = 0.8 and stops 0.16 above z = 0.
    par.set_transform(Transform::from_scale({2.0, 2.0, 2.0}));
    Ray r2 = down_ray({0.8, 0.0, 100.0});
    Hit h2;
    REQUIRE(par.intersect(r2, 1e-6, 1e9, h2));
    CHECK(h2.t == doctest::Approx(99.84).epsilon(1e-12));
    CHECK(h2.position.x == doctest::Approx(0.8).epsilon(1e-10));
    CHECK(h2.position.z == doctest::Approx(0.16).epsilon(1e-10));
}

TEST_CASE("Scale: CylindricalParaboloid under uniform scale 2") {
    CylindricalParaboloid trough(0.5, 0.45, 1.0);
    Ray r = down_ray({0.4, 0.3, 100.0});
    Hit h;
    REQUIRE(trough.intersect(r, 1e-6, 1e9, h));
    CHECK(h.t == doctest::Approx(99.92).epsilon(1e-12));

    trough.set_transform(Transform::from_scale({2.0, 2.0, 2.0}));
    Ray r2 = down_ray({0.8, 0.6, 100.0});
    Hit h2;
    REQUIRE(trough.intersect(r2, 1e-6, 1e9, h2));
    CHECK(h2.t == doctest::Approx(99.84).epsilon(1e-12));
    CHECK(h2.position.x == doctest::Approx(0.8).epsilon(1e-10));
    CHECK(h2.position.y == doctest::Approx(0.6).epsilon(1e-10));
    CHECK(h2.position.z == doctest::Approx(0.16).epsilon(1e-10));
}

TEST_CASE("Scale: GeneralQuadric unit sphere under uniform scale 2") {
    QuadricCoeffs c;
    c.A = c.B = c.C = 1.0;
    c.J = -1.0;
    GeneralQuadric q(c, AABB{vec3{-1, -1, -1}, vec3{1, 1, 1}});

    Ray r;
    r.origin    = {0.0, 0.0, -10.0};
    r.direction = {0.0, 0.0, 1.0};

    Hit h;
    REQUIRE(q.intersect(r, 1e-6, 1e9, h));
    CHECK(h.t == doctest::Approx(9.0).epsilon(1e-12));

    q.set_transform(Transform::from_scale({2.0, 2.0, 2.0}));
    Hit h2;
    REQUIRE(q.intersect(r, 1e-6, 1e9, h2));
    CHECK(h2.t == doctest::Approx(8.0).epsilon(1e-12));
    CHECK(h2.position.z == doctest::Approx(-2.0).epsilon(1e-12));
}

TEST_CASE("Scale: FresnelZoneLens under uniform scale 2") {
    // Annulus from r=0.05 to r=0.05+20*0.01 = 0.25 in the local z=0 plane.
    FresnelZoneLens lens(0.4, 0.05, 0.01, 20, 1.49);

    Ray inner = down_ray({0.02, 0.0, 5.0});   // inside the hole: always a miss
    Ray mid   = down_ray({0.30, 0.0, 5.0});   // outside at identity, inside at 2x
    Hit h;
    CHECK_FALSE(lens.intersect(inner, 1e-6, 1e9, h));
    CHECK_FALSE(lens.intersect(mid,   1e-6, 1e9, h));

    lens.set_transform(Transform::from_scale({2.0, 2.0, 2.0}));
    REQUIRE(lens.intersect(mid, 1e-6, 1e9, h));
    CHECK(h.t == doctest::Approx(5.0).epsilon(1e-12));
    CHECK(h.position.x == doctest::Approx(0.30).epsilon(1e-12));
    CHECK(h.position.z == doctest::Approx(0.0).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// Non-uniform scale: normals follow the inverse transpose
// ---------------------------------------------------------------------------

TEST_CASE("Scale: Plane normal under scale(2,1,1) applied after a 45 deg Y rotation") {
    // M = S * R with S = diag(2,1,1), R = rotY(45 deg). The linear part L = S*R has
    // L^-T = S^-1 * R, so the local normal (0,0,1) maps to
    //   normalize(diag(1/2,1,1) * R*z_hat) = normalize(0.5*sin45, 0, cos45) = (1,0,2)/sqrt(5).
    Plane p(1.0, 1.0);
    p.set_transform(Transform::from_scale({2.0, 1.0, 1.0})
                        .compose(Transform::from_euler_xyz({0.0, PI / 4.0, 0.0})));

    Ray r = down_ray({0.0, 0.0, 10.0});
    Hit h;
    REQUIRE(p.intersect(r, 1e-6, 1e9, h));

    // The plane passes through the world origin, so t is the full 10 m.
    CHECK(h.t == doctest::Approx(10.0).epsilon(1e-12));
    CHECK(std::abs(h.position.x) < 1e-12);
    CHECK(std::abs(h.position.z) < 1e-12);

    const double inv = 1.0 / std::sqrt(5.0);
    CHECK(h.normal.x == doctest::Approx(1.0 * inv).epsilon(1e-12));
    CHECK(h.normal.y == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(h.normal.z == doctest::Approx(2.0 * inv).epsilon(1e-12));
}

TEST_CASE("Scale: TriangleMesh normal under non-uniform scale(2,1,1)") {
    // Triangle in the local plane x + y = 1; local geometric normal is +-(1,1,0)/sqrt(2).
    std::vector<vec3> verts{{1.0, 0.0, 0.0}, {1.0, 0.0, 1.0}, {0.0, 1.0, 0.0}};
    std::vector<std::uint32_t> idx{0, 1, 2};
    TriangleMesh mesh(std::move(verts), std::move(idx));
    mesh.set_transform(Transform::from_scale({2.0, 1.0, 1.0}));

    // World triangle: (2,0,0), (2,0,1), (0,1,0), i.e. the plane x/2 + y = 1.
    Ray r;
    r.origin    = {5.0, 0.3, 0.3};
    r.direction = {-1.0, 0.0, 0.0};

    Hit h;
    REQUIRE(mesh.intersect(r, 1e-6, 1e9, h));

    // x = 2*(1 - 0.3) = 1.4, so t = 5 - 1.4 = 3.6.
    CHECK(h.t == doctest::Approx(3.6).epsilon(1e-12));
    CHECK(h.position.x == doctest::Approx(1.4).epsilon(1e-12));
    CHECK(h.position.y == doctest::Approx(0.3).epsilon(1e-12));
    CHECK(h.position.z == doctest::Approx(0.3).epsilon(1e-12));

    // Inverse transpose of diag(2,1,1) is diag(1/2,1,1): (1,1,0) -> (1,2,0)/sqrt(5).
    const double inv = 1.0 / std::sqrt(5.0);
    CHECK(h.normal.x == doctest::Approx(1.0 * inv).epsilon(1e-12));
    CHECK(h.normal.y == doctest::Approx(2.0 * inv).epsilon(1e-12));
    CHECK(h.normal.z == doctest::Approx(0.0).epsilon(1e-12));

    // Sanity: the reported normal must be unit length and face the incoming ray.
    CHECK(glm::length(h.normal) == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(glm::dot(h.normal, r.direction) < 0.0);
}

// ---------------------------------------------------------------------------
// Parameter scale: the focus must move with the shape
// ---------------------------------------------------------------------------

TEST_CASE("Scale: Paraboloid parameter scale moves the focus from f to 3f") {
    const double f = 0.5;
    Paraboloid dish(f, 0.45);
    scrt::materials::PerfectMirror mirror;
    dish.set_material(&mirror);
    Rng rng(11);

    auto focus_z_for = [&](const Paraboloid& p, double x0, double y0) {
        Ray ray = down_ray({x0, y0, 100.0});
        Hit hit;
        REQUIRE(p.intersect(ray, 1e-6, 1e9, hit));
        auto inter = mirror.interact(ray, hit, rng);
        vec3 d = inter.reflected.direction;
        vec3 o = hit.position;
        REQUIRE(std::abs(d.x) + std::abs(d.y) > 0.0);
        // Parameter at which the reflected ray crosses the optical axis.
        const double t_axis = -o.x / d.x;
        return o.z + t_axis * d.z;
    };

    for (double x0 : {-0.3, -0.1, 0.1, 0.3})
        CHECK(focus_z_for(dish, x0, 0.0) == doctest::Approx(f).epsilon(1e-9));

    // Uniform parameter scale by 3: f -> 3f and the aperture grows with it.
    dish.set_focal_length(3.0 * f);
    dish.set_aperture_radius(3.0 * 0.45);
    for (double x0 : {-0.9, -0.3, 0.3, 0.9})
        CHECK(focus_z_for(dish, x0, 0.0) == doctest::Approx(3.0 * f).epsilon(1e-9));
}

// ---------------------------------------------------------------------------
// Degeneracy epsilons must be relative, not absolute
// ---------------------------------------------------------------------------

TEST_CASE("Scale: millimetre-scale mesh at scale 1000 is still hit") {
    // Moller-Trumbore's determinant a = e1 . (d x e2) scales as |e1|*|e2|*|d_local|.
    // Here |e1| = |e2| = 1e-3 m and, at scale 1000, |d_local| = 1e-3, so a ~ 1e-9.
    // Against the old absolute guard (|a| < EPSILON_T = 1e-6) every triangle is
    // silently dropped: no crash, just missing geometry and quietly wrong flux.
    std::vector<vec3> verts{{0.0, 0.0, 0.0},
                            {1e-3, 0.0, 0.0},
                            {1e-3, 1e-3, 0.0},
                            {0.0, 1e-3, 0.0}};
    std::vector<std::uint32_t> idx{0, 1, 2, 0, 2, 3};
    TriangleMesh mesh(std::move(verts), std::move(idx));
    mesh.set_transform(Transform::from_scale({1000.0, 1000.0, 1000.0}));

    // World quad spans [0,1] x [0,1] in the z=0 plane.
    Ray r = down_ray({0.3, 0.3, 5.0});
    Hit h;
    REQUIRE(mesh.intersect(r, 1e-6, 1e9, h));
    CHECK(h.t == doctest::Approx(5.0).epsilon(1e-12));
    CHECK(h.position.x == doctest::Approx(0.3).epsilon(1e-12));
    CHECK(h.position.y == doctest::Approx(0.3).epsilon(1e-12));
    CHECK(h.position.z == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(h.normal.z == doctest::Approx(1.0).epsilon(1e-12));
}

TEST_CASE("Scale: unscaled millimetre mesh is hit (same geometry, no transform)") {
    // Control for the test above: at scale 1 the same tiny triangles have
    // a ~ 1e-6 * |d| and sit right on the old absolute threshold, so the
    // relative guard must not regress this case either.
    std::vector<vec3> verts{{0.0, 0.0, 0.0},
                            {1e-3, 0.0, 0.0},
                            {1e-3, 1e-3, 0.0},
                            {0.0, 1e-3, 0.0}};
    std::vector<std::uint32_t> idx{0, 1, 2, 0, 2, 3};
    TriangleMesh mesh(std::move(verts), std::move(idx));

    Ray r = down_ray({3e-4, 3e-4, 5.0});
    Hit h;
    REQUIRE(mesh.intersect(r, 1e-6, 1e9, h));
    CHECK(h.t == doctest::Approx(5.0).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// Bounds and per-surface scale policy
// ---------------------------------------------------------------------------

TEST_CASE("Scale: world_bounds is local_bounds pushed through the transform") {
    Sphere s(1.0);
    AABB lb = s.local_bounds();
    CHECK(lb.min().x == doctest::Approx(-1.0).epsilon(1e-14));
    CHECK(lb.max().z == doctest::Approx(1.0).epsilon(1e-14));

    s.set_transform(Transform::from_scale({2.0, 3.0, 4.0}));
    AABB wb = s.world_bounds();
    CHECK(wb.min().x == doctest::Approx(-2.0).epsilon(1e-12));
    CHECK(wb.min().y == doctest::Approx(-3.0).epsilon(1e-12));
    CHECK(wb.min().z == doctest::Approx(-4.0).epsilon(1e-12));
    CHECK(wb.max().x == doctest::Approx(2.0).epsilon(1e-12));
    CHECK(wb.max().y == doctest::Approx(3.0).epsilon(1e-12));
    CHECK(wb.max().z == doctest::Approx(4.0).epsilon(1e-12));

    // local_bounds must be independent of the transform.
    AABB lb2 = s.local_bounds();
    CHECK(lb2.min().x == doctest::Approx(-1.0).epsilon(1e-14));
    CHECK(lb2.max().x == doctest::Approx(1.0).epsilon(1e-14));
}

TEST_CASE("Scale: per-surface scale policy") {
    Plane plane(1.0, 1.0);
    Sphere sphere(1.0);
    Paraboloid par(0.5, 0.45);
    CylindricalParaboloid trough(0.5, 0.45, 1.0);
    FresnelZoneLens lens(0.4, 0.05, 0.01, 8, 1.49);
    QuadricCoeffs c;
    c.A = c.B = c.C = 1.0;
    c.J = -1.0;
    GeneralQuadric quad(c, AABB{vec3{-1, -1, -1}, vec3{1, 1, 1}});
    TriangleMesh mesh(std::vector<vec3>{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}},
                      std::vector<std::uint32_t>{0, 1, 2});

    CHECK(plane.scale_support()  == ScaleSupport::Free);
    CHECK(quad.scale_support()   == ScaleSupport::Free);
    CHECK(mesh.scale_support()   == ScaleSupport::Free);
    CHECK(sphere.scale_support() == ScaleSupport::UniformOnly);
    CHECK(par.scale_support()    == ScaleSupport::UniformOnly);
    CHECK(trough.scale_support() == ScaleSupport::UniformOnly);
    CHECK(lens.scale_support()   == ScaleSupport::UniformOnly);
}

TEST_CASE("Scale: decompose_trs round-trips at both gimbal-lock poles") {
    // Regression: the b = -90 branch dropped a sign and returned -(a - c),
    // so recomposition produced a completely different rotation while still
    // reporting success. Both poles and several free-angle values are covered.
    const scrt::math::vec3 t_in{0.3, -1.1, 2.4};
    const scrt::math::vec3 s_in{1.0, 1.0, 1.0};
    for (double sign : {1.0, -1.0}) {
        for (double a : {0.0, 0.3, 1.1, -0.9}) {
            for (double c : {0.0, 0.7, 0.4}) {
                const scrt::math::vec3 e_in{a, sign * scrt::math::PI / 2.0, c};
                const auto xf = scrt::core::Transform::from_trs(t_in, e_in, s_in);
                scrt::math::vec3 t_out, e_out, s_out;
                REQUIRE(xf.decompose_trs(t_out, e_out, s_out));
                const auto back = scrt::core::Transform::from_trs(t_out, e_out, s_out);
                for (int col = 0; col < 4; ++col)
                    for (int row = 0; row < 4; ++row)
                        CHECK(back.matrix()[col][row] ==
                              doctest::Approx(xf.matrix()[col][row]).epsilon(1e-12));
            }
        }
    }
}
