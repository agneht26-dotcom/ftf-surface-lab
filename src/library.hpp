#pragma once
#include "preset.hpp"
#include <array>

struct LibraryEntry {std::string id,name;std::filesystem::path path;bool favorite{};bool builtin{};bool customDefault{};std::string ftfVersion{};};

inline const std::array<const char*,3>& ftfPresetVersionOrder(){
    static const std::array<const char*,3> kOrder{"1.0.0032","1.0.0013","1.0.0010"};
    return kOrder;
}
inline std::string libraryVersionTag(const LibraryEntry& entry){
    return "[FTF "+(entry.ftfVersion.empty()?std::string("unknown"):entry.ftfVersion)+"]";
}
inline std::string libraryDisplayLabel(const LibraryEntry& entry){
    return (entry.favorite?std::string("* "):std::string{})+entry.name+"  "+libraryVersionTag(entry);
}

class PresetLibrary {
public:
    explicit PresetLibrary(std::filesystem::path root):root(std::move(root)){}
    std::vector<LibraryEntry> list(const std::string& builtinVersion={},bool includeAllBuiltinVersions=false)const;
    LibraryEntry save(const PresetDocument& document,int64_t seed,const std::string& name,const std::string& id={},const std::string& ftfVersion={})const;
    void rename(const LibraryEntry& entry,const std::string& name)const;
    void promote(const LibraryEntry& entry,bool enabled)const;
    void favorite(const LibraryEntry& entry,bool enabled)const;
    void trash(const LibraryEntry& entry)const;
    void restoreLast()const;
private:
    std::filesystem::path root;
};
