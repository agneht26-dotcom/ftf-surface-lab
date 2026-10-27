#pragma once
#include "preset.hpp"

struct LibraryEntry {std::string id,name;std::filesystem::path path;bool favorite{};bool builtin{};bool customDefault{};};
class PresetLibrary {
public:
    explicit PresetLibrary(std::filesystem::path root):root(std::move(root)){}
    std::vector<LibraryEntry> list()const;
    LibraryEntry save(const PresetDocument& document,int64_t seed,const std::string& name,const std::string& id={})const;
    void rename(const LibraryEntry& entry,const std::string& name)const;
    void promote(const LibraryEntry& entry,bool enabled)const;
    void favorite(const LibraryEntry& entry,bool enabled)const;
    void trash(const LibraryEntry& entry)const;
    void restoreLast()const;
private:
    std::filesystem::path root;
};
