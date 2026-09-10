// Wave 5 validation coverage.
//
// Everything here pins behaviour that landed in commit 0eb2d6d with no test behind it:
// the below-horizon predicates, the zero-vector rejections that replaced silent NaN, the
// auto_fit input validation, and the DNI plumbing that replaced a hardcoded 1000 W/m^2.
//
// test_sun.cpp already covers the *happy* path of the angle conversions (round-trip,
// bearing convention, pole degeneracy, auto_fit geometry). This file deliberately covers
// only the rejection and boundary behaviour, so the two do not overlap.

#include <doctest/doctest.h>

#include "scrt/core/AABB.hpp"
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/io/ResultsExporter.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/sources/Pillbox.hpp"
#include "scrt/sources/SunSource.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

using namespace scrt::math;
using scrt::core::AABB;
using scrt::scene::Aperture;
using scrt::sources::SunAngles;
using scrt::sources::SunSource;

// ---------------------------------------------------------------------------
// below_horizon: the predicate that decides whether a sun should be simulated
// ---------------------------------------------------------------------------

TEST_CASE("below_horizon(SunAngles): the boundary sits at exactly 0, which is ABOVE") {
    // Elevation 0 is the sun sitting *on* the horizon: geometrically it is sunrise, the
    // aperture cosine is exactly sin(0) == 0, and Aperture::auto_fit accepts it (it
    // rejects only to_sun.z < 0). Calling 0 "below" would therefore make the predicate
    // disagree with auto_fit at the one elevation they both name, and would flag a
    // legitimate, if useless, sun as an error. The implementation is `elevation_deg < 0`,
    // a strict comparison, and this test pins that choice in both directions.
    CHECK_FALSE(SunSource::below_horizon({180.0, 0.0}));

    // Above the horizon: false at every elevation up to the zenith.
    for (double el : {1e-12, 0.5, 5.0, 23.5, 45.0, 89.999, 90.0})
        CHECK_FALSE(SunSource::below_horizon({180.0, el}));

    // Below: true from the first negative value onward.
    for (double el : {-1e-12, -0.5, -5.0, -30.0, -89.0, -90.0})
        CHECK(SunSource::below_horizon({180.0, el}));

    // Azimuth is irrelevant to the predicate — it must not leak into the decision.
    for (double az : {0.0, 90.0, 180.0, 271.5, 359.9}) {
        CHECK(SunSource::below_horizon({az, -10.0}));
        CHECK_FALSE(SunSource::below_horizon({az, 10.0}));
    }

    // Negative zero is not negative: `-0.0 < 0.0` is false. Slider and JSON arithmetic can
    // both produce -0.0, and it names the same horizon as +0.0, so it must classify the
    // same way. Pinned because a switch to `<=` or to std::signbit would silently break it.
    CHECK_FALSE(SunSource::below_horizon({180.0, -0.0}));
}

TEST_CASE("below_horizon(vec3): agrees with the angle form at every elevation") {
    // The vec3 overload tests the propagation direction (+z means light travels upward,
    // i.e. from underground). The two overloads must never disagree, because callers pick
    // whichever one they happen to hold.
    for (double az : {0.0, 45.0, 137.0, 300.0}) {
        for (double el : {-90.0, -45.0, -1.0, -1e-9, 0.0, 1e-9, 1.0, 45.0, 90.0}) {
            const SunAngles a{az, el};
            const vec3      d = SunSource::direction_from_angles(a);
            INFO("azimuth_deg = ", az, "  elevation_deg = ", el);
            CHECK(SunSource::below_horizon(d) == SunSource::below_horizon(a));
        }
    }
}

