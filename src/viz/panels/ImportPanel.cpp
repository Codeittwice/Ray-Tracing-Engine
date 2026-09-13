#include "scrt/viz/Panels.hpp"
#include "scrt/viz/FileDialog.hpp"
#include "scrt/viz/Icons.hpp"

#include "scrt/io/MeshImporter.hpp"
#include "scrt/materials/Material.hpp"
#include "scrt/scene/Scene.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "imgui.h"

namespace scrt::viz {

namespace {

// ---- File selection ----------------------------------------------------------------------

/// Extensions Assimp is asked for. Not Assimp's full list: these are the formats a user of this
/// app actually exports from CAD, and a dialog offering ninety filters helps nobody.
const std::vector<FileFilter> kMeshFilters = {
    {"3D model", "stl,obj,ply,3ds,dae,fbx,glb,gltf"},
    {"STL", "stl"},
    {"Wavefront OBJ", "obj"},
};

/// Asks the OS for a mesh file and writes it to `path_out`; returns false when cancelled.
/// `error_out` is filled only on a real failure, so a cancel leaves the panel's message alone.
bool browse_for_mesh_file(std::string& path_out, const std::filesystem::path& start_dir,
                          std::string& error_out) {
    std::filesystem::path picked;
    if (!open_file_dialog(kMeshFilters, start_dir, picked, error_out)) return false;
    path_out = picked.string();
    return true;
}

// ---- Unit choices ------------------------------------------------------------------------

/// Index of the "Custom" entry in the unit dropdown, which enables a free scale factor.
constexpr int kCustomUnitChoice = 3;

/// Labels of the unit dropdown, in the order guess_mesh_unit()'s result maps onto.
const char* const kUnitItems[] = {"Millimetres (x0.001)", "Centimetres (x0.01)",
                                  "Metres (x1)", "Custom..."};

/// Dropdown index for a detected unit.
int choice_for_unit(io::MeshUnit unit) {
    switch (unit) {
        case io::MeshUnit::Millimeters: return 0;
        case io::MeshUnit::Centimeters: return 1;
        case io::MeshUnit::Meters:      return 2;
    }
    return 2;
}

/// Unit behind a dropdown index; the Custom entry has no unit and reports metres.
io::MeshUnit unit_for_choice(int choice) {
    switch (choice) {
        case 0:  return io::MeshUnit::Millimeters;
        case 1:  return io::MeshUnit::Centimeters;
        default: return io::MeshUnit::Meters;
    }
}

// ---- Panel state -------------------------------------------------------------------------

/// Everything the import panel remembers between frames; one pending import at a time.
struct ImportState {
    char        path_buf[512] = {};  ///< Mesh file path being imported (edited in place).
    char        name_buf[128] = {};  ///< Element name for the merged import.
    bool        have_info = false;   ///< True once inspect succeeded for path_buf.
    io::MeshFileInfo info;           ///< Raw bounds/submeshes of the inspected file.
    std::string inspected_path;      ///< Path `info` describes, to detect edits to path_buf.
    std::string error;               ///< Last inspect/commit failure, empty when none.
    std::string status;              ///< Last success message, empty when none.
    int         unit_choice = 2;     ///< Index into kUnitItems; set from the guess on inspect.
    double      custom_scale = 1.0;  ///< Metres per raw unit when unit_choice is Custom.
    bool        split_submeshes = false; ///< Import each submesh as its own object.
    bool        center_on_scene = true;  ///< Place the mesh centre at the scene centre.
    int         material_choice = 0;     ///< Index into the live scene's material list.
    std::vector<io::ElementDoc> staged;  ///< Elements the last commit built.
};

ImportState g_import;  ///< The panel's persistent state (one panel, one pending import).

/// Metres per raw file unit for the current dropdown selection.
double current_scale() {
    if (g_import.unit_choice == kCustomUnitChoice) return g_import.custom_scale;
    return io::unit_scale_to_meters(unit_for_choice(g_import.unit_choice));
}

/// Directory imported mesh paths are stored relative to: the scene's own directory once the
/// Viewer tracks it, the working directory until then.
std::filesystem::path base_dir_for(const PanelContext& ctx) {
    if (ctx.scene_dir) return *ctx.scene_dir;
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path{} : cwd;
}

/// Centre of the current scene's world bounds, or the origin when there is no scene.
math::vec3 scene_center(const PanelContext& ctx) {
    if (!ctx.scene) return math::vec3(0.0);
    return ctx.scene->world_bounds().centroid();
}

/// Material ids offered by the live scene, in scene order.
std::vector<std::string> scene_material_ids(PanelContext& ctx) {
    std::vector<std::string> ids;
    if (!ctx.scene) return ids;
    for (const auto& mat : ctx.scene->mutable_materials()) ids.push_back(mat->name());
    return ids;
}

/// Reads the file at path_buf and refreshes the preview, unit guess and default element name.
///
/// WAVE 5 NOTE — this is the panel's ONLY blocking file read, deliberately confined to one
/// function so it can be moved onto a worker thread wholesale. A large STL (the 910 kB
/// two_level_entire_assembly.stl in assets/meshes, say) takes on the order of a second to parse,
/// and doing that inside the GUI frame makes the window look hung. It is driven by an explicit
/// "Inspect" click, never per frame, which bounds the damage until async lands.
void inspect_selected_file() {
    g_import.have_info = false;
    g_import.error.clear();
    g_import.status.clear();
    g_import.staged.clear();

    const std::filesystem::path path{g_import.path_buf};
    if (path.empty()) {
        g_import.error = "No file path given.";
        return;
    }

    try {
        g_import.info           = io::inspect_mesh_file(path);
        g_import.have_info      = true;
        g_import.inspected_path = g_import.path_buf;
        g_import.unit_choice    = choice_for_unit(g_import.info.guessed_unit);
        if (g_import.name_buf[0] == '\0') {
            const std::string stem = path.stem().string();
            std::snprintf(g_import.name_buf, sizeof(g_import.name_buf), "%s", stem.c_str());
        }
    } catch (const std::exception& e) {
        g_import.error = e.what();
    }
}

/// Builds the io::ElementDoc(s) this import would add: one for a merged import, one per submesh
/// when splitting. Pure document construction — it touches neither the scene nor the file.
std::vector<io::ElementDoc> build_elements(PanelContext& ctx, const std::string& material_id) {
    std::vector<io::ElementDoc> out;
    if (!g_import.have_info) return out;

    const double            scale = current_scale();
    const std::string       rel   = io::scene_relative_mesh_path(
        std::filesystem::path{g_import.path_buf}, base_dir_for(ctx));
    const math::vec3        target = scene_center(ctx);
    const std::string       base_name =
        g_import.name_buf[0] ? std::string(g_import.name_buf) : std::string("imported_mesh");

    auto make = [&](const std::string& name, const core::AABB& bounds) {
        io::ElementDoc d;
        d.name        = name;
        d.material_id = material_id;
        d.surface     = io::MeshDoc{rel, scale};
        d.transform.translation =
            g_import.center_on_scene
                ? io::mesh_centering_translation(bounds, scale, target)
                : math::vec3(0.0);
        return d;
    };

    if (!g_import.split_submeshes) {
        out.push_back(make(base_name, g_import.info.bounds));
        return out;
    }

    for (const auto& part : g_import.info.parts) {
        const std::string name = part.name.empty()
                                     ? base_name + "_" + std::to_string(part.index)
                                     : part.name;
        out.push_back(make(name, part.bounds));
    }
    return out;
}

/// Draws the raw-file facts, the unit dropdown and the live real-world size readout.
void draw_unit_section() {
    const io::MeshFileInfo& info = g_import.info;

    ImGui::Text("Submeshes: %d   Triangles: %d",
                static_cast<int>(info.parts.size()),
                static_cast<int>(info.triangle_count));

    const math::vec3 raw_extent = info.bounds.max() - info.bounds.min();
    ImGui::Text("Raw size: %.3f x %.3f x %.3f (file units)",
                raw_extent.x, raw_extent.y, raw_extent.z);
    ImGui::Text("Raw diagonal: %.3f", info.raw_diagonal);
    ImGui::TextDisabled("Detected unit: %s", io::mesh_unit_label(info.guessed_unit));

    ImGui::Combo("Unit", &g_import.unit_choice, kUnitItems,
                 static_cast<int>(IM_ARRAYSIZE(kUnitItems)));
    if (g_import.unit_choice == kCustomUnitChoice)
        ImGui::InputDouble("Metres per unit", &g_import.custom_scale, 0.0, 0.0, "%.6f");

    const double     scale  = current_scale();
    const math::vec3 metres = raw_extent * scale;
    const double     diag   = info.raw_diagonal * scale;

    ImGui::Text("Real size: %.3f x %.3f x %.3f m", metres.x, metres.y, metres.z);

    // The whole point of this panel: a 1000x unit mistake must be visible before committing.
    if (diag > 50.0)
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           "That is %.1f m across - wrong unit?", diag);
    else if (diag < 0.01 && diag > 0.0)
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           "That is only %.1f mm across - wrong unit?", diag * 1000.0);
}

