#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "implot.h"

namespace scrt::viz {

/// How the flux map and the ray paths are drawn. Presentation only - nothing here changes a
/// number that is traced, reported or exported.
///
/// Shared because the same flux array is drawn twice, in two libraries: as an ImPlot heatmap in
/// the analysis panel and as a Polyscope scalar quantity on the receiver in the 3D view. If the
/// two took their colours and their smoothing from different places they would disagree, and the
/// user would be looking at two pictures of one result.

/// Colour ramps offered for the flux map, by Polyscope's own name for them.
///
/// Polyscope's list is the shorter of the two libraries', so it sets what can be offered; the
/// ImPlot side is built from Polyscope's own ramp (see implot_flux_colormap) rather than matched
/// to a near-equivalent, so the plot and the 3D view are the same colours rather than similar.
const std::vector<std::string>& flux_colormap_names();

/// Human-readable label for one of those names, for the settings dropdown.
const char* flux_colormap_label(const std::string& name);

/// The active flux colour ramp; "viridis" until changed.
const std::string& flux_colormap();
void               set_flux_colormap(std::string name);

/// Standard deviation, in receiver bins, of the Gaussian blur applied to the DISPLAYED flux map.
/// Zero means no smoothing.
///
/// Display only, and deliberately so. Monte Carlo flux over a fine grid is Poisson-noisy - the
/// confetti look - and a blur makes the shape of the hot spot legible. It also lowers the peak,
/// so the headline "hottest spot" figure, the CSV and the JSON summary are all still computed
/// from the raw bins. A smoothed picture is a picture, not a measurement.
float flux_smoothing_sigma();
void  set_flux_smoothing_sigma(float sigma);

/// Radius of a drawn ray path, in metres. Absolute, not relative to the scene's length scale.
float ray_radius_m();
void  set_ray_radius_m(float r);

/// Opacity of the drawn ray paths, 0 (invisible) to 1 (solid).
float ray_opacity();
void  set_ray_opacity(float a);

/// Whether the placement grid is drawn (lines at the move-snap step, inside the scene's bounds).
bool show_grid();
void set_show_grid(bool on);

/// Whether the drawn-only posts under bench parts are shown (they are never traced either way).
bool show_posts();
void set_show_posts(bool on);

/// Separable Gaussian blur of an `nx` x `ny` flux grid, row-major, with `sigma` in bins.
///
/// Returns `in` unchanged when sigma is not positive or the grid is degenerate. Edges are
/// handled by clamping rather than by wrapping or zero-padding: the receiver has a boundary, and
/// zero-padding would draw a dark rim that is not in the data.
std::vector<double> gaussian_smooth(const std::vector<double>& in, int nx, int ny, double sigma);

/// The active flux ramp as an ImPlot colormap id, built from Polyscope's own ramp and cached.
///
/// Must be called with a live Polyscope render engine; returns ImPlotColormap_Viridis before
/// Polyscope is initialised.
ImPlotColormap implot_flux_colormap();

} // namespace scrt::viz
