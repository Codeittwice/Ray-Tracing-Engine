#include <doctest/doctest.h>
#include "scrt/core/Ray.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/LightSource.hpp"
#include "scrt/sources/Pillbox.hpp"
#include "scrt/tracer/EmissionPlan.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <vector>

#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

namespace {

/// A source with a fixed power and nothing else, for exercising the allocation alone.
class FixedPower final : public scrt::sources::LightSource {
public:
    explicit FixedPower(double w) : w_(w) {}
    double total_power_w() const override { return w_; }
    scrt::core::Ray sample_ray(scrt::math::Rng&) const override { return {}; }
    std::string_view type_name() const override { return "fixed"; }
private:
    double w_;
};

std::vector<std::unique_ptr<scrt::sources::LightSource>> make(std::initializer_list<double> ws) {
    std::vector<std::unique_ptr<scrt::sources::LightSource>> v;
    for (double w : ws) v.push_back(std::make_unique<FixedPower>(w));
    return v;
}

} // namespace

using scrt::tracer::build_emission_plan;
using scrt::tracer::EmissionPlan;

TEST_CASE("EmissionPlan: one emitting source gets every ray and total / N exactly") {
    // The invariant every golden value rests on: with a single sun, per_ray_w must be the
    // same double the old tracer computed, dni * area * cos / N. Checked with ==.
    const auto srcs = make({663.88135955660175});
    const auto plan = build_emission_plan(srcs, 20000);
    REQUIRE(plan.entries.size() == 1);
    CHECK(plan.entries[0].first == 0);
    CHECK(plan.entries[0].count == 20000);
    CHECK(plan.entries[0].per_ray_w == 663.88135955660175 / 20000.0);
    CHECK(plan.total_rays() == 20000);
    CHECK(&plan.entry_for(0) == &plan.entries[0]);
    CHECK(&plan.entry_for(19999) == &plan.entries[0]);
}

TEST_CASE("EmissionPlan: a dead source is dropped, and an all-dead scene plans nothing") {
    const auto one  = make({0.0, 5.0, -1.0});
    const auto plan = build_emission_plan(one, 1000);
    REQUIRE(plan.entries.size() == 1);
    CHECK(plan.entries[0].source == one[1].get());
    CHECK(plan.entries[0].count == 1000);
    CHECK(plan.entries[0].per_ray_w == 5.0 / 1000.0);  // still the single-source branch

    const auto none = make({0.0, 0.0});
    CHECK(build_emission_plan(none, 1000).empty());
    CHECK(build_emission_plan(one, 0).empty());
}

TEST_CASE("EmissionPlan: shares follow power, partition [0, N), and are deterministic") {
    const auto srcs = make({3.0, 1.0, 2.0});
    const auto plan = build_emission_plan(srcs, 600);
    REQUIRE(plan.entries.size() == 3);
    CHECK(plan.entries[0].count == 300);
    CHECK(plan.entries[1].count == 100);
    CHECK(plan.entries[2].count == 200);
    CHECK(plan.entries[0].first == 0);
    CHECK(plan.entries[1].first == 300);
    CHECK(plan.entries[2].first == 400);
    CHECK(plan.total_rays() == 600);
    for (const auto& e : plan.entries)
        CHECK(e.per_ray_w == e.source->total_power_w() / static_cast<double>(e.count));

    // Boundaries of entry_for, both sides of each seam.
    CHECK(plan.entry_for(299).source == srcs[0].get());
    CHECK(plan.entry_for(300).source == srcs[1].get());
    CHECK(plan.entry_for(399).source == srcs[1].get());
    CHECK(plan.entry_for(400).source == srcs[2].get());
    CHECK(plan.entry_for(599).source == srcs[2].get());

    // Remainder goes by descending fractional part: 7 rays over 1:1:1 is 3,2,2 (fractions
    // tie at 1/3, ties by index), and every ray is accounted for.
    const auto tie = make({1.0, 1.0, 1.0});
    const auto p7  = build_emission_plan(tie, 7);
    CHECK(p7.entries[0].count == 3);
    CHECK(p7.entries[1].count == 2);
    CHECK(p7.entries[2].count == 2);
    CHECK(p7.total_rays() == 7);

    // Same inputs, same plan: no RNG anywhere in the allocation.
    const auto again = build_emission_plan(srcs, 600);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(again.entries[i].count == plan.entries[i].count);
        CHECK(again.entries[i].first == plan.entries[i].first);
    }
}

