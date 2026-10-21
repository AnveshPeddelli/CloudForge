#include "benchmark_session.hpp"
#include "options.hpp"
#include <iostream>
int main(int argc,char** argv){
    try{
        const auto o=cloudforge::app::parse(argc,argv);
        cloudforge::app::BenchmarkConfig config{o.input,o.csv.empty()?std::filesystem::path("build/benchmark.csv"):o.csv,o.voxel,o.frames,o.repeats,o.warmups};
        cloudforge::app::BenchmarkProgress progress;
        const auto report=cloudforge::app::benchmark(config,progress);
        std::cout<<"Frames="<<report.frames<<" samples="<<report.samples
                 <<" CPU p10/median/p90 ms="<<report.cpu.p10<<'/'<<report.cpu.median<<'/'<<report.cpu.p90;
        if(report.gpu)std::cout<<" CUDA p10/median/p90 ms="<<report.cuda.p10<<'/'<<report.cuda.median<<'/'<<report.cuda.p90
                             <<" aggregate CPU/CUDA ratio="<<(report.cuda_total>0?report.cpu_total/report.cuda_total:0);
        std::cout<<" correctness="<<(report.passed?"PASS":"FAIL")<<" CSV="<<report.csv<<'\n';
        return report.passed?0:2;
    }catch(const std::exception& e){std::cerr<<"cloudForge benchmark: "<<e.what()<<'\n';return 1;}
}
