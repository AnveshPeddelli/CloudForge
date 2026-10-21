#include "cloudforge/engine.hpp"
#ifdef CLOUDFORGE_HAS_CUDA
#include "cloudforge/voxel_grid_cuda.hpp"
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace cloudforge {
bool cuda_backend_compiled() {
#ifdef CLOUDFORGE_HAS_CUDA
    return true;
#else
    return false;
#endif
}
PointCloud process(const PointCloud& input, double size, Backend backend) {
    if (backend == Backend::cpu) return downsample(input, size);
#ifdef CLOUDFORGE_HAS_CUDA
    return downsample_cuda(input, size);
#else
    throw std::runtime_error("CUDA backend was not compiled; use CPU or enable CLOUDFORGE_ENABLE_CUDA");
#endif
}
namespace {
bool timestamp(const std::filesystem::path& path, long double& value) {
    const auto stem = path.stem().string();
    // Restrict names to nonnegative decimal timestamps (no NaN, exponents, etc.).
    if (stem.empty() || stem.find_first_not_of("0123456789.") != std::string::npos) return false;
    try {
        std::size_t used{};
        value = std::stold(stem, &used);
        return used == stem.size() && std::isfinite(value);
    } catch (const std::exception&) { return false; }
}
}
std::vector<std::filesystem::path> list_frames(const std::filesystem::path& input) {
    if (std::filesystem::is_regular_file(input)) {
        if (input.extension() != ".pcd") throw std::invalid_argument("Expected a .pcd file");
        return {input};
    }
    if (!std::filesystem::is_directory(input)) throw std::runtime_error("Input does not exist: " + input.string());
    std::vector<std::filesystem::path> frames;
    for (const auto& entry : std::filesystem::directory_iterator(input))
        if (entry.is_regular_file() && entry.path().extension() == ".pcd") frames.push_back(entry.path());
    std::sort(frames.begin(), frames.end(), [](const auto& a, const auto& b) {
        long double ta{}, tb{};
        const bool na = timestamp(a, ta), nb = timestamp(b, tb);
        if (na != nb) return na;
        if (na && ta != tb) return ta < tb;
        return a.filename().string() < b.filename().string();
    });
    if (frames.empty()) throw std::runtime_error("No .pcd frames in input directory");
    return frames;
}
Measurement measure(const PointCloud& input, double size, Backend backend) {
    const auto start = std::chrono::steady_clock::now();
    auto points = process(input, size, backend);
    // CUDA returns host data through a synchronous device-to-host copy.
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return {std::move(points), ms};
}
Comparison compare(const PointCloud& cpu, const PointCloud& gpu, double atol, double rtol) {
    if (!std::isfinite(atol) || !std::isfinite(rtol) || atol < 0 || rtol < 0)
        throw std::invalid_argument("Comparison tolerances must be finite and nonnegative");
    Comparison result{cpu.size() == gpu.size(), 0, 0};
    for (std::size_t i = 0; i < std::min(cpu.size(), gpu.size()); ++i) {
        const std::array<double, 3> a{cpu[i].x, cpu[i].y, cpu[i].z};
        const std::array<double, 3> b{gpu[i].x, gpu[i].y, gpu[i].z};
        bool matches = true;
        for (std::size_t j = 0; j < 3; ++j) {
            if (!std::isfinite(a[j]) || !std::isfinite(b[j])) {
                matches = false; result.max_error = std::numeric_limits<double>::infinity(); continue;
            }
            const double error = std::abs(a[j] - b[j]);
            result.max_error = std::max(result.max_error, error);
            if (error > atol + rtol * std::max(std::abs(a[j]), std::abs(b[j]))) matches = false;
        }
        if (!matches) { ++result.mismatches; result.passed = false; }
    }
    result.mismatches += std::max(cpu.size(), gpu.size()) - std::min(cpu.size(), gpu.size());
    return result;
}
double reduction_percent(std::size_t original, std::size_t simplified) {
    return original == 0 ? 0.0 : 100.0 * (1.0 - static_cast<double>(simplified) / static_cast<double>(original));
}
}
