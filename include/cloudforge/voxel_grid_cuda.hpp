#pragma once

#include "cloudforge/point_cloud.hpp"

namespace cloudforge
{
    [[nodiscard]] PointCloud downsample_cuda(const PointCloud& input, double voxel_size);
}
