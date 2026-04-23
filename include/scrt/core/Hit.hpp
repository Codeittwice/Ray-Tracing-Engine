#pragma once
#include "scrt/math/Vec.hpp"

namespace scrt::surfaces { class Surface; }

namespace scrt::core {

/// Intersection record filled by Surface::intersect.
struct Hit {
    double                      t          = 0.0;
    math::vec3                  position   {};
    math::vec3                  normal     {};  ///< Unit, oriented against incoming ray.
    math::vec2                  uv         {};  ///< Surface parametric coordinates.
    bool                        front_face = true;  ///< True if ray hits the outer surface (entering medium).
    const surfaces::Surface*    surface    = nullptr;
};

} // namespace scrt::core
