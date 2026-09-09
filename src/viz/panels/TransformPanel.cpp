#include "scrt/viz/Panels.hpp"

#include <string>

#include "imgui.h"

namespace scrt::viz {

/// Draws the per-object transform editor panel.
void draw_transform_panel(PanelContext& ctx) {
    if (!ctx.scene || !ctx.edits || ctx.edits->empty()) return;
    if (!ImGui::CollapsingHeader("Transforms")) return;

    auto surfs = ctx.scene->mutable_surfaces();
    for (std::size_t i = 0; i < surfs.size(); ++i) {
        auto& surf = *surfs[i];
        std::uint64_t id = surf.id();
        auto it = ctx.edits->find(id);
        if (it == ctx.edits->end()) continue;
        auto& st = it->second;

        ImGui::PushID(static_cast<int>(i));

        std::string surf_name = surf.name().empty()
            ? "surface_" + std::to_string(i) : surf.name();
        ImGui::Text("%s", surf_name.c_str());
        ImGui::Indent();

        bool changed = false;
        changed |= ImGui::DragFloat3("Translate (m)", st.trans,   0.001f, -5.f, 5.f,    "%.3f");
        changed |= ImGui::DragFloat3("Rotate (deg)",  st.rot_deg, 0.5f,   -180.f, 180.f, "%.1f");
        if (changed) {
            if (ctx.apply_object_xform) ctx.apply_object_xform(id);
        }

        if (ImGui::Button("Reset##xf")) {
            st.trans[0] = st.trans[1] = st.trans[2] = 0.f;
            st.rot_deg[0] = st.rot_deg[1] = st.rot_deg[2] = 0.f;
            if (ctx.apply_object_xform) ctx.apply_object_xform(id);
        }
        ImGui::Unindent();
        ImGui::PopID();
        ImGui::Separator();
    }
}

} // namespace scrt::viz
