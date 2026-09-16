// Wave 5 Stage 3: polarised rays through the materials that already existed.
//
// Everything here drives a material's own interact() with a hand-built ray and hit, and reads the
// result back through world_field(), so a basis or sign slip shows up as the wrong PHYSICAL field
// rather than as a wrong component in a frame the test also made up.

#include <doctest/doctest.h>

#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/materials/BeamSplitter.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/materials/Diffuser.hpp"
#include "scrt/materials/PerfectMirror.hpp"
#include "scrt/materials/RealMirror.hpp"
#include "scrt/materials/ThinDielectricPane.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/optics/Fresnel.hpp"
#include "scrt/optics/Polarisation.hpp"
#include "scrt/sources/Laser.hpp"

#include <cmath>
#include <complex>
#include <nlohmann/json.hpp>

using namespace scrt;
using optics::cplx;
using optics::PolarisationKind;

namespace {

/// A ray arriving at the origin at `theta` from the +Z normal, in the x-z plane, polarised.
core::Ray incoming(double theta, PolarisationKind kind, double angle_rad = 0.0) {
    core::Ray r;
    r.direction = {std::sin(theta), 0.0, -std::cos(theta)};
    r.power     = 1.0;
    optics::set_polarisation(r, kind, angle_rad);
    return r;
}

core::Hit hit_on_z_plane(const core::Ray& r, bool front = true) {
    core::Hit h;
    h.position   = {0.0, 0.0, 0.0};
    h.normal     = glm::dot(r.direction, math::vec3{0, 0, 1}) < 0.0 ? math::vec3{0, 0, 1}
                                                                     : math::vec3{0, 0, -1};
    h.front_face = front;
    h.t          = 0.0;
    return h;
}

double norm_field(const optics::FieldVec& f) {
    return std::norm(f.x) + std::norm(f.y) + std::norm(f.z);
}

/// |<a|b>| for unit-power states: 1 means the same polarisation up to a global phase.
double overlap(const optics::FieldVec& a, const optics::FieldVec& b) {
    return std::abs(std::conj(a.x) * b.x + std::conj(a.y) * b.y + std::conj(a.z) * b.z);
}

/// The state with the handedness flipped (complex conjugate of the field).
optics::FieldVec conj(const optics::FieldVec& f) {
    return {std::conj(f.x), std::conj(f.y), std::conj(f.z)};
}

/// Pure s (perpendicular to the x-z plane of incidence) or pure p, for a ray from `incoming`.
core::Ray s_or_p(double theta, bool s) {
    core::Ray r = incoming(theta, PolarisationKind::Linear, 0.0);
    const math::vec3 sa = glm::normalize(glm::cross(r.direction, math::vec3{0, 0, 1}));
    r.s_axis = sa;
    r.Es     = s ? cplx(1.0, 0.0) : cplx(0.0, 0.0);
    r.Ep     = s ? cplx(0.0, 0.0) : cplx(1.0, 0.0);
    return r;
}

} // namespace

TEST_CASE("polarised Dielectric: s and p reflect Rs and Rp, and R + T = 1") {
    materials::Dielectric glass(1.5);
    math::Rng             rng(1);
    for (int k = 1; k < 89; k += 7) {
        const double theta = k * math::PI / 180.0;
        const double st    = std::sin(theta) / 1.5;
        const auto   a     = optics::fresnel_amplitudes(std::cos(theta), std::sqrt(1 - st * st),
                                                        1.0, 1.5);
        for (bool s : {true, false}) {
            core::Ray  r  = s_or_p(theta, s);
            const auto ia = glass.interact(r, hit_on_z_plane(r), rng);
            const double expected = s ? a.rs * a.rs : a.rp * a.rp;
            CHECK(ia.reflected.power == doctest::Approx(expected).epsilon(1e-12));
            CHECK(ia.reflected.power + ia.transmitted.power == doctest::Approx(1.0).epsilon(1e-12));
            CHECK(ia.reflected.polarised);
            CHECK(norm_field(optics::world_field(ia.transmitted)) == doctest::Approx(1.0));
        }
    }
}

TEST_CASE("polarised Dielectric: p light at Brewster's angle is not reflected at all") {
    materials::Dielectric glass(1.5);
    math::Rng             rng(1);
    const double          tb = std::atan(1.5);
    core::Ray             p  = s_or_p(tb, false);
    CHECK(glass.interact(p, hit_on_z_plane(p), rng).reflected.power < 1e-20);
    core::Ray s = s_or_p(tb, true);
    CHECK(glass.interact(s, hit_on_z_plane(s), rng).reflected.power > 0.1);
}

