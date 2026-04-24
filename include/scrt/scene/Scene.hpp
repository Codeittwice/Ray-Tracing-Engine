#pragma once
#include "scrt/accel/BVH.hpp"
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/materials/Material.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/sources/SunSource.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <memory>
#include <span>
#include <vector>

namespace scrt::scene {

/// Owns all optical elements; provides world-space intersection.
/// Call build_acceleration_structure() after adding all surfaces for BVH-accelerated traversal.
class Scene {
public:
    /// Takes ownership of a material; returns its index.
    std::size_t add_material(std::unique_ptr<materials::Material> m);

    /// Takes ownership of a surface; returns its index.
    std::size_t add_surface(std::unique_ptr<surfaces::Surface> s);

    void set_receiver(std::unique_ptr<Receiver> r);
    void set_sun(std::unique_ptr<sources::SunSource> s);
    void set_aperture(Aperture a);

    /// Build BVH over all currently added surfaces. Must be called before run().
    void build_acceleration_structure();

    std::span<const std::unique_ptr<surfaces::Surface>> surfaces()  const;
    const Receiver*      receiver() const { return receiver_.get(); }
    Receiver*            receiver()       { return receiver_.get(); }
    const sources::SunSource* sun()  const { return sun_.get(); }
    const Aperture&      aperture()  const { return aperture_; }

    /// World-space closest-hit across all surfaces + receiver.
    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const;

private:
    std::vector<std::unique_ptr<materials::Material>> materials_;
    std::vector<std::unique_ptr<surfaces::Surface>>   surfaces_;
    std::unique_ptr<Receiver>                         receiver_;
    std::unique_ptr<sources::SunSource>               sun_;
    Aperture                                          aperture_;
    accel::BVH                                        bvh_;
    bool                                              use_bvh_ = false;
};

} // namespace scrt::scene
