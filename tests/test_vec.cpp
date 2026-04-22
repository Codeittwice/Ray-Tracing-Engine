#include <doctest/doctest.h>
#include "scrt/math/Vec.hpp"
#include "scrt/math/Rng.hpp"

using namespace scrt::math;

TEST_CASE("Rng uniform01 stays in [0,1)") {
    Rng rng(42);
    for (int i = 0; i < 10000; ++i) {
        double v = rng.uniform01();
        CHECK(v >= 0.0);
        CHECK(v < 1.0);
    }
}

TEST_CASE("Rng uniform(a,b) stays in [a,b)") {
    Rng rng(7);
    for (int i = 0; i < 1000; ++i) {
        double v = rng.uniform(-3.0, 5.0);
        CHECK(v >= -3.0);
        CHECK(v < 5.0);
    }
}

TEST_CASE("Rng unit_sphere_direction has unit length") {
    Rng rng(123);
    for (int i = 0; i < 200; ++i) {
        vec3 d = rng.unit_sphere_direction();
        CHECK(glm::length(d) == doctest::Approx(1.0).epsilon(1e-12));
    }
}

TEST_CASE("Rng unit_disk_concentric lies within unit disk") {
    Rng rng(456);
    for (int i = 0; i < 200; ++i) {
        vec2 p = rng.unit_disk_concentric();
        CHECK(glm::dot(p, p) <= 1.0 + 1e-12);
    }
}

TEST_CASE("Rng seed produces deterministic output") {
    Rng a(99), b(99);
    for (int i = 0; i < 20; ++i)
        CHECK(a.uniform01() == b.uniform01());
}

TEST_CASE("safe_normalize produces unit vector") {
    vec3 v{3.0, 4.0, 0.0};
    CHECK(glm::length(safe_normalize(v)) == doctest::Approx(1.0).epsilon(1e-14));
}
