#pragma once
#include "scrt/math/Vec.hpp"
#include <cstdint>
#include <random>

namespace scrt::math {

/// Mersenne Twister RNG wrapper; create one per thread for parallel traces.
class Rng {
public:
    Rng();                     ///< Seeds from std::random_device.
    explicit Rng(uint64_t s);  ///< Deterministic seed for testing.

    double uniform01();                  ///< Uniform sample in [0, 1).
    double uniform(double a, double b);  ///< Uniform sample in [a, b).
    vec2   unit_disk_concentric();       ///< Point on unit disk (Shirley mapping).
    vec3   unit_sphere_direction();      ///< Direction uniform on unit sphere.
    void   seed(uint64_t s);            ///< Reseed for deterministic testing.

private:
    std::mt19937_64                     engine_;
    std::uniform_real_distribution<double> dist_{0.0, 1.0};
};

} // namespace scrt::math
