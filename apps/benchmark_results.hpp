#pragma once
#include "benchmark_session.hpp"
#include <charconv>
#include <map>

namespace cloudforge::app {
struct FrameTiming {
    std::string frame;
    std::size_t input_points{},output_points{},samples{};
    Distribution cpu,cuda;
    bool gpu{},passed{true};
};
inline std::vector<FrameTiming> frame_timings(const BenchmarkReport& report){
    struct Group {FrameTiming frame;std::vector<double> cpu,cuda;};
    std::vector<Group> groups;std::map<std::string,std::size_t> indices;
    for(const auto& sample:report.raw){
        const auto [it,inserted]=indices.emplace(sample.frame,groups.size());
        if(inserted){Group group;group.frame.frame=sample.frame;groups.push_back(std::move(group));}
        auto& group=groups[it->second];auto& f=group.frame;
        f.input_points=sample.input_points;f.output_points=sample.cpu_points;++f.samples;
        f.gpu=sample.gpu;f.passed&=sample.passed;group.cpu.push_back(sample.cpu_ms);
        if(sample.gpu)group.cuda.push_back(sample.cuda_ms);
    }
    std::vector<FrameTiming> out;
    for(auto& group:groups){group.frame.cpu=distribution(group.cpu);if(group.frame.gpu)group.frame.cuda=distribution(group.cuda);out.push_back(std::move(group.frame));}
    return out;
}
// RFC-style quoted fields, including commas, doubled quotes and embedded newlines.
inline bool csv_record(std::istream& stream,std::vector<std::string>& cells){
    cells.clear();std::string field;bool quoted=false,closed=false,started=false;std::size_t length{};
    char c{};
    while(stream.get(c)){
        started=true;if(++length>1024*1024)throw std::runtime_error("CSV record too long");
        if(quoted){if(c=='\"'){if(stream.peek()=='\"'){stream.get(c);field+='\"';}else{quoted=false;closed=true;}}else field+=c;continue;}
        if(c==','){cells.push_back(std::move(field));field.clear();closed=false;continue;}
        if(c=='\n'||c=='\r'){if(c=='\r'&&stream.peek()=='\n')stream.get(c);cells.push_back(std::move(field));return true;}
        if(closed)throw std::runtime_error("Unexpected characters after quoted CSV field");
        if(c=='\"'){if(!field.empty())throw std::runtime_error("Unexpected CSV quote");quoted=true;}
        else field+=c;
    }
    if(quoted)throw std::runtime_error("Unterminated CSV quote");
    if(started)cells.push_back(std::move(field));
    return started;
}
inline std::size_t csv_count(const std::string& value){
    std::size_t result{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if(value.empty()||parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size())throw std::runtime_error("Invalid CSV integer");
    return result;
}
inline double csv_real(const std::string& value){
    std::size_t used{};double result{};
    try{result=std::stod(value,&used);}catch(...){throw std::runtime_error("Invalid CSV timing");}
    if(used!=value.size()||!std::isfinite(result)||result<0)throw std::runtime_error("CSV values must be finite and nonnegative");
    return result;
}
inline BenchmarkReport load_benchmark_csv(const std::filesystem::path& path){
    if(std::filesystem::file_size(path)>64*1024*1024)throw std::runtime_error("CSV exceeds 64 MiB limit");
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot open benchmark CSV");
    BenchmarkReport report;report.csv=path;std::string line;bool status=false,schema=false;
    while(file.peek()=='#'){
        std::getline(file,line);if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line=="# cloudForge benchmark schema 1")schema=true;
        if(line=="# status=CANCELLED"){report.cancelled=true;status=true;}
        if(line=="# status=COMPLETE")status=true;
        if(line=="# status=CORRECTNESS_FAILED"){report.passed=false;status=true;}
    }
    std::vector<std::string> cells;
    const std::vector<std::string> expected{"frame","input_points","cpu_points","cuda_points","repeat","order","cpu_ms","cuda_ms","max_error","correctness"};
    if(!schema||!status||!csv_record(file,cells)||cells!=expected)throw std::runtime_error("Use a cloudForge schema 1 raw benchmark CSV; legacy summary CSVs are not raw samples");
    std::vector<double> cpu,gpu;
    while(csv_record(file,cells)){
        if(cells.size()!=10||cells[0].empty())throw std::runtime_error("Malformed benchmark CSV row");
        if(report.raw.size()>=200000)throw std::runtime_error("CSV exceeds 200000 samples");
        const bool has_gpu=cells[9]!="CPU_ONLY";
        if(cells[9]!="PASS"&&cells[9]!="FAIL"&&cells[9]!="CPU_ONLY")throw std::runtime_error("Invalid correctness status");
        if(cells[5]!="CPU_FIRST"&&cells[5]!="CUDA_FIRST")throw std::runtime_error("Invalid measurement order");
        if(!report.raw.empty()&&report.gpu!=has_gpu)throw std::runtime_error("Mixed CPU-only and CUDA CSV rows");
        TimingSample sample{cells[0],cells[5],csv_count(cells[1]),csv_count(cells[2]),csv_count(cells[3]),csv_count(cells[4]),csv_real(cells[6]),has_gpu?csv_real(cells[7]):0,csv_real(cells[8]),has_gpu,cells[9]!="FAIL"};
        if(!has_gpu&&!cells[7].empty())throw std::runtime_error("CPU-only row contains CUDA timing");
        report.gpu=has_gpu;report.passed&=sample.passed;report.max_error=std::max(report.max_error,sample.max_error);
        cpu.push_back(sample.cpu_ms);report.cpu_total+=sample.cpu_ms;
        if(has_gpu){gpu.push_back(sample.cuda_ms);report.cuda_total+=sample.cuda_ms;}
        report.raw.push_back(std::move(sample));
    }
    report.samples=report.raw.size();report.frames=frame_timings(report).size();
    if(!cpu.empty())report.cpu=distribution(cpu);
    if(!gpu.empty())report.cuda=distribution(gpu);
    if(cpu.empty()&&!report.cancelled)throw std::runtime_error("CSV contains no samples");
    return report;
}
}
