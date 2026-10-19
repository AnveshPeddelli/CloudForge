#pragma once

#include "cloudforge/point_cloud.hpp"

#include <filesystem>

namespace cloudforge
{
    //Loads xyz coordinates from a uncompressed binary pcd file.
    //Throws std::runtime_error for unsupported or malformed files.
    [[nodiscard]] PointCloud load_pcd(const std::filesystem::path& path);
}