#include "scrt/viz/Panels.hpp"
#include "scrt/viz/RayRenderer.hpp"

#include "scrt/materials/Material.hpp"
#include "scrt/scene/Receiver.hpp"

#include <cstdint>
#include <string>
#include <tuple>

#include "imgui.h"
#include "polyscope/pick.h"
#include "polyscope/polyscope.h"
#include "polyscope/structure.h"
#include "polyscope/surface_mesh.h"
#include "polyscope/view.h"

namespace scrt::viz {

namespace {

/// Accent colour applied to the selected structure's surface.
const glm::vec3 kAccentColor{1.0f, 0.55f, 0.10f};
/// Accent colour applied to the selected structure's edges.
const glm::vec3 kAccentEdgeColor{1.0f, 0.90f, 0.55f};
/// Edge width applied to the selected structure.
const double kAccentEdgeWidth = 1.5;

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

/// Adopts a selection made by clicking in the 3D view, once per distinct pick.
void sync_from_pick(PanelContext& ctx) {
    auto pick = polyscope::pick::getSelection();
    const std::string picked = pick.first ? pick.first->getName() : std::string{};
    if (picked == g_last_pick) return;
    g_last_pick = picked;
    if (picked.empty()) return;
    // Only surface meshes are outliner rows; ignore picks on e.g. the ray-path curve network
    // so the selection invariant (structure is empty, or names a live surface mesh) holds.
    if (!mesh_or_null(picked)) return;

    const std::uint64_t id = StructureRegistry::instance().id_for(picked);
    select_row(ctx, "structure:" + picked, picked, id, /*push_to_pick=*/false);
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

/// Draws one clickable outliner row.
void outliner_row(PanelContext& ctx, const char* label, const std::string& row_key,
                  const std::string& structure, std::uint64_t surface_id) {
    const bool selected = (g_sel.row_key == row_key);
    ImGui::PushID(row_key.c_str());
    if (ImGui::Selectable(label, selected))
        select_row(ctx, row_key, structure, surface_id, /*push_to_pick=*/true);
    ImGui::PopID();
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
void draw_outliner_panel(PanelContext& ctx) {
    if (!ImGui::CollapsingHeader("Outliner", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (!ctx.scene) {
        ImGui::TextDisabled("No scene loaded.");
        return;
    }

    drop_selection_if_gone(ctx);
    sync_from_pick(ctx);
    reassert_highlight();

    const auto& reg = StructureRegistry::instance();

    // ---- Reflectors --
    if (ImGui::TreeNodeEx("Reflectors", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto surfs = ctx.scene->surfaces();
        if (surfs.empty()) ImGui::TextDisabled("(none)");
        for (const auto& surf : surfs) {
            const std::uint64_t id = surf->id();
            const std::string&  ps_name = reg.name_for(id);
            const std::string   label =
                ps_name.empty()
                    ? (surf->name().empty() ? "surface_" + std::to_string(id) : surf->name())
                    : ps_name;
            outliner_row(ctx, label.c_str(), "surface:" + std::to_string(id), ps_name, id);
        }
        ImGui::TreePop();
    }

    // ---- Receiver (faces as children) --
    if (const auto* recv = ctx.scene->receiver()) {
        if (ImGui::TreeNodeEx("Receiver", ImGuiTreeNodeFlags_DefaultOpen)) {
            const bool multi = recv->is_multi_face();
            for (const auto& face : recv->faces()) {
                const std::string structure =
                    receiver_flux_structure_name(multi, face->name());
                outliner_row(ctx, face->name().c_str(), "face:" + face->name(),
                             structure, 0);
            }
            ImGui::TreePop();
        }
    }

    // ---- Sun / Aperture / Materials --
    outliner_row(ctx, "Sun", "sun", std::string{}, 0);
    outliner_row(ctx, "Aperture", "aperture", aperture_structure_name(), 0);

    if (ImGui::TreeNodeEx("Materials")) {
        auto mats = ctx.scene->mutable_materials();
        if (mats.empty()) ImGui::TextDisabled("(none)");
        for (const auto& mat : mats)
            outliner_row(ctx, mat->name().c_str(), "material:" + mat->name(),
                         std::string{}, 0);
        ImGui::TreePop();
    }

    // ---- Actions --
    ImGui::Separator();
    ImGui::BeginDisabled(g_sel.structure.empty());
    if (ImGui::Button("Frame selected", ImVec2(-1, 0)))
        frame_selected();
    ImGui::EndDisabled();

    if (g_sel.row_key.empty())
        ImGui::TextDisabled("Nothing selected");
    else
        ImGui::TextDisabled("Selected: %s", g_sel.row_key.c_str());
}

} // namespace scrt::viz
