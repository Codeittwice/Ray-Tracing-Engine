#include "scrt/viz/Panels.hpp"

#include "scrt/scene/Scene.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/core/Hit.hpp"
#include "scrt/scene/SceneEditor.hpp"
#include "scrt/viz/Icons.hpp"
#include "scrt/viz/Library.hpp"
#include "scrt/viz/Preview.hpp"

#include "polyscope/view.h"

#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "imgui.h"

namespace scrt::viz {

namespace {

/// Where a component dropped by BUTTON lands. The user sets it before clicking, so two do not
/// pile up at the origin; a component dropped by DRAGGING lands under the cursor instead.
float g_place[3] = {0.0f, 0.0f, 0.0f};

/// ImGui payload id for a library component being dragged into the viewport.
constexpr const char* kComponentPayload = "SCRT_COMPONENT";

/// A message under the buttons: what the last click actually did.
std::string g_status;
bool        g_status_bad = false;

/// Tooltip on the item just drawn. A file-local copy, as in every other panel here.
void tip(const char* text) {
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        ImGui::TextUnformatted(text);   // Unformatted: a '%' in the text is not a format spec.
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

/// Grey explanatory line, wrapped to the panel width.
void help_line(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

/// The user's own material entries, read once per session.
std::vector<MaterialEntry>* user_entries() {
    static std::vector<MaterialEntry> v = load_user_materials();
    return &v;
}

void say(std::string s, bool bad = false) {
    g_status     = std::move(s);
    g_status_bad = bad;
}

/// True when the open document already has a material with this id.
bool scene_has_material(PanelContext& ctx, const std::string& id) {
    if (!ctx.editor) return false;
    for (const auto& m : ctx.editor->doc().materials)
        if (m.id == id) return true;
    return false;
}

/// Adds `e` to the scene unless it is already there. Returns false only on a real failure.
bool ensure_material(PanelContext& ctx, const MaterialEntry& e) {
    if (scene_has_material(ctx, e.name)) return true;
    if (!ctx.add_material) return false;
    return ctx.add_material(e.doc);
}

/// Finds a material entry by name across the built-ins and the user's own.
const MaterialEntry* find_material(const std::string& name) {
    for (const auto& e : builtin_materials())
        if (e.name == name) return &e;
    for (const auto& e : *user_entries())
        if (e.name == name) return &e;
    return nullptr;
}

/// A tooltip carrying an entry's note.
void note_tip(const std::string& note) {
    if (!note.empty()) tip(note.c_str());
}

void draw_materials_section(PanelContext& ctx) {
    if (!ImGui::CollapsingHeader(ICON_FA_PALETTE "  Materials")) return;
    help_line("Adds a material to the scene so an object can be given it on the Design tab. "
              "Hover any entry to see where its numbers come from and what they assume.");

    std::vector<const MaterialEntry*> all;
    for (const auto& e : builtin_materials()) all.push_back(&e);
    for (const auto& e : *user_entries())     all.push_back(&e);

    std::string open_group;
    bool        indented = false;
    for (const MaterialEntry* e : all) {
        if (e->group != open_group) {
            if (indented) ImGui::Unindent();
            open_group = e->group;
            ImGui::TextDisabled("%s", open_group.c_str());
            ImGui::Indent();
            indented = true;
        }
        ImGui::PushID(e->name.c_str());
        const bool present = scene_has_material(ctx, e->name);
        // The diagram is drawn by the material's own interact(), so it cannot claim behaviour
        // the material does not have. It sits outside BeginDisabled so an already-added
        // material is still legible.
        const float dh = ImGui::GetFrameHeight() * 1.6f;
        draw_material_diagram(e->name.c_str(), e->doc, dh * 1.35f, dh);
        ImGui::SameLine();
        ImGui::BeginDisabled(present || !ctx.add_material);
        if (ImGui::Button(e->name.c_str(), ImVec2(-1, dh))) {
            if (ensure_material(ctx, *e)) say("Added material: " + e->name);
            else                          say("Could not add " + e->name, true);
        }
        ImGui::EndDisabled();
        note_tip(present ? e->note + "\n\nAlready in this scene." : e->note);
        ImGui::PopID();
    }
    if (indented) ImGui::Unindent();

    // ---- keeping your own entries -----------------------------------------------------
    // Without this the user-material file could be read and never written, so half of the
    // library would ship dead: entries could appear only if the user hand-wrote the JSON.
    ImGui::Separator();
    help_line("Your own entries live in a file beside the assistant's config and appear above "
              "under \"My materials\". Tune a reflectance on the Design tab, then keep it here "
              "and it is offered in every future scene.");
    if (ImGui::Button("Keep this scene's materials in my library", ImVec2(-1, 0))) {
        auto*       mine  = user_entries();
        std::size_t added = 0;
        for (const auto& md : ctx.editor->doc().materials) {
            const bool known =
                std::any_of(builtin_materials().begin(), builtin_materials().end(),
                            [&](const MaterialEntry& e) { return e.name == md.id; }) ||
                std::any_of(mine->begin(), mine->end(),
                            [&](const MaterialEntry& e) { return e.name == md.id; });
            if (known) continue;
            MaterialEntry e;
            e.name         = md.id;
            e.group        = "My materials";
            e.note         = "Your own entry, kept from a scene.";
            e.doc          = md;
            e.user_defined = true;
            mine->push_back(std::move(e));
            ++added;
        }
        if (added == 0)
            say("Every material in this scene is already in the library.");
        else if (save_user_materials(*mine))
            say("Kept " + std::to_string(added) + " material(s) in " +
                user_materials_path().string());
        else
            say("Could not write " + user_materials_path().string(), true);
    }
    tip("Writes every material in the open scene that the library does not already have to your "
        "own material file, so it is offered from now on. Plain JSON in your roaming profile, "
        "which you can edit or delete by hand.");
}

void draw_components_section(PanelContext& ctx) {
    if (!ImGui::CollapsingHeader(ICON_FA_CUBE "  Components", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    help_line("Drops a ready-made object into the scene at the point below, with the material "
              "it needs. Move it afterwards with the placement controls on the right.");

    help_line("Place at (x, y, z) in metres:");
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat3("##place", g_place, 0.01f);
    tip("Where the new object's origin goes, in metres. Set it before clicking so two "
        "components do not land on top of each other.");
    ImGui::Separator();

    std::string open_group;
    bool        indented = false;
    for (const auto& c : builtin_components()) {
        if (c.group != open_group) {
            if (indented) ImGui::Unindent();
            open_group = c.group;
            ImGui::TextDisabled("%s", open_group.c_str());
            ImGui::Indent();
            indented = true;
        }
        ImGui::PushID(c.name.c_str());
        // A real render of the real geometry, from the same tessellate() the 3D view uses.
        const float th = ImGui::GetFrameHeight() * 1.6f;
        draw_surface_thumbnail(c.name.c_str(), c.surface,
                               ctx.scene_dir ? *ctx.scene_dir : std::filesystem::path("."), th);
        ImGui::SameLine();
        ImGui::BeginDisabled(!ctx.add_element || !ctx.add_material);
        if (ImGui::Button(c.name.c_str(), ImVec2(-1, th))) {
            const MaterialEntry* m = find_material(c.material);
            if (!m) {
                say("Component names a material that is not in the library: " + c.material, true);
            } else if (!ensure_material(ctx, *m)) {
                say("Could not add the material this component needs (" + c.material + ")", true);
            } else {
                io::ElementDoc el;
                el.name        = c.name;
                el.material_id = c.material;
                el.surface     = c.surface;
                el.transform.translation = {g_place[0], g_place[1], g_place[2]};
                if (ctx.add_element(std::move(el))) {
                    char buf[160];
                    std::snprintf(buf, sizeof(buf), "Added %s at (%.3f, %.3f, %.3f) m",
                                  c.name.c_str(), g_place[0], g_place[1], g_place[2]);
                    say(buf);
                    if (ctx.need_retrace) *ctx.need_retrace = true;
                } else {
                    say("Could not add " + c.name, true);
                }
            }
        }
        ImGui::EndDisabled();
        // Dragging is the other half of placing: typed coordinates put something exactly, a
        // drag puts it roughly and then you adjust. The payload is the entry's index.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const int idx = static_cast<int>(&c - builtin_components().data());
            ImGui::SetDragDropPayload(kComponentPayload, &idx, sizeof(idx));
            draw_surface_thumbnail(c.name.c_str(), c.surface,
                                   ctx.scene_dir ? *ctx.scene_dir : std::filesystem::path("."),
                                   ImGui::GetFrameHeight() * 1.6f);
            ImGui::SameLine();
            ImGui::TextUnformatted(c.name.c_str());
            ImGui::EndDragDropSource();
        }
        note_tip(c.note + "\n\nMaterial: " + c.material);
        ImGui::PopID();
    }
    if (indented) ImGui::Unindent();
}

void draw_sources_section(PanelContext& ctx) {
    if (!ImGui::CollapsingHeader(ICON_FA_LIGHTBULB "  Light sources")) return;
    help_line("Adds a light source. A scene may hold several; their power is divided among the "
              "rays in proportion to the watts each one emits.");

    for (const auto& s : builtin_sources()) {
        ImGui::PushID(s.name.c_str());
        ImGui::BeginDisabled(!ctx.add_source);
        if (ImGui::Button(s.name.c_str(), ImVec2(-1, 0))) {
            if (ctx.add_source(s.doc)) {
                say("Added source: " + s.name);
                if (ctx.need_retrace) *ctx.need_retrace = true;
            } else {
                say("Could not add " + s.name, true);
            }
        }
        ImGui::EndDisabled();
        note_tip(s.note);
        ImGui::PopID();
    }
    if (ctx.scene && ctx.scene->sources().empty())
        ImGui::TextDisabled("This scene has no light source yet.");
}

} // namespace

void draw_viewport_drop_target(PanelContext& ctx, const ImVec2& vmin, const ImVec2& vmax) {
    // No drag, no window. This is the whole reason the camera still works normally.
    if (!ImGui::GetDragDropPayload()) return;
    if (vmax.x <= vmin.x || vmax.y <= vmin.y) return;

    ImGui::SetNextWindowPos(vmin, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vmax.x - vmin.x, vmax.y - vmin.y), ImGuiCond_Always);
    ImGui::Begin("##viewport_drop", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    ImGui::InvisibleButton("##drop_area", ImVec2(vmax.x - vmin.x, vmax.y - vmin.y));

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(kComponentPayload)) {
            int idx = -1;
            if (pl->DataSize == static_cast<int>(sizeof(int)))
                std::memcpy(&idx, pl->Data, sizeof(int));
            const auto& comps = builtin_components();
            if (ctx.trace_running) {
                say("The scene is locked while a trace runs.", true);
            } else if (idx < 0 || idx >= static_cast<int>(comps.size())) {
                say("Dropped something the library does not recognise.", true);
            } else {
                const ComponentEntry& c = comps[static_cast<std::size_t>(idx)];
                const ImVec2 m = ImGui::GetMousePos();

                // Polyscope passes ImGui::GetMousePos() straight to its own picking, so ImGui's
                // coordinates ARE the screen coordinates these want, and the window-to-buffer
                // scaling is handled inside Polyscope. Checked in its source, not assumed.
                //
                // The obvious call here, view::screenCoordsToWorldPosition, reads the depth
                // buffer - and MEASURED from inside this callback it returns infinity even with
                // the cursor squarely on an object, so a drop onto the cooker landed 1.8 m away
                // on the ground plane. So cast the ray against the SCENE instead: exact, no
                // buffer read, and it is the same geometry the tracer traces, which is the
                // honest thing for a drop to land on.
                math::vec3  where{g_place[0], g_place[1], g_place[2]};
                const char* how = "at the typed point";
                const glm::vec3 eye = polyscope::view::getCameraWorldPosition();
                const glm::vec3 dir = polyscope::view::screenCoordsToWorldRay(glm::vec2{m.x, m.y});

                core::Ray pick_ray;
                pick_ray.origin    = math::vec3{eye.x, eye.y, eye.z};
                pick_ray.direction = math::safe_normalize(math::vec3{dir.x, dir.y, dir.z});
                core::Hit pick_hit;
                if (ctx.scene && ctx.scene->intersect(pick_ray, 1e-6,
                                                      std::numeric_limits<double>::max(),
                                                      pick_hit)) {
                    where = pick_hit.position;
                    how   = "on the surface under the cursor";
                } else if (pick_ray.direction.z < -1e-6) {
                    // Nothing in the scene under the cursor: meet the ground plane z = 0.
                    const double t = -pick_ray.origin.z / pick_ray.direction.z;
                    where = pick_ray.origin + t * pick_ray.direction;
                    where.z = 0.0;
                    how   = "on the ground plane";
                }

                const MaterialEntry* mat = find_material(c.material);
                if (!mat) {
                    say("Component names a material that is not in the library: " + c.material,
                        true);
                } else if (!ensure_material(ctx, *mat)) {
                    say("Could not add the material this component needs (" + c.material + ")",
                        true);
                } else {
                    io::ElementDoc el;
                    el.name        = c.name;
                    el.material_id = c.material;
                    el.surface     = c.surface;
                    el.transform.translation = where;
                    if (ctx.add_element && ctx.add_element(std::move(el))) {
                        char buf[200];
                        std::snprintf(buf, sizeof(buf), "Dropped %s %s: (%.3f, %.3f, %.3f) m",
                                      c.name.c_str(), how, where.x, where.y, where.z);
                        say(buf);
                        if (ctx.need_retrace) *ctx.need_retrace = true;
                    } else {
                        say("Could not add " + c.name, true);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
}

void draw_library_panel(PanelContext& ctx) {
    if (!ctx.editor) {
        ImGui::TextDisabled("Open a scene first: the library adds things to the open document.");
        return;
    }
    draw_components_section(ctx);
    ImGui::Spacing();
    draw_materials_section(ctx);
    ImGui::Spacing();
    draw_sources_section(ctx);

    if (!g_status.empty()) {
        ImGui::Separator();
        if (g_status_bad) ImGui::TextColored({1.0f, 0.4f, 0.3f, 1.0f}, "%s", g_status.c_str());
        else              ImGui::TextDisabled("%s", g_status.c_str());
    }
}

} // namespace scrt::viz
