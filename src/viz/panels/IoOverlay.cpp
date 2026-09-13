#include "scrt/viz/Panels.hpp"
#include "scrt/viz/FileDialog.hpp"
#include "scrt/viz/Fonts.hpp"
#include "scrt/viz/Icons.hpp"

#include "scrt/io/ResultsExporter.hpp"
#include "scrt/viz/FluxPlotter.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/scene/SceneEditor.hpp"

#include <exception>
#include <filesystem>
#include <string>

#include "imgui.h"
#include "polyscope/screenshot.h"

namespace scrt::viz {

namespace {

/// Last thing the overlay did, shown at its foot. Empty until the user does something.
std::string g_message;
/// True when g_message reports a failure rather than a success.
bool g_failed = false;

void ok(std::string m) {
    g_message = std::move(m);
    g_failed  = false;
}
void fail(std::string m) {
    g_message = std::move(m);
    g_failed  = true;
}

/// One row of the overlay: a big clickable card with an icon, a title and a line of explanation.
///
/// Deliberately not a plain button row. Everything in this overlay writes or reads a file
/// somewhere on the user's disk, and the difference between "flux map" and "summary" is exactly
/// the sort of thing a bare label leaves the user guessing at.
bool action_row(const char* icon, const char* title, const char* detail, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushID(title);

    const float  k   = ui_scale();
    const ImVec2 sz(-1.0f, ImGui::GetTextLineHeight() * 2.0f + 22.0f * k);
    const ImVec2 p0  = ImGui::GetCursorScreenPos();
    const bool   hit = ImGui::Button("##row", sz);

    // The button is the hit target and the frame; the contents are drawn over it, because
    // ImGui::Button takes a single label and this needs two lines at two different weights.
    ImDrawList*       dl = ImGui::GetWindowDrawList();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float       pad = 11.0f * k;

    const ImU32 icon_col =
        ImGui::GetColorU32(enabled ? st.Colors[ImGuiCol_CheckMark] : st.Colors[ImGuiCol_TextDisabled]);
    dl->AddText(ImVec2(p0.x + pad, p0.y + pad), icon_col, icon);

    const float text_x = p0.x + pad + ImGui::GetFontSize() * 1.9f;
    dl->AddText(ImVec2(text_x, p0.y + pad * 0.8f),
                ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled), title);
    dl->AddText(ImVec2(text_x, p0.y + pad * 0.8f + ImGui::GetTextLineHeight()),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), detail);

    ImGui::PopID();
    ImGui::EndDisabled();
    return hit;
}

/// Directory a save dialog should open in: next to the scene when there is one.
std::filesystem::path start_dir(const PanelContext& ctx) {
    return (ctx.scene_dir && !ctx.scene_dir->empty()) ? *ctx.scene_dir : std::filesystem::path{};
}

// ---- import actions -------------------------------------------------------------------------

void do_import_model(PanelContext& ctx) {
    std::filesystem::path picked;
    std::string           err;
    if (!open_file_dialog({{"3D model", "stl,obj,ply,3ds,dae,fbx,glb,gltf"}}, start_dir(ctx),
                          picked, err)) {
        if (!err.empty()) fail(err);
        return;
    }
    set_import_file(picked);
    if (ctx.focus_tab) ctx.focus_tab("Design");
    if (ctx.io_overlay_open) *ctx.io_overlay_open = false;
    ok("Loaded " + picked.filename().string() + ". Set its size and place it on the Design tab.");
}

void do_import_scene(PanelContext& ctx) {
    std::filesystem::path picked;
    std::string           err;
    if (!open_file_dialog({{"Scene JSON", "json"}}, start_dir(ctx), picked, err)) {
        if (!err.empty()) fail(err);
        return;
    }
    if (!ctx.load_scene) { fail("This build cannot load a scene from here."); return; }
    // Deferred by the Viewer to the end of the frame: loading in place would destroy the Scene
    // and SceneEditor that this overlay and every panel after it still hold pointers to.
    ctx.load_scene(picked);
    if (ctx.io_overlay_open) *ctx.io_overlay_open = false;
}

// ---- export actions -------------------------------------------------------------------------

void do_export_flux_csv(PanelContext& ctx) {
    std::filesystem::path picked;
    std::string           err;
    if (!save_file_dialog({{"CSV", "csv"}}, start_dir(ctx), "flux.csv", picked, err)) {
        if (!err.empty()) fail(err);
        return;
    }
    try {
        io::export_flux_csv(*ctx.acc, picked.string());
        ok("Wrote " + picked.filename().string());
    } catch (const std::exception& e) {
        fail(std::string("Could not write the CSV: ") + e.what());
    }
}

