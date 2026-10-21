#include "../apps/processing.hpp"
#include "../apps/file_browser.hpp"
#include "../apps/benchmark_session.hpp"
#include "../apps/benchmark_results.hpp"
#include "test_files.hpp"
#include <gtest/gtest.h>

namespace {
std::filesystem::path fixture(TestDirectory& directory) {
    const auto path=directory.path/"frame.pcd";
    std::ofstream file(path,std::ios::binary);
    file<<"FIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nPOINTS 2\nDATA binary\n";
    const float points[]{0.1f,0.2f,0.3f,0.2f,0.3f,0.4f};
    file.write(reinterpret_cast<const char*>(points),sizeof(points));
    file.close();
    if(!file)throw std::runtime_error("Fixture write failed");
    return path;
}
}
TEST(StudioProcessing, SelectedCPUDoesNotRunCUDA) {
    TestDirectory directory;
    const auto result=cloudforge::app::execute(fixture(directory),1,cloudforge::Backend::cpu,false,3);
    EXPECT_TRUE(result.has_cpu);
    EXPECT_FALSE(result.has_cuda);
    EXPECT_FALSE(result.compared);
    ASSERT_EQ(result.cpu.points.size(),1u);
    EXPECT_NEAR(result.cpu.points[0].x,0.15,1e-7);
}
TEST(StudioProcessing, WorkerQueuesJobsAndPropagatesErrors) {
    TestDirectory directory;
    const auto path=fixture(directory);
    cloudforge::app::ProcessingWorker worker;
    auto first=worker.submit(path,1,cloudforge::Backend::cpu,false,1);
    auto second=worker.submit(path,0.01,cloudforge::Backend::cpu,false,1);
    auto missing=worker.submit(directory.path/"missing.pcd",1,cloudforge::Backend::cpu,false,1);
    EXPECT_EQ(first.get().cpu.points.size(),1u);
    EXPECT_EQ(second.get().cpu.points.size(),2u);
    EXPECT_THROW(static_cast<void>(missing.get()),std::runtime_error);
}

TEST(StudioBrowser, CachesTypesAndFiltersBeforeRendering) {
    TestDirectory directory;
    std::filesystem::create_directory(directory.path/"folder");
    std::ofstream(directory.path/"b.pcd")<<"larger";
    std::ofstream(directory.path/"a.pcd").put('\n');
    std::ofstream(directory.path/"ignore.zip").put('\n');
    const auto entries=cloudforge::app::read_directory(directory.path);
    ASSERT_EQ(entries.size(),3u);
    EXPECT_TRUE(entries[0].directory);
    EXPECT_EQ(entries[0].name,"folder");
    EXPECT_EQ(entries[1].name,"b.pcd");
    ASSERT_TRUE(entries[1].bytes.has_value());
    EXPECT_EQ(*entries[1].bytes,6u);
    EXPECT_FALSE(entries[1].directory);
    EXPECT_EQ(entries[2].name,"a.pcd");
    // Cached presentation remains usable even if the directory changes afterward.
    std::filesystem::remove(directory.path/"folder");
    EXPECT_TRUE(entries[0].directory);
    EXPECT_EQ(entries[0].name,"folder");
}

TEST(StudioBrowser, DiscoveryAndListingRunOnWorker) {
    TestDirectory directory;
    const auto file=fixture(directory);
    cloudforge::app::ProcessingWorker worker;
    auto listing=worker.post([path=directory.path]{return cloudforge::app::read_directory(path);});
    auto source=worker.post([path=directory.path]{return cloudforge::app::discover_source(path,30);});
    ASSERT_EQ(listing.get().size(),1u);
    const auto selected=source.get();
    ASSERT_EQ(selected.frames.size(),1u);
    EXPECT_EQ(selected.frames[0],file);
    EXPECT_EQ(selected.browser_directory,directory.path);
}

TEST(Benchmark, ComputesInterpolatedPercentiles) {
    const auto stats=cloudforge::app::distribution({5,1,4,2,3});
    EXPECT_DOUBLE_EQ(stats.median,3);EXPECT_NEAR(stats.p10,1.4,1e-12);EXPECT_NEAR(stats.p90,4.6,1e-12);
    EXPECT_THROW(static_cast<void>(cloudforge::app::distribution({})),std::invalid_argument);
}
TEST(Benchmark, RerunsInputAndSavesSeparateReportsAutomatically) {
    TestDirectory directory;const auto input=fixture(directory);
    cloudforge::app::BenchmarkConfig config{input,directory.path/"report.csv",1.0,1,2,1};
    cloudforge::app::BenchmarkProgress progress;
    const auto report=cloudforge::app::benchmark(config,progress);
    EXPECT_EQ(report.samples,2u);EXPECT_EQ(progress.completed.load(),2u);EXPECT_TRUE(report.passed);
    std::ifstream csv(config.output);const std::string content((std::istreambuf_iterator<char>(csv)),{});
    EXPECT_NE(content.find("# status=COMPLETE"),std::string::npos);
    EXPECT_NE(content.find("cpu_ms,cuda_ms"),std::string::npos);
    const auto loaded=cloudforge::app::load_benchmark_csv(config.output);
    EXPECT_EQ(loaded.samples,report.samples);EXPECT_EQ(loaded.gpu,report.gpu);
    EXPECT_DOUBLE_EQ(loaded.cpu.median,report.cpu.median);
    const auto frames=cloudforge::app::frame_timings(loaded);
    ASSERT_EQ(frames.size(),1u);EXPECT_EQ(frames[0].samples,2u);
    EXPECT_EQ(frames[0].input_points,2u);
    const auto rerun=cloudforge::app::benchmark(config,progress);
    EXPECT_EQ(rerun.samples,2u);EXPECT_NE(rerun.csv,report.csv);
    EXPECT_TRUE(std::filesystem::exists(report.csv));EXPECT_TRUE(std::filesystem::exists(rerun.csv));
}

