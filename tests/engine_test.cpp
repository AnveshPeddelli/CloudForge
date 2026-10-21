#include "cloudforge/engine.hpp"
#include "test_files.hpp"
#include <gtest/gtest.h>
#include <limits>

TEST(Sequence, SortsDecimalTimestampsNumerically) {
    TestDirectory dir;
    for (const auto* name : {"10.0.pcd", "2.9.pcd", "2.10.pcd", "ignored.txt"}) std::ofstream(dir.path / name).put('\n');
    const auto files = cloudforge::list_frames(dir.path);
    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(files[0].filename(), "2.10.pcd");
    EXPECT_EQ(files[1].filename(), "2.9.pcd");
    EXPECT_EQ(files[2].filename(), "10.0.pcd");
}
TEST(Sequence, RejectsEmptyDirectoryAndMissingPath) {
    TestDirectory dir;
    EXPECT_THROW(static_cast<void>(cloudforge::list_frames(dir.path)),std::runtime_error);
    EXPECT_THROW(static_cast<void>(cloudforge::list_frames(dir.path / "missing")),std::runtime_error);
}
TEST(Comparison, DetectsCountAndCoordinateMismatch) {
    EXPECT_FALSE(cloudforge::compare({{1,2,3}},{}).passed);
    EXPECT_FALSE(cloudforge::compare({{1,2,3}},{{1,2,4}}).passed);
    EXPECT_TRUE(cloudforge::compare({{1,2,3}},{{1 + 1e-12,2,3}}).passed);
    EXPECT_FALSE(cloudforge::compare({{1,2,3}},{{std::numeric_limits<double>::quiet_NaN(),2,3}}).passed);
}
TEST(Metrics, EmptyReductionAndNormalReduction) {
    EXPECT_DOUBLE_EQ(cloudforge::reduction_percent(0,0),0);
    EXPECT_DOUBLE_EQ(cloudforge::reduction_percent(100,25),75);
}
