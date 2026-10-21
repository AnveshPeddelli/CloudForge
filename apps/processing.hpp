#pragma once
#include "cloudforge/engine.hpp"
#include "cloudforge/pcd_io.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>

namespace cloudforge::app {
struct ProcessingResult {
    PointCloud original;
    Measurement cpu, cuda;
    bool has_cpu{}, has_cuda{}, compared{};
    Comparison comparison;
    double loading_ms{}, total_ms{};
    double first_call_ms{};
    double first_cpu_ms{}, first_cuda_ms{};
};
// Playback performs one completed backend call per frame; benchmark repetitions
// belong to the dedicated benchmark runner, not the animation loop.
inline ProcessingResult execute_frame(const std::filesystem::path& path,double voxel,Backend backend){
    ProcessingResult result;const auto start=std::chrono::steady_clock::now();
    result.original=load_pcd(path);
    result.loading_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    auto measured=measure(result.original,voxel,backend);
    if(backend==Backend::cpu){result.has_cpu=true;result.cpu=std::move(measured);}
    else{result.has_cuda=true;result.cuda=std::move(measured);}
    result.total_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return result;
}
inline ProcessingResult execute(const std::filesystem::path& path, double voxel, Backend selected,
                                 bool compare_both, std::size_t repeats) {
    if(repeats==0)throw std::invalid_argument("At least one measured repeat is required");
    ProcessingResult result;
    const auto start = std::chrono::steady_clock::now();
    result.original = load_pcd(path);
    result.loading_ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    const auto warm_backend=[&](Backend backend){
        const auto warm=measure(result.original,voxel,backend);
        if(backend==Backend::cpu)result.first_cpu_ms=warm.milliseconds;
        else result.first_cuda_ms=warm.milliseconds;
        return warm.milliseconds;
    };
    result.first_call_ms=warm_backend(selected);
    const bool run_cpu = compare_both || selected == Backend::cpu;
    const bool run_gpu = (compare_both && cuda_backend_compiled()) || selected == Backend::cuda;
    if (compare_both && run_gpu)static_cast<void>(warm_backend(selected == Backend::cpu ? Backend::cuda : Backend::cpu));
    std::vector<double> ct,gt;
    const auto run=[&](Backend backend) {
        auto m = measure(result.original,voxel,backend);
        if(backend==Backend::cpu){ct.push_back(m.milliseconds);result.cpu=std::move(m);}
        else{gt.push_back(m.milliseconds);result.cuda=std::move(m);}
    };
    for(std::size_t i=0;i<repeats;++i){
        if(i%2 && run_gpu){run(Backend::cuda);if(run_cpu)run(Backend::cpu);}
        else{if(run_cpu)run(Backend::cpu);if(run_gpu)run(Backend::cuda);}
    }
    const auto median=[](std::vector<double>& times){std::sort(times.begin(),times.end());const auto n=times.size();return n%2?times[n/2]:(times[n/2-1]+times[n/2])/2;};
    if(run_cpu){result.has_cpu=true;result.cpu.milliseconds=median(ct);}
    if(run_gpu){result.has_cuda=true;result.cuda.milliseconds=median(gt);}
    result.compared=run_cpu&&run_gpu;
    if(result.compared)result.comparison=compare(result.cpu.points,result.cuda.points);
    result.total_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return result;
}
// One long-lived worker owns the thread-local CUDA workspace across jobs.
class ProcessingWorker {
public:
    ProcessingWorker():thread_([this](std::stop_token stop){
        while(true){
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock,[&]{return stop.stop_requested()||!tasks_.empty();});
                if(stop.stop_requested()&&tasks_.empty())return;
                task=std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }){}
    ~ProcessingWorker(){thread_.request_stop();condition_.notify_all();}
    template<class Function>
    auto post(Function function) -> std::future<std::invoke_result_t<Function>> {
        using Result=std::invoke_result_t<Function>;
        auto task=std::make_shared<std::packaged_task<Result()>>(std::move(function));
        auto result=task->get_future();
        {std::lock_guard lock(mutex_);tasks_.push_back([task]{(*task)();});}
        condition_.notify_one();return result;
    }
    std::future<ProcessingResult> submit(std::filesystem::path path,double voxel,Backend backend,bool comparison,std::size_t repeats){
        return post([=]{return execute(path,voxel,backend,comparison,repeats);});
    }
private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::function<void()>> tasks_;
    std::jthread thread_;
};
}
