#include "scrt/viz/Panels.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <vector>

#include "imgui.h"

namespace scrt::viz {

namespace {

/// Draws the example-scene list and its Load button; returns true when a scene was just loaded.
///
/// A true return means the Viewer destroyed the scene::SceneEditor mid-frame, so every pointer
/// in the caller's PanelContext (scene, editor, acc, ...) now dangles: the caller must stop
/// drawing panels for this frame rather than pass `ctx` on.
bool draw_browser_body(PanelContext& ctx) {
    if (!ImGui::CollapsingHeader("Scene Browser")) return false;
    if (!ctx.available_scenes || ctx.available_scenes->empty()) {
        ImGui::TextDisabled("No example scenes found.");
        return false;
    }

    // Selectable rows rather than ImGui::ListBox: a ListBox cannot report a double-click, and a
    // double-click is how the user expected to open a scene. 14 rows rather than 6, because the list
    // is about a hundred scenes long and six rows made it a keyhole.
    bool open_now = false;
    ImGui::TextDisabled("Double-click a scene to open it.");
    if (ImGui::BeginListBox("##scenes",
                            ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 14.25f))) {
        const auto& names = *ctx.scene_display_names;
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            const bool selected = (*ctx.selected_scene_idx == i);
            if (ImGui::Selectable(names[static_cast<std::size_t>(i)].c_str(), selected,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                *ctx.selected_scene_idx = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open_now = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    if (!ctx.load_error->empty())
        ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "%s", ctx.load_error->c_str());

    bool loaded = false;
    ImGui::BeginDisabled(*ctx.selected_scene_idx < 0);
    if (ImGui::Button("Load selected scene", ImVec2(-1, 0))) open_now = true;
    ImGui::EndDisabled();
    if (open_now && *ctx.selected_scene_idx >= 0) {
        ctx.load_error->clear();
        const std::filesystem::path& path =
            (*ctx.available_scenes)[static_cast<std::size_t>(*ctx.selected_scene_idx)];
        try {
            if (ctx.load_scene) {
                ctx.load_scene(path);
                // Only on success, and only after the load returned without throwing: the save
                // panel has no other way to learn which file this document came from, and a path
                // recorded for a load that failed would aim the next Save at the wrong file.
                note_scene_loaded(path);
                loaded = true;
            }
        } catch (const std::exception& e) {
            *ctx.load_error = e.what();
        }
    }
    return loaded;
}

} // namespace

/// Draws the scene browser panel (load example scenes) and the save panel below it.
void draw_scene_browser_panel(PanelContext& ctx) {
    if (draw_browser_body(ctx)) return;  // ctx describes a destroyed scene; resume next frame.

    // The save panel is dispatched from here rather than from Viewer::draw_gui(), because
    // Viewer.cpp is owned by another workstream this wave. Loading and saving are neighbours in
}

} // namespace scrt::viz
