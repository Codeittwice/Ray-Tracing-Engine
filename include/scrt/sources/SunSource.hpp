#pragma once
#include "scrt/core/Ray.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"

namespace scrt::scene { struct Aperture; }

namespace scrt::sources {

/// Abstract sun model; samples primary rays through the collection aperture.
class SunSource {
public:
    virtual ~SunSource() = default;
    SunSource(const SunSource&) = delete;
    SunSource& operator=(const SunSource&) = delete;
    SunSource(SunSource&&) = delete;
    SunSource& operator=(SunSource&&) = delete;

    /// Draw one primary ray from the aperture toward the sun.
    virtual core::Ray sample_ray(const scene::Aperture& ap, math::Rng& rng) const = 0;

    void set_sun_direction(math::vec3 d) { sun_direction_ = glm::normalize(d); }
    math::vec3 sun_direction() const { return sun_direction_; }

    void set_dni(double dni_wm2) { dni_ = dni_wm2; }
    double dni() const { return dni_; }

protected:
    SunSource() = default;
    math::vec3 sun_direction_ {0.0, 0.0, -1.0};
    double     dni_           {1000.0};
};

} // namespace scrt::sources
