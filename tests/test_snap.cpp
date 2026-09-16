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

TEST_CASE("grid_positions stays inside the range and coarsens to the line budget") {
    double used = 0.0;
    auto p = scrt::viz::grid_positions(-0.034, 0.051, 0.01, 100, used);
    CHECK(used == 0.01);
    REQUIRE(p.size() == 9);                        // -0.03 .. 0.05
    CHECK(p.front() == doctest::Approx(-0.03));
    CHECK(p.back() == doctest::Approx(0.05));

    p = scrt::viz::grid_positions(-1.0, 1.0, 0.01, 50, used);
    CHECK(p.size() <= 50);
    CHECK(used == doctest::Approx(0.08));          // 0.01 doubled until 2/used + 1 <= 50

    // An endpoint that is itself a multiple must not be pushed past the range by rounding.
    p = scrt::viz::grid_positions(0.0, 0.3, 0.1, 100, used);
    for (double x : p) {
        CHECK(x >= 0.0);
        CHECK(x <= 0.3);
    }
    CHECK(p.size() == 4);

    CHECK(scrt::viz::grid_positions(1.0, 0.0, 0.1, 100, used).empty());
    CHECK(scrt::viz::grid_positions(0.0, 1.0, 0.0, 100, used).empty());
}

TEST_CASE("snap_scale never rounds a scale down to zero") {
    CHECK(snap_scale(0.04, 0.1) == doctest::Approx(0.1));
    CHECK(snap_scale(0.0, 0.1) == doctest::Approx(0.1));
    CHECK(snap_scale(1.26, 0.1) == doctest::Approx(1.3));
    CHECK(snap_scale(1.24, 0.1) == doctest::Approx(1.2));
}
