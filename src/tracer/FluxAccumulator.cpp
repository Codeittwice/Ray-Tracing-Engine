#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/math/Constants.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace scrt::tracer {

FluxAccumulator::FluxAccumulator(double half_width, double half_height, int nx, int ny)
    : hw_(half_width), hh_(half_height), nx_(nx), ny_(ny),
      power_sum_(static_cast<std::size_t>(nx * ny), 0.0),
      flux_(static_cast<std::size_t>(nx * ny), 0.0) {
    assert(nx > 0 && ny > 0 && half_width > 0.0 && half_height > 0.0);
}

void FluxAccumulator::deposit(const core::Ray& r, const core::Hit& h) noexcept {
    // h.uv is the local surface coordinate set by Plane::intersect
    double u = h.uv.x;
    double v = h.uv.y;
    if (std::abs(u) > hw_ || std::abs(v) > hh_)
        return;

    int ix = static_cast<int>((u + hw_) / (2.0 * hw_ / nx_));
    int iy = static_cast<int>((v + hh_) / (2.0 * hh_ / ny_));
    ix = std::clamp(ix, 0, nx_ - 1);
    iy = std::clamp(iy, 0, ny_ - 1);

    power_sum_[static_cast<std::size_t>(iy * nx_ + ix)] += r.power;
}

void FluxAccumulator::finalize(std::size_t /*total_primary_rays*/) {
    double bin_area = bin_width_m() * bin_height_m();
    for (std::size_t i = 0; i < power_sum_.size(); ++i)
        flux_[i] = power_sum_[i] / bin_area;
}

double FluxAccumulator::total_power_w() const {
    return std::accumulate(power_sum_.begin(), power_sum_.end(), 0.0);
}

double FluxAccumulator::peak_flux_wm2() const {
    auto it = std::max_element(flux_.begin(), flux_.end());
    return (it != flux_.end()) ? *it : 0.0;
}

double FluxAccumulator::concentration_ratio(double dni_wm2) const {
    return peak_flux_wm2() / dni_wm2;
}

double FluxAccumulator::encircled_diameter(double fraction) const {
    if (power_sum_.empty())
        return 0.0;

    double total = total_power_w();
    if (total <= 0.0)
        return 0.0;

    // Build list of (distance_from_centre², power) for each bin
    struct BinInfo { double r2; double power; };
    std::vector<BinInfo> bins;
    bins.reserve(power_sum_.size());

    double bw = bin_width_m(), bh = bin_height_m();
    for (int j = 0; j < ny_; ++j) {
        double vc = -hh_ + (j + 0.5) * bh;
        for (int i = 0; i < nx_; ++i) {
            double uc = -hw_ + (i + 0.5) * bw;
            double p  = power_sum_[static_cast<std::size_t>(j * nx_ + i)];
            if (p > 0.0)
                bins.push_back({uc*uc + vc*vc, p});
        }
    }
    std::sort(bins.begin(), bins.end(),
              [](const BinInfo& a, const BinInfo& b){ return a.r2 < b.r2; });

    double target = fraction * total;
    double acc    = 0.0;
    for (const auto& b : bins) {
        acc += b.power;
        if (acc >= target)
            return 2.0 * std::sqrt(b.r2);
    }
    return 2.0 * std::sqrt(bins.back().r2);
}

} // namespace scrt::tracer
