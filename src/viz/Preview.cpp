#include "scrt/viz/Preview.hpp"

#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/materials/Material.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/surfaces/Surface.hpp"
#include "scrt/viz/Appearance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

#include "imgui.h"

namespace scrt::viz {

namespace {

// ---- component thumbnails ---------------------------------------------------------------

/// One flat-shaded triangle, in [0,1] x [0,1] thumbnail space, already depth-sorted.
struct Tri {
    ImVec2 p[3];
    float  shade;   ///< 0..1 facet lighting; the colour is applied at draw time.
};

/// A whole cached thumbnail. `ok` is false when the surface could not be built or tessellated.
struct Thumb {
    std::vector<Tri> tris;
    bool             ok = false;
};

/// Fixed three-quarter view, so every entry is seen from the same angle and they compare.
/// Azimuth 35 degrees, elevation 25, with the project's Z as up.
void view_basis(math::vec3& right, math::vec3& up, math::vec3& fwd) {
    const double az = 35.0 * 3.14159265358979323846 / 180.0;
    // 42 degrees, not 25: at a shallow angle a flat disk, a lens body and a dish all project to
    // similar thin ellipses, which is exactly the discrimination a thumbnail exists to provide.
    const double el = 42.0 * 3.14159265358979323846 / 180.0;
    fwd   = math::safe_normalize(math::vec3{std::cos(el) * std::sin(az),
                                            -std::cos(el) * std::cos(az),
                                            -std::sin(el)});
    const math::vec3 world_up{0.0, 0.0, 1.0};
    right = math::safe_normalize(glm::cross(fwd, world_up));
    up    = glm::cross(right, fwd);
}

Thumb build_thumb(const io::SurfaceDoc& sd, const std::filesystem::path& base_dir) {
    Thumb t;
    std::unique_ptr<surfaces::Surface> surf;
    try {
        surf = io::build_surface(sd, base_dir);
    } catch (const std::exception&) {
        return t;   // not buildable: the caller draws a placeholder
    }
    if (!surf) return t;

    // Coarse on purpose: 12 segments is plenty at 48 pixels and keeps the cache small. A dish
    // at the 3D view's 32 would be a couple of thousand triangles per library row.
    std::vector<math::vec3>    verts;
    std::vector<std::uint32_t> idx;
    surf->tessellate(12, verts, idx);
    if (verts.empty() || idx.size() < 3) return t;   // e.g. ImplicitSDF's empty stub

    math::vec3 right, up, fwd;
    view_basis(right, up, fwd);

    // Project, then fit: the extent is measured in view space, not from local_bounds, so a
    // surface whose bounds are padded (Plane pads z by 1e-4) still fills the thumbnail.
    std::vector<math::vec3> proj;   // (x, y, depth)
    proj.reserve(verts.size());
    math::vec3 centroid{0.0};
    for (const auto& v : verts) centroid += v;
    centroid /= static_cast<double>(verts.size());
    double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300;
    for (const auto& v : verts) {
        const math::vec3 d = v - centroid;
        const double     x = glm::dot(d, right), y = glm::dot(d, up), z = glm::dot(d, fwd);
        proj.push_back({x, y, z});
        minx = std::min(minx, x); maxx = std::max(maxx, x);
        miny = std::min(miny, y); maxy = std::max(maxy, y);
    }
    const double span = std::max({maxx - minx, maxy - miny, 1e-12});
    const double s    = 0.84 / span;   // leave a margin so nothing touches the edge

    // One fixed light, from over the viewer's shoulder.
    const math::vec3 light = math::safe_normalize(right * 0.4 + up * 0.7 - fwd * 0.8);

    struct Sortable { Tri tri; double depth; };
    std::vector<Sortable> out;
    out.reserve(idx.size() / 3);
    for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
        const auto a = idx[i], b = idx[i + 1], c = idx[i + 2];
        if (a >= verts.size() || b >= verts.size() || c >= verts.size()) continue;
        math::vec3 n = glm::cross(verts[b] - verts[a], verts[c] - verts[a]);
        const double len = glm::length(n);
        if (!(len > 0.0)) continue;
        n /= len;
        // Two-sided: a thumbnail shows an open surface (a plane, a dish) from either side.
        const double lambert = std::abs(glm::dot(n, light));
        Sortable so;
        so.tri.shade = static_cast<float>(0.35 + 0.65 * lambert);
        double depth = 0.0;
        for (int k = 0; k < 3; ++k) {
            const auto&  p = proj[idx[i + static_cast<std::size_t>(k)]];
            so.tri.p[k] = ImVec2(static_cast<float>(0.5 + p.x * s),
                                 static_cast<float>(0.5 - p.y * s));   // ImGui y is down
            depth += p.z;
        }
        so.depth = depth / 3.0;
        out.push_back(so);
    }
    if (out.empty()) return t;

