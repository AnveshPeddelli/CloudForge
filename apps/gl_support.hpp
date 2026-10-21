#pragma once
#include "cloudforge/engine.hpp"
#include "cloudforge/pcd_io.hpp"
#include "options.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
// Five bitmap columns, least-significant bit at the top; a tiny built-in
// display font keeps the viewer free of font paths and GUI runtime resources.
const std::map<char, std::array<unsigned char,5>> font{
 {'A',{126,17,17,17,126}}, {'B',{127,73,73,73,54}}, {'C',{62,65,65,65,34}},
 {'D',{127,65,65,34,28}}, {'E',{127,73,73,73,65}}, {'F',{127,9,9,9,1}},
 {'G',{62,65,73,73,122}}, {'H',{127,8,8,8,127}}, {'I',{0,65,127,65,0}},
 {'J',{32,64,65,63,1}}, {'K',{127,8,20,34,65}}, {'L',{127,64,64,64,64}},
 {'M',{127,2,12,2,127}}, {'N',{127,4,8,16,127}}, {'O',{62,65,65,65,62}},
 {'P',{127,9,9,9,6}}, {'Q',{62,65,81,33,94}}, {'R',{127,9,25,41,70}},
 {'S',{70,73,73,73,49}}, {'T',{1,1,127,1,1}}, {'U',{63,64,64,64,63}},
 {'V',{31,32,64,32,31}}, {'W',{63,64,56,64,63}}, {'X',{99,20,8,20,99}},
 {'Y',{3,4,120,4,3}}, {'Z',{97,81,73,69,67}},
 {'0',{62,81,73,69,62}}, {'1',{0,66,127,64,0}}, {'2',{98,81,73,73,70}},
 {'3',{34,65,73,73,54}}, {'4',{24,20,18,127,16}}, {'5',{39,69,69,69,57}},
 {'6',{60,74,73,73,48}}, {'7',{1,113,9,5,3}}, {'8',{54,73,73,73,54}},
 {'9',{6,73,73,41,30}}, {'.',{0,96,96,0,0}}, {':',{0,54,54,0,0}},
 {'-',{8,8,8,8,8}}, {'/',{32,16,8,4,2}}, {'%',{99,19,8,100,99}},
 {'+',{8,8,62,8,8}}, {'=',{20,20,20,20,20}}, {'[',{0,127,65,65,0}},
 {']',{0,65,65,127,0}}, {'(',{0,28,34,65,0}}, {')',{0,65,34,28,0}},
 {'_',{64,64,64,64,64}}, {'?',{2,1,81,9,6}}
};
void rectangle(double x, double y, double w, double h) {
    glBegin(GL_QUADS); glVertex2d(x,y); glVertex2d(x+w,y); glVertex2d(x+w,y+h); glVertex2d(x,y+h); glEnd();
}
void text(double x, double y, const std::string& message, int scale=2) {
    glBegin(GL_QUADS);
    for (unsigned char raw : message) {
        const char c = static_cast<char>(std::toupper(raw));
        const auto found = font.find(c);
        if (found != font.end()) for (int column=0;column<5;++column) for (int row=0;row<7;++row) {
            if (!(found->second[static_cast<std::size_t>(column)] & (1 << row))) continue;
            const double px=x+column*scale, py=y+row*scale;
            glVertex2d(px,py); glVertex2d(px+scale,py); glVertex2d(px+scale,py+scale); glVertex2d(px,py+scale);
        }
        x += 6*scale;
    }
    glEnd();
}
std::string number(double value, int precision=3) {
    std::ostringstream out; out << std::fixed << std::setprecision(precision) << value; return out.str();
}
struct GlfwOwner {
    GlfwOwner() {
#if defined(__linux__) && defined(GLFW_PLATFORM_X11)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed; check DISPLAY / WSLg");
    }
    ~GlfwOwner() { glfwTerminate(); }
};
struct WindowOwner {
    GLFWwindow* handle{};
    ~WindowOwner() { if (handle) glfwDestroyWindow(handle); }
};
    void save_screenshot(GLFWwindow* window, const std::filesystem::path& path) {
        int w{},h{};glfwGetFramebufferSize(window,&w,&h);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(w)*h*3);
        glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
        const auto row=static_cast<std::size_t>(w)*3;
        const auto padded=(row+3)&~std::size_t(3);
        std::array<unsigned char,54> header{};
        const auto put=[&header](std::size_t offset,std::uint32_t value){for(std::size_t i=0;i<4;++i)header[offset+i]=static_cast<unsigned char>((value>>(8*i))&255);};
        header[0]='B';header[1]='M';put(2,static_cast<std::uint32_t>(54+padded*h));put(10,54);put(14,40);
        put(18,static_cast<std::uint32_t>(w));put(22,static_cast<std::uint32_t>(h));header[26]=1;header[28]=24;
        std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(header.data()),header.size());
        const char zero[3]{};
        for(int y=0;y<h;++y){
            auto* begin=pixels.data()+static_cast<std::size_t>(y)*row;
            for(int x=0;x<w;++x)std::swap(begin[x*3],begin[x*3+2]);
            out.write(reinterpret_cast<const char*>(begin),static_cast<std::streamsize>(row));
            out.write(zero,static_cast<std::streamsize>(padded-row));
        }
        out.close();
        if(!out)throw std::runtime_error("Cannot save screenshot: "+path.string());
        std::cout<<"Screenshot: "<<path<<std::endl;
    }

}

