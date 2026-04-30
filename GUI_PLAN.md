# GUI Plan: Live Transform Editing + Scene Browser

## Features
1. Translation/rotation sliders per surface that immediately update 3D geometry and mark trace stale
2. Scene browser listbox — load any example scene without restarting the app

## Files changed
| File | Change |
|------|--------|
| `include/scrt/scene/Scene.hpp` | Add `mutable_surfaces()` declaration |
| `src/scene/Scene.cpp` | Implement `mutable_surfaces()` |
| `include/scrt/viz/Viewer.hpp` | New members: `owned_scene_`, `surf_xforms_`, `available_scenes_`, `need_rebuild_`; new methods |
| `src/viz/Viewer.cpp` | New methods: scene browser, transform editor, BVH-rebuild gate in run_trace |
| `app/main.cpp` | Add `--examples-dir` flag; call `viewer.set_examples_dir()` |

## Key design points
- `SurfXformState { base, trans[3], rot_deg[3] }` — delta stored separately from load-time base transform
- `apply_surf_xform(idx)`: delta = T·R, new = delta.compose(base); re-tessellates + re-registers Polyscope mesh by name
- `run_trace()`: if `need_rebuild_`, calls `scene_->build_acceleration_structure()` first
- `load_scene_internal(ls)`: full reload path (removeAllStructures → set_scene → init_surf_xforms → register_scene → preview trace)
- `scene_display_names_` stores `std::string` copies of filenames to keep stable `c_str()` pointers for ImGui ListBox

## Verification
1. `cmake --build --preset debug` — no new warnings
2. `ctest --preset debug` — all tests still pass (no test code touched)
3. Manual: drag translate slider → mesh moves, orange banner → Full Trace → flux map on moved geometry
4. Manual: click scene in browser → Load → geometry switches, preview trace runs
