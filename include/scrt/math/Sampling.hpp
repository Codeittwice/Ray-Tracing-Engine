#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <vector>

namespace scrt::math {

/// Tabulated 1D probability distribution with inverse-CDF sampling.
/// Build once from a PDF vector, then call sample() in tight loops.
class TabulatedDist1D {
public:
    TabulatedDist1D() = default;

    /// Build from non-negative pdf values linearly spaced over [x_min, x_max].
    void build(std::vector<double> pdf, double x_min, double x_max) {
        assert(!pdf.empty() && x_max > x_min);
        x_min_ = x_min;
        x_max_ = x_max;
        n_     = pdf.size();

        cdf_.resize(n_ + 1);
        cdf_[0] = 0.0;
        for (std::size_t i = 0; i < n_; ++i)
            cdf_[i + 1] = cdf_[i] + pdf[i];

        double total = cdf_[n_];
        assert(total > 0.0);
        for (auto& v : cdf_) v /= total;
    }

    /// Map uniform u ∈ [0,1) to a sample x ∈ [x_min, x_max] via inverse CDF.
    double sample(double u) const {
        auto it  = std::lower_bound(cdf_.begin(), cdf_.end(), u);
        auto raw = static_cast<std::ptrdiff_t>(it - cdf_.begin()) - 1;
        auto idx = static_cast<std::size_t>(std::clamp(raw, std::ptrdiff_t{0},
                                                        static_cast<std::ptrdiff_t>(n_ - 1)));
        double c0 = cdf_[idx], c1 = cdf_[idx + 1];
        double t  = (c1 > c0) ? std::clamp((u - c0) / (c1 - c0), 0.0, 1.0) : 0.5;
        double step = (x_max_ - x_min_) / static_cast<double>(n_);
        return x_min_ + (static_cast<double>(idx) + t) * step;
    }

    bool empty() const { return n_ == 0; }

private:
    std::vector<double> cdf_;
    double      x_min_ {0.0};
    double      x_max_ {1.0};
    std::size_t n_     {0};
};

} // namespace scrt::math
