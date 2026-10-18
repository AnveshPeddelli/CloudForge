#include "cloudforge/point_cloud.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>

namespace cloudforge
{
namespace
{
using VoxelKey =
    std::tuple<std::int64_t, std::int64_t, std::int64_t>;

struct Accumulator
{
    double x{};
    double y{};
    double z{};
    std::size_t count{};
};

std::int64_t voxel_index(double coordinate, double voxel_size)
{
    if (!std::isfinite(coordinate))
    {
        throw std::invalid_argument("Point coordinates must be finite");
    }

    const double index = std::floor(coordinate / voxel_size);

    if (!std::isfinite(index) || index < -0x1p63 || index >= 0x1p63)
    {
        throw std::out_of_range("Voxel index exceeds int64 range");
    }

    return static_cast<std::int64_t>(index);
}
}

PointCloud downsample(const PointCloud& input, double voxel_size)
{
    if (!std::isfinite(voxel_size) || voxel_size <= 0.0)
    {
        throw std::invalid_argument("Voxel size must be finite and positive");
    }

    std::map<VoxelKey, Accumulator> voxels;

    for (const auto& point : input)
    {
        const VoxelKey key{
            voxel_index(point.x, voxel_size),
            voxel_index(point.y, voxel_size),
            voxel_index(point.z, voxel_size)
        };

        auto& accumulator = voxels[key];

        ++accumulator.count;
        const double count = static_cast<double>(accumulator.count);
        const double weight = (count - 1.0) / count;

        accumulator.x = accumulator.x * weight + point.x / count;
        accumulator.y = accumulator.y * weight + point.y / count;
        accumulator.z = accumulator.z * weight + point.z / count;
    }

    PointCloud output;
    output.reserve(voxels.size());

    for (const auto& [key, accumulator] : voxels)
    {
        output.push_back({
            accumulator.x,
            accumulator.y,
            accumulator.z
        });
    }

    return output;
}
}
