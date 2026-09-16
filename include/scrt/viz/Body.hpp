#pragma once

#include "scrt/io/SceneDocument.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/surfaces/Surface.hpp"

#include <cstdint>
#include <vector>

namespace scrt::viz {

/// The two drawn-only parts an element's body can have; drawn as separate meshes because a
/// splitter cube is see-through and a post is not.
enum class BodyPart {
    Mount, ///< The substrate slab behind the surface, or the cube around it.
    Post,  ///< A 6 mm post from the part's lowest point down to z = 0.
};

/// Post radius, metres. A real lab post is 12.7 mm across; the user found that visually heavy for
/// parts of 25 mm, so the drawn post is 6 mm. Presentation only - nothing traces it.
inline constexpr double kPostRadius = 0.003;

/// World-space triangles for one part of an element's body under the surface's CURRENT transform.
///
/// Returns false, with both vectors empty, when that part is not asked for or has nothing to
/// draw (a post on a part that already reaches z = 0, a cube on a surface with no extent).
/// Nothing produced here is ever traced: it is what the part is mounted in, not what it does.
bool tessellate_body(const surfaces::Surface& surf, const io::BodyDoc& body, BodyPart part,
                     std::vector<math::vec3>& verts, std::vector<std::uint32_t>& indices);

} // namespace scrt::viz