    // Painter's algorithm: no depth buffer here, so draw far triangles first. Correct for the
    // convex-ish bodies in the library; a self-occluding shape would need more than this.
    std::sort(out.begin(), out.end(),
              [](const Sortable& x, const Sortable& y) { return x.depth > y.depth; });
    t.tris.reserve(out.size());
    for (const auto& so : out) t.tris.push_back(so.tri);
    t.ok = true;
    return t;
}

std::unordered_map<std::string, Thumb>& thumb_cache() {
    static std::unordered_map<std::string, Thumb> c;
    return c;
}

// ---- material diagrams ------------------------------------------------------------------

/// The plane of the diagram is 3D x-z: the surface lies along x, its normal is +z, and the
/// incoming ray arrives from the upper left. Screen x is 3D x; screen y is -3D z.
ImVec2 to_screen(const ImVec2& origin, float w, float h, double x, double z) {
    return ImVec2(origin.x + static_cast<float>(0.45 + x * 0.42) * w,
                  origin.y + static_cast<float>(0.62 - z * 0.42) * h);
}

} // namespace

void draw_surface_thumbnail(const char* key, const io::SurfaceDoc& sd,
                            const std::filesystem::path& base_dir, float size) {
    auto& cache = thumb_cache();
    auto  it    = cache.find(key);
    if (it == cache.end())
        it = cache.emplace(key, build_thumb(sd, base_dir)).first;
    const Thumb& th = it->second;

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    auto* dl = ImGui::GetWindowDrawList();

    const ImU32 frame = ImGui::GetColorU32(ImGuiCol_Border, 0.5f);
    dl->AddRect(p0, ImVec2(p0.x + size, p0.y + size), frame, 3.0f);

    if (!th.ok) {
        // A surface that will not build or tessellate: say so, rather than show an empty box
        // that reads as a working entry with nothing in it.
        const ImU32 c = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        dl->AddLine(ImVec2(p0.x + size * 0.25f, p0.y + size * 0.25f),
                    ImVec2(p0.x + size * 0.75f, p0.y + size * 0.75f), c, 1.5f);
        dl->AddLine(ImVec2(p0.x + size * 0.75f, p0.y + size * 0.25f),
                    ImVec2(p0.x + size * 0.25f, p0.y + size * 0.75f), c, 1.5f);
        return;
    }

    const ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    for (const auto& t : th.tris) {
        const ImU32 col = ImGui::GetColorU32(ImVec4(base.x * t.shade, base.y * t.shade,
                                                    base.z * t.shade, 0.95f));
        dl->AddTriangleFilled(ImVec2(p0.x + t.p[0].x * size, p0.y + t.p[0].y * size),
                              ImVec2(p0.x + t.p[1].x * size, p0.y + t.p[1].y * size),
                              ImVec2(p0.x + t.p[2].x * size, p0.y + t.p[2].y * size), col);
    }
}

void draw_material_diagram(const char* key, const io::MaterialDoc& md, float w, float h) {
    static std::unordered_map<std::string, std::shared_ptr<materials::Material>> cache;
    auto it = cache.find(key);
    if (it == cache.end()) {
        std::shared_ptr<materials::Material> built;
        try {
            built = io::build_material(md);
        } catch (const std::exception&) {
            built.reset();
        }
        it = cache.emplace(key, std::move(built)).first;
    }
    if (!it->second) {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h));
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRect(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(ImGuiCol_Border, 0.5f), 3.0f);
        const ImU32 c = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        dl->AddLine(ImVec2(p0.x + w * 0.3f, p0.y + h * 0.3f),
                    ImVec2(p0.x + w * 0.7f, p0.y + h * 0.7f), c, 1.5f);
        dl->AddLine(ImVec2(p0.x + w * 0.7f, p0.y + h * 0.3f),
                    ImVec2(p0.x + w * 0.3f, p0.y + h * 0.7f), c, 1.5f);
        return;
    }
    draw_material_diagram(*it->second, w, h);
}