TEST_CASE("below_horizon(vec3): elevation 0 gives a zero z, which is not > 0") {
    // SunSource.hpp justifies the strict `> 0.0` comparison by asserting that elevation 0
    // "yields z == -0.0, which is not > 0". The conclusion is right and the guard is
    // correct, but the stated sign is NOT what the code produces: glm's unary minus is
    // implemented as `0 - v`, and 0.0 - 0.0 is +0.0, not -0.0. So z comes back as positive
    // zero. Either sign satisfies `> 0.0 == false`, which is why nothing is broken — but
    // the assertion below is deliberately written against the observed value rather than
    // the comment, so it cannot drift into pinning a claim that is not true.
    for (double az : {0.0, 90.0, 180.0, 270.0}) {
        const vec3 d = SunSource::direction_from_angles({az, 0.0});
        INFO("azimuth_deg = ", az);
        CHECK(d.z == 0.0);                  // True for either signed zero.
        CHECK_FALSE(std::signbit(d.z));     // It is specifically +0.0, not -0.0.
        CHECK_FALSE(SunSource::below_horizon(d));
    }

    // Straight underfoot is unambiguously below.
    CHECK(SunSource::below_horizon(vec3{0.0, 0.0, 1.0}));
    CHECK_FALSE(SunSource::below_horizon(vec3{0.0, 0.0, -1.0}));
}

TEST_CASE("sun_below_horizon(): a live source reports its own sun") {
    scrt::sources::Pillbox sun(SOLAR_HALF_ANGLE_RAD);

    // Default construction is the zenith, which every bundled scene uses.
    CHECK_FALSE(sun.sun_below_horizon());

    sun.set_sun_angles({180.0, 30.0});
    CHECK_FALSE(sun.sun_below_horizon());

    sun.set_sun_angles({180.0, 0.0});
    CHECK_FALSE(sun.sun_below_horizon());  // On the horizon, per the boundary above.

    sun.set_sun_angles({180.0, -0.5});
    CHECK(sun.sun_below_horizon());

    sun.set_sun_angles({180.0, -45.0});
    CHECK(sun.sun_below_horizon());

    // Reachable through the direction setter too, not just the angle setter.
    sun.set_sun_direction({0.0, 0.0, 1.0});
    CHECK(sun.sun_below_horizon());
    sun.set_sun_direction({0.0, 0.0, -1.0});
    CHECK_FALSE(sun.sun_below_horizon());
}

// ---------------------------------------------------------------------------
// direction_from_angles is total by design: a set sun still names a real direction
// ---------------------------------------------------------------------------

TEST_CASE("direction_from_angles: a below-horizon sun points light UPWARD, exactly") {
    // "Total" means it does not throw or clamp — it hands back the direction that
    // elevation genuinely names. Assert the actual components, not just the sign, so a
    // future clamp-to-zero cannot pass this test.
    //
    // Due south (azimuth 180) at elevation -30: to_sun = (sin180*cos(-30), cos180*cos(-30),
    // sin(-30)) = (0, -cos30, -0.5), so the propagation direction is its negation.
    const vec3 d = SunSource::direction_from_angles({180.0, -30.0});
    CHECK(std::abs(d.x) < 1e-15);
    CHECK(d.y == doctest::Approx(std::cos(30.0 * DEG2RAD)).epsilon(1e-12));
    CHECK(d.z == doctest::Approx(0.5).epsilon(1e-12));
    CHECK(d.z > 0.0);  // Light travelling up: the sun has set.

    // Unit length is preserved below the horizon as well.
    CHECK(glm::length(d) == doctest::Approx(1.0).epsilon(1e-15));

    // The nadir is the exact mirror of the zenith.
    const vec3 nadir = SunSource::direction_from_angles({0.0, -90.0});
    CHECK(nadir.x == 0.0);
    CHECK(nadir.y == 0.0);
    CHECK(nadir.z == 1.0);
}

TEST_CASE("Round-trip survives below the horizon, because the mapping is total") {
    // test_sun.cpp already sweeps a grid that includes negative elevations. This case
    // states the *reason* separately and checks the extremes it does not reach, so the
    // totality contract is pinned even if that sweep is retuned.
    const double azimuths[]   = {0.0, 12.5, 90.0, 180.0, 264.0, 359.0};
    const double elevations[] = {-89.9, -75.0, -60.0, -30.0, -10.0, -0.001};

    for (double az : azimuths) {
        for (double el : elevations) {
            const SunAngles a{az, el};
            const vec3      d = SunSource::direction_from_angles(a);
            REQUIRE(SunSource::below_horizon(d));  // Still recognised as set.

            const SunAngles r = SunSource::angles_from_direction(d);
            INFO("azimuth_deg = ", az, "  elevation_deg = ", el);
            CHECK(std::abs(r.azimuth_deg   - az) < 1e-9);
            CHECK(std::abs(r.elevation_deg - el) < 1e-9);
            CHECK_FALSE(std::isnan(r.azimuth_deg));
            CHECK_FALSE(std::isnan(r.elevation_deg));
        }
    }
}

