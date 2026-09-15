#pragma once
#include "scrt/io/SceneDocument.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace scrt::viz {

/// One named material a user can drop into the open scene.
///
/// EVERY entry here must behave as its name says. The Wave 3 library specification lists
/// several the engine cannot honour yet — a polarising beam splitter, a dichroic, a
/// ground-glass (transmissive) diffuser, a brushed-metal lobe — and they are deliberately
/// absent rather than approximated, because a row whose behaviour does not match its name is
/// worse than a missing row: the user cannot tell, and the number they get looks authoritative.
struct MaterialEntry {
    std::string    name;      ///< What the user sees, and the material id it is added under.
    std::string    group;     ///< Section heading: "Mirrors", "Glass", ...
    std::string    note;      ///< Tooltip: where the figure comes from and what it assumes.
    io::MaterialDoc doc;      ///< The document entry; doc.id is set from `name` when added.
    bool           user_defined = false;  ///< True for entries read from the user's own file.
};

/// One ready-made element: a surface, the material it needs, and a suggested name.
///
/// `material` names an entry in the material library; dropping the component adds that material
/// first when the scene does not already have it, because a surface borrows a raw pointer to
/// its material and cannot be built without one.
struct ComponentEntry {
    std::string    name;
    std::string    group;     ///< "Mirrors", "Lenses", "Apertures", "Detectors", ...
    std::string    note;
    std::string    material;  ///< Material library entry name this component binds.
    io::SurfaceDoc surface;
};

/// One ready-made light source.
struct SourceEntry {
    std::string   name;
    std::string   note;
    io::SourceDoc doc;
};

/// The built-in material presets. Figures are catalogue or representative values; each entry's
/// `note` says which, because the engine is monochromatic and a single number for a surface
/// whose behaviour varies with wavelength is an approximation that must be stated.
const std::vector<MaterialEntry>& builtin_materials();

/// The built-in component templates.
const std::vector<ComponentEntry>& builtin_components();

/// The built-in source templates.
const std::vector<SourceEntry>& builtin_sources();

/// Where user-defined materials live: beside the assistant's config, for the same reason.
std::filesystem::path user_materials_path();

/// Reads the user's own material entries; returns an empty list when the file is missing or
/// unreadable. Never throws: a corrupt library must not stop the app starting.
std::vector<MaterialEntry> load_user_materials();

/// Writes the user's material entries. Returns false when the file could not be written.
bool save_user_materials(const std::vector<MaterialEntry>& entries);

} // namespace scrt::viz
