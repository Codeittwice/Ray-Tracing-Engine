# Project: Solar Cooker Ray Tracer (C++ / CMake / vcpkg)

## Source of truth
Follow `SolarCookerRayTracer_Plan.md` exactly. That document specifies:
- Repository layout (Section 1)
- Dependencies and vcpkg manifest (Section 2)
- Build configuration (Section 3)
- Coding conventions (Section 4) — obey strictly
- Module specifications (Section 5)
- Validation tests T1-T12 (Section 9)
- Phased build order (Section 10)

## Rules
- Execute phases in order. Do not skip ahead.
- After each phase, run the acceptance tests before proposing the next phase.
- All geometry and physics in `double` precision (`glm::dvec3`, no custom vector types).
- SI units throughout (meters, watts, radians; nanometers only for wavelengths in I/O).
- No `shared_ptr` unless truly required.
- Every public class/function gets a one-line Doxygen comment.
- If unsure about a design decision, ask the user before coding.

## Current phase
Phase 7 — Complete. All phases done.

## Platform
Windows 10/11, MSVC (VS 2025, v18) x64, CMake 4.x, Ninja generator, vcpkg at C:\dev\vcpkg in manifest mode. CMakePresets.json encodes MSVC/SDK paths (INCLUDE, LIB, PATH, compiler) so cmake --preset debug works from any shell without needing a Developer Command Prompt.

## Working style
- Plan Mode is ON. Propose plans before multi-file changes.
- Show diffs before writing large new files.
- After meaningful changes, run `cmake --build build/debug` and `ctest --test-dir build/debug --output-on-failure`. Report results.
- Commit in logical units with descriptive messages.
