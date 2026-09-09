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

/// How much of a scaling transform a surface stays physically meaningful under.
enum class ScaleSupport {
    Free,         ///< Any invertible scale, including non-uniform, is still the same surface.
    UniformOnly,  ///< Non-uniform scale traces, but destroys the defining optical property.
    None          ///< Scale is not supported at all.
};

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

    /// Local-frame AABB enclosing the surface's own geometry.
    virtual core::AABB local_bounds() const = 0;

    /// Which scaling transforms keep this surface physically meaningful.
    virtual ScaleSupport scale_support() const { return ScaleSupport::Free; }

    /// World-space AABB for BVH construction: the 8 transformed corners of local_bounds().
    core::AABB world_bounds() const {
        const core::AABB lb = local_bounds();
        core::AABB box;
        for (double x : {lb.min().x, lb.max().x})
            for (double y : {lb.min().y, lb.max().y})
                for (double z : {lb.min().z, lb.max().z})
                    box.expand(xform_.point_to_world({x, y, z}));
        return box;
    }

    /// Triangulation for visualization; appends to verts/indices.
    virtual void tessellate(int nseg, std::vector<math::vec3>& verts,
                            std::vector<std::uint32_t>& indices) const = 0;

    void set_transform(core::Transform t) { xform_ = std::move(t); }
    const core::Transform& transform() const { return xform_; }

    void set_material(const materials::Material* m) { material_ = m; }
    const materials::Material* material() const { return material_; }

    void set_name(std::string s) { name_ = std::move(s); }
    const std::string& name() const { return name_; }

    /// Stable identity assigned by the owning Scene; 0 until added to a scene.
    std::uint64_t id() const { return id_; }
    /// Sets the stable identity; called by Scene::add_surface only.
    void set_id(std::uint64_t v) { id_ = v; }

protected:
    Surface() = default;
    core::Transform           xform_;
    const materials::Material* material_ = nullptr;
    std::string               name_;

private:
    std::uint64_t id_ = 0;
};

} // namespace scrt::surfaces
