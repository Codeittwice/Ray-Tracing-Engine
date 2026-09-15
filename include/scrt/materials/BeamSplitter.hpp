#pragma once
#include "scrt/materials/Material.hpp"

namespace scrt::materials {

/// A beam splitter with a DESIGNED split ratio: reflects a fixed fraction, transmits the rest
/// straight through, loses a fixed fraction in the coating. Wave 3 (library section 1.5).
///
/// Every split ratio before this was derived from Fresnel, so a 50:50 splitter could only be
/// faked by solving for an index that gives R = 0.5 at one angle and drifts everywhere else.
/// This one holds its ratio at every angle of incidence, which is what a coated splitter is
/// specified to do over its design range. It is a zero-thickness surface: the transmitted ray
/// keeps its direction with no lateral offset and no second-surface ghost, which is exact for a
/// pellicle and an approximation for a plate; the library says which is which.
class BeamSplitter final : public Material {
public:
    /// reflectance R and absorptance A, each in [0, 1] with R + A <= 1; transmittance is
    /// 1 - R - A. Throws std::invalid_argument otherwise.
    BeamSplitter(double reflectance, double absorptance = 0.0);

    Interaction interact(const core::Ray& r, const core::Hit& h, math::Rng& rng) const override;

    double reflectance() const { return r_; }
    double absorptance() const { return a_; }
    double transmittance() const { return 1.0 - r_ - a_; }

    /// Throws unless the new value keeps R + A <= 1.
    void set_reflectance(double reflectance);
    /// Throws unless the new value keeps R + A <= 1.
    void set_absorptance(double absorptance);

private:
    double r_;
    double a_;
};

} // namespace scrt::materials
