# Wave 2 design — generalising the light source and the power model

Detailed design for Wave 2 of `v2_optical_simulator.md`, produced before implementation began.
Everything here is verified against the code as of `feat/optical-simulator` @ Wave 1 complete.

**The goal:** the tracer stops assuming a sun, so a laser is a first-class source — without moving
the four golden flux values.

---

## Facts established by measurement, not by reading

1. **No shipped scene is dispersive.** `grep sellmeier|alpha_spectrum` over `examples/`,
   `examples/panel_tests/`, `finalized_designs/scenes/` and `results/compact_study/scenes/`
   returns zero hits. `fresnel_lens_cooker.json` uses a constant `"n": 1.5`, so
   `Dielectric::n_at` short-circuits on `if (!sellmeier_) return n_;`. **Wavelength is inert in
   every corpus scene and every golden scene today** — which is good for stage safety and bad for
   confidence: the golden baseline *cannot* catch a wavelength regression, so wavelength needs a
   test of its own.
2. **All 94 shipped scenes carry an explicit `"aperture"` block.** The auto_fit default at
   `SceneDocument.cpp:321-325` fires for zero committed files.
3. **`polyscope::removeAllStructures()` runs before every `register_scene()`**, so an early-return
   in `register_aperture()` cannot leave a stale disk on screen.
4. **`record_paths` is false in every golden run**, and `path` is write-only inside `trace_one`.

---

## 1. The source abstraction

New `include/scrt/sources/LightSource.hpp`. `SunSource` survives in place with the same name and
file — `SunAngles`, `direction_from_angles`, `angles_from_direction` and both `below_horizon`
overloads are referenced by ~60 assertions in `test_sun.cpp` / `test_sun_validation.cpp` — and
gains a base class, an aperture and a wavelength.

```cpp
class LightSource {
public:
    virtual ~LightSource() = default;
    // non-copyable, non-movable, as SunSource is today

    /// Total optical power this source emits into the scene, in watts. The ONLY place a source
    /// expresses its strength; it never sees the primary ray count. A sun returns
    /// DNI * aperture_area * cos; a laser returns its authored watts. May be 0 (a set sun, a
    /// laser switched off), which the tracer must handle without dividing by zero.
    virtual double total_power_w() const = 0;

    /// Draw one primary ray: origin, unit direction and wavelength_nm all set.
    ///
    /// `ray.power` is a RELATIVE WEIGHT with mean 1.0 over this source's own sampling
    /// distribution, NOT watts. The tracer multiplies by total_power_w()/N. A source that samples
    /// its emission profile exactly returns exactly 1.0.
    ///
    /// THREAD-SAFETY (unchanged from SunSource): const, callable concurrently from every worker
    /// slot; the passed-in Rng is the only mutable state an implementation may touch.
    virtual core::Ray sample_ray(math::Rng& rng) const = 0;

    virtual std::string_view type_name() const = 0;   // "sun", "laser"

    /// Non-null only for solar sources. An explicit downcast hook rather than dynamic_cast: the
    /// solar-specific panel and the DNI-based reporting still need to find the sun, and RTTI
    /// would hide how much is still coupled.
    virtual const SunSource* as_sun() const { return nullptr; }
    virtual       SunSource* as_sun()       { return nullptr; }
};
```

**The source owns its aperture.** Leaving it on `Scene` and passing it in is exactly what blocks a
laser, and it misfiles a piece of *solar sampling apparatus* as a property of the world.
`SunSource::total_power_w()` becomes `dni_ * aperture_.area() * aperture_.cosine_to(to_sun())`.

`Pillbox`/`Buie` change to `sample_ray(math::Rng&)`, replace `ap.` with `aperture_.`, and add
`ray.wavelength_nm = wavelength_nm_;` before returning.

---

## 2. Power normalisation — `EmissionPlan`

New `include/scrt/tracer/EmissionPlan.hpp`. Built once per run, before the parallel region.

```cpp
struct Entry {
    const sources::LightSource* source;
    std::size_t first, count;     ///< Contiguous run of global ray indices.
    double      per_ray_w;        ///< total_power_w() / count [W].
};
```

Entries partition the **global ray index space** `[0, N)`, which is the same space
`Tracer::run` already derives `ray_start` from — so slot decomposition and per-slot seeding are
untouched. A slot may straddle two sources; that is fine.

Allocation: proportional to `total_power_w()`, truncating, remainder distributed by descending
fractional part with ties broken by source index (deterministic, no RNG). Sources emitting `<= 0 W`
get no rays and never reach a division. Every kept source gets at least one ray, so a 1 mW pilot
beam beside a 5 kW dish is still visible in the path render — *this minimum is an invention, not
derived from anything in the repo; drop it if a per-source ray budget lands in the GUI.*

