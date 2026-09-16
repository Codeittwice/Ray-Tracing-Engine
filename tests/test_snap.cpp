#include <doctest/doctest.h>

#include "scrt/viz/Snap.hpp"

#include <cmath>
#include <limits>

using scrt::viz::snap_scale;
using scrt::viz::snap_to;

TEST_CASE("snap_to rounds to the nearest multiple of the step") {
    CHECK(snap_to(0.123, 0.01) == doctest::Approx(0.12));
    CHECK(snap_to(0.126, 0.01) == doctest::Approx(0.13));
    CHECK(snap_to(-0.126, 0.01) == doctest::Approx(-0.13));
    CHECK(snap_to(22.0, 15.0) == doctest::Approx(15.0));
    CHECK(snap_to(23.0, 15.0) == doctest::Approx(30.0));
    CHECK(snap_to(0.0, 0.01) == 0.0);
}

TEST_CASE("snap_to is idempotent, so re-snapping every frame cannot drift") {
    for (double v : {0.0, 0.37, -1.234567, 12.5, 359.9}) {
        for (double step : {0.001, 0.01, 0.1, 15.0}) {
            const double once  = snap_to(v, step);
            const double twice = snap_to(once, step);
            CHECK(twice == doctest::Approx(once).epsilon(1e-12));
        }
    }
}

TEST_CASE("a non-positive or NaN step leaves the value alone, bit for bit") {
    CHECK(snap_to(0.123456789, 0.0) == 0.123456789);
    CHECK(snap_to(0.123456789, -1.0) == 0.123456789);
    CHECK(snap_to(0.123456789, std::numeric_limits<double>::quiet_NaN()) == 0.123456789);
    CHECK(snap_scale(0.123456789, 0.0) == 0.123456789);
}

TEST_CASE("snap_scale never rounds a scale down to zero") {
    CHECK(snap_scale(0.04, 0.1) == doctest::Approx(0.1));
    CHECK(snap_scale(0.0, 0.1) == doctest::Approx(0.1));
    CHECK(snap_scale(1.26, 0.1) == doctest::Approx(1.3));
    CHECK(snap_scale(1.24, 0.1) == doctest::Approx(1.2));
}
