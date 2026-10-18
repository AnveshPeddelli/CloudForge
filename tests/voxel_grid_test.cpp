#include "cloudforge/point_cloud.hpp"

#include <limits>
#include <stdexcept>
#include <gtest/gtest.h>

TEST(VoxelGrid, ComputesCentroidForSharedVoxel)
{
    const cloudforge::PointCloud input{
        {0.1, 0.2, 0.3},
        {0.2, 0.3, 0.4}
    };

    const auto output = cloudforge::downsample(input, 1.0);

    ASSERT_EQ(output.size(), 1u);
    EXPECT_NEAR(output[0].x, 0.15, 1e-12);
    EXPECT_NEAR(output[0].y, 0.25, 1e-12);
    EXPECT_NEAR(output[0].z, 0.35, 1e-12);
}

TEST(VoxelGrid, SeparatesNegativeAndNonnegativeVoxels)
{
    const cloudforge::PointCloud input{
        {-0.1, 0.0, 0.0},
        { 0.1, 0.0, 0.0}
    };

    const auto output = cloudforge::downsample(input, 1.0);

    ASSERT_EQ(output.size(), 2u);
    EXPECT_NEAR(output[0].x, -0.1, 1e-12);
    EXPECT_NEAR(output[1].x,  0.1, 1e-12);
}


TEST(VoxelGrid, HandlesEmptyInput)
{
    const auto output = cloudforge::downsample({}, 1.0);

    EXPECT_TRUE(output.empty());
}

TEST(VoxelGrid, HandlesExactBoundaries)
{
    const cloudforge::PointCloud input{
        {-1.0, 0.0, 0.0},
        {-0.5, 0.0, 0.0},
        { 0.0, 0.0, 0.0},
        { 0.5, 0.0, 0.0},
        { 1.0, 0.0, 0.0}
    };

    const auto output = cloudforge::downsample(input, 1.0);

    ASSERT_EQ(output.size(), 3u);
    EXPECT_NEAR(output[0].x, -0.75, 1e-12);
    EXPECT_NEAR(output[1].x,  0.25, 1e-12);
    EXPECT_NEAR(output[2].x,  1.00, 1e-12);
}

TEST(VoxelGrid, CollapsesDuplicatePoints)
{
    const cloudforge::PointCloud input{
        {0.25, 0.5, 0.75},
        {0.25, 0.5, 0.75},
        {0.25, 0.5, 0.75}
    };

    const auto output = cloudforge::downsample(input, 1.0);

    ASSERT_EQ(output.size(), 1u);
    EXPECT_NEAR(output[0].x, 0.25, 1e-12);
    EXPECT_NEAR(output[0].y, 0.50, 1e-12);
    EXPECT_NEAR(output[0].z, 0.75, 1e-12);
}
TEST(VoxelGrid, RejectsInvalidVoxelSizes)
{
    const cloudforge::PointCloud input{{0.0, 0.0, 0.0}};

    for (const double size : {
        0.0,
        -1.0,
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()
    })
    {
        EXPECT_THROW(
            static_cast<void>(cloudforge::downsample(input, size)),
            std::invalid_argument);
    }
}

TEST(VoxelGrid, RejectsMalformedCoordinates)
{
    for (const double bad : {
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()
    })
    {
        const cloudforge::PointCloud bad_x{{bad, 0.0, 0.0}};
        const cloudforge::PointCloud bad_y{{0.0, bad, 0.0}};
        const cloudforge::PointCloud bad_z{{0.0, 0.0, bad}};

        EXPECT_THROW(
            static_cast<void>(cloudforge::downsample(bad_x, 1.0)),
            std::invalid_argument);

        EXPECT_THROW(
            static_cast<void>(cloudforge::downsample(bad_y, 1.0)),
            std::invalid_argument);

        EXPECT_THROW(
            static_cast<void>(cloudforge::downsample(bad_z, 1.0)),
            std::invalid_argument);
    }
}

TEST(VoxelGrid, RejectsVoxelIndexOverflow)
{
    // 2^63 is one past the largest signed 64-bit index.
    const cloudforge::PointCloud input{{0x1p63, 0.0, 0.0}};

    EXPECT_THROW(
        static_cast<void>(cloudforge::downsample(input, 1.0)),
        std::out_of_range);
}