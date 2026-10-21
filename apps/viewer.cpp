#include "gl_support.hpp"
#include "processing.hpp"
#include "file_browser.hpp"
#include "benchmark_session.hpp"
#include "benchmark_results.hpp"
#include <set>

namespace {
using Cell = std::array<std::int64_t,3>;
struct Button { int x,y,w,h; std::string name; std::function<void()> action; bool active{}; };
std::string normalize_path(std::string path) {
    if(path.size()>=2 && path.front()=='"' && path.back()=='"')path=path.substr(1,path.size()-2);
    std::replace(path.begin(),path.end(),'\\','/');
#ifdef __linux__
    if(path.size()>2 && path[1]==':' && std::isalpha(static_cast<unsigned char>(path[0])))
        path="/mnt/"+std::string(1,static_cast<char>(std::tolower(static_cast<unsigned char>(path[0]))))+path.substr(2);
#endif
    return path;
}
class Player {
public:
    ~Player(){if(bench_progress_)bench_progress_->cancel=true;}
    explicit Player(cloudforge::app::Options options):options_(std::move(options)) {
        gpu_=cloudforge::cuda_backend_compiled();detail_=options_.detail;raw_view_=options_.results_samples;
        if(options_.backend=="cuda"&&!gpu_)throw std::runtime_error("CUDA not compiled; select CPU");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,2);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
        window_.handle=glfwCreateWindow(1600,980,"cloudForge | Point Cloud Studio",nullptr,nullptr);
        if(!window_.handle)throw std::runtime_error("Cannot open OpenGL window");
        glfwSetWindowSizeLimits(window_.handle,1280,800,GLFW_DONT_CARE,GLFW_DONT_CARE);
        glfwMakeContextCurrent(window_.handle);glfwSwapInterval(1);glfwSetWindowUserPointer(window_.handle,this);
        glfwSetKeyCallback(window_.handle,[](GLFWwindow* w,int key,int,int action,int mods){
            auto& player=*static_cast<Player*>(glfwGetWindowUserPointer(w));
            if(action==GLFW_PRESS||action==GLFW_REPEAT){
                try{player.key(key,mods);}catch(const std::exception& e){player.error_=e.what();}
            }
        });
        glfwSetCharCallback(window_.handle,[](GLFWwindow* w,unsigned int c){
            auto& p=*static_cast<Player*>(glfwGetWindowUserPointer(w));

            if(p.editing_&&c>=32&&c<127)p.path_text_+=static_cast<char>(c);
        });
        glfwSetDropCallback(window_.handle,[](GLFWwindow* w,int n,const char** paths){
            if(n>0)static_cast<Player*>(glfwGetWindowUserPointer(w))->choose(paths[0]);
        });
        glfwSetMouseButtonCallback(window_.handle,[](GLFWwindow* w,int button,int action,int mods){
            if(button!=GLFW_MOUSE_BUTTON_LEFT&&button!=GLFW_MOUSE_BUTTON_RIGHT&&button!=GLFW_MOUSE_BUTTON_MIDDLE)return;
            auto& p=*static_cast<Player*>(glfwGetWindowUserPointer(w));double x{},y{};glfwGetCursorPos(w,&x,&y);
            int ww{},wh{},fw{},fh{};glfwGetWindowSize(w,&ww,&wh);glfwGetFramebufferSize(w,&fw,&fh);
            x*=static_cast<double>(fw)/std::max(ww,1);y*=static_cast<double>(fh)/std::max(wh,1);
            p.last_x_=x;p.last_y_=y;
            if(action==GLFW_PRESS){
                if(button==GLFW_MOUSE_BUTTON_LEFT&&!(mods&GLFW_MOD_SHIFT))p.click(x,y);
                else if(!p.browser_&&!p.editing_&&x>270&&y>310){p.panning_=true;p.dragging_=false;}
            }else{p.dragging_=false;p.panning_=false;}
        });
        glfwSetCursorPosCallback(window_.handle,[](GLFWwindow* w,double x,double y){
            auto& p=*static_cast<Player*>(glfwGetWindowUserPointer(w));
            int ww{},wh{},fw{},fh{};glfwGetWindowSize(w,&ww,&wh);glfwGetFramebufferSize(w,&fw,&fh);
            x*=static_cast<double>(fw)/std::max(ww,1);y*=static_cast<double>(fh)/std::max(wh,1);
            if(p.dragging_){p.yaw_+=(x-p.last_x_)*0.4;p.pitch_=std::clamp(p.pitch_+(y-p.last_y_)*0.4,-175.0,175.0);}
            if(p.panning_){
                const double scale=2*2.6*p.zoom_*std::tan(3.141592653589793/6)/std::max(fh-348,1);
                p.pan_x_+=(x-p.last_x_)*scale;p.pan_y_-=(y-p.last_y_)*scale;
            }
            p.last_x_=x;p.last_y_=y;
        });
        glfwSetScrollCallback(window_.handle,[](GLFWwindow* w,double,double dy){
            auto& p=*static_cast<Player*>(glfwGetWindowUserPointer(w));
            if(p.browser_){if(dy<0)p.browser_offset_=std::min(p.browser_offset_+3,p.entries_.size());else p.browser_offset_=p.browser_offset_>3?p.browser_offset_-3:0;}
            else p.zoom_=std::clamp(p.zoom_*std::exp(-dy*0.1),0.05,20.0);
        });
        browser_dir_=std::filesystem::current_path();
        if(!options_.results_csv.empty()){
            show_benchmark();bench_fields_[5]=options_.results_csv.string();open_csv();
        }else if(!options_.benchmark_smoke.empty()){
            bench_smoke_pending_=true;bench_fields_[2]="3";bench_fields_[3]="10";choose(options_.benchmark_smoke.string());
        }else if(!options_.browser_smoke.empty()){
            browser_dir_=options_.browser_smoke;browser_=true;refresh_browser();
        }else if(!options_.input.empty())choose(options_.input.string());
        std::cout<<"OpenGL renderer: "<<glGetString(GL_RENDERER)<<std::endl;
    }
    int run(){
        while(!glfwWindowShouldClose(window_.handle)){
            glfwPollEvents();poll_browser();poll_source();poll_result();poll_benchmark();
            if(benchmark_window_.handle&&glfwWindowShouldClose(benchmark_window_.handle)){
                glfwSetWindowShouldClose(benchmark_window_.handle,0);glfwHideWindow(benchmark_window_.handle);benchmark_visible_=false;
            }
            if(bench_smoke_pending_&&result_&&!busy_&&!discovering_){bench_smoke_pending_=false;show_benchmark();benchmark_sequence_=frames_.size()>1;bench_fields_[1]=number(options_.voxel*2,6);start_benchmark();}
            if(options_.smoke_frames>1&&frames_.size()==1&&result_&&!busy_&&!discovering_&&!playback_smoke_expanded_){playback_smoke_expanded_=true;toggle_play();continue;}
            if(bench_running_){
                const auto title="cloudForge | Benchmark "+std::to_string(bench_progress_->completed.load())+" / "+std::to_string(bench_progress_->total.load())+" | Cancel available";
                glfwSetWindowTitle(benchmark_window_.handle,title.c_str());
                std::this_thread::sleep_for(std::chrono::milliseconds(16));continue;
            }
            const auto now=Clock::now();
            if(!options_.benchmark_smoke.empty()&&bench_report_&&!busy_&&!discovering_&&!bench_running_&&bench_smoke_runs_++==0){
                std::cout<<"First benchmark CSV: "<<bench_report_->csv<<" / rerunning the same loaded frame"<<std::endl;
                glfwHideWindow(benchmark_window_.handle);benchmark_visible_=false;show_benchmark();start_benchmark();continue;
            }
            if((!options_.benchmark_smoke.empty()||(!options_.results_csv.empty()&&!options_.screenshot.empty()))&&!bench_smoke_pending_&&!busy_&&!discovering_&&!bench_running_&&(bench_report_||!error_.empty())){
                if(!options_.benchmark_smoke.empty()&&result_frames_.size()==1){
                    int bw{},bh{};glfwGetFramebufferSize(benchmark_window_.handle,&bw,&bh);
                    bench_mouse_x_=(349+bw-48)/2.0-18;bench_mouse_y_=230+(bh-300)/2-51;
                }
                draw_benchmark_window();if(!options_.screenshot.empty()){
                    save_screenshot(benchmark_window_.handle,options_.screenshot);
                    auto main_image=options_.screenshot.parent_path()/(options_.screenshot.stem().string()+"-main.bmp");
                    draw();glfwSwapBuffers(window_.handle);save_screenshot(window_.handle,main_image);
                }
                if(bench_report_)std::cout<<"Completed benchmark CSV: "<<bench_report_->csv<<" / samples="<<bench_report_->samples<<std::endl;
                return bench_report_&&bench_report_->passed?0:2;
            }
            if((options_.smoke_frames||!options_.browser_smoke.empty())&&failed_)return 2;
            if(!options_.browser_smoke.empty()&&!browser_loading_&&now-browser_done_>std::chrono::seconds(1)){
                draw();if(!options_.screenshot.empty())save_screenshot(window_.handle,options_.screenshot);
                glfwSwapBuffers(window_.handle);
                std::cout<<"Browser entries="<<entries_.size()<<" UI frames while listing="<<browser_loading_frames_<<std::endl;
                return browser_loading_frames_>0?0:2;
            }
            if(options_.smoke_frames && loaded_>=(frames_.size()>1?options_.smoke_frames:1) && loaded_>0 && !busy_){
                if(now-last_done_>std::chrono::seconds(1)){draw();if(!options_.screenshot.empty())save_screenshot(window_.handle,options_.screenshot);glfwSwapBuffers(window_.handle);break;}
            }else if(playing_&&!busy_&&!discovering_&&now>=next_due_)advance(true);
            draw();glfwSwapBuffers(window_.handle);
            if(capture_pending_&&!options_.capture_dir.empty()){
                std::filesystem::create_directories(options_.capture_dir);
                std::ostringstream name;name<<"frame-"<<std::setw(5)<<std::setfill('0')<<capture_count_++<<".bmp";
                save_screenshot(window_.handle,options_.capture_dir/name.str());capture_pending_=false;
            }
            if(benchmark_visible_)draw_benchmark_window();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            if(browser_loading_)++browser_loading_frames_;
        }
        return failed_?2:0;
    }
private:
    void show_benchmark(){
        if(!benchmark_window_.handle){
            glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);
            benchmark_window_.handle=glfwCreateWindow(1100,780,"cloudForge | Benchmark and Results",nullptr,nullptr);
            if(!benchmark_window_.handle)throw std::runtime_error("Cannot open benchmark window");
            glfwSetWindowSizeLimits(benchmark_window_.handle,960,740,GLFW_DONT_CARE,GLFW_DONT_CARE);
            glfwSetWindowUserPointer(benchmark_window_.handle,this);
            glfwSetKeyCallback(benchmark_window_.handle,[](GLFWwindow* w,int key,int,int action,int mods){
                auto& p=*static_cast<Player*>(glfwGetWindowUserPointer(w));if(action!=GLFW_PRESS&&action!=GLFW_REPEAT)return;
                if(key==GLFW_KEY_ESCAPE){p.bench_field_=-1;return;}
                if(p.bench_field_<1||p.bench_field_>4||p.bench_running_)return;
                auto& value=p.bench_fields_[p.bench_field_];
                if(key==GLFW_KEY_BACKSPACE&&!value.empty())value.pop_back();
                else if(key==GLFW_KEY_ENTER)p.bench_field_=-1;
                else if((mods&GLFW_MOD_CONTROL)&&key==GLFW_KEY_A)value.clear();
                else if((mods&GLFW_MOD_CONTROL)&&key==GLFW_KEY_V){const auto* s=glfwGetClipboardString(w);if(s)value+=s;}
            });
            glfwSetCharCallback(benchmark_window_.handle,[](GLFWwindow* w,unsigned int c){auto& p=*static_cast<Player*>(glfwGetWindowUserPointer(w));if(p.bench_field_>=1&&p.bench_field_<=4&&!p.bench_running_&&c>=32&&c<127)p.bench_fields_[p.bench_field_]+=static_cast<char>(c);});
            glfwSetCursorPosCallback(benchmark_window_.handle,[](GLFWwindow* w,double x,double y){auto& p=*static_cast<Player*>(glfwGetWindowUserPointer(w));int ww{},wh{},fw{},fh{};glfwGetWindowSize(w,&ww,&wh);glfwGetFramebufferSize(w,&fw,&fh);p.bench_mouse_x_=x*fw/std::max(ww,1);p.bench_mouse_y_=y*fh/std::max(wh,1);});
            glfwSetMouseButtonCallback(benchmark_window_.handle,[](GLFWwindow* w,int button,int action,int){
                if(button!=GLFW_MOUSE_BUTTON_LEFT||action!=GLFW_PRESS)return;
                auto& p=*static_cast<Player*>(glfwGetWindowUserPointer(w));
                double x{},y{};int ww{},wh{},fw{},fh{};glfwGetCursorPos(w,&x,&y);glfwGetWindowSize(w,&ww,&wh);glfwGetFramebufferSize(w,&fw,&fh);
                p.bench_mouse_x_=x*fw/std::max(ww,1);p.bench_mouse_y_=y*fh/std::max(wh,1);
                for(const auto& b:p.benchmark_buttons_)if(p.bench_mouse_x_>=b.x&&p.bench_mouse_x_<b.x+b.w&&p.bench_mouse_y_>=b.y&&p.bench_mouse_y_<b.y+b.h){auto callback=b.action;callback();return;}
                p.bench_field_=-1;
            });
        }
        benchmark_visible_=true;playing_=false;editing_=false;bench_field_=-1;
        if(!bench_running_){bench_fields_[1]=number(options_.voxel,6);bench_fields_[0]=path_text_;}
        glfwShowWindow(benchmark_window_.handle);glfwFocusWindow(benchmark_window_.handle);
    }
    void draw_benchmark_window(){
        if(!benchmark_window_.handle)return;
        auto main_buttons=std::move(buttons_);const auto mx=last_x_,my=last_y_;
        last_x_=bench_mouse_x_;last_y_=bench_mouse_y_;
        draw(benchmark_window_.handle);glfwSwapBuffers(benchmark_window_.handle);
        benchmark_buttons_=std::move(buttons_);buttons_=std::move(main_buttons);last_x_=mx;last_y_=my;
    }
    void start_benchmark(){
        if(bench_running_||busy_||discovering_||browser_loading_)return;
        try{
            cloudforge::app::BenchmarkConfig config;
            if(!result_||frames_.empty())throw std::invalid_argument("Load a PCD file or sequence in the main window first");
            config.input=benchmark_sequence_?std::filesystem::path(path_text_):frames_[index_];config.voxel=cloudforge::app::positive_number(bench_fields_[1]);
            config.frames=cloudforge::app::positive_count(bench_fields_[2]);config.repeats=cloudforge::app::positive_count(bench_fields_[3]);
            config.warmups=cloudforge::app::positive_count(bench_fields_[4]);
            config.selected_frames=benchmark_sequence_?frames_:std::vector<std::filesystem::path>{frames_[index_]};
            if(!benchmark_sequence_)config.current_input=std::make_shared<const cloudforge::PointCloud>(result_->original);
            if(config.repeats<10)throw std::invalid_argument("Use at least 10 measured repeats for the benchmark tab");
            bench_progress_=std::make_shared<cloudforge::app::BenchmarkProgress>();bench_running_=true;results_tab_=false;bench_field_=-1;playing_=false;error_.clear();bench_report_.reset();
            benchmark_input_=config.input.string();benchmark_voxel_=config.voxel;benchmark_imported_=false;
            options_.voxel=config.voxel;
            draw();glfwSwapBuffers(window_.handle);draw_benchmark_window(); // Paint once, then suspend both windows during timing.
            bench_future_=worker_.post([config,progress=bench_progress_]{return cloudforge::app::benchmark(config,*progress);});
        }catch(const std::exception& e){bench_running_=false;error_=e.what();}
    }
    void poll_benchmark(){
        if(!bench_running_||!bench_future_.valid()||bench_future_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        bench_running_=false;glfwSetWindowTitle(benchmark_window_.handle,"cloudForge | Benchmark and Results");
        try{bench_report_=bench_future_.get();result_frames_=cloudforge::app::frame_timings(*bench_report_);results_tab_=true;results_frame_=0;raw_page_=0;
            if(!benchmark_imported_&&result_&&processed_voxel_!=options_.voxel)start(false);
        }catch(const std::exception& e){error_=e.what();}
    }
    void open_csv(){
        if(bench_running_||busy_||discovering_||browser_loading_)return;
        bench_field_=-1;error_.clear();playing_=false;
        const auto path=std::filesystem::path(normalize_path(bench_fields_[5]));
        benchmark_imported_=true;
        bench_progress_=std::make_shared<cloudforge::app::BenchmarkProgress>();bench_running_=true;
        draw_benchmark_window();
        bench_future_=worker_.post([path]{return cloudforge::app::load_benchmark_csv(path);});
    }
    void plot(int x,int y,int width,int height,bool speedup){
        const auto& report=*bench_report_;
        glColor3d(.045,.065,.095);rectangle(x,y,width,height);
        glColor3d(.8,.87,.95);text(x+12,y+12,speedup?"SPEEDUP BY FRAME / CPU MEDIAN DIVIDED BY CUDA MEDIAN":"LATENCY BY FRAME / MEDIAN WITH P10-P90 WHISKERS (MS)",1);
        glColor3d(.65,.75,.86);text(x+12,y+32,speedup?"Y: SPEEDUP (X TIMES) / HIGHER IS BETTER":"Y: PROCESSING TIME (MS) / LOWER IS BETTER",1);
        const int left=x+65,right=x+width-22,top=y+55,bottom=y+height-48;
        const auto n=result_frames_.size();if(!n)return;
        double maximum=speedup?1.2:0.001;
        for(const auto& f:result_frames_){
            if(speedup){if(f.gpu&&f.passed&&f.input_points&&f.cuda.median>0)maximum=std::max(maximum,f.cpu.median/f.cuda.median*1.1);}
            else maximum=std::max({maximum,f.cpu.p90*1.12,f.gpu?f.cuda.p90*1.12:0});
        }
        const auto px=[&](std::size_t i){return n==1?(left+right)/2.0:left+(right-left)*static_cast<double>(i)/(n-1);};
        const auto py=[&](double value){return bottom-(bottom-top)*value/maximum;};
        for(int i=0;i<=4;++i){const double value=maximum*i/4;
            glColor3d(.14,.19,.25);glBegin(GL_LINES);glVertex2d(left,py(value));glVertex2d(right,py(value));glEnd();
            glColor3d(.58,.68,.8);text(x+6,static_cast<int>(py(value))-3,number(value,2),1);
        }
        if(speedup){glColor3d(.8,.68,.35);glBegin(GL_LINES);glVertex2d(left,py(1));glVertex2d(right,py(1));glEnd();text(right-130,static_cast<int>(py(1))-12,"1X / EQUAL LATENCY",1);}
        const auto stride=std::max<std::size_t>(1,(n+299)/300);
        if(!speedup){for(int backend=0;backend<(report.gpu?2:1);++backend){
            if(backend)glColor3d(1,.64,.2);else glColor3d(.25,.68,1);
            if(n==1){const auto& d=backend?result_frames_[0].cuda:result_frames_[0].cpu;const double xx=px(0)+(backend?18:-18);rectangle(static_cast<int>(xx-12),static_cast<int>(py(d.median)),24,static_cast<int>(bottom-py(d.median)));}
            glBegin(GL_LINES);
            for(std::size_t i=0;i<n;i+=stride){const auto& f=result_frames_[i];const auto& d=backend?f.cuda:f.cpu;const double xx=px(i)+(n==1?(backend?18:-18):0);
                glVertex2d(xx,py(d.p10));glVertex2d(xx,py(d.p90));glVertex2d(xx-3,py(d.p10));glVertex2d(xx+3,py(d.p10));glVertex2d(xx-3,py(d.p90));glVertex2d(xx+3,py(d.p90));
            }glEnd();
            glBegin(GL_LINE_STRIP);for(std::size_t i=0;i<n;i+=stride){const auto& d=backend?result_frames_[i].cuda:result_frames_[i].cpu;glVertex2d(px(i),py(d.median));}glEnd();
            glPointSize(5);glBegin(GL_POINTS);for(std::size_t i=0;i<n;i+=stride){const auto& d=backend?result_frames_[i].cuda:result_frames_[i].cpu;glVertex2d(px(i)+(n==1?(backend?18:-18):0),py(d.median));}glEnd();
        }}else if(report.gpu&&report.passed&&!report.cancelled){
            glColor3d(.35,.87,.73);
            if(n==1&&result_frames_[0].input_points&&result_frames_[0].cuda.median>0){const double ratio=result_frames_[0].cpu.median/result_frames_[0].cuda.median;rectangle(static_cast<int>(px(0)-18),static_cast<int>(py(ratio)),36,static_cast<int>(bottom-py(ratio)));text(static_cast<int>(px(0)+26),static_cast<int>(py(ratio)),number(ratio,2)+"X",1);}
            glBegin(GL_LINE_STRIP);
            for(std::size_t i=0;i<n;i+=stride){const auto& f=result_frames_[i];if(f.input_points&&f.cuda.median>0)glVertex2d(px(i),py(f.cpu.median/f.cuda.median));}glEnd();
            glPointSize(5);glBegin(GL_POINTS);for(std::size_t i=0;i<n;i+=stride){const auto& f=result_frames_[i];if(f.input_points&&f.cuda.median>0)glVertex2d(px(i),py(f.cpu.median/f.cuda.median));}glEnd();
        }else{glColor3d(.8,.7,.5);text(left+20,top+35,"NO SPEEDUP CLAIM: CPU ONLY, FAILED CHECK, OR PARTIAL RUN",1);}
        glColor3d(.65,.74,.85);text(left,bottom+14,"1",1);text(right-35,bottom+14,std::to_string(n),1);
        text((left+right)/2-135,bottom+29,"X: FRAME NUMBER (TEMPORAL ORDER)",1);
        if(n==1)text(left+28,bottom+14,speedup?"SINGLE FRAME / CPU TIME DIVIDED BY CUDA TIME":"SINGLE FRAME / BLUE BAR = CPU / ORANGE BAR = CUDA",1);
        if(last_x_>=left&&last_x_<=right&&last_y_>=top&&last_y_<=bottom)
            results_frame_=n==1?0:static_cast<std::size_t>(std::llround((last_x_-left)/(right-left)*(n-1)));
        glColor3d(.4,.48,.58);glBegin(GL_LINES);glVertex2d(px(results_frame_),top);glVertex2d(px(results_frame_),bottom);glEnd();
        if(stride>1){glColor3d(.65,.74,.85);text(left+120,bottom+14,"DISPLAY STRIDE "+std::to_string(stride)+" / FULL DATA IN CSV",1);}
        std::string heading,detail;
        const auto& hovered=result_frames_[results_frame_];
        const bool inside=last_x_>=left&&last_x_<=right&&last_y_>=top&&last_y_<=bottom;
        if(inside&&!speedup){
            for(int backend=0;backend<(report.gpu?2:1);++backend){
                const auto& d=backend?hovered.cuda:hovered.cpu;
                const double xx=px(results_frame_)+(n==1?(backend?18:-18):0);
                const bool hit=n==1?std::abs(last_x_-xx)<=12&&last_y_>=py(d.median):std::abs(last_y_-py(d.median))<=14;
                if(hit){
                    heading=std::string(backend?"CUDA: ":"CPU: ")+number(d.median,4)+" MS (MEDIAN)";
                    detail="P10: "+number(d.p10,4)+" / P90: "+number(d.p90,4)+" MS";
                    break;
                }
            }
        }else if(inside&&speedup&&report.gpu&&report.passed&&!report.cancelled&&hovered.input_points&&hovered.cuda.median>0){
            const double ratio=hovered.cpu.median/hovered.cuda.median;
            const bool hit=n==1?std::abs(last_x_-px(0))<=18&&last_y_>=py(ratio):std::abs(last_y_-py(ratio))<=14;
            if(hit){heading="CPU TIME / CUDA TIME: "+number(ratio,4)+"X";
                detail=ratio>=1?"CUDA IS "+number(ratio,2)+"X FASTER":"CPU IS "+number(1/ratio,2)+"X FASTER";}
        }
        if(!heading.empty()){
            const int tw=static_cast<int>(std::max(heading.size(),detail.size())*6+24);
            const int tx=std::clamp(static_cast<int>(last_x_)+16,x+4,x+width-tw-4);
            const int ty=std::clamp(static_cast<int>(last_y_)-62,y+4,y+height-54);
            glColor3d(.24,.46,.58);rectangle(tx-1,ty-1,tw+2,50);
            glColor3d(.055,.10,.15);rectangle(tx,ty,tw,48);
            glColor3d(.94,.97,1);text(tx+12,ty+10,heading,1);
            glColor3d(.7,.81,.9);text(tx+12,ty+30,detail,1);
        }
    }
    void draw_results(int w,int h){
        const auto& r=*bench_report_;const int x=284,width=w-310;
        glColor3d(.9,.94,1);text(x,26,"BENCHMARK RESULTS / "+std::string(raw_view_?"CSV SAMPLES":"GRAPHS"),2);
        glColor3d(.65,.75,.86);auto path=r.csv.string();if(path.size()>static_cast<std::size_t>((width-20)/6))path="..."+path.substr(path.size()-static_cast<std::size_t>((width-40)/6));text(x,65,path,1);
        add_button(x,88,110,"GRAPHS",[this]{raw_view_=false;},!raw_view_);
        add_button(x+122,88,130,"CSV SAMPLES",[this]{raw_view_=true;},raw_view_);
        std::string verdict=r.cancelled?"PARTIAL RUN":!r.passed?"CORRECTNESS FAILED":r.gpu?"ALL CHECKS PASSED":"CPU ONLY";
        const bool has_points=std::any_of(r.raw.begin(),r.raw.end(),[](const auto& s){return s.input_points>0;});
        if(r.gpu&&r.passed&&!r.cancelled&&r.cuda.median>0&&r.cpu.median>0&&has_points){
            const double ratio=r.cpu.median/r.cuda.median;
            verdict=std::abs(ratio-1)<.01?"CPU AND CUDA TAKE ABOUT THE SAME TIME":ratio>1?"CUDA IS "+number(ratio,2)+"X FASTER / ALL FRAMES":"CPU IS "+number(1/ratio,2)+"X FASTER / ALL FRAMES";
        }
        glColor3d(.8,.87,.95);text(x+275,101,verdict,1);
        glColor3d(.25,.68,1);text(x,145,"CPU MEDIAN "+number(r.cpu.median)+" MS",1);
        glColor3d(1,.64,.2);text(x+235,145,r.gpu?"CUDA MEDIAN "+number(r.cuda.median)+" MS":"CUDA NOT MEASURED",1);
        glColor3d(.35,.87,.73);text(x+490,145,std::to_string(r.frames)+" FRAMES / "+std::to_string(r.samples)+" SAMPLES",1);
        if(raw_view_){
            glColor3d(.65,.75,.86);text(x,185,"FRAME FILE                      REP   INPUT     OUTPUT    ORDER        CPU MS     CUDA MS    CHECK",1);
            const auto rows=static_cast<std::size_t>(std::max(1,(h-290)/28));
            for(std::size_t i=0;i<rows&&raw_page_+i<r.raw.size();++i){const auto& s=r.raw[raw_page_+i];
                glColor3d(i%2?.07:.055,.09,.13);rectangle(x,208+static_cast<int>(i)*28,width,26);
                std::ostringstream line;line<<std::left<<std::setw(32)<<std::filesystem::path(s.frame).filename().string().substr(0,30)<<std::setw(6)<<s.repeat
                    <<std::setw(10)<<s.input_points<<std::setw(10)<<s.cpu_points<<std::setw(13)<<s.order<<std::setw(11)<<number(s.cpu_ms)
                    <<std::setw(11)<<(s.gpu?number(s.cuda_ms):"N/A")<<(s.gpu?(s.passed?"PASS":"FAIL"):"CPU_ONLY");
                glColor3d(.8,.86,.94);text(x+8,217+static_cast<int>(i)*28,line.str(),1);
            }
            add_button(x,h-65,110,"PREVIOUS",[this,rows]{raw_page_=raw_page_>rows?raw_page_-rows:0;});
            add_button(x+122,h-65,110,"NEXT PAGE",[this,rows]{if(raw_page_+rows<bench_report_->raw.size())raw_page_+=rows;});
            glColor3d(.65,.75,.86);text(x+250,h-50,"RAW ROW "+std::to_string(r.raw.empty()?0:raw_page_+1)+" / "+std::to_string(r.raw.size())+" / TIMES IN MILLISECONDS",1);
        }else{
            if(!result_frames_.empty()){
                const auto& f=result_frames_[results_frame_];
                glColor3d(.8,.86,.94);auto name=std::filesystem::path(f.frame).filename().string();
                text(x,181,"FRAME "+std::to_string(results_frame_+1)+" / "+name+" / "+std::to_string(f.input_points)+" -> "+std::to_string(f.output_points)+" POINTS",1);
                text(x,202,"HOVER A FRAME: CPU "+number(f.cpu.median)+" MS / CUDA "+(f.gpu?number(f.cuda.median)+" MS":"N/A")+" / "+std::to_string(f.samples)+" REPEATS",1);
            }
            const int ph=(h-300)/2;
            plot(x,230,width,ph,false);plot(x,245+ph,width,ph,true);
            glColor3d(.65,.75,.86);text(x,h-20,"BLUE: CPU / ORANGE: CUDA / GREEN: SPEEDUP (>1 FAVORS CUDA). P10-P90 IS VARIATION, NOT CONFIDENCE.",1);
        }
        if(!error_.empty()){glColor3d(1,.4,.35);text(x,h-8,error_.substr(0,140),1);}
    }
    void draw_benchmark(int w,int h){
        glColor3d(.35,.82,.9);text(18,22,"BENCHMARK",2);
        glColor3d(.65,.75,.86);text(18,52,"USES MAIN WINDOW INPUT",1);
        add_button(16,85,105,"THIS FRAME",[this]{if(!bench_running_)benchmark_sequence_=false;},!benchmark_sequence_);
        add_button(132,85,106,"SEQUENCE",[this]{if(!bench_running_)benchmark_sequence_=true;},benchmark_sequence_);
        const auto source=frames_.empty()?std::string("LOAD INPUT IN MAIN WINDOW"):benchmark_sequence_?std::to_string(frames_.size())+" LOADED FRAMES":frames_[index_].filename().string();
        glColor3d(.65,.75,.86);text(18,135,source.substr(0,36),1);
        constexpr std::array<const char*,4> labels{"VOXEL SIDE LENGTH","MAX SEQUENCE FRAMES","REPEATS PER FRAME (MIN 10)","WARM-UPS PER BACKEND"};
        for(int i=1;i<=4;++i){const int y=165+(i-1)*80;
            glColor3d(.65,.75,.86);text(18,y,labels[i-1],1);
            add_button(16,y+20,222,bench_fields_[i]+(bench_field_==i?"_":""),[this,i]{if(!bench_running_)bench_field_=i;},bench_field_==i);
        }
        add_button(16,510,222,bench_running_?"CANCEL AFTER CURRENT CALL":"RUN AGAIN / BENCHMARK",[this]{if(bench_running_)bench_progress_->cancel=true;else start_benchmark();},true,44);
        glColor3d(.65,.75,.86);text(18,580,"CSV SAVED AUTOMATICALLY",1);text(18,602,"EVERY CLICK IS A NEW RUN",1);
        text(18,635,"RESULTS UPDATE MAIN SCREEN",1);
        text(18,h-80,"BOTH WINDOWS PAUSE DRAWING",1);text(18,h-58,"DURING MEASUREMENTS",1);text(18,h-36,"PROGRESS IN WINDOW TITLE",1);
        if(bench_report_)draw_results(w,h);
        else{
            glColor3d(.9,.94,1);text(284,26,"BENCHMARK AND RESULTS",2);
            glColor3d(.65,.75,.86);text(284,110,bench_running_?"MEASURING THE LOADED INPUT...":"SET PARAMETERS ON THE LEFT, THEN RUN BENCHMARK.",1);
            text(284,155,"TOP GRAPH: X = FRAME NUMBER; Y = PROCESSING TIME IN MILLISECONDS.",1);
            text(284,185,"LOWER GRAPH: X = FRAME NUMBER; Y = CPU TIME / CUDA TIME (SPEEDUP).",1);
            text(284,215,"LOWER LATENCY IS BETTER. SPEEDUP ABOVE 1X FAVORS CUDA.",1);
            text(284,245,"ONE FRAME USES BARS; A SEQUENCE USES CURVES IN TEMPORAL ORDER.",1);
            text(284,285,"INCLUDES VALIDATION, TRANSFERS AND GPU COMPLETION. EXCLUDES LOADING AND RENDERING.",1);
        }
        if(bench_running_){glColor3d(1,.78,.3);text(284,h-48,"RUNNING / EVENT HANDLING ACTIVE / DRAWING PAUSED TO LIMIT TIMING INTERFERENCE",1);}
        if(!error_.empty()){glColor3d(1,.4,.35);text(284,h-25,error_.substr(0,150),1);}
    }
    void choose(const std::string& value){
        if(bench_running_)return;
        if(busy_||discovering_)return;
        try{
            auto path=std::filesystem::path(normalize_path(value));
            pending_path_=path.string();
            source_future_=worker_.post([path,limit=pending_play_file_.empty()?options_.frames:1000000]{return cloudforge::app::discover_source(path,limit);});
            discovering_=true;browser_=false;editing_=false;playing_=false;error_.clear();failed_=false;
        }catch(const std::exception& e){error_=e.what();if(options_.smoke_frames)failed_=true;}
    }
    void poll_source(){
        if(!discovering_||source_future_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        discovering_=false;
        try{
            auto source=source_future_.get();frames_=std::move(source.frames);browser_dir_=std::move(source.browser_directory);
            bench_report_.reset();result_frames_.clear();results_frame_=0;raw_page_=0;
            index_=displayed_index_=0;loaded_=0;result_.reset();path_text_=pending_path_;
            const bool play_requested=!pending_play_file_.empty();
            if(play_requested){
                const auto found=std::find(frames_.begin(),frames_.end(),pending_play_file_);
                if(found!=frames_.end())index_=displayed_index_=static_cast<std::size_t>(found-frames_.begin());
                pending_play_file_.clear();
            }
            playing_=options_.smoke_frames>0||(frames_.size()>1&&!bench_smoke_pending_);
            if(play_requested&&frames_.size()<2){playing_=false;playback_notice_="ONLY ONE PCD FRAME IN THIS FOLDER. ANIMATION NEEDS MULTIPLE RECORDED FRAMES.";}
            else playback_notice_.clear();
            if(frames_.size()>1)detail_=false;
            start(false,frames_.size()>1);
        }catch(const std::exception& e){error_=e.what();failed_=true;}
    }
    void start(bool compare_both,bool streaming=false){
        if(busy_||discovering_||frames_.empty())return;
        busy_=true;comparison_requested_=compare_both;error_.clear();failed_=false;
        submitted_=Clock::now();submitted_voxel_=options_.voxel;submitted_backend_=options_.backend;
        submitted_stream_=streaming;
        const auto backend=options_.backend=="cuda"?cloudforge::Backend::cuda:cloudforge::Backend::cpu;
        if(streaming)future_=worker_.post([path=frames_[index_],voxel=options_.voxel,backend]{return cloudforge::app::execute_frame(path,voxel,backend);});
        else future_=worker_.submit(frames_[index_],options_.voxel,backend,compare_both,options_.repeats);
    }
    void poll_result(){
        if(!busy_||future_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        busy_=false;
        try{
            auto next=future_.get();result_=std::make_unique<cloudforge::app::ProcessingResult>(std::move(next));
            refit_scene_=loaded_==0||frames_[displayed_index_].parent_path()!=frames_[index_].parent_path();
            displayed_index_=index_;displayed_stream_=submitted_stream_;
            capture_pending_=!options_.capture_dir.empty();
            processed_voxel_=submitted_voxel_;processed_backend_=submitted_backend_;
            if(result_->compared&&!result_->comparison.passed){failed_=true;playing_=false;error_="CPU/GPU correctness check failed. No timing winner accepted.";}
            prepare_scene();
            const auto done=Clock::now();if(loaded_>0)actual_fps_=1.0/std::chrono::duration<double>(done-last_done_).count();
            last_done_=done;++loaded_;next_due_=submitted_+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1/options_.playback_fps));
            std::cout<<"Processed frame "<<index_+1<<'/'<<frames_.size()<<" backend="<<processed_backend_
                     <<" compared="<<result_->compared<<" input="<<result_->original.size()<<" simplified="<<selected().points.size();
            if(result_->compared)std::cout<<" CPU_ms="<<result_->cpu.milliseconds<<" CUDA_ms="<<result_->cuda.milliseconds<<" comparison="<<(result_->comparison.passed?"PASS":"FAIL");
            std::cout<<std::endl;
        }catch(const std::exception& e){error_=e.what();failed_=true;playing_=false;}
    }
    const cloudforge::Measurement& selected()const{return processed_backend_=="cuda"?result_->cuda:result_->cpu;}
    void prepare_scene(){
        bins_.clear();cells_.clear();local_original_.clear();local_simplified_.clear();
        cloudforge::Point low{},high{};
        if(!result_->original.empty())low=high=result_->original.front();
        for(const auto& p:result_->original){
            low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);
            high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);
            const Cell cell{static_cast<std::int64_t>(std::floor(p.x/processed_voxel_)),static_cast<std::int64_t>(std::floor(p.y/processed_voxel_)),static_cast<std::int64_t>(std::floor(p.z/processed_voxel_))};
            ++bins_[cell];
        }
        if(refit_scene_){overview_center_={(low.x+high.x)/2,(low.y+high.y)/2,(low.z+high.z)/2};
            overview_radius_=std::max({high.x-low.x,high.y-low.y,high.z-low.z,1.0})*0.65;}
        std::size_t best{};Cell dense{};
        for(const auto& [cell,count]:bins_){
            const double x=(static_cast<double>(cell[0])+0.5)*processed_voxel_,y=(static_cast<double>(cell[1])+0.5)*processed_voxel_,z=(static_cast<double>(cell[2])+0.5)*processed_voxel_;
            if(count>best&&x*x+y*y+z*z>1){best=count;dense=cell;}
        }
        if(best==0&&!bins_.empty())dense=bins_.begin()->first;
        if(refit_scene_)detail_center_={(static_cast<double>(dense[0])+0.5)*processed_voxel_,(static_cast<double>(dense[1])+0.5)*processed_voxel_,(static_cast<double>(dense[2])+0.5)*processed_voxel_};
        const double half=4.5*processed_voxel_;
        const auto inside=[&](const cloudforge::Point& p){return std::abs(p.x-detail_center_.x)<=half&&std::abs(p.y-detail_center_.y)<=half&&std::abs(p.z-detail_center_.z)<=half;};
        for(const auto& p:result_->original)if(inside(p))local_original_.push_back(p);
        for(const auto& p:selected().points)if(inside(p))local_simplified_.push_back(p);
        for(const auto& [cell,count]:bins_){static_cast<void>(count);cloudforge::Point c{(static_cast<double>(cell[0])+0.5)*processed_voxel_,(static_cast<double>(cell[1])+0.5)*processed_voxel_,(static_cast<double>(cell[2])+0.5)*processed_voxel_};if(inside(c))cells_.push_back(cell);}
        if(loaded_==0){yaw_=0;pitch_=-55;zoom_=detail_?1.0:0.45;pan_x_=pan_y_=0;}
    }
    void advance(bool playback=false){
        if(frames_.empty()||busy_||discovering_)return;
        if(index_+1>=frames_.size()){
            if(playback&&loop_&&frames_.size()>1)index_=0;
            else{playing_=false;return;}
        }else ++index_;
        start(false,true);
    }
    void toggle_play(){
        if(discovering_||frames_.empty())return;
        if(frames_.size()==1){
            if(busy_)return;
            pending_play_file_=frames_.front();auto parent=pending_play_file_.parent_path();
            if(parent.empty())parent=std::filesystem::current_path();
            playback_notice_="LOADING THE OTHER RECORDED FRAMES FROM THIS FILE'S FOLDER...";
            choose(parent.string());return;
        }
        playback_notice_.clear();
        if(playing_){playing_=false;return;}
        playing_=true;next_due_=Clock::now();last_done_=Clock::now();actual_fps_=0;
        if(!busy_&&index_+1==frames_.size()){index_=0;start(false,true);}
    }
    void seek_frame(std::size_t index){if(busy_||discovering_||frames_.empty())return;playing_=false;index_=std::min(index,frames_.size()-1);start(false,true);}
    void set_backend(const std::string& backend){if(busy_||discovering_||(!gpu_&&backend=="cuda"))return;options_.backend=backend;playing_=false;if(!frames_.empty())start(false);}
    void voxel(double multiplier){if(busy_||discovering_)return;options_.voxel=std::clamp(options_.voxel*multiplier,0.000001,1000.0);playing_=false;}
    void browse(){if(busy_||discovering_)return;browser_=true;editing_=false;playing_=false;refresh_browser();}
    void refresh_browser(){
        if(browser_loading_)return;
        entries_.clear();browser_offset_=0;browser_loading_frames_=0;
        const auto directory=browser_dir_;
        browser_future_=worker_.post([directory]{return cloudforge::app::read_directory(directory);});
        browser_loading_=true;
    }
    void poll_browser(){
        if(!browser_loading_||browser_future_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        browser_loading_=false;browser_done_=Clock::now();
        try{entries_=browser_future_.get();}
        catch(const std::exception& e){error_=e.what();if(!options_.browser_smoke.empty())failed_=true;}
    }
    void key(int key,int mods){
        if(bench_running_){if(key==GLFW_KEY_C)show_benchmark();return;}
        if(key==GLFW_KEY_ESCAPE){if(browser_){browser_=false;return;}if(editing_){editing_=false;return;}glfwSetWindowShouldClose(window_.handle,1);return;}
        if(editing_){
            if(key==GLFW_KEY_BACKSPACE&&!path_text_.empty())path_text_.pop_back();
            else if(key==GLFW_KEY_ENTER)choose(path_text_);
            else if((mods&GLFW_MOD_CONTROL)&&key==GLFW_KEY_V){const auto* value=glfwGetClipboardString(window_.handle);if(value)path_text_=value;}
            else if((mods&GLFW_MOD_CONTROL)&&key==GLFW_KEY_A)path_text_.clear();
            return;
        }
        if(key==GLFW_KEY_O){browse();return;}if(browser_)return;
        if(key==GLFW_KEY_SPACE)toggle_play();
        else if(key==GLFW_KEY_N&&!busy_&&!discovering_){playing_=false;advance();}
        else if(key==GLFW_KEY_B)set_backend(options_.backend=="cpu"?"cuda":"cpu");
        else if(key==GLFW_KEY_ENTER)start(false);
        else if(key==GLFW_KEY_C){if(!busy_&&!discovering_)show_benchmark();}
        else if(key==GLFW_KEY_V){detail_=!detail_;grid_=true;zoom_=detail_?1:0.45;}
        else if(key==GLFW_KEY_G)grid_=!grid_;
        else if(key==GLFW_KEY_R){yaw_=0;pitch_=-55;zoom_=detail_?1:0.45;pan_x_=pan_y_=0;}
        else if(key==GLFW_KEY_EQUAL||key==GLFW_KEY_KP_ADD)voxel(2);
        else if(key==GLFW_KEY_MINUS||key==GLFW_KEY_KP_SUBTRACT)voxel(0.5);
        else if(key==GLFW_KEY_S){draw();save_screenshot(window_.handle,options_.screenshot.empty()?std::filesystem::path("build/cloudforge.bmp"):options_.screenshot);}
    }
    void click(double x,double y){
        if(bench_running_){show_benchmark();return;}

        if(!browser_&&x>270&&y>60&&y<98&&!busy_&&!discovering_){editing_=true;return;}
        editing_=false;
        for(const auto& b:buttons_)if(x>=b.x&&x<b.x+b.w&&y>=b.y&&y<b.y+b.h){b.action();return;}
        dragging_=!browser_&&x>270&&y>310;
    }
    void add_button(int x,int y,int w,const std::string& name,std::function<void()> action,bool active=false,int h=36){
        buttons_.push_back({x,y,w,h,name,std::move(action),active});
        if(active)glColor3d(0.13,0.42,0.54);else glColor3d(0.12,0.16,0.23);rectangle(x,y,w,h);
        glColor3d(0.91,0.95,1);text(x+10,y+(h-7)/2,name,1);
    }
    void wire_cell(const Cell& cell){
        const double x=static_cast<double>(cell[0])*processed_voxel_,y=static_cast<double>(cell[1])*processed_voxel_,z=static_cast<double>(cell[2])*processed_voxel_,s=processed_voxel_;
        const std::array<cloudforge::Point,8> p{{{x,y,z},{x+s,y,z},{x+s,y+s,z},{x,y+s,z},{x,y,z+s},{x+s,y,z+s},{x+s,y+s,z+s},{x,y+s,z+s}}};
        constexpr std::array<std::array<int,2>,12> edges{{{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}}};
        for(const auto& e:edges){glVertex3d(p[e[0]].x,p[e[0]].y,p[e[0]].z);glVertex3d(p[e[1]].x,p[e[1]].y,p[e[1]].z);}
    }
    void scene(const cloudforge::PointCloud& points,int x,int width,int height,bool original){
        glViewport(x,110,width,height);glEnable(GL_DEPTH_TEST);
        const auto center=detail_?detail_center_:overview_center_;
        const double radius=detail_?processed_voxel_*7:overview_radius_;
        const double near=std::max(0.0000001,radius*0.001),top=near*std::tan(3.141592653589793/6);
        const double aspect=static_cast<double>(width)/std::max(height,1);
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-top*aspect,top*aspect,-top,top,near,radius*100);
        glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslated(radius*pan_x_,radius*pan_y_,-radius*2.6*zoom_);glRotated(pitch_,1,0,0);glRotated(yaw_,0,0,1);glTranslated(-center.x,-center.y,-center.z);
        if(grid_){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColor4d(0.48,0.62,0.72,0.45);glBegin(GL_LINES);for(const auto& cell:cells_)wire_cell(cell);glEnd();glDisable(GL_BLEND);}
        if(original)glColor3d(0.25,0.68,1);else glColor3d(1,0.64,0.2);
        glPointSize(detail_?4.0f:2.0f);glBegin(GL_POINTS);for(const auto& p:points)glVertex3d(p.x,p.y,p.z);glEnd();
    }
    void card(int x,int y,int w,const std::string& title,const std::string& value,const std::string& note){
        glColor3d(0.07,0.10,0.15);rectangle(x,y,w,80);glColor3d(0.59,0.68,0.79);text(x+12,y+12,title,1);
        glColor3d(0.93,0.96,1);text(x+12,y+32,value,2);glColor3d(0.59,0.68,0.79);text(x+12,y+60,note,1);
    }
    void draw(GLFWwindow* target=nullptr){
        if(!target)target=window_.handle;
        glfwMakeContextCurrent(target);benchmark_tab_=target==benchmark_window_.handle;
        int w{},h{};glfwGetFramebufferSize(target,&w,&h);if(w<=0||h<=0){benchmark_tab_=false;return;}
        const int left=274,half=(w-left-18)/2,scene_height=std::max(1,h-422);
        glClearColor(0.025f,0.035f,0.055f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        if(result_&&!benchmark_tab_){scene(detail_?local_original_:result_->original,left,half,scene_height,true);scene(detail_?local_simplified_:selected().points,left+half+12,half,scene_height,false);}
        glViewport(0,0,w,h);glDisable(GL_DEPTH_TEST);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,w,h,0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();buttons_.clear();
        glColor3d(0.055,0.075,0.11);rectangle(0,0,254,h);rectangle(254,0,w-254,110);
        if(benchmark_tab_){draw_benchmark(w,h);benchmark_tab_=false;return;}
        glColor3d(0.35,0.82,0.9);text(18,22,"CLOUDFORGE",2);glColor3d(0.60,0.69,0.79);text(18,48,"POINT CLOUD STUDIO",1);
        text(18,96,"1 / INPUT",1);
        add_button(16,120,222,"OPEN PCD / FOLDER",[this]{browse();});
        glColor3d(0.65,0.73,0.83);text(18,171,"OR DROP A PCD FILE HERE",1);
        text(18,212,"2 / PROCESSING BACKEND",1);
        add_button(16,236,105,"CPU",[this]{set_backend("cpu");},options_.backend=="cpu");
        add_button(132,236,106,gpu_?"CUDA":"NO CUDA",[this]{set_backend("cuda");},options_.backend=="cuda");
        glColor3d(0.65,0.73,0.83);text(18,298,"VOXEL SIDE LENGTH",1);text(18,321,number(options_.voxel,4),2);
        add_button(16,350,105,"- SMALLER",[this]{voxel(0.5);});add_button(132,350,106,"+ LARGER",[this]{voxel(2);});
        add_button(16,410,222,busy_||discovering_?"PROCESSING...":"PROCESS SELECTED",[this]{start(false);},true,44);
        add_button(16,466,222,"BENCHMARK WINDOW",[this]{if(!busy_&&!discovering_)show_benchmark();},false,44);
        glColor3d(0.65,0.73,0.83);text(18,527,"3 / INSPECT",1);
        add_button(16,550,222,detail_?"VOXEL DETAIL: ON":"VOXEL DETAIL: OFF",[this]{detail_=!detail_;grid_=true;zoom_=detail_?1:0.45;},detail_);
        add_button(16,596,222,grid_?"CELL WIREFRAMES: ON":"CELL WIREFRAMES: OFF",[this]{grid_=!grid_;},grid_);
        add_button(16,642,222,"RESET CAMERA",[this]{key(GLFW_KEY_R,0);});
        glColor3d(.65,.73,.83);text(18,685,"PLAYBACK CONTROLS BELOW VIEWS",1);
        glColor3d(0.6,0.69,0.8);text(18,h-100,"RIGHT DRAG / SHIFT DRAG: PAN",1);text(18,h-78,"LEFT: ORBIT / WHEEL: ZOOM",1);text(18,h-56,"ENTER: PROCESS / C: BENCHMARK",1);text(18,h-34,"V: DETAIL / G: GRID / S: SAVE",1);
        glColor3d(0.91,0.95,1);text(left,22,"INSPECT / ORIGINAL AND CENTROIDS",2);
        glColor3d(0.35,0.82,0.9);text(w-190,25,busy_||discovering_?"PROCESSING":playing_?"PLAYING / "+number(actual_fps_,1)+" FPS":"READY / PAUSED",1);
        glColor3d(editing_?0.12:0.08,editing_?0.21:0.12,editing_?0.29:0.18);rectangle(left,60,w-left-18,38);
        glColor3d(0.72,0.82,0.92);
        const auto visible=static_cast<std::size_t>(std::max(10,(w-left-45)/6));
        std::string shown=path_text_.empty()?"CLICK TO TYPE OR PASTE A PCD FILE / DIRECTORY PATH":path_text_;
        if(shown.size()>visible)shown="..."+shown.substr(shown.size()-visible+3);
        text(left+10,75,shown+(editing_?"_":""),1);
        if(!result_){
            glColor3d(0.9,0.94,1);text(left+30,190,busy_||discovering_?"LOADING / PROCESSING YOUR INPUT...":"START WITH A RECORDED LIDAR FRAME",2);
            glColor3d(0.62,0.72,0.84);text(left+30,235,"1. SELECT CPU OR CUDA ON THE LEFT.",1);text(left+30,265,"2. OPEN A .PCD FILE OR FOLDER, DROP IT HERE, OR PASTE ITS PATH.",1);
            text(left+30,295,"3. PROCESS TO INSPECT; OPEN BENCHMARK WINDOW FOR REPEATED TIMING.",1);
            text(left+30,335,"VOXEL DETAIL SHOWS REAL OCCUPIED CELLS AND THEIR CENTROIDS.",1);
        }else{
            const auto& r=*result_;const int cw=(w-left-40)/3;
            card(left,122,cw,"INPUT POINTS",std::to_string(r.original.size()),"RECORDED XYZ / UNCHANGED");
            card(left+cw+12,122,cw,"OUTPUT CENTROIDS",std::to_string(selected().points.size()),"ONE POINT PER OCCUPIED VOXEL");
            card(left+2*(cw+12),122,cw,"POINT REDUCTION",number(cloudforge::reduction_percent(r.original.size(),selected().points.size()),2)+"%","VOXEL SIZE "+number(processed_voxel_,4));
            glColor3d(0.76,0.84,0.94);
            const cloudforge::app::FrameTiming* measured=nullptr;
            if(bench_report_&&!benchmark_imported_&&!bench_report_->cancelled&&benchmark_voxel_==processed_voxel_&&!frames_.empty())
                for(const auto& f:result_frames_)if(f.frame==frames_[displayed_index_].string()){measured=&f;break;}
            if(measured)text(left,220,"BENCHMARK / THIS FRAME: CPU "+number(measured->cpu.median)+" MS / CUDA "+(measured->gpu?number(measured->cuda.median)+" MS":"N/A")+" / "+std::to_string(measured->samples)+" REPEATS / VOXEL "+number(benchmark_voxel_,4),1);
            else text(left,220,std::string(displayed_stream_?"PLAYBACK / SINGLE CALL: ":"QUICK ESTIMATE: ")+"CPU "+(r.has_cpu?number(r.cpu.milliseconds)+" MS":"NOT RUN")+"   CUDA "+(r.has_cuda?number(r.cuda.milliseconds)+" MS":"NOT RUN")+(displayed_stream_?" / NO BENCHMARK REPEATS":" / "+std::to_string(options_.repeats)+" REPEATS / WARM MEDIAN"),1);
            std::string verdict="SELECTED "+processed_backend_+" / OPEN BENCHMARK WINDOW FOR A REPEATED COMPARISON.";
            if(r.compared){const double ratio=r.cpu.milliseconds/r.cuda.milliseconds;
                verdict=!r.comparison.passed?"CORRECTNESS FAIL - NO WINNER":std::abs(ratio-1)<0.01?"CPU AND CUDA ARE WITHIN 1%":ratio>1?"CUDA IS "+number(ratio,2)+"X FASTER THAN CPU":"CPU IS "+number(1/ratio,2)+"X FASTER THAN CUDA";}
            if(r.original.empty())verdict="EMPTY FRAME - NO PROCESSING WORK / NO WINNER";
            if(measured){
                verdict=!measured->passed?"BENCHMARK CORRECTNESS FAILED":!measured->gpu?"BENCHMARK COMPLETE / CPU ONLY":"BENCHMARK COMPLETE / NO SPEEDUP CLAIM";
                if(measured->passed&&measured->gpu&&measured->input_points&&measured->cuda.median>0&&measured->cpu.median>0){
                    const double ratio=measured->cpu.median/measured->cuda.median;
                    verdict=std::abs(ratio-1)<.01?"CPU AND CUDA TAKE ABOUT THE SAME TIME":ratio>1?"CUDA IS "+number(ratio,2)+"X FASTER THAN CPU":"CPU IS "+number(1/ratio,2)+"X FASTER THAN CUDA";
                }
            }
            glColor3d(0.35,0.87,0.73);text(left,246,verdict,2);
            glColor3d(0.65,0.73,0.83);
            std::ostringstream evidence;
            if(r.compared)evidence<<"   CHECK "<<(r.comparison.passed?"PASS":"FAIL")<<" / MAX ERROR "<<std::scientific<<std::setprecision(2)<<r.comparison.max_error;
            const double latency=measured?(processed_backend_=="cuda"&&measured->gpu?measured->cuda.median:measured->cpu.median):selected().milliseconds;
            text(left,278,"FRAME "+std::to_string(displayed_index_+1)+" / "+std::to_string(frames_.size())+"  LOAD "+number(r.loading_ms,1)+" MS  JOB "+number(r.total_ms,1)+" MS  PROCESSING "+number(latency>0?1000/latency:0,1)+" FPS"+evidence.str(),1);
            glColor3d(0.25,0.68,1);text(left,304,"ORIGINAL"+std::string(detail_?" / LOCAL DETAIL":" / FULL CLOUD"),1);
            glColor3d(1,0.64,0.2);text(left+half+12,304,"CENTROIDS / "+processed_backend_+(detail_?" / SAME REGION":" / FULL CLOUD"),1);
            glColor3d(0.60,0.70,0.82);text(left,h-24,detail_?"LOCAL CROP: "+std::to_string(local_original_.size())+" INPUT -> "+std::to_string(local_simplified_.size())+" CENTROIDS. "+std::to_string(cells_.size())+" OCCUPIED CELL WIREFRAMES. GLOBAL COUNTS ABOVE.":"SAME CAMERA AND POINT SIZE. INCREASE VOXEL SIZE, PROCESS, THEN ENABLE VOXEL DETAIL TO INSPECT DENSITY.",1);
        }
        if(busy_||discovering_){glColor3d(0.13,0.35,0.45);rectangle(left,h-62,w-left-18,22);glColor3d(0.9,0.97,1);text(left+8,h-55,discovering_?"READING FRAME LIST IN BACKGROUND...":"WORKING ON "+submitted_backend_+(comparison_requested_?" + COMPARISON":" ONLY")+" / UI REMAINS RESPONSIVE",1);}
        else if(result_&&options_.voxel!=processed_voxel_){glColor3d(1,0.78,0.3);text(left,h-55,"VOXEL SIZE CHANGED. CLICK PROCESS SELECTED TO APPLY.",1);}
        if(!error_.empty()){glColor3d(1,0.4,0.35);text(left,h-42,error_.substr(0,150),1);}
        glColor3d(.055,.075,.11);rectangle(left,h-104,w-left-18,52);
        add_button(left+8,h-96,90,playing_?"PAUSE":frames_.size()==1?"PLAY CLIP":"PLAY",[this]{toggle_play();},playing_);
        add_button(left+108,h-96,90,"PREVIOUS",[this]{seek_frame(displayed_index_?displayed_index_-1:0);});
        add_button(left+208,h-96,80,"NEXT",[this]{playing_=false;advance();});
        add_button(left+298,h-96,90,"RESTART",[this]{seek_frame(0);});
        add_button(left+398,h-96,100,loop_?"LOOP: ON":"LOOP: OFF",[this]{loop_=!loop_;},loop_);
        add_button(left+508,h-96,40,"-",[this]{options_.playback_fps=std::max(1.0,options_.playback_fps/2);});
        add_button(left+558,h-96,40,"+",[this]{options_.playback_fps=std::min(60.0,options_.playback_fps*2);});
        glColor3d(.65,.75,.86);text(left+615,h-83,"TARGET "+number(options_.playback_fps,1)+" FPS / ACTUAL "+number(actual_fps_,1)+" FPS",1);
        if(!frames_.empty()){glColor3d(.65,.75,.86);text(left,h-115,"FRAME "+std::to_string(displayed_index_+1)+" OF "+std::to_string(frames_.size())+" / "+frames_[displayed_index_].filename().string()+(frames_.size()==1?" / PLAY CLIP LOADS THE OTHER FRAMES":" / "+frames_[displayed_index_].parent_path().filename().string()),1);}
        if(!playback_notice_.empty()){glColor3d(1,.78,.3);text(left,h-42,playback_notice_,1);}
        if(browser_)draw_browser(w,h);
    }
    void draw_browser(int w,int h){
        buttons_.clear();const int x=std::max(270,(w-900)/2),y=100,width=std::min(900,w-x-20);
        glColor3d(0.055,0.08,0.12);rectangle(x,y,width,h-180);
        glColor3d(0.91,0.95,1);text(x+20,y+20,"CHOOSE A PCD FILE OR FRAME DIRECTORY",2);
        glColor3d(0.65,0.75,0.86);text(x+20,y+55,browser_dir_.string().substr(0,static_cast<std::size_t>((width-40)/6)),1);
        add_button(x+20,y+80,100,"UP",[this]{if(!browser_loading_){browser_dir_=browser_dir_.parent_path();refresh_browser();}});
        add_button(x+132,y+80,190,"USE THIS FOLDER",[this]{if(!browser_loading_)choose(browser_dir_.string());});
        add_button(x+width-125,y+80,105,"CANCEL",[this]{browser_=false;});
        const std::size_t rows=static_cast<std::size_t>(std::max(1,(h-400)/34));
        if(browser_loading_){glColor3d(0.35,0.82,0.9);text(x+20,y+120,"LOADING DIRECTORY... YOU CAN CANCEL WHILE IT LOADS.",1);}
        for(std::size_t i=0;i<rows&&browser_offset_+i<entries_.size();++i){
            const auto& entry=entries_[browser_offset_+i];const auto path=entry.path;const bool dir=entry.directory;
            const auto size=dir?std::string{}:entry.bytes?"  ["+number(static_cast<double>(*entry.bytes)/(1024*1024),3)+" MiB]":"  [SIZE UNKNOWN]";
            auto title=std::string(dir?"[DIR] ":"[PCD] ")+entry.name;
            const auto capacity=static_cast<std::size_t>((width-60)/6);
            title=title.substr(0,capacity>size.size()?capacity-size.size():0)+size;
            add_button(x+20,y+130+static_cast<int>(i)*34,width-40,title,[this,path,dir]{if(dir){browser_dir_=path;refresh_browser();}else choose(path.string());},false,29);
        }
        add_button(x+20,h-140,120,"PREVIOUS",[this,rows]{browser_offset_=browser_offset_>rows?browser_offset_-rows:0;});
        add_button(x+152,h-140,120,"NEXT PAGE",[this,rows]{if(browser_offset_+rows<entries_.size())browser_offset_+=rows;});
        glColor3d(0.65,0.75,0.86);text(x+285,h-125,"PCD FILES: LARGEST FIRST / MiB = 1,048,576 BYTES",1);
    }
    cloudforge::app::Options options_;
    WindowOwner window_;
    WindowOwner benchmark_window_;
    bool benchmark_visible_{},benchmark_sequence_{},benchmark_imported_{},bench_smoke_pending_{};
    double bench_mouse_x_{},bench_mouse_y_{},benchmark_voxel_{};
    std::string benchmark_input_;
    std::vector<Button> benchmark_buttons_;
    std::size_t bench_smoke_runs_{};
    std::vector<std::filesystem::path> frames_;
    std::vector<cloudforge::app::BrowserEntry> entries_;
    std::filesystem::path browser_dir_;
    std::size_t index_{},loaded_{},browser_offset_{};
    std::size_t displayed_index_{};
    std::filesystem::path pending_play_file_;
    std::string playback_notice_;
    bool playback_smoke_expanded_{};
    bool capture_pending_{};
    std::size_t capture_count_{};
    bool loop_{true},submitted_stream_{},displayed_stream_{},refit_scene_{true};
    bool gpu_{},busy_{},playing_{},failed_{},dragging_{},editing_{},browser_{},detail_{},grid_{true},comparison_requested_{};
    bool discovering_{},browser_loading_{};
    std::size_t browser_loading_frames_{};
    bool benchmark_tab_{},bench_running_{};
    bool results_tab_{},raw_view_{};
    std::size_t results_frame_{},raw_page_{};
    std::vector<cloudforge::app::FrameTiming> result_frames_;
    int bench_field_{-1};
    std::array<std::string,6> bench_fields_{"","0.1","30","30","3","build/benchmark.csv"};
    std::shared_ptr<cloudforge::app::BenchmarkProgress> bench_progress_;
    std::future<cloudforge::app::BenchmarkReport> bench_future_;
    std::optional<cloudforge::app::BenchmarkReport> bench_report_;
    bool panning_{};
    double pan_x_{},pan_y_{};
    double last_x_{},last_y_{},yaw_{},pitch_{-55},zoom_{0.45},overview_radius_{1},actual_fps_{},processed_voxel_{0.1},submitted_voxel_{0.1};
    cloudforge::Point overview_center_,detail_center_;
    Clock::time_point submitted_{},last_done_{},next_due_{};
    Clock::time_point browser_done_{};
    std::string path_text_,error_,processed_backend_,submitted_backend_;
    std::string pending_path_;
    std::unique_ptr<cloudforge::app::ProcessingResult> result_;
    std::future<cloudforge::app::ProcessingResult> future_;
    std::future<cloudforge::app::SourceSelection> source_future_;
    std::future<std::vector<cloudforge::app::BrowserEntry>> browser_future_;
    std::map<Cell,std::size_t> bins_;
    std::vector<Cell> cells_;
    cloudforge::PointCloud local_original_,local_simplified_;
    std::vector<Button> buttons_;
    cloudforge::app::ProcessingWorker worker_;
};
}
int main(int argc,char** argv){
        try{cloudforge::app::Options options;if(argc>1)options=cloudforge::app::parse(argc,argv);else{options.voxel=0.3;options.frames=1000000;}
        GlfwOwner glfw;Player player(std::move(options));return player.run();}
    catch(const std::exception& e){std::cerr<<"cloudForge studio: "<<e.what()<<'\n';return 1;}
}