// ---------------------------------------------------------------------------
// Zero-length directions: rejection instead of a silent NaN pair
// ---------------------------------------------------------------------------

TEST_CASE("angles_from_direction({0,0,0}) never produces NaN") {
    // The Audit A1 finding was that glm::normalize divided by zero here and returned a
    // NaN azimuth/elevation pair, which then spread into the scene document, the sun
    // widget and any saved JSON. The fix guards the length first and only then normalizes
    // through math::safe_normalize, so the zero vector is rejected outright.
    //
    // Rejection, not a sentinel: a zero vector names no sun, and every finite pair of
    // angles it could return would be a lie the caller cannot distinguish from a real sun.
    CHECK_THROWS_AS(SunSource::angles_from_direction({0.0, 0.0, 0.0}),
                    std::invalid_argument);

    // Also with an explicit fallback azimuth: the fallback covers the *pole*, where the
    // direction is real but the azimuth is degenerate. It must not rescue a zero vector.
    CHECK_THROWS_AS(SunSource::angles_from_direction({0.0, 0.0, 0.0}, 137.0),
                    std::invalid_argument);

    // Signed zeros are still zero length.
    CHECK_THROWS_AS(SunSource::angles_from_direction({-0.0, -0.0, -0.0}),
                    std::invalid_argument);

    // A vector that is merely small is NOT zero, and must still be answered normally: the
    // guard is `dot <= 0`, not an epsilon, so a short direction is a valid sun.
    const SunAngles tiny = SunSource::angles_from_direction({0.0, 0.0, -1e-20});
    CHECK_FALSE(std::isnan(tiny.elevation_deg));
    CHECK(tiny.elevation_deg == doctest::Approx(90.0).epsilon(1e-12));

    // Below roughly 1e-154 per component the guard rejects a direction that is genuinely
    // non-zero, because it tests the SQUARED length and dot() underflows to 0.0 first.
    // That is a real (if unreachable) limit of the check, not a bug: a direction that
    // small carries no usable precision anyway, and rejecting is the safe end to fail on.
    // Pinned so the behaviour is documented rather than rediscovered.
    CHECK_THROWS_AS(SunSource::angles_from_direction({0.0, 0.0, -1e-300}),
                    std::invalid_argument);
}

TEST_CASE("set_sun_direction({0,0,0}) is rejected and leaves the sun untouched") {
    scrt::sources::Pillbox sun(SOLAR_HALF_ANGLE_RAD);
    sun.set_sun_angles({180.0, 42.0});
    const vec3 before = sun.sun_direction();

    CHECK_THROWS_AS(sun.set_sun_direction({0.0, 0.0, 0.0}), std::invalid_argument);

    // Strong guarantee: the throw happens before any assignment, so the previous sun
    // survives intact rather than being left NaN or half-written.
    CHECK(sun.sun_direction().x == before.x);
    CHECK(sun.sun_direction().y == before.y);
    CHECK(sun.sun_direction().z == before.z);
    CHECK_FALSE(std::isnan(sun.sun_direction().z));

    // sun_angles() goes through angles_from_direction, so it must stay answerable.
    const SunAngles a = sun.sun_angles();
    CHECK(a.elevation_deg == doctest::Approx(42.0).epsilon(1e-9));
}

// ---------------------------------------------------------------------------
// Aperture::auto_fit input validation
// ---------------------------------------------------------------------------

namespace {

/// A well-formed 2 x 2 x 2 m box centred on the origin; half-diagonal R = sqrt(3).
AABB unit_box() { return AABB({-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0}); }

} // namespace

