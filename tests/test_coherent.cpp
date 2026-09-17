// Wave 5 Stage 6: the coherent receiver, driven end to end from scene JSON through the real loader
// and Tracer. The Michelson checks the one number that cannot be faked: the fringe period of two
// beams meeting at an angle 2 alpha is lambda / sin(2 alpha).

#include <doctest/doctest.h>

#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/tracer/Tracer.hpp"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using nlohmann::json;
using namespace scrt;

namespace {

constexpr double kLambda = 632.8e-9;

/// A Michelson on a horizontal bench: laser along +x, 50:50 splitter at the origin sending the
/// reflected arm to +y, mirror M1 at x = arm1, mirror M2 at y = 0.1 tilted by alpha about x, and a
/// coherent screen at y = -0.1 facing up the beam. Fringes run along world z = the screen's local y.
json michelson(double alpha_rad, double arm1_m, double coherence_m, const char* sampling,
               bool coherent = true) {
    const double tilt_deg = 90.0 + alpha_rad * 180.0 / math::PI;
    json laser = {{"type", "laser"}, {"origin", {-0.2, 0.0, 0.1}}, {"direction", {1.0, 0.0, 0.0}},
                  {"power_w", 1.0}, {"wavelength_nm", 632.8}, {"beam_diameter_m", 0.004},
                  {"divergence_mrad", 0.0}, {"polarisation", {{"linear_deg", 0.0}}},
                  {"sampling", sampling}};
    if (coherence_m > 0.0) laser["coherence_length_m"] = coherence_m;
    return json{
        {"scene",
         {{"materials",
           {{{"id", "bs"}, {"type", "beam_splitter"}, {"reflectance", 0.5}, {"absorptance", 0.0}},
            {{"id", "mirror"}, {"type", "perfect_mirror"}}}},
          {"elements",
           {{{"name", "splitter"}, {"material", "bs"},
             {"surface", {{"type", "plane"}, {"half_width", 0.018}, {"half_height", 0.0127}}},
             {"transform", {{"translation", {0.0, 0.0, 0.1}}, {"rotation_euler_deg", {90.0, 45.0, 0.0}}}}},
            {{"name", "M1"}, {"material", "mirror"},
             {"surface", {{"type", "disk"}, {"radius", 0.0127}}},
             {"transform", {{"translation", {arm1_m, 0.0, 0.1}}, {"rotation_euler_deg", {0.0, 90.0, 0.0}}}}},
            {{"name", "M2"}, {"material", "mirror"},
             {"surface", {{"type", "disk"}, {"radius", 0.0127}}},
             {"transform", {{"translation", {0.0, 0.1, 0.1}}, {"rotation_euler_deg", {tilt_deg, 0.0, 0.0}}}}}}},
          {"receiver",
           {{"surface", {{"half_width", 0.004}, {"half_height", 0.004}}},
            {"grid", {{"nx", 160}, {"ny", 160}}},
            {"transform", {{"translation", {0.0, -0.1, 0.1}}, {"rotation_euler_deg", {90.0, 0.0, 0.0}}}},
            {"coherent", coherent}}},
          {"sources", {laser}}}},
        {"trace", {{"n_primary_rays", 300000}, {"max_bounces", 8}, {"rng_seed", 5}}}};
}

struct Traced {
    std::vector<double> flux;
    int                 nx = 0, ny = 0;
    double              hh = 0.0, total = 0.0, incoherent = 0.0;
};

Traced trace(const json& j) {
    auto ls = io::build_scene(io::parse_document(j, true), ".");
    ls.scene->build_acceleration_structure();
    ls.cfg.num_threads = 4;
    tracer::Tracer(*ls.scene).run(ls.cfg);
    const auto& acc = ls.scene->receiver()->accumulator();
    return {acc.flux_map_wm2(), acc.nx(), acc.ny(), acc.half_height(), acc.total_power_w(),
            acc.incoherent_power_w()};
}

/// Profile along the screen's local y (the fringe direction), averaged over the central columns.
std::vector<double> central_profile(const Traced& t) {
    std::vector<double> p(static_cast<std::size_t>(t.ny), 0.0);
    for (int j = 0; j < t.ny; ++j) {
        double s = 0.0;
        for (int i = t.nx / 2 - 6; i < t.nx / 2 + 6; ++i) s += t.flux[static_cast<std::size_t>(j * t.nx + i)];
        p[static_cast<std::size_t>(j)] = s / 12.0;
    }
    return p;
}

/// First and last index of the beam in a profile: a CONTIGUOUS range between the outermost samples
/// above 5% of the peak. Filtering sample by sample instead would drop the dark fringes themselves -
/// which is exactly what the first version of this test did, reading a visibility of 0.85 for fringes
/// whose bins go down to 0.4% of the incoherent sum (measured, bin by bin).
std::pair<std::size_t, std::size_t> beam_extent(const std::vector<double>& p) {
    const double peak = *std::max_element(p.begin(), p.end());
    std::size_t  first = p.size(), last = 0;
    for (std::size_t j = 0; j < p.size(); ++j)
        if (p[j] > 0.05 * peak) { first = std::min(first, j); last = j; }
    return {first, last};
}

/// The fringe period (m): the dark fringes inside the beam, each located to sub-bin precision with a
/// parabola through its minimum, and a least-squares line through their positions. A Fourier-peak
/// estimate was tried first and read 1.9% short: with only ~6 fringes across the beam the finite
/// window biases where the peak sits.
double dominant_period(const std::vector<double>& p, double bin_m) {
    const auto [first, last] = beam_extent(p);
    const double hi = *std::max_element(p.begin() + first, p.begin() + last + 1);
    std::vector<double> minima;
    // Not in the outer 10% of the beam on either side: there the two arms only partly overlap (the tilt
    // displaces one by ~0.2 mm), and the first version of this fit took a beam-edge dip at 1.93 mm for a
    // fringe and read 0.606 mm while the real dark fringes were 0.6326-0.6330 mm apart.
    const std::size_t margin = (last - first) / 10;
    for (std::size_t j = first + margin + 1; j + 1 + margin <= last; ++j) {
        if (p[j] < p[j - 1] && p[j] <= p[j + 1] && p[j] < 0.3 * hi) {
            const double denom = p[j - 1] - 2.0 * p[j] + p[j + 1];
            const double shift = denom > 0.0 ? 0.5 * (p[j - 1] - p[j + 1]) / denom : 0.0;
            minima.push_back((static_cast<double>(j) + 0.5 + shift) * bin_m);
        }
    }
    { std::string m; for (double v : minima) m += std::to_string(v * 1e3) + " "; MESSAGE("dark fringes at (mm): " << m); }
    REQUIRE(minima.size() >= 3);
    const double n = static_cast<double>(minima.size());
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (std::size_t k = 0; k < minima.size(); ++k) {
        const double x = static_cast<double>(k);
        sx += x; sy += minima[k]; sxx += x * x; sxy += x * minima[k];
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}
/// Fringe visibility (max - min) / (max + min) over the middle half of the beam.
double visibility(const std::vector<double>& p) {
    const auto [first, last] = beam_extent(p);
    const std::size_t n = last - first + 1;
    double lo = 1e300, hi = 0.0;
    for (std::size_t k = first + n / 4; k < first + 3 * n / 4; ++k) { lo = std::min(lo, p[k]); hi = std::max(hi, p[k]); }
    return (hi - lo) / (hi + lo);
}
} // namespace

TEST_CASE("coherent receiver: one beam reads exactly its incoherent power") {
    // M2 far off the beam: remove the reflected arm by making the splitter transmit everything.
    json j = michelson(0.0, 0.1, 0.0, "grid");
    j["scene"]["materials"][0]["reflectance"] = 0.0;
    j["scene"]["elements"][0]["transform"]["rotation_euler_deg"] = {90.0, 45.0, 0.0};
    // Nothing reaches the screen with R = 0 (M1's return goes straight back to the laser), so use a
    // screen straight in the beam instead.
    j["scene"]["elements"] = json::array();
    j["scene"]["receiver"]["transform"] = {{"translation", {0.1, 0.0, 0.1}}, {"rotation_euler_deg", {0.0, 90.0, 0.0}}};
    const Traced t = trace(j);
    REQUIRE(t.incoherent > 0.9);
    CHECK(t.total == doctest::Approx(t.incoherent).epsilon(1e-9));
}

TEST_CASE("coherent receiver: a tilted Michelson gives fringes at lambda / sin(2 alpha)") {
    const double alpha = 0.5e-3;
    const Traced t     = trace(michelson(alpha, 0.1, 0.0, "grid"));
    const auto   prof  = central_profile(t);
    const double bin   = 2.0 * t.hh / t.ny;
    const double expected = kLambda / std::sin(2.0 * alpha);   // 0.6328 mm
    const double measured = dominant_period(prof, bin);
    MESSAGE("fringe period: expected " << expected * 1e3 << " mm, measured " << measured * 1e3 << " mm");
    // A true relative check: doctest's Approx adds an absolute scale of 1, which on 0.6 mm passes anything.
    CHECK(std::fabs(measured / expected - 1.0) < 0.01);
    const double v = visibility(prof);
    MESSAGE("visibility " << v);
    CHECK(v > 0.9);
    // Interference moves power around, it does not make or destroy it: two 0.25 W arms.
    CHECK(std::fabs(t.total / 0.5 - 1.0) < 0.05);
}

TEST_CASE("coherent receiver: arms 0.2 m apart in path lose their fringes when coherence is 1 cm") {
    const double alpha = 0.5e-3;
    // M1 at 0.2 instead of 0.1: arm 1 is 0.2 m longer round trip than arm 2.
    const double v_coherent = visibility(central_profile(trace(michelson(alpha, 0.2, 0.0, "grid"))));
    const double v_short    = visibility(central_profile(trace(michelson(alpha, 0.2, 0.01, "grid"))));
    MESSAGE("visibility fully coherent " << v_coherent << ", with 1 cm coherence " << v_short);
    CHECK(v_coherent > 0.9);
    CHECK(v_short < 0.1);
}

TEST_CASE("coherent receiver: a randomly sampled source is refused at load, with the reason") {
    try {
        io::build_scene(io::parse_document(michelson(0.5e-3, 0.1, 0.0, "random"), true), ".");
        FAIL("a coherent receiver with a random laser loaded");
    } catch (const std::exception& e) {
        CHECK(std::string(e.what()).find("grid sampling") != std::string::npos);
    }
    // The same scene with an incoherent receiver loads fine.
    CHECK_NOTHROW(io::build_scene(io::parse_document(michelson(0.5e-3, 0.1, 0.0, "random", false), true), "."));
}
