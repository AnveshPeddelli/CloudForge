#pragma once
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace cloudforge::app {
struct Options {
    std::filesystem::path input;
    double voxel{0.1};
    double playback_fps{10.0};
    std::size_t frames{30};
    std::size_t repeats{5};
    std::size_t warmups{3};
    std::size_t smoke_frames{};
    std::string backend{"cpu"};
    std::filesystem::path csv;
    std::filesystem::path screenshot;
    bool detail{};
    bool compare{};
    std::filesystem::path browser_smoke;
    std::filesystem::path benchmark_smoke;
    std::filesystem::path results_csv;
    bool results_samples{};
    std::filesystem::path capture_dir;
};
inline double positive_number(const std::string& text) {
    std::size_t used{};
    const double value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value) || value <= 0)
        throw std::invalid_argument("Expected a finite positive number: " + text);
    return value;
}
inline std::size_t positive_count(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Expected a positive integer: " + text);
    const auto value = std::stoull(text);
    if (value == 0 || value > 1000000) throw std::invalid_argument("Count must be in [1, 1000000]");
    return static_cast<std::size_t>(value);
}
inline Options parse(int argc, char** argv) {
    if (argc < 2) throw std::invalid_argument("Supply a PCD file or directory; see README for options");
    Options o;
    o.input = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string key = argv[i];
        if (i + 1 == argc) throw std::invalid_argument("Missing value for " + key);
        const std::string value = argv[++i];
        if (key == "--voxel") o.voxel = positive_number(value);
        else if (key == "--fps") {
            o.playback_fps = positive_number(value);
            if (o.playback_fps < 0.1 || o.playback_fps > 240)
                throw std::invalid_argument("Playback FPS must be in [0.1, 240]");
        }
        else if (key == "--frames") o.frames = positive_count(value);
        else if (key == "--repeat") o.repeats = positive_count(value);
        else if (key == "--warmup") o.warmups = positive_count(value);
        else if (key == "--smoke-frames") o.smoke_frames = positive_count(value);
        else if (key == "--backend") o.backend = value;
        else if (key == "--csv") o.csv = value;
        else if (key == "--screenshot") o.screenshot = value;
        else if (key == "--browser-smoke") o.browser_smoke = value;
        else if (key == "--benchmark-smoke") o.benchmark_smoke = value;
        else if (key == "--results-csv") o.results_csv = value;
        else if (key == "--capture-dir") o.capture_dir = value;
        else if (key == "--results-view") {
            if(value!="graphs"&&value!="samples")throw std::invalid_argument("Use graphs or samples for --results-view");
            o.results_samples=value=="samples";
        }
        else if (key == "--detail" || key == "--compare") {
            if (value != "on" && value != "off") throw std::invalid_argument("Use on or off for " + key);
            if (key == "--detail") o.detail = value == "on";
            else o.compare = value == "on";
        }
        else throw std::invalid_argument("Unknown option: " + key);
    }
    if (o.backend != "cpu" && o.backend != "cuda") throw std::invalid_argument("Backend must be cpu or cuda");
    return o;
}
}