TEST_CASE("auto_fit: margin 0 is accepted and gives exactly the half-diagonal radius") {
    const AABB   bounds = unit_box();
    const vec3   to_sun{0.0, 0.0, 1.0};
    const double R = std::sqrt(3.0);  // 0.5 * |(2,2,2)|

    const Aperture ap = Aperture::auto_fit(bounds, to_sun, 0.0);
    CHECK(ap.margin == 0.0);
    CHECK(ap.radius == doctest::Approx(R).epsilon(1e-12));
    // With no slack the disk still exactly covers the bounds — that is what makes 0 the
    // legitimate floor rather than an off-by-one that should have been rejected.
    CHECK(ap.covers(bounds, to_sun));
    CHECK(ap.cosine_to(to_sun) == doctest::Approx(1.0).epsilon(1e-12));

    // Negative zero compares as `-0.0 < 0.0 == false`, so it is accepted and behaves
    // identically to +0.0. Pinned so the guard is not "tightened" into rejecting it.
    const Aperture nz = Aperture::auto_fit(bounds, to_sun, -0.0);
    CHECK(nz.radius == doctest::Approx(R).epsilon(1e-12));
}

TEST_CASE("auto_fit: a negative margin is rejected") {
    const AABB bounds = unit_box();
    const vec3 to_sun{0.0, 0.0, 1.0};

    // A negative margin shrinks the disk inside the bounds it is supposed to cover, so
    // covers() would fail on auto_fit's own output — an object that violates its own
    // postcondition. Reject rather than return it.
    for (double m : {-1e-12, -0.01, -0.05, -0.5, -1.0, -1e6}) {
        INFO("margin = ", m);
        CHECK_THROWS_AS(Aperture::auto_fit(bounds, to_sun, m), std::invalid_argument);
    }
}

TEST_CASE("auto_fit: a non-finite margin is rejected") {
    const AABB bounds = unit_box();
    const vec3 to_sun{0.0, 0.0, 1.0};

    const double nan      = std::numeric_limits<double>::quiet_NaN();
    const double inf      = std::numeric_limits<double>::infinity();
    const double signaling = std::numeric_limits<double>::signaling_NaN();

    CHECK_THROWS_AS(Aperture::auto_fit(bounds, to_sun, nan),       std::invalid_argument);
    CHECK_THROWS_AS(Aperture::auto_fit(bounds, to_sun, signaling), std::invalid_argument);
    CHECK_THROWS_AS(Aperture::auto_fit(bounds, to_sun,  inf),      std::invalid_argument);
    // -inf is caught by the isfinite arm, not the `< 0` arm; both must fire.
    CHECK_THROWS_AS(Aperture::auto_fit(bounds, to_sun, -inf),      std::invalid_argument);
}

TEST_CASE("auto_fit: an inside-out AABB is rejected, never yielding inf or NaN") {
    // This was an open Audit A1 finding: a default-constructed AABB is inside-out
    // (min = +DBL_MAX, max = -DBL_MAX), so the half-diagonal is +inf and both the radius
    // and the centre came back infinite. Wave 5 added the extent check; this pins it.
    const vec3 to_sun{0.0, 0.0, 1.0};

    const AABB fresh;  // Never expanded.
    CHECK_THROWS_AS(Aperture::auto_fit(fresh, to_sun, 0.05), std::invalid_argument);

    // Partially inside-out on a single axis must be caught too, not just the pristine one.
    CHECK_THROWS_AS(Aperture::auto_fit(AABB({1.0, -1.0, -1.0}, {-1.0, 1.0, 1.0}),
                                       to_sun, 0.05), std::invalid_argument);
    CHECK_THROWS_AS(Aperture::auto_fit(AABB({-1.0, 5.0, -1.0}, {1.0, 4.0, 1.0}),
                                       to_sun, 0.05), std::invalid_argument);
    CHECK_THROWS_AS(Aperture::auto_fit(AABB({-1.0, -1.0, 0.5}, {1.0, 1.0, -0.5}),
                                       to_sun, 0.05), std::invalid_argument);

    // A flat (zero-extent) box is degenerate but NOT inside-out — a planar reflector is a
    // real scene, and the extent check is `< 0`, so it must still be accepted.
    const AABB flat({-1.0, -1.0, 0.0}, {1.0, 1.0, 0.0});
    const Aperture ap = Aperture::auto_fit(flat, to_sun, 0.05);
    CHECK(std::isfinite(ap.radius));
    CHECK(ap.radius > 0.0);
    CHECK(std::isfinite(ap.center.x));
    CHECK(std::isfinite(ap.center.y));
    CHECK(std::isfinite(ap.center.z));
    CHECK(ap.covers(flat, to_sun));
}

