#include "cloudforge/engine.hpp"
#include "cloudforge/synthetic.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>

TEST(CudaVoxelGrid, MatchesReferenceAcrossVoxelSizesAndAxes) {
    const cloudforge::PointCloud points{{-1,0,0},{-0.1,0,0},{0,0,0},
        {std::nextafter(1.0,0.0),0,0},{1,0,0},{0,1,0},{0,0,1},
        {0.25,0.5,0.75},{0.25,0.5,0.75}};
    for (const double size : {0.1,0.5,1.0,2.0}) {
        const auto cpu = cloudforge::downsample(points,size);
        const auto gpu = cloudforge::process(points,size,cloudforge::Backend::cuda);
        EXPECT_TRUE(cloudforge::compare(cpu,gpu).passed) << "voxel size=" << size;
    }
}
TEST(CudaVoxelGrid, MatchesLargeSyntheticFrame) {
    const auto frame = cloudforge::make_synthetic_frame();
    const auto cpu = cloudforge::downsample(frame,1);
    const auto gpu = cloudforge::process(frame,1,cloudforge::Backend::cuda);
    EXPECT_TRUE(cloudforge::compare(cpu,gpu).passed);
}
TEST(CudaVoxelGrid, EmptyAndInvalidInput) {
    using cloudforge::Backend;
    EXPECT_TRUE(cloudforge::process({},1,Backend::cuda).empty());
    for (double size : {0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        EXPECT_THROW(static_cast<void>(cloudforge::process({},size,Backend::cuda)),std::invalid_argument);
    for (double bad : {std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        EXPECT_THROW(static_cast<void>(cloudforge::process({{0,bad,0}},1,Backend::cuda)),std::invalid_argument);
    EXPECT_THROW(static_cast<void>(cloudforge::process({{0x1p63,0,0}},1,Backend::cuda)),std::out_of_range);
}

TEST(CudaVoxelGrid, ReusesAndGrowsWorkspaceAcrossFrames) {
    for (std::size_t n : {127u, 40000u, 7u, 80000u, 64u}) {
        cloudforge::PointCloud points;
        for (std::size_t i=0;i<n;++i)
            points.push_back({static_cast<double>(i%503)*0.01-2.5,
                              static_cast<double>((i/503)%37)*0.02-0.3,0.2});
        const auto cpu=cloudforge::downsample(points,0.1);
        const auto gpu=cloudforge::process(points,0.1,cloudforge::Backend::cuda);
        EXPECT_TRUE(cloudforge::compare(cpu,gpu).passed)<<"frame size="<<n;
    }
}
TEST(CudaVoxelGrid, WideRangesUseCollisionFreeFallback) {
    const cloudforge::PointCloud points{{0,0,0},{1e8,1e8,1e8},{1e8+0.25,1e8+0.25,1e8+0.25}};
    const auto cpu=cloudforge::downsample(points,1);
    const auto gpu=cloudforge::process(points,1,cloudforge::Backend::cuda);
    ASSERT_EQ(gpu.size(),2u);
    EXPECT_TRUE(cloudforge::compare(cpu,gpu).passed);
}
TEST(CudaVoxelGrid, PackedKeysHandleLargeSignedOffsets) {
    const cloudforge::PointCloud points{{-0x1p63,0,0},{std::nextafter(0x1p63,0.0),0,0}};
    const auto cpu=cloudforge::downsample(points,1);
    const auto gpu=cloudforge::process(points,1,cloudforge::Backend::cuda);
    ASSERT_EQ(gpu.size(),2u);
    EXPECT_TRUE(cloudforge::compare(cpu,gpu).passed);
}
