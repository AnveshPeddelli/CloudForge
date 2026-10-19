#include "cloudforge/point_cloud.hpp"
#include "cloudforge/pcd_io.hpp"

#include <exception>
#include <iostream>

int main(int argc, char* argv[])
{
    if(argc != 2)
    {
        std::cerr << "Usage: cloudforge <frame.pcd>\n";
        return 1;
    }

    try
    {
        const auto original = cloudforge::load_pcd(argv[1]);

        const double voxel_size = 0.1;
        const auto simplified = cloudforge::downsample(original, voxel_size);

        const double reduction = original.empty() ? 0.0 : 100.0 *(1.0-static_cast<double>(simplified.size())/ static_cast<double>(original.size()));

        std::cout << "File: "<< argv[1] << '\n';
        std::cout << "Voxel size: "<< voxel_size<< '\n';
        std::cout << "Original points: " << original.size() << '\n';
        std::cout << "Simplified points: " << simplified.size() << '\n';
        std::cout << "Reduction: " << reduction << '\n';

    }
    catch (const std::exception& error)
    {
        std::cerr << "cloudForge: " << error.what() << '\n';
        return 1;
    }
}