TEST_CASE("auto_fit: a below-horizon sun is rejected") {
    const AABB bounds = unit_box();

    // Fitting to a set sun puts the disk *underneath* the cooker and lights it from
    // underground, producing a plausible-looking nonzero power out of a sun that is not
    // there. That is the failure mode this check exists to prevent.
    for (double el : {-0.001, -5.0, -30.0, -90.0}) {
        const vec3 to_sun = -SunSource::direction_from_angles({180.0, el});
        INFO("elevation_deg = ", el);
        REQUIRE(to_sun.z < 0.0);
        CHECK_THROWS_AS(Aperture::auto_fit(bounds, to_sun, 0.05), std::invalid_argument);
    }

    // Exactly on the horizon is accepted: to_sun.z is -0.0 (or +0.0) and the guard is a
    // strict `< 0.0`. The resulting disk is edge-on and collects nothing, which is the
    // physically correct answer for sunrise rather than an error.
    const vec3 horizon = -SunSource::direction_from_angles({180.0, 0.0});
    const Aperture ap  = Aperture::auto_fit(bounds, horizon, 0.05);
    CHECK(std::isfinite(ap.radius));
    CHECK(ap.covers(bounds, horizon));
}

TEST_CASE("auto_fit: a zero-length to_sun is rejected") {
    CHECK_THROWS_AS(Aperture::auto_fit(unit_box(), {0.0, 0.0, 0.0}, 0.05),
                    std::invalid_argument);
    CHECK_THROWS_AS(Aperture::auto_fit(unit_box(), {-0.0, -0.0, -0.0}, 0.05),
                    std::invalid_argument);
}

// ---------------------------------------------------------------------------
// DNI: the value the user set must reach the concentration ratio and the export
// ---------------------------------------------------------------------------
//
// The Wave 5 bug was a hardcoded 1000.0 W/m^2 in FluxPlotter, which made the displayed
// concentration ratio ignore the DNI slider and baked that fabricated figure into every
// exported summary. FluxPlotter itself lives in scrt_viz and needs a live ImGui frame, so
// it is out of reach of this executable (scrt_tests links scrt_core only). What IS in
// reach is the pair of core entry points the fix routes DNI into — and they are where a
// regression would actually change a number the user reads.

namespace {

/// A deterministic accumulator: one 4 W ray in the centre bin of a 0.125 x 0.125 m cell.
///
/// The grid is 8 x 8 rather than 10 x 10 on purpose. bin_width = 2*0.5/8 = 0.125 is a
/// power of two and therefore exact in binary, so bin_area = 0.015625 is exact and the
/// peak flux is exactly 256 W/m^2. A 10 x 10 grid gives bin_area = 0.010000000000000002
/// and a peak of 399.99999999999994, which silently breaks any `== 1.0` assertion below.
scrt::tracer::FluxAccumulator make_known_accumulator() {
    scrt::tracer::FluxAccumulator acc(0.5, 0.5, 8, 8);

    scrt::core::Ray r;
    r.power = 4.0;
    scrt::core::Hit h;
    h.uv = {0.0, 0.0};
    acc.deposit(r, h);

    acc.finalize(1);
    return acc;
}

} // namespace

