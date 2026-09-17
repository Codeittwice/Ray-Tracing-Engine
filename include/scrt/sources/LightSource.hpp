#pragma once
#include "scrt/core/Ray.hpp"
#include "scrt/math/Rng.hpp"
#include <cstddef>
#include <string_view>

namespace scrt::sources {

class SunSource;

/// A source of primary rays: originates its own rays and states its own strength in watts.
///
/// The tracer knows nothing about suns, apertures or irradiance. It asks each source how many
/// watts it emits, divides that among the rays it allots to the source, and multiplies each
/// sampled ray's relative weight by that per-ray share. A sun answers DNI * aperture area *
/// foreshortening; a laser answers its authored power. Nothing below the source layer needs to
/// know which.
class LightSource {
public:
    virtual ~LightSource() = default;
    LightSource(const LightSource&)            = delete;
    LightSource& operator=(const LightSource&) = delete;
    LightSource(LightSource&&)                 = delete;
    LightSource& operator=(LightSource&&)      = delete;

    /// Total optical power this source emits into the scene [W].
    ///
    /// The ONLY place a source expresses its strength; it never sees the primary ray count.
    /// May be 0 (a set sun, a laser switched off): the tracer must allot such a source no rays
    /// rather than divide by zero.
    virtual double total_power_w() const = 0;

    /// Draw one primary ray with origin, unit direction and wavelength_nm all set.
    ///
    /// `ray.power` is a RELATIVE WEIGHT with mean 1.0 over this source's own sampling
    /// distribution, NOT watts. The tracer multiplies it by total_power_w() / rays allotted. A
    /// source that samples its emission profile exactly returns exactly 1.0.
    ///
    /// THREAD-SAFETY: const and callable concurrently from every worker slot; the passed-in
    /// Rng is the only mutable state an implementation may touch.
    virtual core::Ray sample_ray(math::Rng& rng) const = 0;

    /// Primary ray `k` of the `n` the tracer allots to this source.
    ///
    /// A source that places rays on a deterministic pattern overrides this; every other source
    /// ignores k and n and draws exactly as sample_ray does, consuming the Rng identically, so no
    /// existing result moves. Same THREAD-SAFETY contract as sample_ray.
    virtual core::Ray sample_ray_indexed(math::Rng& rng, std::size_t /*k*/, std::size_t /*n*/) const {
        return sample_ray(rng);
    }

    /// True when sample_ray_indexed lays rays on a regular pattern rather than at random. A
    /// coherent receiver requires it: randomly placed rays summed with their phases are speckle.
    virtual bool deterministic_sampling() const { return false; }

    /// Coherence length [m] used to weight interference between paths; 0 means fully coherent.
    virtual double coherence_length_m() const { return 0.0; }

    /// Short lower-case type tag for reporting and the document: "sun", "laser".
    virtual std::string_view type_name() const = 0;

    /// Non-null only for solar sources.
    ///
    /// An explicit downcast hook rather than dynamic_cast: the solar-specific panel and the
    /// DNI-based concentration-ratio reporting still need to find the sun, and RTTI would hide
    /// how much is still coupled to it. Every call site of as_sun() is a place that has not
    /// been generalised yet.
    virtual const SunSource* as_sun() const { return nullptr; }
    /// Non-null only for solar sources; see the const overload.
    virtual       SunSource* as_sun()       { return nullptr; }

protected:
    LightSource() = default;
};

} // namespace scrt::sources
