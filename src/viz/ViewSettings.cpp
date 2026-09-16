#include "scrt/viz/ViewSettings.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "polyscope/polyscope.h"
#include "polyscope/render/engine.h"

namespace scrt::viz {

namespace {

std::string g_colormap      = "viridis";
// Mild smoothing ON by default. The preview's speckle was the first thing a human noticed about
// this app, and a map nobody can read is worse than a map that is a little soft. The slider in
// Settings turns it off for anyone who wants the raw bins.
float       g_sigma         = 1.0f;
float       g_ray_radius    = 0.0003f;
float       g_ray_opacity   = 0.20f;

/// ImPlot colormap ids built from Polyscope ramps, keyed by name. ImPlot keeps every colormap it
/// is given for the life of its context, so each name is registered exactly once.
std::unordered_map<std::string, ImPlotColormap> g_implot_maps;

} // namespace

const std::vector<std::string>& flux_colormap_names() {
    // Polyscope's built-in set, minus the ones that make no sense for a one-sided quantity:
    // "phase" is cyclic and "pink-green" is diverging about a midpoint that flux does not have.
    static const std::vector<std::string> kNames = {
        "viridis", "turbo", "jet", "spectral", "rainbow", "reds", "blues", "coolwarm",
    };
    return kNames;
}

const char* flux_colormap_label(const std::string& name) {
    // Short enough to fit the dropdown at any interface scale; the trade-offs between them are
    // in the tooltip on the control, not crammed into the entries.
    if (name == "viridis")  return "Viridis";
    if (name == "turbo")    return "Turbo";
    if (name == "jet")      return "Jet";
    if (name == "spectral") return "Spectral";
    if (name == "rainbow")  return "Rainbow";
    if (name == "reds")     return "Reds";
    if (name == "blues")    return "Blues";
    if (name == "coolwarm") return "Cool to warm";
    return name.c_str();
}

const std::string& flux_colormap() { return g_colormap; }

void set_flux_colormap(std::string name) { g_colormap = std::move(name); }

float flux_smoothing_sigma() { return g_sigma; }
void  set_flux_smoothing_sigma(float sigma) { g_sigma = std::max(0.0f, sigma); }

float ray_radius_m() { return g_ray_radius; }
void  set_ray_radius_m(float r) { g_ray_radius = std::clamp(r, 0.0002f, 0.02f); }

float ray_opacity() { return g_ray_opacity; }
void  set_ray_opacity(float a) { g_ray_opacity = std::clamp(a, 0.05f, 1.0f); }

namespace { bool g_show_grid = true; }
bool show_grid() { return g_show_grid; }
void set_show_grid(bool on) { g_show_grid = on; }

// ---- gaussian_smooth --------------------------------------------------------------------------

std::vector<double> gaussian_smooth(const std::vector<double>& in, int nx, int ny, double sigma) {
    if (sigma <= 0.0 || nx <= 0 || ny <= 0 ||
        in.size() != static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny))
        return in;

    // Three standard deviations captures 99.7% of the kernel; going wider costs time and changes
    // nothing anyone can see.
    const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));

    std::vector<double> kernel(static_cast<std::size_t>(2 * radius + 1));
    double              sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double w = std::exp(-0.5 * (i * i) / (sigma * sigma));
        kernel[static_cast<std::size_t>(i + radius)] = w;
        sum += w;
    }
    for (double& w : kernel) w /= sum;

    // Separable: two 1D passes instead of one 2D kernel, O(n*r) rather than O(n*r^2).
    std::vector<double> tmp(in.size(), 0.0), out(in.size(), 0.0);

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            double acc = 0.0;
            for (int k = -radius; k <= radius; ++k) {
                // Clamp at the edge. Zero-padding would draw a dark rim around the receiver that
                // is not in the data, and wrapping would fold the far edge into the near one.
                const int si = std::clamp(i + k, 0, nx - 1);
                acc += kernel[static_cast<std::size_t>(k + radius)] *
                       in[static_cast<std::size_t>(j * nx + si)];
            }
            tmp[static_cast<std::size_t>(j * nx + i)] = acc;
        }
    }
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            double acc = 0.0;
            for (int k = -radius; k <= radius; ++k) {
                const int sj = std::clamp(j + k, 0, ny - 1);
                acc += kernel[static_cast<std::size_t>(k + radius)] *
                       tmp[static_cast<std::size_t>(sj * nx + i)];
            }
            out[static_cast<std::size_t>(j * nx + i)] = acc;
        }
    }
    return out;
}

// ---- implot_flux_colormap ---------------------------------------------------------------------

ImPlotColormap implot_flux_colormap() {
    if (!polyscope::isInitialized() || !polyscope::render::engine)
        return ImPlotColormap_Viridis;

    const auto it = g_implot_maps.find(g_colormap);
    if (it != g_implot_maps.end()) return it->second;

    // Sample Polyscope's own ramp rather than picking ImPlot's nearest-looking built-in, so the
    // heatmap in the analysis panel and the receiver in the 3D view are the same colours.
    constexpr int          kStops = 32;
    std::vector<ImVec4>    stops;
    stops.reserve(kStops);
    try {
        const auto& cmap = polyscope::render::engine->getColorMap(g_colormap);
        for (int i = 0; i < kStops; ++i) {
            // Never exactly 1.0. ValueColorMap::getValue blends values[lowerInd] with
            // values[lowerInd + 1], and at t == 1 lowerInd is the last entry - so the upper
            // index is one past the end of the ramp. Sampling a hair below the top reads the
            // same colour and stays inside the vector.
            const double t = std::min(static_cast<double>(i) / (kStops - 1), 1.0 - 1e-9);
            const glm::vec3 c = cmap.getValue(t);
            stops.push_back(ImVec4(c.r, c.g, c.b, 1.0f));
        }
    } catch (...) {
        return ImPlotColormap_Viridis;   // an unknown name; Polyscope throws rather than returns
    }

    // ImPlot keys its colormaps by name, and re-adding one is an assertion failure rather than a
    // replace - hence the cache above, and a name that cannot collide with a built-in.
    // qual = false: this is a continuous ramp, and ImPlot interpolates between the stops rather
    // than drawing 32 hard bands.
    const std::string name = "scrt_" + g_colormap;
    const ImPlotColormap id = ImPlot::AddColormap(name.c_str(), stops.data(),
                                                  static_cast<int>(stops.size()), /*qual=*/false);
    g_implot_maps[g_colormap] = id;
    return id;
}

} // namespace scrt::viz
