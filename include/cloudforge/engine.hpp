#pragma once
#include "cloudforge/point_cloud.hpp"
#include <filesystem>
#include <vector>

namespace cloudforge {
enum class Backend { cpu, cuda };
bool cuda_backend_compiled();
PointCloud process(const PointCloud& input, double voxel_size, Backend backend);
// Numeric timestamp stems first; lexical order for other names. No recursion.
std::vector<std::filesystem::path> list_frames(const std::filesystem::path& input);
struct Measurement {
    PointCloud points;
    double milliseconds{};
    double fps() const { return milliseconds > 0 ? 1000.0 / milliseconds : 0.0; }
};
Measurement measure(const PointCloud& input, double voxel_size, Backend backend);
struct Comparison {
    bool passed{};
    std::size_t mismatches{};
    double max_error{};
};
// Inputs must both be in lexicographic voxel-key order.
Comparison compare(const PointCloud& cpu, const PointCloud& gpu,
                   double absolute_tolerance = 1e-10, double relative_tolerance = 1e-10);
double reduction_percent(std::size_t original, std::size_t simplified);
}
