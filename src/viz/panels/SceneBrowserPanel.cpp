#include "scrt/viz/Panels.hpp"

#include <stdexcept>
#include <vector>

#include "imgui.h"

namespace scrt::viz {

/// Draws the scene browser panel (load example scenes).
void draw_scene_browser_panel(PanelContext& ctx) {
    if (!ImGui::CollapsingHeader("Scene Browser")) return;
    if (!ctx.available_scenes || ctx.available_scenes->empty()) {
        ImGui::TextDisabled("No example scenes found.");
        return;
    }

    std::vector<const char*> names;
    names.reserve(ctx.scene_display_names->size());
    for (const auto& n : *ctx.scene_display_names)
        names.push_back(n.c_str());

    ImGui::SetNextItemWidth(-1);
    ImGui::ListBox("##scenes", ctx.selected_scene_idx,
                   names.data(), static_cast<int>(names.size()), 6);

    if (!ctx.load_error->empty())
        ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "%s", ctx.load_error->c_str());

    ImGui::BeginDisabled(*ctx.selected_scene_idx < 0);
    if (ImGui::Button("Load selected scene", ImVec2(-1, 0))) {
        ctx.load_error->clear();
        try {
            if (ctx.load_scene)
                ctx.load_scene((*ctx.available_scenes)[static_cast<std::size_t>(*ctx.selected_scene_idx)]);
        } catch (const std::exception& e) {
            *ctx.load_error = e.what();
        }
    }
    ImGui::EndDisabled();
}

} // namespace scrt::viz
