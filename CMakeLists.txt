cmake_minimum_required(VERSION 3.25)
project(solar_cooker_rt C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/CompilerWarnings.cmake)

# Find vcpkg-installed dependencies
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)

# ---------- Phase 6: Polyscope (FetchContent — not in vcpkg registry) ----------
include(FetchContent)

FetchContent_Declare(polyscope
    GIT_REPOSITORY https://github.com/nmwsharp/polyscope.git
    GIT_TAG        v2.3.0
    GIT_SHALLOW    TRUE
)
# GLM_ENABLE_EXPERIMENTAL required because polyscope uses glm/gtx/dual_quaternion
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)
FetchContent_MakeAvailable(polyscope)
# vcpkg GLM 1.0.x removed quaternion op/ that polyscope's dual_quaternion uses.
# Prepend polyscope's own bundled (older) GLM so its source files see the right API.
target_include_directories(polyscope BEFORE PUBLIC "${polyscope_SOURCE_DIR}/deps/glm")

FetchContent_Declare(implot_fetch
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.17
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(implot_fetch)

# Build ImPlot against polyscope's bundled ImGui (same translation unit = one context)
add_library(implot_lib STATIC
    ${implot_fetch_SOURCE_DIR}/implot.cpp
    ${implot_fetch_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(implot_lib PUBLIC ${implot_fetch_SOURCE_DIR})
target_link_libraries(implot_lib PRIVATE polyscope)

FetchContent_Declare(imguizmo_fetch
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        1.83
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imguizmo_fetch)

# Build ImGuizmo against polyscope's bundled ImGui, same as implot_lib above,
# so there is exactly one ImGui context in the process.
add_library(imguizmo_lib STATIC
    ${imguizmo_fetch_SOURCE_DIR}/ImGuizmo.cpp
)
target_include_directories(imguizmo_lib PUBLIC ${imguizmo_fetch_SOURCE_DIR})
target_link_libraries(imguizmo_lib PRIVATE polyscope)
# ImGuizmo uses ImVec2/ImVec4 arithmetic, which imgui_internal.h gates behind this macro.
# PUBLIC so every consumer that includes ImGuizmo.h defines it before imgui.h too.
target_compile_definitions(imguizmo_lib PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# ---------- scrt_core (physics core, no GUI) ----------
add_library(scrt_core STATIC)
target_include_directories(scrt_core PUBLIC include)
target_link_libraries(scrt_core PUBLIC
    glm::glm nlohmann_json::nlohmann_json fmt::fmt spdlog::spdlog assimp::assimp)
target_compile_features(scrt_core PUBLIC cxx_std_20)
set_project_warnings(scrt_core)

# ---------- Phase 1 sources ----------
target_sources(scrt_core PRIVATE
    src/math/Rng.cpp
    src/core/AABB.cpp
    src/core/Transform.cpp
    src/optics/Refract.cpp
    src/optics/Fresnel.cpp
)

# ---------- Phase 3 sources ----------
target_sources(scrt_core PRIVATE
    src/optics/Paraxial.cpp
    src/materials/Dielectric.cpp
    src/materials/RealMirror.cpp
    src/materials/ThinDielectricPane.cpp
    src/materials/BeamSplitter.cpp
    src/materials/Diffuser.cpp
)

# ---------- Phase 4 sources ----------
target_sources(scrt_core PRIVATE
    src/surfaces/GeneralQuadric.cpp
    src/surfaces/ImplicitSDF.cpp
    src/io/SceneLoader.cpp
    src/io/SceneDocument.cpp
    src/io/SceneWriter.cpp
    src/io/ScenePaths.cpp
    src/io/ResultsExporter.cpp
)

# ---------- Phase 5 sources ----------
target_sources(scrt_core PRIVATE
    src/accel/BVH.cpp
    src/io/MeshImporter.cpp
    src/surfaces/TriangleMesh.cpp
)

# ---------- Phase 2 sources ----------
target_sources(scrt_core PRIVATE
    src/surfaces/Plane.cpp
    src/surfaces/Disk.cpp
    src/surfaces/SlitPlate.cpp
    src/surfaces/ThickLens.cpp
    src/surfaces/Sphere.cpp
    src/surfaces/Paraboloid.cpp
    src/materials/PerfectMirror.cpp
    src/materials/Absorber.cpp
    src/sources/Pillbox.cpp
    src/scene/Aperture.cpp
    src/scene/Receiver.cpp
    src/scene/Scene.cpp
    src/scene/SceneEditor.cpp
    src/tracer/FluxAccumulator.cpp
    src/tracer/Tracer.cpp
    src/tracer/EmissionPlan.cpp
)

# ---------- Phase 7 sources ----------
target_sources(scrt_core PRIVATE
    src/sources/Buie.cpp
    src/sources/SunSource.cpp
    src/sources/Laser.cpp
)

# ---------- Item 1: CylindricalParaboloid ----------
target_sources(scrt_core PRIVATE
    src/surfaces/CylindricalParaboloid.cpp
)

# ---------- Item 3: FresnelZoneLens ----------
target_sources(scrt_core PRIVATE
    src/surfaces/FresnelZoneLens.cpp
)

# ---------- scrt_tests ----------
enable_testing()
add_executable(scrt_tests)
target_link_libraries(scrt_tests PRIVATE scrt_core doctest::doctest)
set_project_warnings(scrt_tests)

target_sources(scrt_tests PRIVATE
    tests/test_main.cpp
    tests/test_vec.cpp
    tests/test_reflect_refract.cpp
    tests/test_fresnel.cpp
    tests/test_intersections.cpp
    tests/test_light_source.cpp
    tests/test_emission_plan.cpp
    tests/test_laser.cpp
    tests/test_sources_schema.cpp
    tests/test_reporting.cpp
    tests/test_scene_editor_library.cpp
    tests/test_apertures.cpp
    tests/test_tracer_overloads.cpp
    tests/test_beam_splitter.cpp
    tests/test_thick_lens.cpp
    tests/test_diffuser.cpp
    tests/test_library.cpp
    # The library and the config path it uses are GUI-free (json and filesystem only),
    # so the test binary compiles them directly rather than linking scrt_viz, which
    # would drag in Polyscope, GLFW and a window.
    src/viz/Library.cpp
    src/viz/AppConfig.cpp
    tests/test_sun.cpp
    tests/test_paraxial.cpp
    tests/test_scene_io.cpp
    tests/test_dielectric_spectrum.cpp
    tests/test_fresnel_zone_lens.cpp
    tests/test_compare.cpp
    tests/test_regression_corpus.cpp
    tests/test_scale.cpp
    tests/test_scene_document.cpp
    tests/test_scene_editor.cpp
    tests/test_mesh_import.cpp
    tests/test_sun_validation.cpp
    tests/test_assistant_schema.cpp
    tests/test_split_paths.cpp
)

target_compile_definitions(scrt_tests PRIVATE
    SCRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(CTest)
add_test(NAME scrt_tests COMMAND scrt_tests)

# ---------- scrt_compare (headless multi-scene comparison, no GUI) ----------
add_executable(scrt_compare app/compare_main.cpp)
target_link_libraries(scrt_compare PRIVATE scrt_core)
set_project_warnings(scrt_compare)

# ---------- scrt_viz (visualization, depends on Polyscope + ImPlot) ----------
add_library(scrt_viz STATIC
    src/viz/Viewer.cpp
    src/viz/Theme.cpp
    src/viz/ViewSettings.cpp
    src/viz/Fonts.cpp
    src/viz/FileDialog.cpp
    src/viz/AppConfig.cpp
    src/viz/ClaudeClient.cpp
    src/viz/IconFontData.cpp
    src/viz/RayRenderer.cpp
    src/viz/FluxPlotter.cpp
    src/viz/panels/OutlinerPanel.cpp
    src/viz/panels/SceneBrowserPanel.cpp
    src/viz/panels/TransformPanel.cpp
    src/viz/panels/MaterialsPanel.cpp
    src/viz/panels/LibraryPanel.cpp
    src/viz/Library.cpp
    src/viz/panels/SunPanel.cpp
    src/viz/panels/TracePanel.cpp
    src/viz/panels/ImportPanel.cpp
    src/viz/panels/SavePanel.cpp
    src/viz/panels/SettingsPanel.cpp
    src/viz/panels/SelectionPanel.cpp
    src/viz/panels/IoOverlay.cpp
    src/viz/panels/AiOverlay.cpp
    src/viz/panels/TopBarPanel.cpp
)
# glfw is PRIVATE on the polyscope target, so link it here too: Viewer::run() needs
# glfwMaximizeWindow, and Polyscope exposes no accessor for its own window handle.
target_link_libraries(scrt_viz PUBLIC scrt_core polyscope implot_lib imguizmo_lib nfd::nfd glfw)
# dwmapi: the window's own title bar is the shell's to paint, and DwmSetWindowAttribute is the
# only way to ask for a dark one under a dark interface.
if(WIN32)
    # dwmapi: the dark title bar. winhttp: the scene assistant's HTTPS client - it ships with
    # Windows, so the assistant costs the build no new third-party dependency.
    target_link_libraries(scrt_viz PUBLIC dwmapi winhttp)
endif()
set_project_warnings(scrt_viz)

# ---------- scrt_app (desktop entry point) ----------
add_executable(scrt_app app/main.cpp)
target_link_libraries(scrt_app PRIVATE scrt_viz)
set_project_warnings(scrt_app)

# ---------- Documentation (Doxygen → HTML + LaTeX/PDF) ----------
find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation (HTML + LaTeX)"
        VERBATIM
    )
    message(STATUS "Doxygen found: 'cmake --build build/debug --target docs' generates docs.")
    message(STATUS "  PDF: run 'make' inside docs/generated/latex/ after building docs target.")
else()
    message(STATUS "Doxygen not found; 'docs' target unavailable.")
endif()
