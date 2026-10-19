#include "cloudforge/pcd_io.hpp"
#include "cloudforge/point_cloud.hpp"
#include "cloudforge/voxel_grid_cuda.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[])
{
    if (argc < 2 || argc > 3)
    {
        std::cerr << "Usage: cloudforge_compare <frame.pcd> [voxel_size]\n";
        return 1;
    }

    try
    {
        double voxel_size = 0.1;
        if (argc == 3)
        {
            std::size_t consumed{};
            voxel_size = std::stod(argv[2], &consumed);
            if (consumed != std::string(argv[2]).size())
                throw std::invalid_argument("Trailing text in voxel size");
        }

        const auto input = cloudforge::load_pcd(argv[1]);
        const auto cpu = cloudforge::downsample(input, voxel_size);
        const auto gpu = cloudforge::downsample_cuda(input, voxel_size);

        // Both implementations emit centroids in lexicographic voxel-key order.
        // Compare corresponding representatives, allowing reduction rounding.
        constexpr double absolute_tolerance = 1e-10;
        constexpr double relative_tolerance = 1e-10;
        std::size_t mismatched_points{};
        double maximum_error{};
        bool all_finite = true;

        for (std::size_t i = 0; i < std::min(cpu.size(), gpu.size()); ++i)
        {
            const std::array<double, 3> a{cpu[i].x, cpu[i].y, cpu[i].z};
            const std::array<double, 3> b{gpu[i].x, gpu[i].y, gpu[i].z};
            bool matches = true;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(a[axis]) || !std::isfinite(b[axis]))
                {
                    all_finite = false;
                    matches = false;
                    continue;
                }
                const double error = std::abs(a[axis] - b[axis]);
                maximum_error = std::max(maximum_error, error);
                const double limit = absolute_tolerance + relative_tolerance
                    * std::max(std::abs(a[axis]), std::abs(b[axis]));
                if (error > limit) matches = false;
            }
            if (!matches) ++mismatched_points;
        }

        const bool passed = cpu.size() == gpu.size()
            && all_finite && mismatched_points == 0;

        std::cout << std::setprecision(12)
                  << "File: " << argv[1] << '\n'
                  << "Voxel size: " << voxel_size << '\n'
                  << "Input points: " << input.size() << '\n'
                  << "CPU centroids: " << cpu.size() << '\n'
                  << "GPU centroids: " << gpu.size() << '\n'
                  << "Absolute tolerance: " << absolute_tolerance << '\n'
                  << "Relative tolerance: " << relative_tolerance << '\n'
                  << "Maximum finite coordinate difference: " << maximum_error << '\n'
                  << "All compared coordinates finite: " << (all_finite ? "yes" : "no") << '\n'
                  << "Mismatched paired centroids: " << mismatched_points << '\n'
                  << "CPU/GPU comparison: " << (passed ? "PASS" : "FAIL") << '\n';
        return passed ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "cloudForge comparison: " << error.what() << '\n';
        return 1;
    }
}