TEST_CASE("EmissionPlan: a milliwatt source beside a kilowatt one still gets one ray") {
    const auto srcs = make({5000.0, 0.001});
    const auto plan = build_emission_plan(srcs, 1000);
    REQUIRE(plan.entries.size() == 2);
    CHECK(plan.entries[0].count == 999);
    CHECK(plan.entries[1].count == 1);
    CHECK(plan.entries[1].per_ray_w == 0.001);
    CHECK(plan.total_rays() == 1000);

    // Fewer rays than sources: nobody is invented, the plan just covers what it can.
    const auto many = make({1.0, 1.0, 1.0, 1.0});
    const auto p2   = build_emission_plan(many, 2);
    CHECK(p2.total_rays() == 2);
    for (const auto& e : p2.entries) CHECK(e.count >= 1);
}

TEST_CASE("Golden flux is untouched by a dead second sun in the scene") {
    // The strongest statement of "the sun stops being special": adding a source that emits
    // nothing must not move a golden value by a single bit, because it is dropped from the
    // plan before any arithmetic and the surviving sun still takes the single-source branch.
    const auto path = std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "parabolic_dish.json";
    REQUIRE(std::filesystem::exists(path));

    auto trace = [&](bool add_dead_sun) {
        auto ls = scrt::io::load_scene(path);
        REQUIRE(ls.scene != nullptr);
        if (add_dead_sun) {
            auto dead = std::make_unique<scrt::sources::Pillbox>(0.0);
            dead->set_sun_angles({180.0, -20.0});  // below the horizon: cosine clamps to 0
            dead->set_aperture(scrt::scene::Aperture{});
            REQUIRE(dead->total_power_w() == 0.0);
            ls.scene->add_source(std::move(dead));
        }
        ls.cfg.rng_seed       = 42;
        ls.cfg.num_threads    = 1;
        ls.cfg.n_primary_rays = 20000;
        ls.cfg.record_paths   = false;
        scrt::tracer::Tracer tracer(*ls.scene);
        tracer.run(ls.cfg);
        double total = 0.0;
        for (const auto& face : ls.scene->receiver()->faces())
            total += face->accumulator().total_power_w();
        return total;
    };

    const double alone     = trace(false);
    const double with_dead = trace(true);
    CHECK(alone == doctest::Approx(663.88135955660175).epsilon(1e-9));
    CHECK(with_dead == alone);
}

TEST_CASE("Two half-strength suns collect what one full-strength sun does") {
    // Not bit-identical, and it must not be claimed to be: the two suns consume the slot's
    // RNG sequence differently. But the power model is linear, so on a flat absorbing plate
    // under collimated light, where every ray lands, the totals agree exactly up to rounding.
    auto build = [](std::vector<double> dnis) {
        auto scene    = std::make_unique<scrt::scene::Scene>();
        auto absorber = std::make_unique<scrt::materials::Absorber>();
        auto* absorber_ptr = absorber.get();
        scene->add_material(std::move(absorber));
        auto recv = std::make_unique<scrt::scene::Receiver>(0.5, 0.5, 8, 8);
        recv->surface()->set_material(absorber_ptr);
        scene->set_receiver(std::move(recv));
        for (double dni : dnis) {
            auto sun = std::make_unique<scrt::sources::Pillbox>(0.0);
            sun->set_sun_direction({0.0, 0.0, -1.0});
            sun->set_dni(dni);
            scrt::scene::Aperture ap;
            ap.center = {0.0, 0.0, 1.0};
            ap.radius = 0.5;  // Inscribed in the plate: every ray lands.
            sun->set_aperture(ap);
            scene->add_source(std::move(sun));
        }
        scene->build_acceleration_structure();
        return scene;
    };
    auto total = [](scrt::scene::Scene& scene) {
        scrt::tracer::TraceConfig cfg;
        cfg.n_primary_rays = 10000;
        cfg.rng_seed       = 11;
        cfg.num_threads    = 1;
        scrt::tracer::Tracer tracer(scene);
        tracer.run(cfg);
        return scene.receiver()->accumulator().total_power_w();
    };
    auto one = build({1000.0});
    auto two = build({500.0, 500.0});
    scrt::scene::Aperture unit;
    unit.radius = 0.5;
    const double expected = 1000.0 * unit.area();
    CHECK(total(*one) == doctest::Approx(expected).epsilon(1e-9));
    CHECK(total(*two) == doctest::Approx(expected).epsilon(1e-9));
}
