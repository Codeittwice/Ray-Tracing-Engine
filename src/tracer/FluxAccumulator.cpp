#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/optics/Polarisation.hpp"
#include "scrt/surfaces/Surface.hpp"
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

FluxAccumulator FluxAccumulator::from_flux_map(double half_width, double half_height, int nx, int ny,
                                               const std::vector<double>& flux_wm2) {
    FluxAccumulator a(half_width, half_height, nx, ny);
    const double area = a.bin_width_m() * a.bin_height_m();
    for (std::size_t i = 0; i < a.flux_.size() && i < flux_wm2.size(); ++i) {
        a.flux_[i]      = flux_wm2[i];
        a.power_sum_[i] = flux_wm2[i] * area;
    }
    return a;
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
    // Coherent mode adds the ray's field as well. The line above is the whole of the incoherent
    // path and is unchanged, so every existing (incoherent) receiver reads exactly as before.
    if (coherent_) deposit_coherent(r, h, ix, iy);
}

void FluxAccumulator::set_coherent(bool on) {
    coherent_ = on;
    arms_.assign(on ? power_sum_.size() : 0u, {});
}

void FluxAccumulator::deposit_coherent(const core::Ray& r, const core::Hit& h, int ix, int iy) {
    const std::size_t bin = static_cast<std::size_t>(iy * nx_ + ix);
    // Amplitude sqrt(P) with phase 2 pi opl / lambda. Wavelength is in nm on the ray, metres here.
    const double lambda_m = r.wavelength_nm * 1.0e-9;
    double       opl      = r.opl_m;

    // Carry the phase to the BIN CENTRE along the ray's own direction, treating the ray as a local
    // plane wave, so the field is sampled AT the bin centre rather than averaged over the phase ramp a
    // tilted beam lays across the bin. For bins well under a fringe the difference is small (measured:
    // it did not change the Michelson test's numbers at 12 bins per fringe); it keeps a coarse grid
    // from under-reading a tilted beam, which a straight in-bin sum would do.
    if (h.surface) {
        const double     u_c = -hw_ + (ix + 0.5) * bin_width_m();
        const double     v_c = -hh_ + (iy + 0.5) * bin_height_m();
        const math::vec3 off = h.surface->transform().direction_to_world(
            {u_c - h.uv.x, v_c - h.uv.y, 0.0});
        opl += r.medium_n * glm::dot(r.direction, off);
    }
    const double phase = math::TWO_PI * opl / lambda_m;
    const std::complex<double> a = std::sqrt(std::max(0.0, r.power)) *
                                   std::complex<double>(std::cos(phase), std::sin(phase));

    auto& arms = arms_[bin];
    Arm*  arm  = nullptr;
    for (auto& candidate : arms)
        if (candidate.source == r.source && candidate.branch == r.branch) { arm = &candidate; break; }
    if (!arm) {
        arms.push_back(Arm{});
        arm         = &arms.back();
        arm->source = r.source;
        arm->branch = r.branch;
    }
    if (r.polarised) {
        const optics::FieldVec f = optics::world_field(r);
        arm->ex += a * f.x;
        arm->ey += a * f.y;
        arm->ez += a * f.z;
    } else {
        arm->eu += a;
    }
    // A primary ray's whole tree is traced before the next primary starts on this thread, so a
    // change of id is a new primary. This is what an arm is normalised by.
    if (!arm->has_last || arm->last_id != r.id) {
        arm->primaries += 1.0;
        arm->last_id  = r.id;
        arm->has_last = true;
    }
    arm->contributions += 1.0;
    arm->opl_sum       += r.opl_m;
}

void FluxAccumulator::clear() noexcept {
    std::fill(power_sum_.begin(), power_sum_.end(), 0.0);
    std::fill(flux_.begin(), flux_.end(), 0.0);
    for (auto& a : arms_) a.clear();
    coherent_power_ = 0.0;
}

void FluxAccumulator::merge_from(const FluxAccumulator& other) noexcept {
    assert(power_sum_.size() == other.power_sum_.size());
    for (std::size_t i = 0; i < power_sum_.size(); ++i)
        power_sum_[i] += other.power_sum_[i];
    if (!coherent_ || arms_.size() != other.arms_.size()) return;
    // Fields add; so do primary counts, because slots trace disjoint primaries.
    for (std::size_t b = 0; b < arms_.size(); ++b) {
        for (const Arm& o : other.arms_[b]) {
            Arm* mine = nullptr;
            for (auto& a : arms_[b])
                if (a.source == o.source && a.branch == o.branch) { mine = &a; break; }
            if (!mine) { arms_[b].push_back(o); continue; }
            mine->ex += o.ex;
            mine->ey += o.ey;
            mine->ez += o.ez;
            mine->eu += o.eu;
            mine->primaries     += o.primaries;
            mine->contributions += o.contributions;
            mine->opl_sum       += o.opl_sum;
        }
    }
}

void FluxAccumulator::finalize(std::size_t /*total_primary_rays*/) {
    double bin_area = bin_width_m() * bin_height_m();
    for (std::size_t i = 0; i < power_sum_.size(); ++i)
        flux_[i] = power_sum_[i] / bin_area;
    if (!coherent_) return;

    // Coherent intensity per bin: I = sum_a |E_a|^2 + sum_{a<b, same source} 2 Re<E_a, E_b> gamma_ab,
    // with E_a = (arm field) / sqrt(arm primaries) and gamma = exp(-|mean opl_a - mean opl_b| / Lc).
    coherent_power_ = 0.0;
    for (std::size_t i = 0; i < arms_.size(); ++i) {
        const auto& arms = arms_[i];
        double      I    = 0.0;
        for (std::size_t a = 0; a < arms.size(); ++a) {
            const Arm&   A  = arms[a];
            const double ka = A.primaries > 0.0 ? 1.0 / A.primaries : 0.0;
            I += ka * (std::norm(A.ex) + std::norm(A.ey) + std::norm(A.ez) + std::norm(A.eu));
            for (std::size_t b = a + 1; b < arms.size(); ++b) {
                const Arm& B = arms[b];
                if (B.source != A.source || A.primaries <= 0.0 || B.primaries <= 0.0) continue;
                const double kab   = 1.0 / std::sqrt(A.primaries * B.primaries);
                const std::complex<double> cross = std::conj(A.ex) * B.ex + std::conj(A.ey) * B.ey +
                                                   std::conj(A.ez) * B.ez + std::conj(A.eu) * B.eu;
                double gamma = 1.0;
                const double lc = A.source < coherence_len_.size() ? coherence_len_[A.source] : 0.0;
                if (lc > 0.0) {
                    const double da = A.opl_sum / A.contributions;
                    const double db = B.opl_sum / B.contributions;
                    gamma = std::exp(-std::fabs(da - db) / lc);
                }
                I += 2.0 * kab * gamma * cross.real();
            }
        }
        I = std::max(0.0, I);   // rounding only: the full sum is a norm when gamma = 1
        flux_[i] = I / bin_area;
        coherent_power_ += I;
    }
}

double FluxAccumulator::incoherent_power_w() const {
    return std::accumulate(power_sum_.begin(), power_sum_.end(), 0.0);
}

double FluxAccumulator::total_power_w() const {
    if (coherent_) return coherent_power_;
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
