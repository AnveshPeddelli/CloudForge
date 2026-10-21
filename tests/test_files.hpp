#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

struct TestDirectory {
    std::filesystem::path path;
    TestDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 100; ++i) {
            path = std::filesystem::temp_directory_path() / ("cloudforge-test-" + std::to_string(stamp) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("Cannot create temporary test directory");
    }
    ~TestDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;
};