In `Tracer::run`, replacing lines 120-126 / 241-247 and 195-197 / 320-322:

```cpp
const EmissionPlan plan = build_emission_plan(scene_->sources(), cfg.n_primary_rays);
if (plan.empty()) return {};          // same shape as the existing !receiver early-out
...
const EmissionPlan::Entry& e = plan.entry_for(ray_start + i);
core::Ray r = e.source->sample_ray(slot_rng);
r.power    *= e.per_ray_w;            // *=, not =
r.id        = static_cast<std::uint32_t>(ray_start + i);
```

**`*=` rather than `=` is bit-safe and load-bearing.** Both sunshapes return `power = 1.0`, and
`1.0 * x == x` exactly in IEEE754.

**Single-source bit-identity.** With one sun, `n_0 = N` exactly, so
`per_ray_w = (dni_ * aperture_.area() * aperture_.cosine_to(to_sun())) / N`. Today's expression is
`sun.dni() * ap.area() * cos_ap / N` with `cos_ap = ap.cosine_to(-sun.sun_direction())`; `to_sun()`
returns `-sun_direction_`. Same members, same left-to-right association, **same double** — assert
it with `==`, not `Approx`.

### The `finalize` attractive nuisance

`FluxAccumulator::finalize(std::size_t /*total_primary_rays*/)` ignores its argument because each
ray arrives pre-divided. That stays true and composes across sources. But it will look even more
like a bug to a reader who has just learned there are several sources, and "fixing" it would divide
every bin by 1e6 and move all four golden values by six orders of magnitude. Defuse it with a test
that deposits into two accumulators, finalizes with wildly different counts, and requires
**bit-identical** flux.

---

## 3. Multiple sources — a vector, now rather than later

`Scene` holds `std::vector<std::unique_ptr<sources::LightSource>> sources_`. Reasons in order:

- The document has to be a list anyway; a document list beside a single scene pointer forces
  `build_scene` to invent a "which one wins" policy, and that policy will be wrong.
- The hot loop gets touched once instead of twice, so one golden-flux gate instead of two.
- It removes a live latent crash: `Tracer.cpp:120` does `*scene_->sun()` with **no null check**,
  while `app/main.cpp:89`, `compare_main.cpp:70` and `test_compare.cpp:41` all defensively guard
  for a null sun. Three call sites believe it can be null; the one that would segfault does not.

`set_sun()`/`sun()`/`set_aperture()`/`aperture()` are **deleted, not kept as wrappers**. The rename
to `primary_sun()` is the compile tripwire that forces every one of the ~12 call sites to be looked
at rather than silently carried forward.

**Determinism caveat to note, not fix:** the number of RNG draws per ray varies by source type, so
a source that draws a different count shifts the sequence for rays after it in the same slot.
Already true across sunshapes; newly reachable by editing a source. Per-ray decorrelated RNG is a
separate, larger change.

---

## 4. Optional aperture — remove it from `Scene` entirely

Smaller and more honest than `std::optional<Aperture>`, which would be two copies of one fact.

| Concern | Resolution |
|---|---|
| `Scene::aperture_` by value | Deleted with its accessors. |
| Existing scene files | Untouched. `build_scene` resolves the aperture in the same place, and the last line becomes `sun->set_aperture(ap)`. |
| GUI aperture disk | `Scene::display_aperture()` returns the first source's aperture or nullptr; `register_aperture()` early-returns. Marked **presentation only — `Tracer` must not call this**. |
| `auto_fit`'s below-horizon throw | Unchanged, and now obviously correct: it is solar geometry, reachable only from the sun branch. A laser scene cannot reach it. |
| absent-aperture ⇒ auto_fit default | Moves inside sun parsing, where it belongs. Zero committed files affected. |

**Ordering constraint in `build_scene`:** a sun's aperture can only be resolved after elements and
the receiver are in the scene, because `auto_fit` unions `world_bounds()` — which does not include
apertures, so there is no circularity. Construct all sources into a local vector, resolve each
sun's aperture, then `add_source` in document order.

---

## 5. Wavelength — exactly 550.0, and prove it

The sun sets the literal `550.0`, identical to `Ray`'s own member initialiser, so the assignment
writes the same double that was already there.

**Why 550 and not the solar spectrum:** the engine is strictly monochromatic — one wavelength per
ray, no spectral weighting anywhere. 550 nm is a *convention* (photopic peak), not a derivation,
and the comment should say so. Having the sun *sample* a wavelength from AM1.5 would be actively
wrong: it turns a deterministic per-ray quantity into a random one, adds a fourth RNG draw to every
primary ray (shifting the sequence for every scene), and produces spectrally-weighted flux with no
spectral weighting in `FluxAccumulator`.

