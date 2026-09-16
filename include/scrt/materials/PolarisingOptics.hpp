#pragma once

#include "scrt/materials/Material.hpp"

namespace scrt::materials {

/// A linear polariser: passes the field component along its transmission axis.
///
/// The axis lies in the part's own surface plane, at `transmission_axis_deg` from the part's local
/// +X about its local +Z, so turning the part turns the axis. `transmission` is the principal
/// transmittance k1 (1 = ideal; a sheet polariser is about 0.77, which passes 38.5% of unpolarised
/// light), and `extinction_ratio` is k1/k2 (the crossed component is attenuated by that factor).
///
/// Unpolarised input leaves polarised along the axis carrying k1 (1 + 1/ER) / 2 of the power. The
/// small crossed leak is then folded into a fully polarised state: a single Jones vector cannot
/// carry partial polarisation, and at any realistic extinction ratio the difference is below 1e-3.
class Polariser final : public Material {
public:
    Polariser(double transmission_axis_deg, double extinction_ratio, double transmission = 1.0);

    Interaction interact(const core::Ray& r, const core::Hit& h, math::Rng& rng) const override;

    double axis_deg() const { return axis_deg_; }
    double extinction_ratio() const { return extinction_; }
    double transmission() const { return k1_; }
    void   set_axis_deg(double deg);
    void   set_extinction_ratio(double er);   ///< Throws unless finite and >= 1.
    void   set_transmission(double k1);       ///< Throws unless in [0, 1].

private:
    double axis_deg_;
    double extinction_;
    double k1_;
};

/// A waveplate: retards the slow-axis field component by `retardance_waves` of a wave.
///
/// 0.25 is a quarter-wave plate, 0.5 a half-wave plate. The fast axis is set like a polariser's
/// transmission axis. The retardance is the same at EVERY wavelength here - a zero-order plate at
/// its design wavelength; a real plate's retardance scales roughly as 1/lambda. Unpolarised light
/// passes unchanged.
class Waveplate final : public Material {
public:
    Waveplate(double retardance_waves, double fast_axis_deg, double transmission = 1.0);

    Interaction interact(const core::Ray& r, const core::Hit& h, math::Rng& rng) const override;

    double retardance_waves() const { return retardance_; }
    double fast_axis_deg() const { return axis_deg_; }
    double transmission() const { return k_; }
    void   set_retardance_waves(double w);    ///< Throws unless finite.
    void   set_fast_axis_deg(double deg);
    void   set_transmission(double k);        ///< Throws unless in [0, 1].

private:
    double retardance_;
    double axis_deg_;
    double k_;
};

/// A polarising beam splitter coating: transmits p, reflects s, relative to its plane of incidence.
///
/// `extinction_ratio` sets the leak: 1/ER of the s power is transmitted and 1/ER of the p power is
/// reflected. Unpolarised input splits 50:50 into a pure-s reflected and a pure-p transmitted ray.
/// Zero thickness, like `beam_splitter`: a cube is drawn with a body, the glass is not traced. At
/// exactly normal incidence there is no plane of incidence and the s direction is arbitrary.
class PolarisingBeamSplitter final : public Material {
public:
    explicit PolarisingBeamSplitter(double extinction_ratio);

    Interaction interact(const core::Ray& r, const core::Hit& h, math::Rng& rng) const override;

    double extinction_ratio() const { return extinction_; }
    void   set_extinction_ratio(double er);   ///< Throws unless finite and >= 1.

private:
    double extinction_;
};

} // namespace scrt::materials
