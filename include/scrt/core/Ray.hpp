#pragma once
#include "scrt/math/Vec.hpp"
#include <cstdint>

namespace scrt::core {

/// A ray carrying optical power through the scene.
struct Ray {
    math::vec3    origin      {0.0};
    math::vec3    direction   {0.0, 0.0, 1.0};  ///< Unit vector.
    double        power       {1.0};             ///< Watts.
    double        wavelength_nm{550.0};          ///< Nanometres (I/O only; physics in metres).
    int           bounces     {0};
    std::uint32_t id          {0};
};

} // namespace scrt::core
