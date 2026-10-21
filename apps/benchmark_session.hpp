#pragma once
#include "processing.hpp"
#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace cloudforge::app {
inline std::string csv_quote(const std::string& value){std::string out="\"";for(char c:value){if(c=='\"')out+='\"';out+=c;}return out+'\"';}
struct Distribution {double p10{},median{},p90{};};
inline Distribution distribution(std::vector<double> values){
    if(values.empty())throw std::invalid_argument("No timing samples");
    std::sort(values.begin(),values.end());
    const auto percentile=[&](double p){const double at=p*(values.size()-1);const auto i=static_cast<std::size_t>(at);return values[i]+(values[std::min(i+1,values.size()-1)]-values[i])*(at-i);};
    return {percentile(.1),percentile(.5),percentile(.9)};
}
struct BenchmarkConfig {
    std::filesystem::path input,output;
    double voxel{.1};
    std::size_t frames{30},repeats{30},warmups{3};
    std::vector<std::filesystem::path> selected_frames{};
    std::shared_ptr<const PointCloud> current_input{};
};
inline std::filesystem::path benchmark_output(std::filesystem::path requested){
    if(requested.empty()){
        const auto directory=std::filesystem::path("build/benchmarks");std::filesystem::create_directories(directory);
        requested=directory/"benchmark.csv";
    }
    if(!std::filesystem::exists(requested))return requested;
    const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
    const auto stem=requested.stem().string()+"-"+std::to_string(stamp);
    for(std::size_t i=0;;++i){auto path=requested.parent_path()/(stem+"-"+std::to_string(i)+requested.extension().string());if(!std::filesystem::exists(path))return path;}
}
struct BenchmarkProgress {std::atomic<std::size_t> completed{},total{};std::atomic<bool> cancel{};};
struct TimingSample {
    std::string frame,order;
    std::size_t input_points{},cpu_points{},cuda_points{},repeat{};
    double cpu_ms{},cuda_ms{},max_error{};
    bool gpu{},passed{true};
};
struct BenchmarkReport {
    Distribution cpu,cuda;
    std::size_t frames{},samples{};
    bool gpu{},passed{true},cancelled{};
    double max_error{},cpu_total{},cuda_total{};
    std::filesystem::path csv;
    std::vector<TimingSample> raw;
};
inline BenchmarkReport benchmark(const BenchmarkConfig& config,BenchmarkProgress& progress){
    if(!std::isfinite(config.voxel)||config.voxel<=0||!config.frames||!config.repeats||!config.warmups||config.frames>1000000||config.repeats>1000000||config.warmups>1000000)
        throw std::invalid_argument("Voxel must be positive; counts must be 1..1000000");
    const auto output=benchmark_output(config.output);
    const auto parent=output.parent_path();
    if(!parent.empty()&&!std::filesystem::is_directory(parent))throw std::invalid_argument("CSV parent directory does not exist");
    auto frames=config.selected_frames.empty()?list_frames(config.input):config.selected_frames;frames.resize(std::min(frames.size(),config.frames));
    if(frames.size()>200000/config.repeats)throw std::invalid_argument("Limit a report to 200000 measured pairs; reduce frames or repeats");
    BenchmarkReport report;report.gpu=cuda_backend_compiled();report.csv=output;
    progress.total=frames.size()*config.repeats;progress.completed=0;
    // Buffer until a complete/cancelled run; never leave a success-looking partial report on failure.
    std::ostringstream rows;rows<<std::setprecision(17);
    std::vector<double> cpu_times,gpu_times;
    for(const auto& path:frames){
        if(progress.cancel){report.cancelled=true;break;}
        const auto loaded=(config.current_input&&path==frames.front())?PointCloud{}:load_pcd(path);
        const auto& input=(config.current_input&&path==frames.front())?*config.current_input:loaded;PointCloud cpu,gpu;
        for(std::size_t i=0;i<config.warmups;++i){
            if(progress.cancel){report.cancelled=true;break;}
            cpu=process(input,config.voxel,Backend::cpu);
            if(report.gpu)gpu=process(input,config.voxel,Backend::cuda);
        }
        if(report.cancelled)break;
        for(std::size_t r=0;r<config.repeats;++r){
            if(progress.cancel){report.cancelled=true;break;}
            Measurement c,g;
            if(report.gpu&&r%2){g=measure(input,config.voxel,Backend::cuda);c=measure(input,config.voxel,Backend::cpu);}
            else{c=measure(input,config.voxel,Backend::cpu);if(report.gpu)g=measure(input,config.voxel,Backend::cuda);}
            const auto check=report.gpu?compare(c.points,g.points):Comparison{true,0,0};
            report.passed&=check.passed;report.max_error=std::max(report.max_error,check.max_error);
            cpu_times.push_back(c.milliseconds);report.cpu_total+=c.milliseconds;
            if(report.gpu){gpu_times.push_back(g.milliseconds);report.cuda_total+=g.milliseconds;}
            rows<<csv_quote(path.string())<<','<<input.size()<<','<<c.points.size()<<','<<g.points.size()<<','<<r<<','<<(report.gpu&&r%2?"CUDA_FIRST":"CPU_FIRST")<<','<<c.milliseconds<<',';
            if(report.gpu)rows<<g.milliseconds;
            rows<<','<<check.max_error<<','<<(report.gpu?(check.passed?"PASS":"FAIL"):"CPU_ONLY")<<'\n';
            report.raw.push_back({path.string(),report.gpu&&r%2?"CUDA_FIRST":"CPU_FIRST",input.size(),c.points.size(),g.points.size(),r,c.milliseconds,g.milliseconds,check.max_error,report.gpu,check.passed});
            ++report.samples;++progress.completed;
        }
        ++report.frames;if(report.cancelled)break;
    }
    if(!cpu_times.empty())report.cpu=distribution(cpu_times);
    if(!gpu_times.empty())report.cuda=distribution(gpu_times);
    std::ofstream csv(output);if(!csv)throw std::runtime_error("Cannot create CSV: "+output.string());
    csv<<"# cloudForge benchmark schema 1\n# status="<<(report.cancelled?"CANCELLED":report.passed?"COMPLETE":"CORRECTNESS_FAILED")
       <<"\n# input="<<std::quoted(config.input.string())<<"\n# voxel="<<std::setprecision(17)<<config.voxel<<" warmups_per_frame="<<config.warmups<<" repeats="<<config.repeats<<" requested_frames="<<config.frames
       <<"\n# scope=completed backend call; validation+allocation+transfers+synchronization included; file I/O+comparison+rendering excluded\n# order=fixed CPU then CUDA warmups; alternating measured order; persistent GPU workspace\n"
       <<"# epoch_seconds="<<std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()
       <<"\n# compiler="
#ifdef _MSC_VER
       <<"MSVC "<<_MSC_VER
#else
       <<__VERSION__
#endif
       <<" cpu_hardware_threads="<<std::thread::hardware_concurrency()<<"\n"
#ifdef NDEBUG
       <<"# build=Release/NDEBUG\n"
#else
       <<"# build=Debug; do not use for performance claims\n"
#endif
       <<"frame,input_points,cpu_points,cuda_points,repeat,order,cpu_ms,cuda_ms,max_error,correctness\n"<<rows.str();
    csv.flush();if(!csv)throw std::runtime_error("CSV write failed");return report;
}
}
