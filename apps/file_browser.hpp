#pragma once
#include "cloudforge/engine.hpp"
#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cloudforge::app {
struct BrowserEntry {
    std::filesystem::path path;
    std::string name;
    bool directory{};
    std::optional<std::uintmax_t> bytes;
};
inline std::vector<BrowserEntry> read_directory(const std::filesystem::path& path) {
    std::vector<BrowserEntry> entries;
    for(const auto& entry:std::filesystem::directory_iterator(path,std::filesystem::directory_options::skip_permission_denied)){
        std::error_code error;
        // Cache metadata on the worker, never inside sorting or rendering.
        const auto status=entry.status(error);
        if(error)continue;
        const bool directory=std::filesystem::is_directory(status);
        if(directory||(std::filesystem::is_regular_file(status)&&entry.path().extension()==".pcd")){
            std::optional<std::uintmax_t> bytes;
            if(!directory){const auto size=entry.file_size(error);if(!error)bytes=size;}
            entries.push_back({entry.path(),entry.path().filename().string(),directory,bytes});
        }
    }
    std::sort(entries.begin(),entries.end(),[](const BrowserEntry& a,const BrowserEntry& b){
        if(a.directory!=b.directory)return a.directory;
        if(!a.directory&&a.bytes!=b.bytes){
            if(!a.bytes)return false;
            if(!b.bytes)return true;
            return *a.bytes>*b.bytes;
        }
        return a.name<b.name;
    });
    return entries;
}
struct SourceSelection {
    std::vector<std::filesystem::path> frames;
    std::filesystem::path browser_directory;
};
inline SourceSelection discover_source(const std::filesystem::path& path,std::size_t limit){
    std::vector<std::filesystem::path> frames;
    try{frames=list_frames(path);}
    catch(const std::runtime_error& error){
        if(std::string(error.what())!="No .pcd frames in input directory")throw;
        // A parent folder containing separate clip folders is a playlist.
        std::vector<std::filesystem::path> clips;
        for(const auto& entry:std::filesystem::directory_iterator(path))if(entry.is_directory())clips.push_back(entry.path());
        std::sort(clips.begin(),clips.end());
        for(const auto& clip:clips){
            try{auto sequence=list_frames(clip);frames.insert(frames.end(),sequence.begin(),sequence.end());}
            catch(const std::runtime_error& e){if(std::string(e.what())!="No .pcd frames in input directory")throw;}
        }
        if(frames.empty())throw std::runtime_error("No PCD frames in folder or immediate clip folders");
    }
    frames.resize(std::min(frames.size(),limit));
    auto directory=std::filesystem::is_directory(path)?path:path.parent_path();
    if(directory.empty())directory=std::filesystem::current_path();
    return {std::move(frames),std::move(directory)};
}
}
