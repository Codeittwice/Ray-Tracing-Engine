#include <doctest/doctest.h>
#include "scrt/core/Transform.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/Pillbox.hpp"
#include "scrt/surfaces/Plane.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <memory>

// Tracer::run has two overloads: one deposits into the receiver's own faces (the goldens use
// it), one into a caller-supplied FluxAccumulator (headless, scrt_compare, the GUI preview).
// The second used to deposit on ANY absorbed hit, so a black plate anywhere in the scene was
// booked as light on the receiver. They must agree, and on identical rays they must agree to
// the bit.

namespace {

void build(scrt::scene::Scene& scene) {
    auto absorber = std::make_unique<scrt::materials::Absorber>();
    auto* abs_ptr = absorber.get();
    scene.add_material(std::move(absorber));

    // A black half-plate covering x < 0, halfway between the sun and the receiver.
    auto wall = std::make_unique<scrt::surfaces::Plane>(0.25, 0.5);
    wall->set_material(abs_ptr);
    wall->set_transform(scrt::core::Transform::from_translation({-0.25, 0.0, 0.5}));
    scene.add_surface(std::move(wall));

    auto recv = std::make_unique<scrt::scene::Receiver>(0.5, 0.5, 8, 8);
    recv->surface()->set_material(abs_ptr);
    scene.set_receiver(std::move(recv));

    auto sun = std::make_unique<scrt::sources::Pillbox>(0.0);
    sun->set_sun_direction({0.0, 0.0, -1.0});
    sun->set_dni(1000.0);
    scrt::scene::Aperture ap;
    ap.center = {0.0, 0.0, 1.0};
    ap.radius = 0.5;
    sun->set_aperture(ap);
    scene.add_source(std::move(sun));
    scene.build_acceleration_structure();
}

} // namespace

TEST_CASE("Tracer: the accumulator overload deposits only on receiver faces, like the receiver overload") {
    scrt::tracer::TraceConfig cfg;
    cfg.n_primary_rays = 20000;
    cfg.rng_seed       = 101;
    cfg.num_threads    = 1;
    cfg.max_bounces    = 2;

    scrt::scene::Scene a;
    build(a);
    scrt::tracer::Tracer ta(a);
    ta.run(cfg);
    const double via_receiver = a.receiver()->accumulator().total_power_w();

    scrt::scene::Scene b;
    build(b);
    scrt::tracer::FluxAccumulator acc(0.5, 0.5, 8, 8);
    scrt::tracer::Tracer tb(b);
    tb.run(cfg, acc);

    CHECK(acc.total_power_w() == via_receiver);
    // Half the beam disk is shadowed: the plate collects about half the sun's power, not all.
    const double full = 1000.0 * scrt::math::PI * 0.25;
    CHECK(via_receiver == doctest::Approx(0.5 * full).epsilon(0.02));
    CHECK(via_receiver < 0.6 * full);
}