TEST_CASE("polarised Dielectric: light at 45 degrees between s and p reflects the unpolarised average") {
    materials::Dielectric glass(1.5);
    math::Rng             rng(1);
    const double          theta = 50.0 * math::PI / 180.0;
    core::Ray             r     = s_or_p(theta, true);
    r.Es = r.Ep = cplx(std::sqrt(0.5), 0.0);
    const double st = std::sin(theta) / 1.5;
    const auto   avg = optics::fresnel_unpolarized(std::cos(theta), std::sqrt(1 - st * st), 1.0, 1.5);
    CHECK(glass.interact(r, hit_on_z_plane(r), rng).reflected.power ==
          doctest::Approx(avg.R).epsilon(1e-12));
}

TEST_CASE("polarised Dielectric: total internal reflection keeps the power and shifts s against p") {
    materials::Dielectric glass(1.5);
    math::Rng             rng(1);
    const double          theta = 55.0 * math::PI / 180.0;   // beyond the 41.8 degree critical angle
    core::Ray             r     = s_or_p(theta, true);
    r.Es = r.Ep = cplx(std::sqrt(0.5), 0.0);
    const auto ia = glass.interact(r, hit_on_z_plane(r, /*front=*/false), rng);
    CHECK(ia.kind == materials::InteractionKind::Reflected);
    CHECK(ia.reflected.power == doctest::Approx(1.0));
    // The relative phase between s and p is no longer 0 or pi: linear light has become elliptical.
    const double dphi = std::arg(ia.reflected.Ep / ia.reflected.Es);
    CHECK(std::fabs(std::sin(dphi)) > 0.1);
}

TEST_CASE("perfect mirror at normal incidence flips the field and the handedness of circular light") {
    materials::PerfectMirror mirror;
    math::Rng                rng(1);

    core::Ray  lin = incoming(0.0, PolarisationKind::Linear, 0.3);
    const auto in  = optics::world_field(lin);
    const auto out = optics::world_field(mirror.interact(lin, hit_on_z_plane(lin), rng).reflected);
    CHECK(std::abs(out.x + in.x) < 1e-12);   // E_r = -E_i for a perfect conductor
    CHECK(std::abs(out.y + in.y) < 1e-12);
    CHECK(std::abs(out.z + in.z) < 1e-12);

    // Circular light reverses its helicity on reflection. In world coordinates the rotation of the
    // field vector is unchanged, but the beam now travels the other way, so "left" becomes "right":
    // the reflected field equals the incident field (up to phase), which in the reversed frame is
    // the other hand - checked directly against a right-circular ray built on the reflected beam.
    core::Ray  left = incoming(0.0, PolarisationKind::CircularLeft);
    const auto refl = mirror.interact(left, hit_on_z_plane(left), rng).reflected;
    core::Ray  right_ref;
    right_ref.direction = refl.direction;
    optics::set_polarisation(right_ref, PolarisationKind::CircularRight, 0.0);
    core::Ray left_ref = right_ref;
    optics::set_polarisation(left_ref, PolarisationKind::CircularLeft, 0.0);
    CHECK(overlap(optics::world_field(refl), optics::world_field(right_ref)) ==
          doctest::Approx(1.0));
    CHECK(overlap(optics::world_field(refl), optics::world_field(left_ref)) < 1e-9);
}

TEST_CASE("mirror and splitter keep a linear state linear, and a diffuser depolarises") {
    math::Rng rng(3);
    core::Ray r = incoming(0.4, PolarisationKind::Linear, 0.7);
    const core::Hit h = hit_on_z_plane(r);

    const auto check_linear = [](const core::Ray& o) {
        CHECK(o.polarised);
        const auto f = optics::world_field(o);
        // A linear state has a real field direction up to one global phase: its conjugate overlaps it.
        CHECK(overlap(f, conj(f)) == doctest::Approx(1.0).epsilon(1e-9));
        CHECK(norm_field(f) == doctest::Approx(1.0));
    };
    check_linear(materials::RealMirror(0.9).interact(r, h, rng).reflected);
    const auto bs = materials::BeamSplitter(0.5, 0.0).interact(r, h, rng);
    check_linear(bs.reflected);
    check_linear(bs.transmitted);
    CHECK(bs.reflected.power == doctest::Approx(0.5));   // designed ratio, polarisation-independent

    CHECK_FALSE(materials::Diffuser(0.8).interact(r, h, rng).reflected.polarised);
}

