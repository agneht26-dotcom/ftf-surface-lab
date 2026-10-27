#include <windows.h>
#include "datapack.hpp"
#include "ftf_packgen.hpp"
#include <miniz.h>
#include <fstream>
#include <functional>
#include <cmath>
#include <set>

namespace {
constexpr size_t limit=256*1024*1024;
bool safeName(const std::string& name){
    if(name.empty()||name.front()=='/'||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos||name.find('\0')!=std::string::npos)return false;
    std::filesystem::path p(std::u8string(name.begin(),name.end()));for(auto& part:p)if(part==".."||part==".")return false;return true;
}
std::string readFile(const std::filesystem::path& p){
    if(std::filesystem::file_size(p)>limit)throw std::runtime_error("Datapack file exceeds 256 MiB");
    std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("Cannot read datapack file");return {std::istreambuf_iterator<char>(in),{}};
}
}
Datapack Datapack::read(const std::filesystem::path& path){
    Datapack pack;size_t total=0;
    auto add=[&](std::string name,std::string bytes){
        if(!safeName(name))throw std::runtime_error("Unsafe datapack entry: "+name);
        total+=bytes.size();if(total>limit||pack.files.size()>=16384)throw std::runtime_error("Datapack exceeds import limits");
        if(!pack.files.emplace(name,std::move(bytes)).second)throw std::runtime_error("Duplicate datapack entry: "+name);
    };
    if(std::filesystem::is_directory(path)){
        for(auto& entry:std::filesystem::recursive_directory_iterator(path)){
            if(entry.is_symlink())throw std::runtime_error("Datapack folders cannot contain symbolic links");
            if(entry.is_regular_file()){auto u=entry.path().lexically_relative(path).generic_u8string();add(std::string(u.begin(),u.end()),readFile(entry.path()));}
        }
    }else{
        auto bytes=readFile(path);mz_zip_archive zip{};
        if(!mz_zip_reader_init_mem(&zip,bytes.data(),bytes.size(),0))throw std::runtime_error("Cannot open ZIP datapack");
        try{for(mz_uint i=0;i<mz_zip_reader_get_num_files(&zip);++i){
            mz_zip_archive_file_stat stat{};if(!mz_zip_reader_file_stat(&zip,i,&stat))throw std::runtime_error("Invalid ZIP directory");
            if(stat.m_is_directory)continue;
            if(stat.m_uncomp_size>limit-total)throw std::runtime_error("Datapack exceeds 256 MiB");
            std::string name(stat.m_filename);if(!safeName(name))throw std::runtime_error("Unsafe ZIP entry");
            std::string result(static_cast<size_t>(stat.m_uncomp_size),'\0');
            if(!mz_zip_reader_extract_to_mem(&zip,i,result.data(),result.size(),0))throw std::runtime_error("Invalid ZIP content or checksum");
            add(name,std::move(result));
        }}catch(...){mz_zip_reader_end(&zip);throw;}mz_zip_reader_end(&zip);
    }
    if(!pack.files.contains("pack.mcmeta"))throw std::runtime_error("Datapack needs pack.mcmeta at its root");
    auto meta=PackJson::parse(pack.files.at("pack.mcmeta"));if(!meta.contains("pack"))throw std::runtime_error("Invalid pack.mcmeta");
    for(auto& [name,bytes]:pack.files)if(name.starts_with("data/")&&name.ends_with("/freeterraforged/worldgen/preset/preset.json")){
        if(!pack.presetPath.empty())throw std::runtime_error("Multiple active FTF presets in datapack");pack.presetPath=name;
    }
    if(pack.presetPath.empty())throw std::runtime_error("No FTF worldgen preset/preset.json in this datapack");
    pack.original=PackJson::parse(pack.files.at(pack.presetPath));return pack;
}
void Datapack::write(const std::filesystem::path& path)const{
    mz_zip_archive zip{};if(!mz_zip_writer_init_heap(&zip,0,0))throw std::runtime_error("Cannot create ZIP");
    void* buffer=nullptr;size_t size=0;
    try{
        for(auto& [name,bytes]:files)if(!safeName(name)||!mz_zip_writer_add_mem(&zip,name.c_str(),bytes.data(),bytes.size(),MZ_BEST_SPEED))throw std::runtime_error("Cannot encode datapack ZIP");
        if(!mz_zip_writer_finalize_heap_archive(&zip,&buffer,&size))throw std::runtime_error("Cannot finalize datapack ZIP");
        mz_zip_writer_end(&zip);
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());auto tmp=path;tmp+=L".tmp";
        std::ofstream out(tmp,std::ios::binary);out.write(static_cast<const char*>(buffer),size);out.close();mz_free(buffer);buffer=nullptr;
        if(!out||!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot save datapack ZIP");
    }catch(...){mz_zip_writer_end(&zip);if(buffer)mz_free(buffer);throw;}
}
void Datapack::update(const PackJson& preset,const Datapack& sourceTemplate){
    auto meta=PackJson::parse(files.at("pack.mcmeta"));
    if(meta.at("pack").value("pack_format",0)!=48)throw std::runtime_error("This exporter targets the supplied FTF build / Minecraft 1.21.1 (pack format 48)");
    auto before=generateFtfDatapack(original,sourceTemplate),after=generateFtfDatapack(preset,sourceTemplate);
    // Only replace source-owned registries whose generated meaning changes.
    // Other imported entries (including mod resources and binary data) survive.
    std::set<std::string> owned;for(auto& [name,bytes]:before.files)owned.insert(name);for(auto& [name,bytes]:after.files)owned.insert(name);
    for(auto& name:owned){
        if(name=="pack.mcmeta"||name==sourceTemplate.presetPath)continue;
        bool had=before.files.contains(name),has=after.files.contains(name);
        if(had&&has&&before.files.at(name)==after.files.at(name))continue;
        if(has)files[name]=after.files.at(name);else files.erase(name);
    }
    files.at(presetPath)=preset.dump(2)+"\n";
}
