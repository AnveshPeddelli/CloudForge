#pragma once

#include <vector>

namespace cloudforge 
{
    
    struct Point
    {
        double x{};
        double y{};
        double z{};
    };

    using PointCloud = std::vector<Point>;

    [[nodiscard]] PointCloud downsample(const PointCloud& cloud, double voxel_size);
    
}