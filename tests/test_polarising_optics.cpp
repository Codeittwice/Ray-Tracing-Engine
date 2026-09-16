// Wave 5 Stage 4: polariser, waveplate, polarising beam splitter.
//
// Each check is a textbook result driven through the material's own interact(), with the part's
// axis coming from a real surface transform, so "the axis is in the part's own frame" is tested
// rather than assumed.

#include <doctest/doctest.h>

#include "scrt/core/Hit.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/materials/PolarisingOptics.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/optics/Polarisation.hpp"
#include "scrt/surfaces/Plane.hpp"

#include <cmath>
#include <complex>

using namespace scrt;
using optics::PolarisationKind;

namespace {

/// A face-on part for a beam travelling along +X: local Z turned onto X, local X then points to -Z
/// (vertical), so axis angle 0 is vertical - the same "0 = vertical" as the laser's linear_deg.
struct Bench {
    surfaces::Plane plane{0.02, 0.02};
    Bench() {
        plane.set_transform(core::Transform::from_trs({0, 0, 0}, {0.0, math::PI / 2, 0.0}, {1, 1, 1}));
    }
    core::Hit hit() const {
        core::Hit h;
        h.position = {0.0, 0.0, 0.0};
        h.normal   = {-1.0, 0.0, 0.0};
        h.surface  = &plane;
        return h;
    }
};

core::Ray beam(PolarisationKind kind, double deg = 0.0) {
    core::Ray r;
    r.direction = {1.0, 0.0, 0.0};
    r.power     = 1.0;
    optics::set_polarisation(r, kind, deg * math::PI / 180.0);
    return r;
}

core::Ray through(const materials::Material& m, const core::Ray& r, const core::Hit& h) {
    math::Rng  rng(1);
    const auto ia = m.interact(r, h, rng);
    REQUIRE(ia.kind == materials::InteractionKind::Refracted);
    return ia.transmitted;
}

/// Relative phase of the two components of a polarised ray, in (-pi, pi].
double rel_phase(const core::Ray& r) { return std::arg(r.Ep / r.Es); }

} // namespace

TEST_CASE("polariser: Malus's law, cos^2 of the angle, and extinction at 90 degrees") {
    Bench b;
    for (int deg = 0; deg <= 90; deg += 15) {
        materials::Polariser pol(static_cast<double>(deg), 1.0e12, 1.0);
        const double t = through(pol, beam(PolarisationKind::Linear, 0.0), b.hit()).power;
        const double c = std::cos(deg * math::PI / 180.0);
        CHECK(t == doctest::Approx(c * c).epsilon(1e-9));
    }
    // A finite extinction ratio lets 1/ER through when crossed.
    materials::Polariser real(90.0, 1000.0, 1.0);
    CHECK(through(real, beam(PolarisationKind::Linear, 0.0), b.hit()).power ==
          doctest::Approx(1.0e-3));
}

TEST_CASE("polariser: unpolarised light passes k1 (1 + 1/ER) / 2 and leaves polarised on the axis") {
    Bench                b;
    materials::Polariser sheet(30.0, 1000.0, 0.77);
    const core::Ray      out = through(sheet, beam(PolarisationKind::Unpolarised), b.hit());
    CHECK(out.power == doctest::Approx(0.5 * 0.77 * (1.0 + 1e-3)));
    CHECK(out.polarised);
    // A second, identical polariser passes it all (times k1): it is polarised along THIS axis.
    materials::Polariser ideal_same(30.0, 1.0e12, 1.0);
    CHECK(through(ideal_same, out, b.hit()).power == doctest::Approx(out.power));
    materials::Polariser ideal_crossed(120.0, 1.0e12, 1.0);
    CHECK(through(ideal_crossed, out, b.hit()).power < 1e-9);
}