The writer must **not** emit a `wavelength_nm` key while it equals 550 — emitting it would change
every shipped file on re-save and break round-trip stability. Same "emit only when it means
something" policy `SceneWriter` already uses for `scale`, `visible` and the battery optionals.

**The gap this leaves, stated plainly:** no corpus scene is dispersive, so a wavelength bug cannot
be caught by the golden values — they would sit still while the model changed underneath. Close it
with `examples/dispersive_lens.json` (the Fresnel geometry with a `sellmeier: bk7` dielectric), a
fifth golden case, and a direct assertion that `sample_ray().wavelength_nm == 550.0` exactly, with
`Dielectric::n_at` named in the comment as the reason.

---

## 6. Document and schema

```json
"sources": [
  { "type": "sun", "direction": [0,0,-1], "dni_wm2": 1000.0,
    "sunshape": {"type": "pillbox", "half_angle_mrad": 4.65},
    "aperture": {"type":"disk","center":[0,0,2],"normal":[0,0,1],"radius":1.0,"mode":"fixed"} },
  { "type": "laser", "power_w": 5.0, "wavelength_nm": 632.8,
    "origin": [0,0,1.0], "direction": [0,0,-1],
    "beam_diameter_m": 0.002, "divergence_mrad": 1.0 }
]
```

`SunDoc` becomes `SunSourceDoc` (the rename is the tripwire), gains `ApertureDoc aperture` and
`double wavelength_nm{550.0}`; new `LaserSourceDoc`; `using SourceDoc = std::variant<...>`;
`SceneDocument::sources` replaces `sun` and `aperture`.

**Reading:** `"sources"` parses the array; a legacy `"sun"` (+ optional `"aperture"`) desugars into
one `SunSourceDoc`. **Both present is a hard error in strict AND lax mode** — picking a winner
silently drops an authored source, and this repo's history says silent-drop bugs cost more than a
loud one.

**Writing is legacy-preferring**, and this is the load-bearing decision of the section:

```cpp
if (doc.sources.size() == 1 && holds_alternative<SunSourceDoc>(doc.sources.front())) {
    s["sun"] = ...; s["aperture"] = ...;     // the shape all 94 shipped files have
} else {
    s["sources"] = ...;
}
```

It means **all 94 scenes re-save byte-identically, and the assistant's system prompt and
`test_assistant_schema.cpp` need no change at all** — otherwise scene generation breaks silently
for every user with an API key.

**Use the compile tripwire twice.** `source_to_json` in `SceneWriter.cpp` and a new `make_source`
in `SceneLoader.cpp`, both `std::visit` over `overloaded{...}` with one lambda per alternative and
**no catch-all**, so a third source type that reaches the document but not the loader is a compile
error. Extract `overloaded` to a shared `include/scrt/io/Overloaded.hpp` rather than copying it.

Files: `SceneDocument.hpp/.cpp`, `SceneWriter.cpp`, `SceneLoader.cpp`, new `Overloaded.hpp`,
`test_scene_document.cpp`, `test_scene_io.cpp`. **Not** `AiOverlay.cpp`, **not**
`test_assistant_schema.cpp` — by design.

---

## Staged order (each stage ends green, goldens unchanged)

- **Stage 0 — measure.** Capture the 94-scene `scrt_compare` CSV at HEAD to the scratchpad. Re-run
  and diff every column but `wall_time_s` at each stage gate. Any non-empty diff is a bug, not a
  judgement call. Keep the CSVs out of the repo — a build-time instrument, not a committed baseline.
- **Stage 1 — `LightSource` base; the aperture moves into the sun.** `Scene` still holds one
  `unique_ptr<SunSource>`. Zero behaviour change intended.
- **Stage 2 — `Scene` holds a vector; `EmissionPlan`.** The `sun()` → `primary_sun()` rename must
  land here or the build breaks; it is not deferrable.
- **Stage 3 — `Laser`.** Purely additive.
- **Stage 4 — document and schema.** Extra gate: re-save all 94 scenes and require an empty
  `git diff`.
- **Stage 5 — reporting decoupling.** `concentration_ratio(dni)` is meaningless for a laser; omit
  the field when there is no sun rather than printing a ratio against a fictitious 1000 W/m².
  Killing `FluxPlotter`'s global mutable `g_scene_dni_wm2` belongs to a later GUI pass.

---

## Risks — where confidence is lowest

1. **`Aperture::cosine_to` and `auto_fit` call `glm::normalize` on a vector the caller may already
   have normalised.** `Aperture.hpp:18-29` warns at length that re-normalising a unit vector is not
   the identity in floating point, and that this is inert today only because every shipped scene
   uses exactly `(0,0,-1)`. Moving the aperture into the sun changes *who holds* the vector and must
   not change *how many times it is normalised*. Check this specifically in review.
