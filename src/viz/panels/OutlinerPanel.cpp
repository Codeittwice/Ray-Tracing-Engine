#include "scrt/viz/Panels.hpp"
#include "scrt/viz/Icons.hpp"
#include "scrt/viz/RayRenderer.hpp"

#include "scrt/materials/Material.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/surfaces/Surface.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <tuple>
#include <unordered_map>

#include "imgui.h"
#include "polyscope/pick.h"
#include "polyscope/polyscope.h"
#include "polyscope/structure.h"
#include "polyscope/surface_mesh.h"
#include "polyscope/view.h"

namespace scrt::viz {

namespace {

/// Accent colour applied to the selected structure's surface.
const glm::vec3 kAccentColor{1.0f, 0.62f, 0.12f};
/// Accent colour applied to the selected structure's edges.
///
/// Near-pure yellow, and wider than the previous pale 1.5px line. The first human to drive the
/// app could not tell at a glance which object was selected; a tint alone does not survive a
/// scene where half the objects are already gold-coloured mirrors, so the cue is an outline.
const glm::vec3 kAccentEdgeColor{1.0f, 0.98f, 0.20f};
/// Edge width applied to the selected structure.
const double kAccentEdgeWidth = 2.5;

/// Appearance of a structure before the outliner overrode it, so it can be restored exactly.
struct AppearanceBackup {
    bool        valid = false;   ///< False until something has actually been overridden.
    std::string structure;       ///< Polyscope structure name the backup belongs to.
    const polyscope::SurfaceMesh* handle = nullptr; ///< Exact object the values were read from.
    glm::vec3   surface_color{}; ///< Surface colour as read back before the override.
    glm::vec3   edge_color{};    ///< Edge colour as read back before the override.
    double      edge_width = 0;  ///< Edge width as read back before the override.
};

/// The outliner's persistent selection; lives across frames, unlike PanelContext.
struct OutlinerSelection {
    std::string   row_key;         ///< Stable identifier of the selected outliner row.
    std::string   structure;       ///< Polyscope structure name, empty for rows without geometry.
    std::uint64_t surface_id = 0;  ///< Stable Scene surface id, 0 for non-surface rows.
    std::string   kind;            ///< Human-readable category, for the selection read-out.
    std::string   label;           ///< Display name as the row showed it.
    const char*   icon = "";       ///< Icon literal for the row's kind.
    std::string   detail;          ///< One line of type-specific detail; may be empty.
};

OutlinerSelection g_sel;        ///< Current selection (persists across frames).
AppearanceBackup  g_backup;     ///< What the highlight overrode, for exact restoration.
std::string       g_last_pick;  ///< Last 3D-view pick seen, so a pick is acted on only once.

/// Returns the registered surface mesh with this name, or nullptr.
polyscope::SurfaceMesh* mesh_or_null(const std::string& name) {
    if (name.empty() || !polyscope::hasSurfaceMesh(name)) return nullptr;
    return polyscope::getSurfaceMesh(name);
}

/// Restores the appearance the highlight overrode, if that structure still exists.
void restore_highlight() {
    if (!g_backup.valid) return;
    auto* mesh = mesh_or_null(g_backup.structure);
    // Only restore onto the very object the values were read from: a same-named structure
    // that was re-registered in the meantime is a fresh object with its own defaults.
    if (mesh && mesh == g_backup.handle) {
        mesh->setSurfaceColor(g_backup.surface_color);
        mesh->setEdgeColor(g_backup.edge_color);
        mesh->setEdgeWidth(g_backup.edge_width);
    }
    g_backup = AppearanceBackup{};
}

/// Saves the current appearance of a structure, then paints it with the accent highlight.
void apply_highlight(const std::string& name) {
    auto* mesh = mesh_or_null(name);
    if (!mesh) return;
    g_backup.valid         = true;
    g_backup.structure     = name;
    g_backup.handle        = mesh;
    g_backup.surface_color = mesh->getSurfaceColor();
    g_backup.edge_color    = mesh->getEdgeColor();
    g_backup.edge_width    = mesh->getEdgeWidth();

    mesh->setSurfaceColor(kAccentColor);
    mesh->setEdgeColor(kAccentEdgeColor);
    mesh->setEdgeWidth(kAccentEdgeWidth);
}

/// Makes one outliner row the selection, moving the highlight and the 3D-view pick with it.
void select_row(PanelContext& ctx, const std::string& row_key,
                const std::string& structure, std::uint64_t surface_id,
                bool push_to_pick) {
    if (g_sel.structure != structure) {
        restore_highlight();
        apply_highlight(structure);
    }
    g_sel.row_key    = row_key;
    g_sel.structure  = structure;
    g_sel.surface_id = surface_id;
    if (ctx.selected_id) *ctx.selected_id = surface_id;

    if (!push_to_pick) return;
    if (auto* mesh = mesh_or_null(structure)) {
        polyscope::pick::setSelection(
            {static_cast<polyscope::Structure*>(mesh), std::size_t{0}});
        g_last_pick = structure;
    } else {
        polyscope::pick::resetSelection();
        g_last_pick.clear();
    }
}

/// Drops the selection entirely (used when the previously selected structure disappeared).
void clear_selection(PanelContext& ctx) {
    restore_highlight();
    g_sel = OutlinerSelection{};
    if (ctx.selected_id) *ctx.selected_id = 0;
}

/// Polyscope structure name -> the outliner row key that draws it.
///
/// A 3D pick gives a structure name; the rows are keyed by what they ARE ("surface:7",
/// "face:top"). Without this table the pick set a row_key of its own invention that no row could
/// ever match, so clicking an object in the viewport highlighted it in 3D and left the tree
/// looking as though nothing was selected.
std::unordered_map<std::string, std::string> g_row_for_structure;

/// Rebuilds that table by walking the scene the same way the tree does.
///
/// Walked rather than recorded while drawing: the tree only draws while the Scene tab is open,
/// and a pick made from the Design tab must still land.
void refresh_row_table(const PanelContext& ctx) {
    g_row_for_structure.clear();
    if (!ctx.scene) return;

    const auto& reg = StructureRegistry::instance();
    for (const auto& surf : ctx.scene->surfaces()) {
        const std::string& ps_name = reg.name_for(surf->id());
        if (!ps_name.empty())
            g_row_for_structure[ps_name] = "surface:" + std::to_string(surf->id());
    }
    if (const auto* recv = ctx.scene->receiver()) {
        const bool multi = recv->is_multi_face();
        for (const auto& face : recv->faces())
            g_row_for_structure[receiver_flux_structure_name(multi, face->name())] =
                "face:" + face->name();
    }
    g_row_for_structure[aperture_structure_name()] = "aperture";
}

/// Adopts a selection made by clicking in the 3D view, once per distinct pick.
///
/// An empty pick is a click on the background, and it clears the selection - that is the only
/// way to get back to "nothing selected" without reloading the scene.
void sync_from_pick(PanelContext& ctx) {
    auto        pick   = polyscope::pick::getSelection();
    std::string picked = pick.first ? pick.first->getName() : std::string{};
    // A click on a part's drawn-only post or substrate selects the part it holds. Without this
    // the click fell through as "a structure the tree does not list" and did nothing.
    for (const std::string& suffix : {body_mount_structure_name(""), body_post_structure_name("")}) {
        if (picked.size() > suffix.size() &&
            picked.compare(picked.size() - suffix.size(), suffix.size(), suffix) == 0) {
            picked.resize(picked.size() - suffix.size());
            break;
        }
    }
    // A click on a drawn ray names that ray for the inspector (Stage 7b). Handled before the
    // same-pick check, because clicking along one ray is the SAME structure every time.
    if (picked == "ray_paths") {
        set_picked_ray_edge(ray_edge_for_pick(pick.second));
        if (g_last_pick != picked) clear_selection(ctx);
        g_last_pick = picked;
        return;
    }
    if (picked == g_last_pick) return;
    g_last_pick = picked;
    set_picked_ray_edge(-1);

    if (picked.empty()) {
        clear_selection(ctx);
        return;
    }
    // Only surface meshes are outliner rows; ignore picks on e.g. the ray-path curve network
    // so the selection invariant (structure is empty, or names a live surface mesh) holds.
    if (!mesh_or_null(picked)) return;

    const auto it = g_row_for_structure.find(picked);
    if (it == g_row_for_structure.end()) return;   // a structure the tree does not list

    const std::uint64_t id = StructureRegistry::instance().id_for(picked);
    select_row(ctx, it->second, picked, id, /*push_to_pick=*/false);
}

/// Re-paints the highlight when the selected structure was re-registered under the same
/// name (the receiver flux meshes are rebuilt after every trace), which yields a fresh
/// object carrying default appearance.
void reassert_highlight() {
    if (g_sel.structure.empty()) return;
    auto* mesh = mesh_or_null(g_sel.structure);
    if (!mesh) return;
    if (g_backup.valid && g_backup.handle == mesh) return;
    g_backup = AppearanceBackup{}; // the object the backup belonged to no longer exists
    apply_highlight(g_sel.structure);
}

/// Drops a stale highlight after a scene reload removed the selected structure.
void drop_selection_if_gone(PanelContext& ctx) {
    if (g_sel.structure.empty()) return;
    if (polyscope::hasSurfaceMesh(g_sel.structure)) return;
    g_backup = AppearanceBackup{}; // structure is gone; nothing left to restore
    clear_selection(ctx);
    g_last_pick.clear();
}

/// Draws one clickable outliner row: icon, name, and an accent bar down the left when selected.
///
/// The bar is drawn rather than relying on ImGuiCol_Header alone, which in this palette is a
/// grey only a shade off the panel and reads as "hovered" more than "selected".
void outliner_row(PanelContext& ctx, const char* icon, const char* label,
                  const std::string& row_key, const std::string& structure,
                  std::uint64_t surface_id, const char* kind, const char* detail = "") {
    const bool selected = (g_sel.row_key == row_key);
    ImGui::PushID(row_key.c_str());

    const ImGuiStyle& st     = ImGui::GetStyle();
    const ImVec4      accent = st.Colors[ImGuiCol_CheckMark];
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(accent.x, accent.y, accent.z, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(accent.x, accent.y, accent.z, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
    }

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    char         buf[256];
    std::snprintf(buf, sizeof(buf), "%s  %s", icon, label);
    const bool clicked = ImGui::Selectable(buf, selected);
    const float row_h  = ImGui::GetItemRectSize().y;

    if (selected) {
        ImGui::PopStyleColor(3);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(p0.x - st.WindowPadding.x * 0.5f, p0.y),
            ImVec2(p0.x - st.WindowPadding.x * 0.5f + 3.0f, p0.y + row_h),
            ImGui::GetColorU32(accent));
    }

    if (clicked) {
        select_row(ctx, row_key, structure, surface_id, /*push_to_pick=*/true);
        g_sel.kind   = kind;
        g_sel.label  = label;
        g_sel.icon   = icon;
        g_sel.detail = detail;
    } else if (selected) {
        // Keep the read-out current even when the row was selected by a 3D pick, or when the
        // detail line has changed underneath us (a scaled reflector's extent, say).
        g_sel.kind   = kind;
        g_sel.label  = label;
        g_sel.icon   = icon;
        g_sel.detail = detail;
    }
    ImGui::PopID();
}

/// One line describing a surface, for the selection read-out: its world-space extent.
///
/// Surface has no type name to report - the JSON knows "paraboloid", the runtime object does
/// not - so this states what can actually be measured from it.
std::string surface_detail(const surfaces::Surface& surf) {
    const auto b = surf.world_bounds();
    const auto d = b.max() - b.min();
    const auto c = b.centroid();
    char       buf[200];
    // The empty-state hint promises "what it is and where it SITS", and the position was the
    // half that was missing. It is also the only way to read back where a dragged component
    // actually landed.
    std::snprintf(buf, sizeof(buf),
                  "Centre (%.3f, %.3f, %.3f) m\nExtent %.3f x %.3f x %.3f m",
                  c.x, c.y, c.z, d.x, d.y, d.z);
    return buf;
}

/// Points the camera at the selected structure's bounding box.
void frame_selected() {
    auto* mesh = mesh_or_null(g_sel.structure);
    if (!mesh) return;
    const auto  box    = mesh->boundingBox();
    const auto  lo     = std::get<0>(box);
    const auto  hi     = std::get<1>(box);
    const glm::vec3 center = 0.5f * (lo + hi);
    float radius = 0.5f * glm::length(hi - lo);
    if (!(radius > 1e-6f)) radius = 1.0f;
    const glm::vec3 dir = glm::normalize(glm::vec3{1.0f, -1.0f, 0.7f});
    polyscope::view::lookAt(center + dir * (3.0f * radius), center, /*flyTo=*/true);
}

} // namespace

/// Draws the object outliner tree (reflectors, receiver faces, sun, aperture, materials).
void draw_outliner_panel(PanelContext& ctx, bool boxed) {
    if (boxed && !ImGui::CollapsingHeader(ICON_FA_LAYER_GROUP "  Scene tree",
                                          ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (!ctx.scene) {
        ImGui::TextDisabled("No scene loaded.");
        return;
    }

    const auto& reg = StructureRegistry::instance();

    // ---- Reflectors --
    if (ImGui::TreeNodeEx(ICON_FA_SOLAR_PANEL "  Reflectors", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto surfs = ctx.scene->surfaces();
        if (surfs.empty()) ImGui::TextDisabled("(none)");
        for (const auto& surf : surfs) {
            const std::uint64_t id = surf->id();
            const std::string&  ps_name = reg.name_for(id);
            const std::string   label =
                ps_name.empty()
                    ? (surf->name().empty() ? "surface_" + std::to_string(id) : surf->name())
                    : ps_name;
            const std::string detail = surface_detail(*surf);
            outliner_row(ctx, ICON_FA_CUBE, label.c_str(), "surface:" + std::to_string(id),
                         ps_name, id, "Reflector", detail.c_str());
        }
        ImGui::TreePop();
    }

    // ---- Receiver (faces as children) --
    if (const auto* recv = ctx.scene->receiver()) {
        if (ImGui::TreeNodeEx(ICON_FA_BOWL_FOOD "  Receiver", ImGuiTreeNodeFlags_DefaultOpen)) {
            const bool multi = recv->is_multi_face();
            for (const auto& face : recv->faces()) {
                const std::string structure =
                    receiver_flux_structure_name(multi, face->name());
                outliner_row(ctx, ICON_FA_FIRE, face->name().c_str(), "face:" + face->name(),
                             structure, 0, "Receiver face",
                             "Where the light is collected. Flux is measured here.");
            }
            ImGui::TreePop();
        }
    }

    // ---- Sources / Aperture / Materials --
    // One row per light source, whatever it is: the sun stopped being special in Wave 2.
    {
        const auto srcs = ctx.scene->sources();
        if (srcs.empty()) ImGui::TextDisabled(ICON_FA_LIGHTBULB "  (no light source)");
        for (std::size_t i = 0; i < srcs.size(); ++i) {
            const auto& src    = *srcs[i];
            const bool  is_sun = src.as_sun() != nullptr;
            std::string label  = is_sun ? "Sun" : std::string(src.type_name());
            if (!is_sun && !label.empty()) label[0] = static_cast<char>(std::toupper(label[0]));
            if (srcs.size() > 1) label += " " + std::to_string(i + 1);
            outliner_row(ctx, is_sun ? ICON_FA_MOUNTAIN_SUN : ICON_FA_LIGHTBULB, label.c_str(),
                         "source:" + std::to_string(i), std::string{}, 0, "Light source",
                         is_sun ? "Direction and strength of the incoming sunlight."
                                : "A laser: its own watts along one direction, no aperture.");
        }
    }
    if (ctx.scene->display_aperture())
        outliner_row(ctx, ICON_FA_SQUARE, "Aperture", "aperture", aperture_structure_name(), 0,
                     "Aperture", "The window rays are fired through. Sets the collected power.");

    if (ImGui::TreeNodeEx(ICON_FA_PALETTE "  Materials")) {
        auto mats = ctx.scene->mutable_materials();
        if (mats.empty()) ImGui::TextDisabled("(none)");
        for (const auto& mat : mats)
            outliner_row(ctx, ICON_FA_PALETTE, mat->name().c_str(), "material:" + mat->name(),
                         std::string{}, 0, "Material",
                         "Edit its reflectance and slope error on the Design tab.");
        ImGui::TreePop();
    }

    // ---- Actions --
    ImGui::Separator();
    ImGui::BeginDisabled(g_sel.structure.empty());
    if (ImGui::Button(ICON_FA_CROSSHAIRS "  Frame selected", ImVec2(-1, 0)))
        frame_selected();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Fly the camera to the selected object.");

    // Duplicate and Delete act on an ELEMENT, so they need a real surface id - a receiver face,
    // the sun and a material are all selectable rows with no element behind them.
    const bool has_element = (g_sel.surface_id != 0);
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    ImGui::BeginDisabled(!has_element || !ctx.duplicate_element);
    if (ImGui::Button(ICON_FA_LAYER_GROUP "  Duplicate", ImVec2(half, 0)))
        ctx.duplicate_element(g_sel.surface_id);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(has_element ? "Make a second copy of this object, in the same place."
                                      : "Select an object first. The sun, a material and a\n"
                                        "receiver face are not objects you can copy.");

    ImGui::SameLine();
    ImGui::BeginDisabled(!has_element || !ctx.remove_element);
    if (ImGui::Button(ICON_FA_TRASH "  Delete", ImVec2(half, 0)))
        ctx.remove_element(g_sel.surface_id);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(has_element ? "Remove this object from the scene."
                                      : "Select an object first.");
}

void update_selection_from_view(PanelContext& ctx) {
    if (!polyscope::isInitialized()) return;
    refresh_row_table(ctx);
    drop_selection_if_gone(ctx);
    sync_from_pick(ctx);
    reassert_highlight();
}

SelectionInfo current_selection() {
    SelectionInfo info;
    info.any        = !g_sel.row_key.empty();
    info.kind       = g_sel.kind;
    info.label      = g_sel.label;
    info.structure  = g_sel.structure;
    info.surface_id = g_sel.surface_id;
    info.icon       = g_sel.icon;
    info.detail     = g_sel.detail;
    return info;
}

} // namespace scrt::viz
