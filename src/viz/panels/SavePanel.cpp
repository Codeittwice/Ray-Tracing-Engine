#include "scrt/viz/Panels.hpp"
#include "scrt/viz/FileDialog.hpp"

#include "scrt/io/ScenePaths.hpp"
#include "scrt/scene/SceneEditor.hpp"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

#include "imgui.h"

namespace scrt::viz {

namespace {

// =============================================================================================
// THE ONE THING TO GET RIGHT
// =============================================================================================
//
// Mesh paths in a scene document are stored RELATIVE TO THE SCENE FILE'S OWN DIRECTORY: the
// shipped scenes read "../assets/meshes/foo.stl". io::save_scene(doc, path) has nowhere in its
// signature to put the directory the document came from, so it delegates with
// old_base == new_base, which makes the rebase a documented no-op (pinned by T14 in
// tests/test_scene_io.cpp, "plain save_scene cannot relocate a mesh scene"). Wiring Save-As to
// it writes a file that *looks* saved and throws on reload.
//
// So this panel calls io::save_scene_as(doc, destination, doc_base) for BOTH buttons, and keeps
// two independent pieces of state that must never be conflated:
//
//   doc_base      The directory the IN-MEMORY document's relative paths resolve against. Set
//                 once when a scene is loaded; a Save-As never changes it, because Save-As does
//                 not rewrite the in-memory document, only the bytes on disk.
//   current_path  The file Save writes to. A Save-As does move this.
//
// Saving in place is then just the degenerate case where destination.parent_path() == doc_base
// and the rebase is a correct no-op, so there is one code path and no special case to forget.

/// Opens a native "save as" dialog for a scene JSON file.
bool ask_for_scene_save_path(const std::filesystem::path& dir, const std::string& name,
                             std::filesystem::path& out, std::string& error_out) {
    return save_file_dialog({{"Scene JSON", "json"}}, dir, name, out, error_out);
}

// ---- Panel state ------------------------------------------------------------------------------

/// Everything the save panel remembers between frames.
struct SaveState {
    std::filesystem::path current_path;   ///< File Save writes to; empty means "no target".
    std::filesystem::path doc_base;       ///< Directory the document's relative paths resolve to.
    std::filesystem::path pending_path;   ///< Path of a load in flight, consumed on the next swap.
    const scene::SceneEditor* last_editor = nullptr;  ///< Editor identity, to detect a new scene.
    std::string message;                  ///< Last save/revert result, shown under the buttons.
    bool        message_ok = false;       ///< Colour of `message`: green when true, red when not.
    /// Carries `message` through the editor swap that a Revert itself causes, so the "reloaded"
    /// confirmation survives the sync that otherwise wipes messages from a previous document.
    bool        keep_message = false;
    char        typed_path[512] = {};     ///< Manual destination, used when no dialog is available.
};

SaveState g_save;  ///< The panel's persistent state (one panel, one document).

/// Re-syncs the save target whenever the Viewer swaps in a different scene::SceneEditor.
///
/// Every load - browser, revert, or a route this panel knows nothing about - destroys the old
/// editor and constructs a new one, so a changed pointer means "this is a different document".
/// The path comes from note_scene_loaded() when the load went through a panel, otherwise from
/// ctx.scene_path when the Viewer supplies it. When neither is available the target is cleared
/// rather than left pointing at the previously loaded file: a disabled Save button is a nuisance,
/// silently overwriting the wrong scene is not.
void sync_to_current_scene(const PanelContext& ctx) {
    if (ctx.editor == g_save.last_editor) return;
    g_save.last_editor = ctx.editor;

    if (!g_save.pending_path.empty()) {
        g_save.current_path = g_save.pending_path;
        g_save.pending_path.clear();
    } else if (ctx.scene_path && !ctx.scene_path->empty()) {
        g_save.current_path = *ctx.scene_path;
    } else {
        g_save.current_path.clear();
    }

    // The document was built by io::load_scene(p), which resolves mesh paths against
    // p.parent_path() - so the file's own directory IS the document base. Only fall back to the
    // editor's own base_dir() when the path is unknown.
    if (!g_save.current_path.empty())
        g_save.doc_base = g_save.current_path.parent_path();
    else if (ctx.editor)
        g_save.doc_base = ctx.editor->base_dir();
    else
        g_save.doc_base.clear();

    if (!g_save.keep_message) {
        g_save.message.clear();
        g_save.message_ok = false;
    }
    g_save.keep_message = false;
    std::snprintf(g_save.typed_path, sizeof(g_save.typed_path), "%s",
                  g_save.current_path.string().c_str());
}

/// Appends ".json" when the user's destination has no extension at all.
std::filesystem::path with_json_extension(std::filesystem::path p) {
    if (p.has_filename() && !p.has_extension()) p += ".json";
    return p;
}

/// Writes the document to `dest` and, on success, adopts it as the new Save target.
/// `doc_base` is deliberately left alone: the in-memory document did not move, only the file did.
void perform_save_as(PanelContext& ctx, std::filesystem::path dest) {
    dest = with_json_extension(std::move(dest));

    const SaveOutcome outcome = save_document_to(ctx.editor->doc(), dest, g_save.doc_base);
    g_save.message    = outcome.message;
    g_save.message_ok = outcome.ok;
    if (!outcome.ok) return;

    ctx.editor->mark_saved();
    g_save.current_path = dest;
    std::snprintf(g_save.typed_path, sizeof(g_save.typed_path), "%s", dest.string().c_str());
    if (ctx.set_scene_path) ctx.set_scene_path(dest);
}

/// Draws the read-only "where would this go" block: destination, document base, dirty state.
void draw_status_block(const PanelContext& ctx) {
    if (g_save.current_path.empty())
        ImGui::TextDisabled("File: (none - this scene was not loaded from disk)");
    else
        ImGui::TextWrapped("File: %s", g_save.current_path.string().c_str());

    // Worth showing: when it differs from the destination's directory, mesh paths are being
    // rewritten on the way out, and that is exactly the case that used to break silently.
    if (!g_save.doc_base.empty())
        ImGui::TextDisabled("Mesh paths relative to: %s", g_save.doc_base.string().c_str());

    if (ctx.editor->dirty())
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Unsaved changes");
    else
        ImGui::TextDisabled("No unsaved changes");
}

/// Draws the destructive "reload from disk" control plus its confirmation modal.
/// Returns true when a reload actually happened, in which case `ctx` is stale for this frame.
bool draw_revert_block(PanelContext& ctx) {
    // A revert destroys the scene the tracer is reading, so it must wait for a running trace -
    // unlike Save, which only reads the document.
    const bool can_revert =
        !g_save.current_path.empty() && ctx.load_scene != nullptr && !ctx.trace_running;

    ImGui::BeginDisabled(!can_revert);
    if (ImGui::Button("Revert to saved...", ImVec2(-1, 0)))
        ImGui::OpenPopup("Revert scene?");
    ImGui::EndDisabled();
    if (ctx.trace_running && !g_save.current_path.empty())
        ImGui::TextDisabled("Revert is unavailable while a trace is running.");

    bool reverted = false;
    if (ImGui::BeginPopupModal("Revert scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Reload %s from disk?", g_save.current_path.string().c_str());
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                           "Every unsaved edit is discarded. This cannot be undone.");
        ImGui::Separator();

        if (ImGui::Button("Discard my edits", ImVec2(160, 0))) {
            const std::filesystem::path target = g_save.current_path;
            try {
                ctx.load_scene(target);
                note_scene_loaded(target);
                g_save.message      = "Reloaded " + target.string() + " from disk.";
                g_save.message_ok   = true;
                g_save.keep_message = true;  // survive the editor swap this reload just caused
                reverted            = true;
            } catch (const std::exception& e) {
                g_save.message    = std::string("Revert failed: ") + e.what();
                g_save.message_ok = false;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep editing", ImVec2(160, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return reverted;
}

} // namespace

// ---- save_document_to --------------------------------------------------------------------------

/// Writes `doc` to `path` through io::save_scene_as, rebasing relative mesh paths from
/// `doc_base`, then verifies a non-empty file landed. Never throws.
SaveOutcome save_document_to(const io::SceneDocument& doc, const std::filesystem::path& path,
                             const std::filesystem::path& doc_base) {
    if (path.empty()) return {false, "No destination path given."};

    try {
        // save_scene_as, NOT save_scene: only this overload is told where the document's
        // relative mesh paths came from, and only it can therefore repair them. See the header
        // comment at the top of this file.
        io::save_scene_as(doc, path, doc_base);
    } catch (const std::exception& e) {
        return {false, std::string("Save failed: ") + e.what()};
    }

    // "Wrote nothing and said nothing" is the failure mode worth guarding against, so confirm
    // the bytes are actually there rather than trusting the absence of an exception.
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec)
        return {false, "Save reported success but '" + path.string() + "' is not on disk."};

    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return {false, "Saved '" + path.string() + "' but could not stat it: " + ec.message()};
    if (size == 0) return {false, "Save produced an empty file at '" + path.string() + "'."};

    return {true, "Saved " + path.string() + " (" + std::to_string(size) + " bytes)."};
}

// ---- note_scene_loaded -------------------------------------------------------------------------

/// Tells the save panel which file the scene now being loaded came from; call on success only.
void note_scene_loaded(const std::filesystem::path& path) { g_save.pending_path = path; }

// ---- draw_save_panel ---------------------------------------------------------------------------

/// Draws the save / save-as / revert panel. Dispatched from draw_scene_browser_panel().
void draw_save_panel(PanelContext& ctx) {
    if (!ctx.editor) {
        if (ImGui::CollapsingHeader("Save scene###save_scene"))
            ImGui::TextDisabled("No document to save: the viewer owns no scene::SceneEditor.");
        return;
    }

    sync_to_current_scene(ctx);

    // The label carries the dirty marker while the ### id stays fixed, so the header does not
    // collapse itself the moment an edit lands.
    const char* label = ctx.editor->dirty() ? "Save scene *###save_scene"
                                            : "Save scene###save_scene";
    if (!ImGui::CollapsingHeader(label)) return;

    draw_status_block(ctx);
    ImGui::Separator();

    const bool have_path = !g_save.current_path.empty();

    ImGui::BeginDisabled(!have_path);
    if (ImGui::Button("Save", ImVec2(-1, 0)))
        perform_save_as(ctx, g_save.current_path);
    ImGui::EndDisabled();
    if (!have_path)
        ImGui::TextDisabled("Save needs a file to write back to; use Save As.");

    if (ImGui::Button("Save As...", ImVec2(-1, 0))) {
        const std::filesystem::path dir =
            have_path ? g_save.current_path.parent_path() : g_save.doc_base;
        const std::string name =
            have_path ? g_save.current_path.filename().string() : std::string("scene.json");

        std::filesystem::path picked;
        std::string           err;
        if (ask_for_scene_save_path(dir, name, picked, err)) {
            perform_save_as(ctx, picked);
        } else if (!err.empty()) {
            g_save.message    = err;
            g_save.message_ok = false;
        }
    }

    // Fallback for the case where NFD could not initialise, and an escape hatch for anyone who
    // would rather paste a path than click through a dialog.
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##save_typed_path", g_save.typed_path,
                     static_cast<std::size_t>(IM_ARRAYSIZE(g_save.typed_path)));
    ImGui::BeginDisabled(g_save.typed_path[0] == '\0');
    if (ImGui::Button("Save to typed path", ImVec2(-1, 0)))
        perform_save_as(ctx, std::filesystem::path(std::string(g_save.typed_path)));
    ImGui::EndDisabled();

    ImGui::Separator();
    if (draw_revert_block(ctx)) return;  // ctx now describes a destroyed scene; resume next frame.

    if (!g_save.message.empty()) {
        const ImVec4 colour = g_save.message_ok ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
                                                : ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        ImGui::TextColored(colour, "%s", g_save.message.c_str());
    }
}

} // namespace scrt::viz
