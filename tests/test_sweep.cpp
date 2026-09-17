// Wave 5 Stage 8: the sweep engine, checked against the physics it exists to show.
//
// Moving a Michelson mirror by lambda/2 lengthens that arm's round trip by one wavelength, so every
// point on the screen goes through exactly one full fringe cycle. The sweep is run on QA 10 itself.

#include <doctest/doctest.h>

#include "scrt/io/SceneLoader.hpp"
#include "scrt/surfaces/Surface.hpp"
#include "scrt/viz/Sweep.hpp"

#include <algorithm>
#include <cmath>

#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

using namespace scrt;

namespace {

std::uint64_t id_of(const scene::Scene& sc, const std::string& name) {
    for (const auto& s : sc.surfaces())
        if (s->name() == name) return s->id();
    return 0;
}

} // namespace

TEST_CASE("sweep: moving a Michelson mirror by lambda/2 runs the screen through one fringe cycle") {
    auto ls = io::load_scene(std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "feature_checks" /
                             "qa_10_michelson_fringes.json");
    ls.scene->build_acceleration_structure();
    const auto m1 = id_of(*ls.scene, "M1_fixed");
    REQUIRE(m1 != 0);
    const core::Transform before = ls.scene->surface_by_id(m1)->transform();

    const double lambda = 632.8e-9;
    viz::SweepSpec spec;
    spec.surface_id = m1;
    spec.kind       = viz::SweepSpec::Kind::Move;
    spec.axis       = 0;                 // M1 faces the beam along x
    spec.from       = 0.0;
    spec.to         = 0.5 * lambda;      // one wavelength of round-trip path
    spec.steps      = 25;
    spec.rays       = 60000;
    ls.cfg.num_threads = 4;

    viz::SweepRunner run;
    REQUIRE(run.start(*ls.scene, spec, ls.cfg));
    int guard = 0;
    while (!run.step(*ls.scene) && ++guard < 100) {}
    REQUIRE(run.error().empty());
    REQUIRE(run.frames_done() == 25);

    // The pose is restored exactly: a sweep explores, it does not move the part.
    CHECK(ls.scene->surface_by_id(m1)->transform().matrix() == before.matrix());

    // The centre intensity swings through a full cycle and ENDS where it began.
    std::vector<double> c;
    for (const auto& f : run.frames()) c.push_back(f.centre_wm2);
    const double hi = *std::max_element(c.begin(), c.end());
    const double lo = *std::min_element(c.begin(), c.end());
    MESSAGE("centre intensity over the sweep: min " << lo << ", max " << hi << ", first " << c.front()
            << ", last " << c.back());
    CHECK(hi > 5.0 * std::max(lo, 1e-9));                        // it really goes dark and bright
    CHECK(std::fabs(c.back() - c.front()) < 0.05 * (hi - lo));    // and one cycle brings it back
    // Interference moves power around the screen; the total stays put.
    for (const auto& f : run.frames())
        CHECK(std::fabs(f.total_power_w / run.frames().front().total_power_w - 1.0) < 0.05);
}

TEST_CASE("sweep: refuses what it cannot record, with a reason") {
    auto ls = io::load_scene(std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "feature_checks" /
                             "qa_08_malus_polariser.json");
    viz::SweepRunner run;
    viz::SweepSpec   spec;
    spec.surface_id = 999999;
    CHECK_FALSE(run.start(*ls.scene, spec, ls.cfg));
    CHECK_FALSE(run.error().empty());
    spec.surface_id = ls.scene->surfaces()[0]->id();
    spec.steps      = 1;
    CHECK_FALSE(run.start(*ls.scene, spec, ls.cfg));
}

TEST_CASE("sweep: turning a polariser about the beam traces Malus's law on QA 18") {
    auto ls = io::load_scene(std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "feature_checks" /
                             "qa_18_sweep_malus_turn.json");
    ls.scene->build_acceleration_structure();
    const auto pol = id_of(*ls.scene, "polariser_axis_vertical");
    REQUIRE(pol != 0);

    viz::SweepSpec spec;
    spec.surface_id = pol;
    spec.kind       = viz::SweepSpec::Kind::Turn;
    spec.axis       = 0;                           // the beam runs along x
    spec.from       = 0.0;
    spec.to         = 3.141592653589793;
    spec.steps      = 37;                          // every 5 degrees
    spec.rays       = 5000;
    ls.cfg.num_threads = 4;

    viz::SweepRunner run;
    REQUIRE(run.start(*ls.scene, spec, ls.cfg));
    int guard = 0;
    while (!run.step(*ls.scene) && ++guard < 100) {}
    REQUIRE(run.error().empty());
    REQUIRE(run.frames_done() == 37);
    for (const auto& f : run.frames()) {
        const double c = std::cos(f.value);
        CHECK(std::fabs(f.total_power_w - 0.005 * c * c) < 1e-9);   // ideal polariser: exact per ray
    }
    CHECK(std::fabs(run.frames()[9].total_power_w - 0.0025) < 1e-9);   // 45 degrees
}
