#include <doctest/doctest.h>
#include "scrt/io/ResultsExporter.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>

using json = nlohmann::json;

TEST_CASE("Reporting: the summary JSON omits concentration_ratio when there is no sun") {
    // The ratio is peak flux over the sun's DNI. Without a sun there is nothing to divide by,
    // and a value computed against an assumed 1000 W/m2 is a fabricated figure in a file.
    scrt::tracer::FluxAccumulator acc(0.5, 0.5, 4, 4);
    acc.finalize(1);
    scrt::tracer::TraceResult res;
    const auto out = std::filesystem::temp_directory_path() / "scrt_summary_no_sun.json";

    scrt::io::export_summary_json(acc, res, std::nullopt, out);
    {
        std::ifstream in(out);
        const json j = json::parse(in);
        CHECK(j.contains("peak_flux_wm2"));
        CHECK_FALSE(j.contains("concentration_ratio"));
    }
    scrt::io::export_summary_json(acc, res, 1000.0, out);
    {
        std::ifstream in(out);
        const json j = json::parse(in);
        CHECK(j.contains("concentration_ratio"));
    }
}