/// Draws the merge-vs-split control and the submesh listing.
void draw_submesh_section() {
    const io::MeshFileInfo& info = g_import.info;

    // Merging every submesh into one surface is the default, so nothing changes for the 94
    // existing scenes; splitting exists because reflector panels and a frame want different
    // optical materials, which one merged surface cannot carry.
    ImGui::Checkbox("Import submeshes as separate objects", &g_import.split_submeshes);
    if (info.parts.size() < 2)
        ImGui::TextDisabled("This file holds one submesh; splitting yields the same object.");

    if (!g_import.split_submeshes) return;

    if (ImGui::TreeNodeEx("Submeshes", ImGuiTreeNodeFlags_DefaultOpen)) {
        const double scale = current_scale();
        for (const auto& part : g_import.info.parts) {
            const math::vec3 ext = (part.bounds.max() - part.bounds.min()) * scale;
            ImGui::BulletText("%s  -  %d tris, %.3f x %.3f x %.3f m",
                              part.name.empty() ? "(unnamed)" : part.name.c_str(),
                              static_cast<int>(part.triangle_count), ext.x, ext.y, ext.z);
        }
        ImGui::TreePop();
    }

    // io::MeshDoc carries only {path, scale_to_meters}: the scene schema cannot yet say WHICH
    // submesh an element is, so a split element would round-trip as the whole merged file.
    // The importer half is done and tested (io::import_submesh / io::MeshFileInfo::parts);
    // committing a split needs a submesh key on io::MeshDoc plus a SceneLoader reader, both of
    // which live in files this panel does not own.
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                       "Split elements cannot be committed yet: io::MeshDoc has no submesh "
                       "field, so each would reload the whole file. Preview only.");
}

} // namespace

