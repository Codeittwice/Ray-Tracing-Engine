#pragma once
#include "scrt/math/Vec.hpp"

namespace scrt::materials { class Material; }

namespace scrt::viz {

/// How a material should LOOK: one description, used by the 3D view and by the library swatch,
/// so an object and its library entry cannot show different things.
///
/// Dispatched on the material's actual type. Before Wave 4 the only rule was a string match for
/// "glass" or "pmma" in the surface's or material's NAME, so a dielectric called "lens_body"
/// rendered as an opaque grey slab and an absorber called "glass_pot" rendered as glass.
struct Appearance {
    /// Polyscope matcap name. Its shipped set is: clay, wax, candy, flat, mud, ceramic, jade,
    /// normal. Anything else is silently ignored by Polyscope, so keep to that list.
    const char* matcap = "clay";
    math::vec3  color{0.72, 0.72, 0.75};
    /// 1 is opaque. Below 1 the render engine needs a transparency mode; the caller turns it on.
    float       transparency = 1.0f;
    /// True for a body you can see into, so the far wall is not painted the inverse colour.
    bool        identical_backface = false;
    /// True for curved bodies: Polyscope's default is flat, which makes a tessellated lens
    /// look faceted at any segment count the app actually uses.
    bool        smooth = false;
    /// Short human label for the swatch caption ("mirror", "glass", "matte", ...).
    const char* kind = "surface";
};

/// The appearance for a material; a null material gets the neutral default.
Appearance appearance_for(const materials::Material* m);

} // namespace scrt::viz
