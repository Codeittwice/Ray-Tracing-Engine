#pragma once
#include "scrt/accel/BVH.hpp"
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/materials/Material.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/sources/SunSource.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace scrt::scene {

/// Owns all optical elements; provides world-space intersection.
/// Call build_acceleration_structure() after adding all surfaces for BVH-accelerated traversal.
class Scene {
public:
    /// Takes ownership of a material; returns its index.
    std::size_t add_material(std::unique_ptr<materials::Material> m);

    /// Takes ownership of a surface, assigns it a stable non-zero id, and returns that id.
    std::uint64_t add_surface(std::unique_ptr<surfaces::Surface> s);

    /// Removes the surface with the given id, if present; marks the acceleration structure dirty.
    bool remove_surface(std::uint64_t id);

    /// Finds a surface by its stable id, or nullptr if not present.
    surfaces::Surface* surface_by_id(std::uint64_t id);
    /// Finds a surface by its stable id, or nullptr if not present.
    const surfaces::Surface* surface_by_id(std::uint64_t id) const;

    /// Finds the current index of a surface by its stable id, or nullopt if not present.
    std::optional<std::size_t> index_of(std::uint64_t id) const;

    /// Marks the acceleration structure stale; forces Scene::intersect to fall back to a linear scan.
    void mark_acceleration_dirty() { accel_dirty_ = true; }
    /// True when the acceleration structure no longer reflects surfaces_.
    bool acceleration_dirty() const { return accel_dirty_; }

    void set_receiver(std::unique_ptr<Receiver> r);
    void set_sun(std::unique_ptr<sources::SunSource> s);

    /// Build BVH over all currently added surfaces. Must be called before run().
    void build_acceleration_structure();

    std::span<const std::unique_ptr<surfaces::Surface>> surfaces()  const;
    /// Non-const span over materials — for live parameter editing in the viewer.
    std::span<std::unique_ptr<materials::Material>>       mutable_materials();
    /// Non-const span over surfaces — for live transform editing in the viewer.
    std::span<std::unique_ptr<surfaces::Surface>>         mutable_surfaces();
    const Receiver*      receiver() const { return receiver_.get(); }
    Receiver*            receiver()       { return receiver_.get(); }
    const sources::SunSource* sun()  const { return sun_.get(); }
          sources::SunSource* sun()        { return sun_.get(); }

    /// The aperture disk to DRAW, or nullptr when no source has one.
    ///
    /// PRESENTATION ONLY. The tracer must not call this: a source owns its aperture and
    /// expresses it through total_power_w() and sample_ray(); the scene no longer has one.
    const Aperture* display_aperture() const {
        return sun_ ? &sun_->aperture() : nullptr;
    }

    /// World-space AABB over every surface AND every receiver face; empty scene -> zero box.
    core::AABB world_bounds() const;

    /// World-space closest-hit across all surfaces + receiver.
    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const;

private:
    std::vector<std::unique_ptr<materials::Material>> materials_;
    std::vector<std::unique_ptr<surfaces::Surface>>   surfaces_;
    std::unique_ptr<Receiver>                         receiver_;
    std::unique_ptr<sources::SunSource>               sun_;
    accel::BVH                                        bvh_;
    bool                                              use_bvh_ = false;
    bool                                              accel_dirty_ = false;
    std::uint64_t                                     next_surface_id_ = 1;
};

} // namespace scrt::scene
