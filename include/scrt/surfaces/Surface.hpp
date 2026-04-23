#pragma once
#include "scrt/core/AABB.hpp"
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/math/Vec.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace scrt::materials { class Material; }

namespace scrt::surfaces {

/// Abstract optical surface; geometry defined in local canonical frame, placed via Transform.
class Surface {
public:
    virtual ~Surface() = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    Surface(Surface&&) = delete;
    Surface& operator=(Surface&&) = delete;

    /// Intersect ray in world space; transforms to local, solves, transforms hit back.
    virtual bool intersect(const core::Ray& r, double t_min, double t_max,
                           core::Hit& hit) const = 0;

    /// World-space AABB for BVH construction.
    virtual core::AABB world_bounds() const = 0;

    /// Triangulation for visualization; appends to verts/indices.
    virtual void tessellate(int nseg, std::vector<math::vec3>& verts,
                            std::vector<std::uint32_t>& indices) const = 0;

    void set_transform(core::Transform t) { xform_ = std::move(t); }
    const core::Transform& transform() const { return xform_; }

    void set_material(const materials::Material* m) { material_ = m; }
    const materials::Material* material() const { return material_; }

    void set_name(std::string s) { name_ = std::move(s); }
    const std::string& name() const { return name_; }

protected:
    Surface() = default;
    core::Transform           xform_;
    const materials::Material* material_ = nullptr;
    std::string               name_;
};

} // namespace scrt::surfaces
