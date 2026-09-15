// A recorded ray path is a TREE, because a splitting surface sends one ray two ways at once.
//
// It used to be a flat polyline shared by both branches of a split, which the renderer drew as
// consecutive segments — so the picture ran out along the reflected branch and then teleported
// back to the split point to continue transmitted. Cosmetic while the only splitters were an
// incidental dielectric; central once beam splitters are a component you place on purpose.
//
// The second thing pinned here is the bounce budget. The reflected branch used to be recursed
// with a FRESH max_bounces, so total path length was bounded only by the power cutoff.

#include "scrt/io/SceneLoader.hpp"
#include "scrt/tracer/Tracer.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

namespace {

/// Traces a scene with path recording on and returns the recorded paths.
std::vector<scrt::tracer::RayPath> trace_paths(const std::filesystem::path& scene_file,
                                               std::size_t rays, int max_bounces) {
    auto loaded = scrt::io::load_scene(scene_file);
    REQUIRE(loaded.scene != nullptr);
    loaded.scene->build_acceleration_structure();

    scrt::tracer::TraceConfig cfg = loaded.cfg;
    cfg.n_primary_rays      = rays;
    cfg.max_bounces         = max_bounces;
    cfg.record_paths        = true;
    cfg.max_paths_to_record = 400;
    cfg.rng_seed            = 7;
    cfg.num_threads         = 1;   // deterministic recording; which rays get recorded is
                                   // otherwise a race between worker slots.

    scrt::tracer::Tracer tr(*loaded.scene);
    return tr.run(cfg).sampled_paths;
}

std::filesystem::path example(const char* name) {
    return std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / name;
}

/// Writes a scene built specifically to record a BRANCH, and returns its path.
///
/// No shipped scene produces one. fresnel_lens_cooker.json splits ~20000 times per trace, but
/// every reflected branch leaves the lens heading back at the sky and hits nothing, so it adds
/// no node and the recorded tree stays a straight line. A branch needs the reflected ray to land
/// on something, which this arranges: a 45-degree splitter sends it sideways into a catcher.
std::filesystem::path write_branching_scene() {
    const auto dir = std::filesystem::temp_directory_path() / "scrt_split_paths";
    std::filesystem::create_directories(dir);
    const auto path = dir / "branching.json";

    static const char* kJson = R"JSON({
  "scene": {
    "name": "Splitter with a catcher for the reflected branch",
    "sun": {
      "direction": [0.0, 0.0, -1.0],
      "dni_wm2": 1000.0,
      "sunshape": {"type": "pillbox", "half_angle_mrad": 4.65}
    },
    "aperture": {"type": "disk", "center": [0.0, 0.0, 2.0],
                 "normal": [0.0, 0.0, 1.0], "radius": 0.25, "mode": "fixed"},
    "materials": [
      {"id": "pane", "type": "thin_dielectric_pane", "n": 1.5, "thickness_m": 0.005},
      {"id": "black", "type": "absorber"}
    ],
    "elements": [
      {"name": "splitter", "material": "pane",
       "surface": {"type": "plane", "half_width": 0.3, "half_height": 0.3},
       "transform": {"rotation_euler_deg": [0.0, 45.0, 0.0], "translation": [0.0, 0.0, 1.0]}},
      {"name": "catcher", "material": "black",
       "surface": {"type": "plane", "half_width": 0.5, "half_height": 0.5},
       "transform": {"rotation_euler_deg": [0.0, 90.0, 0.0], "translation": [1.2, 0.0, 1.0]}}
    ],
    "receiver": {
      "surface": {"type": "plane", "half_width": 0.6, "half_height": 0.6},
      "grid": {"nx": 16, "ny": 16},
      "transform": {"translation": [0.0, 0.0, 0.0]}
    }
  },
  "trace": {"n_primary_rays": 4000, "max_bounces": 6, "record_paths": false,
            "max_paths_to_record": 200, "rng_seed": 3}
})JSON";

    std::ofstream out(path, std::ios::trunc);
    out << kJson;
    out.close();
    return path;
}

} // namespace

TEST_CASE("Recorded paths are trees rooted at the emission point") {
    // The Fresnel lens scene is the one shipped scene whose dielectric splits, so it is the only
    // one where the distinction between a tree and a polyline is observable at all.
    const auto paths = trace_paths(example("fresnel_lens_cooker.json"), 4000, 8);
    REQUIRE_FALSE(paths.empty());

    for (const auto& p : paths) {
        REQUIRE_FALSE(p.nodes.empty());
        CHECK(p.edges.size() == p.edge_power_w.size());

        // Every node except the root has exactly one incoming edge. That is what makes it a
        // tree, and it is exactly what the old shared flat vector could not express.
        std::vector<int> in_degree(p.nodes.size(), 0);
        for (const auto& e : p.edges) {
            REQUIRE(e[0] < p.nodes.size());
            REQUIRE(e[1] < p.nodes.size());
            CHECK(e[1] != 0u);                 // nothing points back at the emission point
            ++in_degree[e[1]];
        }
        CHECK(in_degree[0] == 0);
        for (std::size_t i = 1; i < in_degree.size(); ++i)
            CHECK(in_degree[i] == 1);
    }
}

TEST_CASE("A split records as a branch, not as a longer line") {
    const auto paths = trace_paths(write_branching_scene(), 4000, 6);
    REQUIRE_FALSE(paths.empty());

    // At least one recorded path must have a node with two children. Without a real branch this
    // whole change would be untested by the case above, which a straight line also satisfies.
    bool saw_branch = false;
    for (const auto& p : paths) {
        std::vector<int> out_degree(p.nodes.size(), 0);
        for (const auto& e : p.edges) ++out_degree[e[0]];
        for (int d : out_degree)
            if (d >= 2) { saw_branch = true; break; }
        if (saw_branch) break;
    }
    CHECK(saw_branch);
}

TEST_CASE("Edge power never increases along a path") {
    // Not an energy-conservation proof — a split's two branches are recorded on separate edges,
    // so their sum is what conserves. This is the weaker, always-true statement: no single edge
    // carries more than the one that fed it.
    const auto paths = trace_paths(example("fresnel_lens_cooker.json"), 2000, 8);
    REQUIRE_FALSE(paths.empty());

    for (const auto& p : paths) {
        std::vector<double> power_into(p.nodes.size(), 0.0);
        for (std::size_t i = 0; i < p.edges.size(); ++i)
            power_into[p.edges[i][1]] = p.edge_power_w[i];

        for (std::size_t i = 0; i < p.edges.size(); ++i) {
            const std::uint32_t from = p.edges[i][0];
            if (from == 0u) continue;                        // the source edge has no predecessor
            CHECK(p.edge_power_w[i] <= power_into[from] * (1.0 + 1e-12));
        }
    }
}

TEST_CASE("A split branch inherits the remaining bounce budget, not a fresh one") {
    // With a tight budget, no recorded path may be deeper than it - including down a branch.
    // Against the old code the reflected branch restarted at max_bounces, so a two-bounce limit
    // could record a path four or more deep.
    constexpr int kMaxBounces = 2;
    const auto paths = trace_paths(write_branching_scene(), 4000, kMaxBounces);
    REQUIRE_FALSE(paths.empty());

    for (const auto& p : paths) {
        std::vector<int> depth(p.nodes.size(), 0);
        // Edges are appended parent-before-child within a branch, and a branch's parent already
        // has its depth by the time the branch is recorded, so one forward pass suffices.
        for (const auto& e : p.edges)
            depth[e[1]] = depth[e[0]] + 1;
        for (int d : depth)
            CHECK(d <= kMaxBounces);
    }
}
