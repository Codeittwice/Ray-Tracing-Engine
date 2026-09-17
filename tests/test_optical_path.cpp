// Wave 5 Stage 5: optical path length, traced through the real Tracer.
//
// A probe material records the optical path of every ray that reaches it. The scene is a pencil laser
// through a flat glass window onto the probe, so the expected paths are arithmetic: the geometric
// distance plus (n - 1) times each pass through the glass.

#include <doctest/doctest.h>

#include "scrt/core/Transform.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/materials/ThinDielectricPane.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/Laser.hpp"
#include "scrt/surfaces/Plane.hpp"
#include "scrt/surfaces/ThickLens.hpp"
#include "scrt/tracer/Tracer.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

using namespace scrt;

namespace {

/// Absorbs every ray and records its optical path length. Thread-safe; the tests run one thread.
class OplProbe final : public materials::Material {
public:
    materials::Interaction interact(const core::Ray& r, const core::Hit&, math::Rng&) const override {
        std::lock_guard<std::mutex> lock(mu_);
        seen_.push_back({r.opl_m, r.power, r.medium_n});
        materials::Interaction ia;
        ia.kind      = materials::InteractionKind::Absorbed;
        ia.reflected = r;
        return ia;
    }
    struct Seen { double opl, power, medium_n; };
    std::vector<Seen> seen() const { std::lock_guard<std::mutex> l(mu_); return seen_; }

private:
    mutable std::mutex        mu_;
    mutable std::vector<Seen> seen_;
};

/// Pencil laser at z = 1 going down, an optional window centred at z = 0.5, the probe at z = 0.2,
/// and a receiver far below that nothing reaches (the tracer needs one to run at all).
struct Bench {
    std::unique_ptr<scene::Scene> scene = std::make_unique<scene::Scene>();
    OplProbe*                     probe = nullptr;

    explicit Bench(std::unique_ptr<materials::Material> window_material, bool thick_window) {
        auto p = std::make_unique<OplProbe>();
        probe  = p.get();
        const materials::Material* probe_mat = p.get();
        scene->add_material(std::move(p));

        auto a = std::make_unique<materials::Absorber>();
        const materials::Material* absorber = a.get();
        scene->add_material(std::move(a));

        if (window_material) {
            const materials::Material* wm = window_material.get();
            scene->add_material(std::move(window_material));
            std::unique_ptr<surfaces::Surface> w;
            if (thick_window) w = std::make_unique<surfaces::ThickLens>(0.0, 0.0, 0.01, 0.05);
            else              w = std::make_unique<surfaces::Plane>(0.03, 0.03);
            w->set_transform(core::Transform::from_translation({0.0, 0.0, 0.5}));
            w->set_material(wm);
            scene->add_surface(std::move(w));
        }

        auto screen = std::make_unique<surfaces::Plane>(0.1, 0.1);
        screen->set_transform(core::Transform::from_translation({0.0, 0.0, 0.2}));
        screen->set_material(probe_mat);
        scene->add_surface(std::move(screen));

        auto recv = std::make_unique<scene::Receiver>(0.01, 0.01, 4, 4);
        recv->surface()->set_material(absorber);
        recv->set_transform(core::Transform::from_translation({5.0, 5.0, -5.0}));
        scene->set_receiver(std::move(recv));

        auto laser = std::make_unique<sources::Laser>();
        laser->set_origin({0.0, 0.0, 1.0});
        laser->set_direction({0.0, 0.0, -1.0});
        laser->set_beam_diameter_m(0.0);
        laser->set_divergence_mrad(0.0);
        laser->set_power_w(1.0);
        scene->add_source(std::move(laser));
        scene->build_acceleration_structure();
    }

    std::vector<OplProbe::Seen> run() {
        tracer::TraceConfig cfg;
        cfg.n_primary_rays = 1;
        cfg.num_threads    = 1;
        cfg.rng_seed       = 7;
        cfg.max_bounces    = 16;
        tracer::Tracer(*scene).run(cfg);
        auto s = probe->seen();
        std::sort(s.begin(), s.end(), [](auto& a, auto& b) { return a.opl < b.opl; });
        return s;
    }
};

} // namespace

TEST_CASE("optical path: in air it is the geometric distance") {
    Bench b(nullptr, false);
    const auto s = b.run();
    REQUIRE(s.size() == 1);
    CHECK(s[0].opl == doctest::Approx(0.8).epsilon(1e-12));   // from z = 1 to z = 0.2
    CHECK(s[0].medium_n == 1.0);
}

TEST_CASE("optical path: a 10 mm glass window adds (n - 1) t, and each internal round trip 2 n t") {
    Bench b(std::make_unique<materials::Dielectric>(1.5), true);
    const auto s = b.run();
    REQUIRE(s.size() >= 2);
    // Straight through: 0.8 m of which 0.01 m in glass at n = 1.5.
    CHECK(s[0].opl == doctest::Approx(0.8 + 0.5 * 0.01).epsilon(1e-12));
    CHECK(s[0].medium_n == 1.0);   // back in air when it reaches the probe
    // The first ghost bounced once inside (back face, front face) before leaving: two more passes.
    CHECK(s[1].opl == doctest::Approx(0.8 + 0.5 * 0.01 + 2.0 * 1.5 * 0.01).epsilon(1e-12));
    CHECK(s[1].power < 0.01 * s[0].power);
}

TEST_CASE("optical path: a thin pane adds its glass without having any geometric thickness") {
    Bench b(std::make_unique<materials::ThinDielectricPane>(1.5, 0.004, 0.0), false);
    const auto s = b.run();
    REQUIRE(s.size() == 1);
    // Geometric 0.8 m plus n t = 0.006 for the glass. The 1e-7 m the origin is nudged past the pane is
    // added back by the material, so it cancels the distance the tracer then no longer measures.
    CHECK(s[0].opl == doctest::Approx(0.8 + 1.5 * 0.004).epsilon(1e-12));
}