void set_import_file(const std::filesystem::path& path) {
    std::snprintf(g_import.path_buf, sizeof(g_import.path_buf), "%s", path.string().c_str());
    inspect_selected_file();
}

/// Draws the 3D model import panel (file, unit detection, submesh split, placement).
void draw_import_panel(PanelContext& ctx) {
    if (!ImGui::CollapsingHeader(ICON_FA_CUBE "  Import a 3D model")) return;

    // ---- File --
    ImGui::InputText("File", g_import.path_buf,
                     static_cast<std::size_t>(IM_ARRAYSIZE(g_import.path_buf)));

    ImGui::BeginDisabled(!file_dialogs_available());
    if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Browse...")) {
        std::string picked;
        // Start where the scene lives, which is where its meshes are: every shipped scene
        // reads "../assets/meshes/...", so the model the user wants is one folder away.
        const std::filesystem::path start = ctx.scene_dir ? *ctx.scene_dir
                                                          : std::filesystem::path{};
        if (browse_for_mesh_file(picked, start, g_import.error)) {
            std::snprintf(g_import.path_buf, sizeof(g_import.path_buf), "%s", picked.c_str());
            inspect_selected_file();
        }
    }
    ImGui::EndDisabled();
    if (!file_dialogs_available()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(file dialog unavailable - type a path)");
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS "  Inspect")) inspect_selected_file();

    if (g_import.have_info && g_import.inspected_path != g_import.path_buf)
        ImGui::TextDisabled("Path edited since the last inspect.");

    if (!g_import.error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", g_import.error.c_str());

    if (!g_import.have_info) {
        ImGui::TextDisabled("Pick a mesh file (.stl, .obj, ...) and press Inspect.");
        return;
    }

    ImGui::Separator();
    draw_unit_section();
    ImGui::Separator();
    draw_submesh_section();
    ImGui::Separator();

    // ---- Naming, material and placement --
    ImGui::InputText("Name", g_import.name_buf,
                     static_cast<std::size_t>(IM_ARRAYSIZE(g_import.name_buf)));

    const std::vector<std::string> material_ids = scene_material_ids(ctx);
    if (material_ids.empty()) {
        ImGui::TextDisabled("No materials in the scene - load a scene before importing.");
    } else {
        if (g_import.material_choice >= static_cast<int>(material_ids.size()))
            g_import.material_choice = 0;
        std::vector<const char*> items;
        items.reserve(material_ids.size());
        for (const auto& id : material_ids) items.push_back(id.c_str());
        ImGui::Combo("Material", &g_import.material_choice, items.data(),
                     static_cast<int>(items.size()));
    }

    ImGui::Checkbox("Centre on scene", &g_import.center_on_scene);
    const math::vec3 c = scene_center(ctx);
    ImGui::TextDisabled("Scene centre: %.3f, %.3f, %.3f m", c.x, c.y, c.z);
    if (!ctx.scene_dir)
        ImGui::TextDisabled("Scene directory unknown; path stored relative to the "
                            "working directory.");

    // ---- Commit --
    const bool blocked = material_ids.empty() || g_import.split_submeshes;
    ImGui::BeginDisabled(blocked);
    if (ImGui::Button("Add to scene", ImVec2(-1, 0))) {
        g_import.error.clear();
        g_import.staged = build_elements(ctx, material_ids[
            static_cast<std::size_t>(g_import.material_choice)]);

        if (ctx.add_element) {
            // WIRING POINT (Wave 5): this is the single call site for
            // scene::SceneEditor::add_element. The Viewer fills ctx.add_element with
            //     [this](io::ElementDoc d) { return editor_->add_element(std::move(d)); }
            // in Viewer::make_panel_context(), then sets need_rebuild_/need_retrace_ so the
            // BVH and the Polyscope structures pick the new surface up.
            std::size_t added = 0;
            for (auto& d : g_import.staged)
                if (ctx.add_element(d) != 0) ++added;
            g_import.status = std::to_string(added) + " element(s) added.";
            if (ctx.need_rebuild) *ctx.need_rebuild = true;
            if (ctx.need_retrace) *ctx.need_retrace = true;
        } else {
            g_import.status = std::to_string(g_import.staged.size()) +
                              " element(s) built, but the Viewer owns no scene::SceneEditor "
                              "yet, so nothing was added to the live scene.";
        }
    }
    ImGui::EndDisabled();

    if (!g_import.status.empty()) ImGui::TextWrapped("%s", g_import.status.c_str());

    // ---- What was (or would have been) produced --
    for (const auto& d : g_import.staged) {
        const auto* mesh = std::get_if<io::MeshDoc>(&d.surface);
        if (!mesh) continue;
        ImGui::BulletText("%s  <-  %s  x%.6g  at (%.3f, %.3f, %.3f)", d.name.c_str(),
                          mesh->path.c_str(), mesh->scale_to_meters,
                          d.transform.translation.x, d.transform.translation.y,
                          d.transform.translation.z);
    }
}

} // namespace scrt::viz