void draw_material_diagram(const materials::Material& m, float w, float h) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    auto* dl = ImGui::GetWindowDrawList();

    dl->AddRect(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(ImGuiCol_Border, 0.5f), 3.0f);

    // The surface: a line along x at z = 0, tinted with the material's own appearance so the
    // swatch also previews how the object will look in the 3D view.
    const Appearance ap = appearance_for(&m);
    const ImU32 surf_col = ImGui::GetColorU32(ImVec4(static_cast<float>(ap.color.x),
                                                     static_cast<float>(ap.color.y),
                                                     static_cast<float>(ap.color.z), 0.95f));
    dl->AddLine(to_screen(p0, w, h, -1.0, 0.0), to_screen(p0, w, h, 1.0, 0.0), surf_col, 2.5f);

    // The incoming ray, 35 degrees from the normal, arriving from the upper left.
    const double si = std::sin(35.0 * 3.14159265358979323846 / 180.0);
    const double ci = std::cos(35.0 * 3.14159265358979323846 / 180.0);
    const math::vec3 dir{si, 0.0, -ci};
    const ImU32 in_col = ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.35f, 0.95f));
    dl->AddLine(to_screen(p0, w, h, -si * 1.0, ci * 1.0), to_screen(p0, w, h, 0.0, 0.0),
                in_col, 2.0f);

    core::Ray r;
    r.origin    = {-si, 0.0, ci};
    r.direction = dir;
    r.power     = 1.0;
    core::Hit hit;
    hit.position   = {0.0, 0.0, 0.0};
    hit.normal     = {0.0, 0.0, 1.0};
    hit.front_face = true;
    hit.t          = 1.0;

    // A fixed seed, so the diagram is the same every frame and does not shimmer.
    math::Rng rng(20240404);

    // POWER IS LENGTH. Opacity alone cannot be read quantitatively, but two arrows of obviously
    // different length can: a 90:10 pickoff shows one long and one short, a 50:50 two equal.
    auto draw_out = [&](const core::Ray& out, double power, float alpha) {
        if (!(power > 1e-4)) return;
        const double len = 0.18 + 0.87 * std::clamp(power, 0.0, 1.0);
        const math::vec3 d = math::safe_normalize(out.direction);
        const ImU32 c = ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.35f, alpha));
        dl->AddLine(to_screen(p0, w, h, 0.0, 0.0),
                    to_screen(p0, w, h, d.x * len, d.z * len), c,
                    1.0f + 1.2f * static_cast<float>(std::clamp(power, 0.0, 1.0)));
    };

    // Is this material stochastic? Ask it, rather than assume: two interactions from the same
    // ray either agree (a mirror, a splitter, glass) or do not (a diffuser's BRDF sample).
    const auto probe_a = m.interact(r, hit, rng);
    const auto probe_b = m.interact(r, hit, rng);
    const bool scatters =
        probe_a.kind == materials::InteractionKind::Reflected &&
        probe_b.kind == materials::InteractionKind::Reflected &&
        glm::length(probe_a.reflected.direction - probe_b.reflected.direction) > 1e-9;

    if (scatters) {
        // Draw the lobe: many faint samples, so the SHAPE is the cosine distribution and the
        // LENGTH of each carries the albedo. Matte black and white card are the same shape at
        // very different lengths, which is exactly the truth about them.
        constexpr int kSamples = 64;
        for (int i = 0; i < kSamples; ++i) {
            const auto ia = m.interact(r, hit, rng);
            if (ia.kind == materials::InteractionKind::Reflected)
                draw_out(ia.reflected, ia.reflected.power, 0.30f);
        }
    } else {
        switch (probe_a.kind) {
            case materials::InteractionKind::Absorbed:
                break;
            case materials::InteractionKind::Reflected:
                draw_out(probe_a.reflected, probe_a.reflected.power, 0.95f);
                break;
            case materials::InteractionKind::Refracted:
                draw_out(probe_a.transmitted, probe_a.transmitted.power, 0.95f);
                break;
            case materials::InteractionKind::Split:
                // Both branches, each at its own power: this is the picture of a splitter.
                draw_out(probe_a.reflected, probe_a.reflected.power, 0.95f);
                draw_out(probe_a.transmitted, probe_a.transmitted.power, 0.95f);
                break;
        }
    }

    // An absorber emits nothing, and an empty diagram reads as "not implemented" rather than as
    // "absorbs everything". Mark the hit point instead.
    if (probe_a.kind == materials::InteractionKind::Absorbed) {
        const ImVec2 c = to_screen(p0, w, h, 0.0, 0.0);
        const ImU32  x = ImGui::GetColorU32(ImVec4(0.9f, 0.4f, 0.3f, 0.9f));
        dl->AddLine(ImVec2(c.x - 4, c.y - 4), ImVec2(c.x + 4, c.y + 4), x, 1.6f);
        dl->AddLine(ImVec2(c.x + 4, c.y - 4), ImVec2(c.x - 4, c.y + 4), x, 1.6f);
    }
}

} // namespace scrt::viz