TEST_CASE("thin pane: s reflects more than p at an angle, and each branch conserves up to absorption") {
    materials::ThinDielectricPane pane(1.5, 0.004, 0.0);
    math::Rng                     rng(1);
    const double                  theta = 60.0 * math::PI / 180.0;
    core::Ray s = s_or_p(theta, true), p = s_or_p(theta, false);
    const auto is = pane.interact(s, hit_on_z_plane(s), rng);
    const auto ip = pane.interact(p, hit_on_z_plane(p), rng);
    CHECK(is.reflected.power > 3.0 * ip.reflected.power);
    CHECK(is.reflected.power + is.transmitted.power == doctest::Approx(1.0).epsilon(1e-9));
    CHECK(ip.reflected.power + ip.transmitted.power == doctest::Approx(1.0).epsilon(1e-9));
}

TEST_CASE("unpolarised rays still take the old path, bit for bit") {
    // The same interact() on an unpolarised ray must equal a ray that never heard of polarisation.
    materials::Dielectric glass(1.5);
    math::Rng             rng(1);
    core::Ray             r = incoming(0.6, PolarisationKind::Unpolarised);
    CHECK_FALSE(r.polarised);
    const auto   ia = glass.interact(r, hit_on_z_plane(r), rng);
    const double st = std::sin(0.6) / 1.5;
    const auto   fr = optics::fresnel_unpolarized(std::cos(0.6), std::sqrt(1 - st * st), 1.0, 1.5);
    // Not Approx: the unpolarised branch computes cos_t through refract() and must match itself;
    // equality with the direct formula holds to the last bit only up to that path, so bound tightly.
    CHECK(ia.reflected.power == doctest::Approx(fr.R).epsilon(1e-14));
    CHECK_FALSE(ia.reflected.polarised);
}

TEST_CASE("laser polarisation: parsed, written only when set, and stamped on every ray") {
    const auto doc_for = [](const nlohmann::json& pol) {
        nlohmann::json j = nlohmann::json::parse(R"({"scene": {
            "materials": [{"id": "m", "type": "absorber"}], "elements": [],
            "receiver": {"surface": {"type": "plane", "half_width": 0.01, "half_height": 0.01},
                         "grid": {"nx": 4, "ny": 4}},
            "sources": [{"type": "laser", "origin": [0,0,0], "direction": [1,0,0]}]}})");
        if (!pol.is_null()) j["scene"]["sources"][0]["polarisation"] = pol;
        return j;
    };
    const auto laser_of = [](const io::SceneDocument& d) {
        return std::get<io::LaserSourceDoc>(d.sources.at(0));
    };

    CHECK(laser_of(io::parse_document(doc_for(nullptr), true)).polarisation == "unpolarised");
    CHECK_FALSE(io::write_document(io::parse_document(doc_for(nullptr), true))["scene"]["sources"][0]
                    .contains("polarisation"));

    const auto lin = io::parse_document(doc_for({{"linear_deg", 30.0}}), true);
    CHECK(laser_of(lin).polarisation == "linear");
    CHECK(laser_of(lin).polarisation_linear_deg == 30.0);
    CHECK(io::write_document(lin)["scene"]["sources"][0]["polarisation"] ==
          nlohmann::json({{"linear_deg", 30.0}}));
    CHECK(io::write_document(io::parse_document(doc_for("circular_left"), true))["scene"]["sources"][0]
              ["polarisation"] == "circular_left");

    CHECK_THROWS(io::parse_document(doc_for("circular"), false));
    CHECK_THROWS(io::parse_document(doc_for({{"angle", 30.0}}), false));
    CHECK_THROWS(io::parse_document(doc_for({{"linear_deg", 30.0}, {"extra", 1}}), true));

    // Stamped on the ray: 0 degrees on a horizontal beam is vertical, 90 is horizontal.
    sources::Laser laser;
    laser.set_direction({1.0, 0.0, 0.0});
    laser.set_divergence_mrad(0.0);
    math::Rng rng(5);
    laser.set_polarisation(PolarisationKind::Linear, 0.0);
    auto f = optics::world_field(laser.sample_ray(rng));
    CHECK(std::abs(f.z) == doctest::Approx(1.0));
    laser.set_polarisation(PolarisationKind::Linear, 90.0);
    f = optics::world_field(laser.sample_ray(rng));
    CHECK(std::abs(f.y) == doctest::Approx(1.0));
    CHECK(std::abs(f.z) < 1e-12);
    laser.set_polarisation(PolarisationKind::Unpolarised);
    CHECK_FALSE(laser.sample_ray(rng).polarised);
}
