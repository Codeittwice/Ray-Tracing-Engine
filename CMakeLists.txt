cmake_minimum_required(VERSION 3.25)
project(solar_cooker_rt CXX)

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
# Polyscope, ImGui, ImPlot, Assimp added in later phases.

# ---------- scrt_core (physics core, no GUI) ----------
add_library(scrt_core STATIC)
target_include_directories(scrt_core PUBLIC include)
target_link_libraries(scrt_core PUBLIC
    glm::glm nlohmann_json::nlohmann_json fmt::fmt spdlog::spdlog)
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
)

# ---------- Phase 4 sources ----------
target_sources(scrt_core PRIVATE
    src/surfaces/GeneralQuadric.cpp
    src/surfaces/ImplicitSDF.cpp
    src/io/SceneLoader.cpp
    src/io/ResultsExporter.cpp
)

# ---------- Phase 2 sources ----------
target_sources(scrt_core PRIVATE
    src/surfaces/Plane.cpp
    src/surfaces/Sphere.cpp
    src/surfaces/Paraboloid.cpp
    src/materials/PerfectMirror.cpp
    src/materials/Absorber.cpp
    src/sources/Pillbox.cpp
    src/scene/Aperture.cpp
    src/scene/Receiver.cpp
    src/scene/Scene.cpp
    src/tracer/FluxAccumulator.cpp
    src/tracer/Tracer.cpp
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
    tests/test_sun.cpp
    tests/test_paraxial.cpp
    tests/test_scene_io.cpp
)

target_compile_definitions(scrt_tests PRIVATE
    SCRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(CTest)
add_test(NAME scrt_tests COMMAND scrt_tests)

# scrt_viz and scrt_app are added in Phase 6.

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
