#include "cloudforge/pcd_io.hpp"
#include "test_files.hpp"
#include <gtest/gtest.h>
#include <array>
#include <limits>

namespace {
std::filesystem::path write(TestDirectory& dir, const std::string& header,
                            const std::vector<float>& data = {}) {
    const auto path = dir.path / "frame.pcd";
    std::ofstream out(path, std::ios::binary);
    out << header;
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size() * sizeof(float)));
    out.close();
    if (!out) throw std::runtime_error("Test fixture write failed");
    return path;
}
const std::string xyz = "FIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n";
}
TEST(PcdLoader, ReadsReorderedFieldsAndPadding) {
    TestDirectory dir;
    const auto path = write(dir,
        "FIELDS intensity z _ x y\nSIZE 4 4 1 4 4\nTYPE F F U F F\nCOUNT 1 1 4 1 1\nWIDTH 2\nHEIGHT 1\nPOINTS 2\nDATA binary\n",
        {77, 3.75f, 0, 1.25f, -2.5f, 88, 6.25f, 0, -4, 5.5f});
    const auto points = cloudforge::load_pcd(path);
    ASSERT_EQ(points.size(), 2u);
    EXPECT_DOUBLE_EQ(points[0].x, 1.25); EXPECT_DOUBLE_EQ(points[0].y, -2.5); EXPECT_DOUBLE_EQ(points[0].z, 3.75);
    EXPECT_DOUBLE_EQ(points[1].x, -4); EXPECT_DOUBLE_EQ(points[1].y, 5.5); EXPECT_DOUBLE_EQ(points[1].z, 6.25);
}
TEST(PcdLoader, SupportsOmittedCountAndCRLF) {
    TestDirectory dir;
    const auto p = write(dir,"FIELDS x y z\r\nSIZE 4 4 4\r\nTYPE F F F\r\nPOINTS 1\r\nDATA binary\r\n",{1,2,3});
    EXPECT_EQ(cloudforge::load_pcd(p).size(),1u);
}
TEST(PcdLoader, EmptyFrame) {
    TestDirectory dir;
    EXPECT_TRUE(cloudforge::load_pcd(write(dir,xyz + "WIDTH 0\nHEIGHT 1\nPOINTS 0\nDATA binary\n")).empty());
}
TEST(PcdLoader, RejectsTruncatedPayload) {
    TestDirectory dir;
    EXPECT_THROW(static_cast<void>(cloudforge::load_pcd(write(dir,xyz + "POINTS 2\nDATA binary\n",{1,2,3}))),std::runtime_error);
}
TEST(PcdLoader, RejectsMissingFile) {
    TestDirectory dir;
    EXPECT_THROW(static_cast<void>(cloudforge::load_pcd(dir.path / "missing.pcd")),std::runtime_error);
}
TEST(PcdLoader, RejectsMalformedHeaders) {
    TestDirectory dir;
    for (const auto& header : {
        xyz + "POINTS -1\nDATA binary\n",
        xyz + "POINTS 1oops\nDATA binary\n",
        xyz + "POINTS 1\nDATA ascii\n",
        xyz + "POINTS 1\nDATA binary_compressed\n",
        xyz + "POINTS 1\n",
        xyz + "WIDTH 2\nHEIGHT 1\nPOINTS 1\nDATA binary\n",
        xyz + "POINTS 1\nPOINTS 1\nDATA binary\n",
        std::string("FIELDS x y z\nSIZE 4 4\nTYPE F F F\nPOINTS 1\nDATA binary\n"),
        std::string("FIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT\nPOINTS 1\nDATA binary\n"),
        std::string("FIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 0 1\nPOINTS 1\nDATA binary\n")}) {
        SCOPED_TRACE(header);
        EXPECT_THROW(static_cast<void>(cloudforge::load_pcd(write(dir,header,{1,2,3}))),std::runtime_error);
    }
}
TEST(PcdLoader, RejectsMissingOrDuplicateXYZ) {
    TestDirectory dir;
    for (const auto* fields : {"x y intensity", "x x z"}) {
        const std::string header = std::string("FIELDS ") + fields + "\nSIZE 4 4 4\nTYPE F F F\nPOINTS 1\nDATA binary\n";
        EXPECT_THROW(static_cast<void>(cloudforge::load_pcd(write(dir,header,{1,2,3}))),std::runtime_error);
    }
}
TEST(PcdLoader, RejectsNonfiniteCoordinates) {
    TestDirectory dir;
    EXPECT_THROW(static_cast<void>(cloudforge::load_pcd(write(dir,xyz + "POINTS 1\nDATA binary\n",{1,std::numeric_limits<float>::quiet_NaN(),3}))),std::runtime_error);
}
