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
)

include(CTest)
add_test(NAME scrt_tests COMMAND scrt_tests)

# scrt_viz and scrt_app are added in Phase 6.