TEST(Benchmark, UsesLoadedFrameSnapshotWithoutRequiringFileAgain) {
    TestDirectory directory;const auto input=fixture(directory);
    cloudforge::app::BenchmarkConfig config{input,directory.path/"snapshot.csv",1.0,1,2,1};
    config.selected_frames={input};config.current_input=std::make_shared<const cloudforge::PointCloud>(cloudforge::load_pcd(input));
    std::filesystem::remove(input);
    cloudforge::app::BenchmarkProgress progress;
    const auto report=cloudforge::app::benchmark(config,progress);
    ASSERT_EQ(report.raw.size(),2u);EXPECT_EQ(report.raw[0].input_points,2u);EXPECT_TRUE(report.passed);
}
TEST(Benchmark, CancellationIsExplicitAndInvalidSettingsFail) {
    TestDirectory directory;const auto input=fixture(directory);
    cloudforge::app::BenchmarkConfig config{input,directory.path/"cancelled.csv",1.0,1,2,1};
    cloudforge::app::BenchmarkProgress progress;progress.cancel=true;
    const auto report=cloudforge::app::benchmark(config,progress);
    EXPECT_TRUE(report.cancelled);EXPECT_EQ(report.samples,0u);
    config.repeats=0;EXPECT_THROW(static_cast<void>(cloudforge::app::benchmark(config,progress)),std::invalid_argument);
}

TEST(BenchmarkCsv, ParsesQuotedFieldsAndRejectsMalformedRecords) {
    std::istringstream source("\"file,with\"\"quote\n.pcd\",12\r\n");
    std::vector<std::string> fields;
    ASSERT_TRUE(cloudforge::app::csv_record(source,fields));ASSERT_EQ(fields.size(),2u);
    EXPECT_EQ(fields[0],"file,with\"quote\n.pcd");EXPECT_EQ(fields[1],"12");
    std::istringstream bad("\"unterminated");
    EXPECT_THROW(static_cast<void>(cloudforge::app::csv_record(bad,fields)),std::runtime_error);
    EXPECT_THROW(static_cast<void>(cloudforge::app::csv_real("nan")),std::runtime_error);
    EXPECT_THROW(static_cast<void>(cloudforge::app::csv_real("-1")),std::runtime_error);
    EXPECT_THROW(static_cast<void>(cloudforge::app::csv_count("12garbage")),std::runtime_error);
}
TEST(BenchmarkCsv, RejectsUnsupportedHeadersAndInvalidTimings) {
    TestDirectory directory;const auto path=directory.path/"invalid.csv";
    {std::ofstream file(path);file<<"frame,cpu_median_ms\nframe,1\n";}
    EXPECT_THROW(static_cast<void>(cloudforge::app::load_benchmark_csv(path)),std::runtime_error);
    {std::ofstream file(path);file<<"# cloudForge benchmark schema 1\n# status=COMPLETE\nframe,input_points,cpu_points,cuda_points,repeat,order,cpu_ms,cuda_ms,max_error,correctness\nf,1,1,1,0,CPU_FIRST,inf,1,0,PASS\n";}
    EXPECT_THROW(static_cast<void>(cloudforge::app::load_benchmark_csv(path)),std::runtime_error);
}

TEST(Playback, ProcessesOneFrameWithSelectedBackend) {
    TestDirectory directory;const auto path=fixture(directory);
    const auto frame=cloudforge::app::execute_frame(path,1.0,cloudforge::Backend::cpu);
    EXPECT_EQ(frame.original.size(),2u);EXPECT_EQ(frame.cpu.points.size(),1u);
    EXPECT_TRUE(frame.has_cpu);EXPECT_FALSE(frame.has_cuda);EXPECT_FALSE(frame.compared);
}
TEST(Playback, DiscoversMultipleClipsInOrder) {
    TestDirectory directory;
    std::filesystem::create_directory(directory.path/"clip-b");std::filesystem::create_directory(directory.path/"clip-a");
    for(const auto& name:{"clip-b/10.pcd","clip-a/10.pcd","clip-a/2.pcd"})std::ofstream(directory.path/name).put('\n');
    const auto source=cloudforge::app::discover_source(directory.path,30);
    ASSERT_EQ(source.frames.size(),3u);
    EXPECT_EQ(source.frames[0],directory.path/"clip-a/2.pcd");
    EXPECT_EQ(source.frames[1],directory.path/"clip-a/10.pcd");
    EXPECT_EQ(source.frames[2],directory.path/"clip-b/10.pcd");
}