TEST_CASE("quarter-wave plate at 45 degrees: linear becomes circular, and a second one crosses it") {
    Bench                b;
    materials::Waveplate qwp(0.25, 45.0, 1.0);
    const core::Ray      in  = beam(PolarisationKind::Linear, 0.0);
    const core::Ray      mid = through(qwp, in, b.hit());
    CHECK(mid.power == doctest::Approx(1.0));
    CHECK(std::norm(mid.Es) == doctest::Approx(0.5));
    CHECK(std::norm(mid.Ep) == doctest::Approx(0.5));
    CHECK(std::fabs(rel_phase(mid)) == doctest::Approx(math::PI / 2));   // circular

    // Circular light: every polariser angle passes exactly half.
    for (double deg : {0.0, 20.0, 45.0, 90.0, 133.0}) {
        materials::Polariser pol(deg, 1.0e12, 1.0);
        CHECK(through(pol, mid, b.hit()).power == doctest::Approx(0.5));
    }

    // Two quarter-wave plates are a half-wave plate: vertical in, horizontal out.
    const core::Ray      out = through(qwp, mid, b.hit());
    materials::Polariser horiz(90.0, 1.0e12, 1.0), vert(0.0, 1.0e12, 1.0);
    CHECK(through(horiz, out, b.hit()).power == doctest::Approx(1.0));
    CHECK(through(vert, out, b.hit()).power < 1e-9);
}

TEST_CASE("half-wave plate rotates linear light by twice its fast-axis angle") {
    Bench b;
    for (double axis : {10.0, 22.5, 30.0, 45.0}) {
        materials::Waveplate hwp(0.5, axis, 1.0);
        const core::Ray      out = through(hwp, beam(PolarisationKind::Linear, 0.0), b.hit());
        materials::Polariser at_twice(2.0 * axis, 1.0e12, 1.0);
        CHECK(through(at_twice, out, b.hit()).power == doctest::Approx(1.0));
    }
}

TEST_CASE("waveplate leaves unpolarised light unpolarised and scales its power") {
    Bench                b;
    materials::Waveplate qwp(0.25, 0.0, 0.99);
    const core::Ray      out = through(qwp, beam(PolarisationKind::Unpolarised), b.hit());
    CHECK_FALSE(out.polarised);
    CHECK(out.power == doctest::Approx(0.99));
}

TEST_CASE("polarising beam splitter: p goes straight on, s turns, unpolarised splits 50:50") {
    // A 45-degree coating in the x-y plane: normal (-1, 1, 0)/sqrt2, beam along +X, the plane of
    // incidence is x-y, so s is along Z (vertical, laser 0 deg) and p is horizontal (90 deg).
    materials::PolarisingBeamSplitter pbs(1000.0);
    core::Hit                         h;
    h.position = {0, 0, 0};
    h.normal   = glm::normalize(math::vec3{-1.0, 1.0, 0.0});
    math::Rng rng(1);

    const auto s = pbs.interact(beam(PolarisationKind::Linear, 0.0), h, rng);
    CHECK(s.reflected.power == doctest::Approx(1.0 - 1e-3));
    CHECK(s.transmitted.power == doctest::Approx(1e-3));
    const auto p = pbs.interact(beam(PolarisationKind::Linear, 90.0), h, rng);
    CHECK(p.transmitted.power == doctest::Approx(1.0 - 1e-3));
    CHECK(p.reflected.power == doctest::Approx(1e-3));

    const auto u = pbs.interact(beam(PolarisationKind::Unpolarised), h, rng);
    CHECK(u.reflected.power == doctest::Approx(0.5));
    CHECK(u.transmitted.power == doctest::Approx(0.5));
    CHECK(u.reflected.polarised);
    CHECK(u.transmitted.polarised);
    // The transmitted arm is p: a second splitter sends almost all of it straight on again.
    const auto again = pbs.interact(u.transmitted, h, rng);
    CHECK(again.transmitted.power == doctest::Approx(0.5 * (1.0 - 1e-3)));
}

TEST_CASE("polarisation materials: built from the document, refused when impossible") {
    auto build = [](const char* type, nlohmann::json params) {
        io::MaterialDoc md;
        md.id     = "m";
        md.type   = type;
        md.params = std::move(params);
        return io::build_material(md);
    };
    CHECK(dynamic_cast<materials::Polariser*>(
        build("polariser", {{"transmission_axis_deg", 10.0}, {"extinction_ratio", 500.0}}).get()));
    CHECK(dynamic_cast<materials::Waveplate*>(build("waveplate", {{"retardance_waves", 0.5}}).get()));
    CHECK(dynamic_cast<materials::PolarisingBeamSplitter*>(
        build("polarising_beam_splitter", nlohmann::json::object()).get()));
    CHECK_THROWS(build("polariser", {{"extinction_ratio", 0.5}}));
    CHECK_THROWS(build("polariser", {{"transmission", 1.5}}));
    CHECK_THROWS(build("waveplate", {{"transmission", -0.1}}));
    CHECK_THROWS(build("polarising_beam_splitter", {{"extinction_ratio", 0.0}}));
}
