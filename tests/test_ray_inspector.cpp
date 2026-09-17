// Wave 5 Stage 7b: what the ray inspector says, checked on the QA 16 scene itself.
//
// The scene is traced with path recording through the real loader and Tracer, and every recorded
// edge is described with the same optics::describe_polarisation the Selection panel calls. So QA 16's
// name - "linear, 0 deg from vertical", then "circular, right", then "circular, left" - is a tested
// claim rather than a hope.

#include <doctest/doctest.h>

#include "scrt/io/SceneLoader.hpp"
#include "scrt/optics/Polarisation.hpp"
#include "scrt/tracer/Tracer.hpp"

#include <cmath>
#include <string>

#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

using namespace scrt;
using K = optics::PolarisationDescription::Kind;

TEST_CASE("ray inspector: QA 16 reads linear, then right circular, then left circular") {
    const auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "feature_checks" /
                      "qa_16_ray_inspector_polarisation.json";
    auto ls = io::load_scene(path);
    ls.scene->build_acceleration_structure();
    ls.cfg.num_threads         = 1;
    ls.cfg.n_primary_rays      = 200;
    ls.cfg.record_paths        = true;
    ls.cfg.max_paths_to_record = 20;
    const auto result = tracer::Tracer(*ls.scene).run(ls.cfg);
    REQUIRE(!result.sampled_paths.empty());

    int checked = 0;
    for (const auto& p : result.sampled_paths) {
        CHECK(p.monochromatic);
        REQUIRE(p.edge_info.size() == p.edges.size());
        // Edges in order: laser -> quarter-wave plate (x = 0), -> half-wave plate (x = 0.12),
        // -> screen (x = 0.25). Each edge records the state the light had while travelling it.
        REQUIRE(p.edges.size() == 3);
        const auto a = optics::describe_polarisation(p.edge_info[0].Es, p.edge_info[0].Ep,
                                                     p.edge_info[0].s_axis, p.edge_info[0].direction);
        const auto b = optics::describe_polarisation(p.edge_info[1].Es, p.edge_info[1].Ep,
                                                     p.edge_info[1].s_axis, p.edge_info[1].direction);
        const auto c = optics::describe_polarisation(p.edge_info[2].Es, p.edge_info[2].Ep,
                                                     p.edge_info[2].s_axis, p.edge_info[2].direction);
        CHECK(a.kind == K::Linear);
        CHECK(std::fabs(a.axis_deg) < 1e-6);
        CHECK(b.kind == K::Circular);
        // RIGHT. Worked by hand: fast axis f = -(s+p)/sqrt2, slow (s-p)/sqrt2; delaying the slow part by
        // 90 deg gives E = s(1+i)/2 + p(1-i)/2, so Ep/Es = -i, i.e. (1,-i)/sqrt2 = right in the
        // documented convention - and E(t) = s cos wt - p sin wt turns clockwise seen looking into the
        // beam. The first draft of QA 16's name said left; this test is what caught it.
        CHECK(b.ellipticity_deg < -44.0);
        CHECK(c.kind == K::Circular);
        CHECK(c.ellipticity_deg > 44.0);    // left: a half-wave plate reverses the hand
        CHECK(p.edge_info[0].wavelength_nm == 532.0);
        CHECK(p.edge_info[2].opl_end_m > p.edge_info[0].opl_end_m);
        ++checked;
    }
    CHECK(checked > 0);
}

TEST_CASE("ray inspector: linear states read their angle from vertical, folded into (-90, 90]") {
    core::Ray r;
    r.direction = {1.0, 0.0, 0.0};
    for (double deg : {0.0, 30.0, -60.0, 90.0, 135.0}) {
        optics::set_polarisation(r, optics::PolarisationKind::Linear, deg * 3.141592653589793 / 180.0);
        const auto d = optics::describe_polarisation(r.Es, r.Ep, r.s_axis, r.direction);
        double expect = deg;
        if (expect > 90.0) expect -= 180.0;
        CHECK(d.kind == K::Linear);
        CHECK(std::fabs(d.axis_deg - expect) < 1e-9);
    }
}
