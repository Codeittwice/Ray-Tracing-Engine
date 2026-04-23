#pragma once
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/math/Rng.hpp"
#include <string>

namespace scrt::materials {

/// Describes what happens to a ray at a surface.
enum class InteractionKind { Absorbed, Reflected, Refracted, Split };

/// Result of a ray-material interaction.
struct Interaction {
    InteractionKind kind      = InteractionKind::Absorbed;
    core::Ray       reflected {};
    core::Ray       transmitted {};
    bool            terminate = false;  ///< True for Absorbed.
};

/// Abstract material interface.
class Material {
public:
    virtual ~Material() = default;
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) = delete;
    Material& operator=(Material&&) = delete;

    /// Compute the interaction of ray r hitting surface at hit h.
    virtual Interaction interact(const core::Ray& r, const core::Hit& h,
                                 math::Rng& rng) const = 0;

    void set_name(std::string s) { name_ = std::move(s); }
    const std::string& name() const { return name_; }

protected:
    Material() = default;
    std::string name_;
};

} // namespace scrt::materials
