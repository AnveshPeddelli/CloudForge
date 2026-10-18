#include "cloudforge/point_cloud.hpp"

#include <exception>
#include <iostream>

int main()
{
    try
    {
        const cloudforge::PointCloud original{
            {0.1, 0.2, 0.3},
            {0.2, 0.3, 0.4},
            {1.1, 1.2, 1.3},
            {-0.1, -0.2, -0.3}
        };

        const double voxel_size = 1.0;
        const auto simplified =
            cloudforge::downsample(original, voxel_size);

        std::cout << "Original points: " << original.size() << '\n';
        std::cout << "Simplified points: " << simplified.size() << '\n';

        for (const auto& point : simplified)
        {
            std::cout << point.x << ", "
                      << point.y << ", "
                      << point.z << '\n';
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "cloudForge: " << error.what() << '\n';
        return 1;
    }
}