#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <string>

using PackJson=nlohmann::ordered_json;
struct Datapack {
    std::map<std::string,std::string> files;
    std::string presetPath;
    PackJson original;
    static Datapack read(const std::filesystem::path& path);
    void write(const std::filesystem::path& path)const;
    void update(const PackJson& preset,const Datapack& sourceTemplate);
};