void do_export_summary(PanelContext& ctx) {
    std::filesystem::path picked;
    std::string           err;
    if (!save_file_dialog({{"JSON", "json"}}, start_dir(ctx), "summary.json", picked, err)) {
        if (!err.empty()) fail(err);
        return;
    }
    // The scene's own DNI, never a hardcoded 1000: it sets concentration_ratio in the exported
    // summary, so a stand-in value writes a fabricated figure to a file someone will cite. The
    // sun panel publishes it each frame; FluxPlotter is where it is kept.
    const double dni = FluxPlotter::scene_dni();
    try {
        io::export_summary_json(*ctx.acc, *ctx.result, dni, picked.string());
        ok("Wrote " + picked.filename().string());
    } catch (const std::exception& e) {
        fail(std::string("Could not write the summary: ") + e.what());
    }
}

void do_export_scene(PanelContext& ctx) {
    std::filesystem::path picked;
    std::string           err;
    const std::string     name =
        (ctx.scene_path && !ctx.scene_path->empty()) ? ctx.scene_path->filename().string()
                                                     : std::string("scene.json");
    if (!save_file_dialog({{"Scene JSON", "json"}}, start_dir(ctx), name, picked, err)) {
        if (!err.empty()) fail(err);
        return;
    }
    // save_document_to, not io::save_scene: the in-memory document's mesh paths are relative to
    // the directory it was LOADED from, and only the _as form can rebase them onto the new
    // destination. Plain save_scene writes paths that no longer resolve.
    const SaveOutcome r = save_document_to(ctx.editor->doc(), picked, start_dir(ctx));
    if (r.ok) {
        ok("Wrote " + picked.filename().string());
        // Deliberately does NOT move the document's base directory or the Save target. The
        // in-memory relative mesh paths still resolve against the original folder, and the
        // save panel tracks that separately; see the note at the top of SavePanel.cpp.
    } else {
        fail(r.message);
    }
}

void do_screenshot(PanelContext& ctx) {
    std::filesystem::path picked;
    std::string           err;
    if (!save_file_dialog({{"PNG image", "png"}}, start_dir(ctx), "cooker.png", picked, err)) {
        if (!err.empty()) fail(err);
        return;
    }
    try {
        // transparentBG = false: this is going into a report, and a transparent PNG on a white
        // page loses the dark viewport the scene was composed against.
        polyscope::screenshot(picked.string(), /*transparentBG=*/false);
        ok("Wrote " + picked.filename().string() + " (the 3D view, without the panels).");
    } catch (const std::exception& e) {
        fail(std::string("Could not save the image: ") + e.what());
    }
}

} // namespace

// ---- draw_io_overlay -------------------------------------------------------------------------

void draw_io_overlay(PanelContext& ctx) {
    if (!ctx.io_overlay_open || !*ctx.io_overlay_open) return;

    const float  k = ui_scale();
    const ImVec2 centre(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(640.0f * k, 0.0f), ImGuiCond_Always);

    // Modal, so the 3D view behind it dims and a stray click cannot start a gizmo drag under it.
    ImGui::OpenPopup("###io_overlay");
    if (!ImGui::BeginPopupModal(ICON_FA_RIGHT_LEFT "  Import and export###io_overlay",
                                ctx.io_overlay_open,
                                ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoMove)) {
        return;
    }

    const bool has_results = ctx.traced && ctx.acc && ctx.result;
    const bool has_doc     = ctx.editor != nullptr;

    ImGui::TextDisabled("Bring something in");
    if (action_row(ICON_FA_CUBE, "3D model",
                   "An STL or OBJ from CAD. You set its real size after picking it.", true))
        do_import_model(ctx);
    if (action_row(ICON_FA_FILE_IMPORT, "Scene file",
                   "A complete cooker saved earlier. Replaces what is open now.",
                   ctx.load_scene != nullptr))
        do_import_scene(ctx);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Take something out");
    if (action_row(ICON_FA_FLOPPY_DISK, "Scene file",
                   "Everything on screen, as JSON you can reopen or share.", has_doc))
        do_export_scene(ctx);
    if (action_row(ICON_FA_CHART_SIMPLE, "Flux map (CSV)",
                   "One row per patch of the pot, for a spreadsheet or a plot.", has_results))
        do_export_flux_csv(ctx);
    if (action_row(ICON_FA_FILE_EXPORT, "Results summary (JSON)",
                   "Total power, hottest spot and concentration, as numbers.", has_results))
        do_export_summary(ctx);
    if (action_row(ICON_FA_FILE, "Picture of the 3D view",
                   "A PNG of the cooker as shown, without the panels.", true))
        do_screenshot(ctx);

    if (!has_results) {
        ImGui::TextDisabled(ICON_FA_CIRCLE_INFO
                            "  Run a trace on the Simulate tab to unlock the result exports.");
    }

    if (!g_message.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              g_failed ? ImVec4(1.0f, 0.45f, 0.40f, 1.0f)
                                       : ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
        ImGui::TextWrapped("%s", g_message.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_XMARK "  Close", ImVec2(-1, 0))) *ctx.io_overlay_open = false;

    ImGui::EndPopup();
}

} // namespace scrt::viz
