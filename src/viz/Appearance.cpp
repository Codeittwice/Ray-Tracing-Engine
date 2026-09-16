#include "scrt/viz/Appearance.hpp"

#include "scrt/materials/Absorber.hpp"
#include "scrt/materials/BeamSplitter.hpp"
#include "scrt/materials/PolarisingOptics.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/materials/Diffuser.hpp"
#include "scrt/materials/PerfectMirror.hpp"
#include "scrt/materials/RealMirror.hpp"
#include "scrt/materials/ThinDielectricPane.hpp"

#include <algorithm>

namespace scrt::viz {

Appearance appearance_for(const materials::Material* m) {
    Appearance a;
    if (!m) return a;

    // dynamic_cast, as the materials panel and SceneEditor already do: a material knows its own
    // parameters and nothing else carries the type. Ordered most specific first, since
    // ThinDielectricPane and Dielectric are unrelated types but read as the same thing.
    if (const auto* rm = dynamic_cast<const materials::RealMirror*>(m)) {
        a.matcap = "candy";
        // Reflectance drives the tint: a 0.85 foil mirror reads visibly duller than 0.975
        // protected silver, which is the number the user is choosing between in the library.
        const float r = static_cast<float>(std::clamp(rm->reflectance(), 0.0, 1.0));
        const float v = 0.45f + 0.5f * r;
        a.color  = {v, v, std::min(1.0f, v * 1.03f)};
        a.smooth = true;
        a.kind   = "mirror";
        return a;
    }
    if (dynamic_cast<const materials::PerfectMirror*>(m)) {
        a.matcap = "candy";
        a.color  = {0.97, 0.98, 1.0};
        a.smooth = true;
        a.kind   = "ideal mirror";
        return a;
    }
    if (dynamic_cast<const materials::Dielectric*>(m)) {
        a.matcap             = "wax";
        a.color              = {0.72, 0.87, 1.0};
        a.transparency       = 0.30f;
        a.identical_backface = true;   // a glass body's far wall is glass, not its inverse
        a.smooth             = true;
        a.kind               = "glass";
        return a;
    }
    if (dynamic_cast<const materials::ThinDielectricPane*>(m)) {
        a.matcap             = "wax";
        a.color              = {0.80, 0.90, 0.98};
        a.transparency       = 0.22f;  // a lid is thinner-looking than a lens body
        a.identical_backface = true;
        a.kind               = "glazing";
        return a;
    }
    if (const auto* bs = dynamic_cast<const materials::BeamSplitter*>(m)) {
        a.matcap = "wax";
        // Leans blue as it transmits more, grey as it reflects more, so a 90:10 pickoff and a
        // 50:50 do not look identical.
        const float t = static_cast<float>(std::clamp(bs->transmittance(), 0.0, 1.0));
        a.color              = {0.70f + 0.10f * (1.0f - t), 0.80f, 0.88f + 0.10f * t};
        a.transparency       = 0.45f;
        a.identical_backface = true;
        a.kind               = "splitter";
        return a;
    }
    if (dynamic_cast<const materials::Polariser*>(m)) {
        a.matcap             = "wax";
        a.color              = {0.22, 0.22, 0.26};   // the grey-violet of polarising film
        a.transparency       = 0.55f;
        a.identical_backface = true;
        a.kind               = "polariser";
        return a;
    }
    if (dynamic_cast<const materials::Waveplate*>(m)) {
        a.matcap             = "wax";
        a.color              = {0.86, 0.90, 0.80};   // a faintly tinted crystal plate
        a.transparency       = 0.30f;
        a.identical_backface = true;
        a.kind               = "waveplate";
        return a;
    }
    if (dynamic_cast<const materials::PolarisingBeamSplitter*>(m)) {
        a.matcap             = "wax";
        a.color              = {0.62, 0.80, 0.95};
        a.transparency       = 0.45f;
        a.identical_backface = true;
        a.kind               = "polarising splitter";
        return a;
    }
    if (const auto* df = dynamic_cast<const materials::Diffuser*>(m)) {
        a.matcap = "clay";
        // The albedo IS the brightness: white card and matte black paint are the same material
        // type at 0.80 and 0.04, and they must not look alike.
        const float v = static_cast<float>(std::clamp(df->albedo(), 0.0, 1.0));
        const float g = 0.04f + 0.90f * v;
        a.color = {g, g, g * 0.98f};
        a.kind  = "matte";
        return a;
    }
    if (dynamic_cast<const materials::Absorber*>(m)) {
        a.matcap = "clay";
        a.color  = {0.09, 0.09, 0.10};
        a.kind   = "absorber";
        return a;
    }
    return a;   // an unknown material type: neutral, rather than wrong
}

} // namespace scrt::viz
