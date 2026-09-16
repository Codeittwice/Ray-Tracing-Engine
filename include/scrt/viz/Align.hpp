#pragma once

#include "scrt/math/Vec.hpp"

#include <cmath>

namespace scrt::viz {

/// The alignment axis: a line elements can be centred on and turned to face along.
///
/// Tool state, like the snap setting - never saved with the scene. It starts on the first laser's
/// beam (the usual optical axis of a bench) and can be re-set from any selected object.
struct AlignAxis {
    bool       valid     = false;          ///< False until something defines it.
    bool       show      = true;           ///< Draw the line in the 3D view.
    math::vec3 origin    {0.0, 0.0, 0.0};  ///< A point on the axis [m].
    math::vec3 direction {1.0, 0.0, 0.0};  ///< Unit direction.
};

/// The one process-wide alignment axis.
inline AlignAxis& align_axis() {
    static AlignAxis a;
    return a;
}

/// The point on the line (origin, direction) nearest to p. `direction` need not be unit length.
inline math::vec3 closest_point_on_axis(math::vec3 p, math::vec3 origin, math::vec3 direction) {
    const double len2 = glm::dot(direction, direction);
    if (!(len2 > 0.0)) return origin;
    return origin + (glm::dot(p - origin, direction) / len2) * direction;
}

/// The smallest rotation taking unit vector `from` onto whichever of +to / -to is nearer.
///
/// "Face along the axis" means the part's optical axis lies ON the line; which way round it faces
/// is the user's choice already expressed in the current pose, so the rotation never flips a
/// part over. Rodrigues' formula; exactly the identity when the two are already parallel.
inline math::mat3 rotation_onto_axis(math::vec3 from, math::vec3 to) {
    from = glm::normalize(from);
    to   = glm::normalize(to);
    if (glm::dot(from, to) < 0.0) to = -to;
    const math::vec3 v = glm::cross(from, to);
    const double     s = glm::length(v);
    const double     c = glm::dot(from, to);
    if (s < 1e-15) return math::mat3(1.0);
    const math::mat3 K(0.0, v.z, -v.y,    // column 0
                       -v.z, 0.0, v.x,    // column 1
                       v.y, -v.x, 0.0);   // column 2
    return math::mat3(1.0) + K + K * K * ((1.0 - c) / (s * s));
}

} // namespace scrt::viz