TEST_CASE("concentration_ratio tracks DNI instead of assuming 1000 W/m^2") {
    const auto acc = make_known_accumulator();

    CHECK(acc.total_power_w() == 4.0);      // Exact: a single deposit, no summation error.
    CHECK(acc.peak_flux_wm2() == 256.0);    // Exact: 4 / 0.015625.

    // Real DNI values the slider can reach. If any of these collapsed back onto the old
    // hardcoded 1000, the 500 and 1500 cases would both read 0.256.
    CHECK(acc.concentration_ratio(1000.0) == doctest::Approx(0.256).epsilon(1e-12));
    CHECK(acc.concentration_ratio(500.0)  == doctest::Approx(0.512).epsilon(1e-12));
    CHECK(acc.concentration_ratio(1500.0) == doctest::Approx(256.0 / 1500.0).epsilon(1e-12));

    // Power-of-two DNIs make the quotient exactly representable, so these are `==`, not
    // Approx: no tolerance to hide a small systematic error in.
    CHECK(acc.concentration_ratio(256.0)  == 1.0);
    CHECK(acc.concentration_ratio(512.0)  == 0.5);
    CHECK(acc.concentration_ratio(128.0)  == 2.0);

    // The relationship is strictly 1/DNI: halving the irradiance doubles the ratio.
    CHECK(acc.concentration_ratio(500.0) ==
          doctest::Approx(2.0 * acc.concentration_ratio(1000.0)).epsilon(1e-12));
}

TEST_CASE("export_summary_json writes the DNI it was given, not a hardcoded 1000") {
    const auto acc = make_known_accumulator();

    scrt::tracer::TraceResult result;
    result.primary_rays_traced = 1234;
    result.total_hits          = 7;
    result.wall_time_s         = 0.5;

    const auto dir = std::filesystem::temp_directory_path() / "scrt_sun_validation";
    std::filesystem::create_directories(dir);

    // Two exports of the SAME accumulator at different DNI must differ in exactly the
    // concentration ratio: that is the field the fabricated 1000 used to pin flat.
    struct Case { double dni; double expected_ratio; const char* file; };
    const Case cases[] = {
        { 500.0,  256.0 /  500.0, "summary_500.json"  },
        {1000.0,  256.0 / 1000.0, "summary_1000.json" },
        {1500.0,  256.0 / 1500.0, "summary_1500.json" },
    };

    for (const auto& c : cases) {
        const auto path = dir / c.file;
        scrt::io::export_summary_json(acc, result, c.dni, path);

        nlohmann::json j;
        {
            // Scoped: on Windows the reader must be closed before remove() below, or the
            // erase fails with a sharing violation and takes the whole case down with it.
            std::ifstream in(path);
            REQUIRE(in.good());
            in >> j;
        }

        INFO("dni_wm2 = ", c.dni);
        CHECK(j.at("concentration_ratio").get<double>() ==
              doctest::Approx(c.expected_ratio).epsilon(1e-12));

        // Power and peak flux are properties of the accumulator alone and must NOT move
        // with DNI — otherwise a "fix" that scaled the wrong quantity would pass above.
        CHECK(j.at("total_power_w").get<double>() == doctest::Approx(4.0).epsilon(1e-12));
        CHECK(j.at("peak_flux_wm2").get<double>() == doctest::Approx(256.0).epsilon(1e-12));

        std::filesystem::remove(path);
    }

    std::filesystem::remove(dir);
}

TEST_CASE("A sun's DNI is what reaches the export, end to end") {
    // The GUI path is: slider -> SunSource::set_dni -> sun->dni() -> the export. Pin the
    // handoff so a caller cannot quietly substitute a literal again.
    scrt::sources::Pillbox sun(SOLAR_HALF_ANGLE_RAD);
    CHECK(sun.dni() == 1000.0);  // Documented default.

    sun.set_dni(732.5);
    CHECK(sun.dni() == 732.5);

    const auto acc = make_known_accumulator();
    CHECK(acc.concentration_ratio(sun.dni()) ==
          doctest::Approx(256.0 / 732.5).epsilon(1e-12));

    // And the slider's own extremes, which are the values a user can actually reach.
    sun.set_dni(500.0);
    CHECK(acc.concentration_ratio(sun.dni()) == doctest::Approx(0.512).epsilon(1e-12));
    sun.set_dni(1500.0);
    CHECK(acc.concentration_ratio(sun.dni()) ==
          doctest::Approx(256.0 / 1500.0).epsilon(1e-12));
}