2. **`SunSource.hpp` including `scene/Aperture.hpp`** makes the layering sources→scene→sources by
   header. No cycle exists (`Aperture.hpp` pulls only `core/AABB.hpp` and `math/Vec.hpp`) and it
   compiles, but whether it is the shape wanted long-term is a judgement call.
3. **`Scene::display_aperture()` is a discipline, not a guarantee.** Nothing structurally stops the
   tracer reading it again, and CLAUDE.md's own lesson prefers structural impossibility. The
   alternative — splitting `Scene` into a physics view and a presentation view — is a bigger change
   than this wave.
4. **`EmissionPlan::entry_for` is a linear scan in the hot loop.** One compare for every scene that
   exists today; measurable only for a hypothetical 50-source scene at 10M rays.

---

## As built (Wave 2 complete, commits ee58456..9dd5afd)

Everything above was followed as written, with these deviations and additions, each measured:

- **Stage 0 also bumped `scrt_compare`'s CSV precision** from 6 to 17 significant digits. At six,
  the corpus diff could not see a change in the seventh digit, a thousand times coarser than the
  1e-9 golden gate it backs up. Two consecutive baseline captures were identical before any
  engine change.
- **`SunSource::set_wavelength_nm` exists**, and the legacy `"sun"` block accepts `wavelength_nm`
  too. Otherwise a single sun at a non-550 wavelength would have forced the writer out of the
  legacy shape merely to record a wavelength. The key is still omitted at exactly 550.
- **Laser `divergence_mrad` is the FULL angle**, as datasheets quote it; the sampler uses half of
  it. The far field is a top hat (uniform over the cap), not a Gaussian.
- **The null-sun guard in `Tracer::run` landed in Stage 1**, not Stage 2, because the unguarded
  dereference segfaulted and three callers already checked.
- **`EmissionPlan` handles the single-source case in its own branch** so the proportional
  arithmetic (a sum, a division by it, a multiplication by N) never touches the case every golden
  value rests on. `tests/test_emission_plan.cpp` pins per_ray_w == total/N with `==`, and pins
  that a dead second sun leaves parabolic_dish.json's golden bit-identical.
- **Stage 4's "byte-identical re-save" gate was measured on the writer's output**, not on the
  shipped files: write_document(parse_document(f)) for all 94 scenes, dumped before and after,
  diffed. The shipped files themselves gain azimuth/elevation and aperture mode/margin keys on
  any re-save, as they did before this wave; that is the writer's existing cheap-and-lossless
  policy, not a Wave 2 change.
- **Stage 5 went slightly past "reporting"**: the outliner lists sources by type and hides the
  Aperture row when there is no disk, the sun panel says when there is no sun, and the flux
  headline prints milliwatts below one watt. `FluxPlotter::g_scene_dni_wm2` survives, told by
  the Viewer on register_scene() whether a sun exists; killing it is still the later GUI pass.
- **Two shipped scenes were added**: `examples/laser_bench.json` (the only file spelt with
  `"sources"`) and `examples/dispersive_lens.json`, the fifth golden at 38.763765222589399 W.
- **Risk 1 (normalisation count) checked in review**: the aperture struct moved whole, and
  `cosine_to`/`tangent_frame` are called the same number of times on the same fields.
  Risk 4 (linear `entry_for`) unmeasured and unchanged: one compare per ray for every shipped scene.

### Independent cold audit (Opus, read-only, after Stage 5)

Asked to read commits b008f5a, e2d3f9d, 0e113a8 and de02c71 without the implementer's reasoning.
Found no path by which a single-sun scene's per-ray power or RNG sequence could differ, no
EmissionPlan invariant violation (counts sum to N, no zero-count entry, seams correct, the
donor loop's break is unreachable by pigeonhole), no thread-safety issue, and no legacy file
that parses or writes differently. Six lesser findings, three fixed in the follow-up commit:

- The laser cone was the small-angle form (theta = half*sqrt(xi)) while accepting any angle;
  7000 mrad sent rays behind the emitter, silently. Now exactly uniform over the spherical cap
  (cos theta uniform on [cos half, 1], same two draws) and bounded at a 180-degree full angle,
  in the setter and in the parser. Pinned by a mean-cosine test at a 120-degree full angle.
- A scene whose every source emits 0 W (a sun that has set) returned an empty result without
  finalizing the receiver. It now finalizes and reports the wall time; rays traced stays 0,
  where the old tracer reported N rays of nothing.
- `Scene::add_source(nullptr)` reached an unchecked dereference in `primary_sun()`; now refused.

Accepted as-is: the min-one-ray rule's ray is the LAST global index, so it deposits flux but is
not usually in the recorded paths (comment corrected, rule kept); lax mode is slightly stricter
(an unknown source type or a non-positive wavelength throws on load, where an unknown key was
ignored - nothing shipped is affected); `total_w` overflow needs two sources near 1e308 W.
