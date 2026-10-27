#pragma once
#include "preset.hpp"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <functional>
#include <algorithm>
#include <future>
#include <memory>

// Projects are independent snapshots; saving never overwrites a source FTF template.
class ProjectSession {
public:
    std::filesystem::path root,currentFile;
    bool autosave=true,restorePending=false;
    int intervalSeconds=60;
    double lastAuto=0,lastRecovery=0;
    std::string lastFingerprint;
    std::future<void> recoveryWorker;
    std::shared_ptr<const Datapack> cachedRecoveryPack;std::string recoveryPackName;
    void flush(){if(recoveryWorker.valid())try{recoveryWorker.get();}catch(...){lastFingerprint.clear();lastRecovery=lastAuto=0;throw;}}
    ~ProjectSession(){if(recoveryWorker.valid())recoveryWorker.wait();}
    explicit ProjectSession(std::filesystem::path appRoot):root(std::move(appRoot)){
        auto settings=root/"state/project-settings.json";
        if(std::filesystem::exists(settings))try{auto j=read(settings);autosave=j.value("autosave",true);intervalSeconds=std::clamp(j.value("interval_seconds",60),5,3600);}catch(...){}
        restorePending=std::filesystem::exists(recoveryPath());
    }
    static PresetJson read(const std::filesystem::path& path){std::ifstream in(path);if(!in)throw std::runtime_error("Cannot open project: "+path.string());PresetJson j;in>>j;return j;}
    static void atomicJson(const std::filesystem::path& path,const PresetJson& j){
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());auto temp=path;temp+=".tmp";
        {std::ofstream out(temp,std::ios::binary);if(!out)throw std::runtime_error("Cannot write project");out<<j.dump(2)<<'\n';out.close();if(!out)throw std::runtime_error("Project write failed");}
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot commit project");
    }
    std::filesystem::path recoveryPath()const{return std::filesystem::exists(root/"state/temp/last-session.ftfproject")?root/"state/temp/last-session.ftfproject":root/"state/last-session.ftfproject";}
    std::filesystem::path autosaveFolder()const{return root/"state/autosaves";}
    void savePreferences()const{atomicJson(root/"state/project-settings.json",{{"autosave",autosave},{"interval_seconds",intervalSeconds}});}
    PresetJson snapshot(const PresetDocument& doc,int64_t seed,const PresetJson& view)const{
        return {{"format","ftf-surface-project.v1"},{"name",doc.name},{"preset",doc.data},{"saved_preset",doc.savedData.is_null()?doc.data:doc.savedData},{"world_seed",seed},{"source_preset",doc.source.string()},{"project_file",currentFile.string()},{"view",view}};
    }
    void write(const std::filesystem::path& path,const PresetDocument& doc,int64_t seed,const PresetJson& view)const{
        if(!doc.loaded())throw std::runtime_error("Load a preset first");auto data=snapshot(doc,seed,view);
        if(doc.datapack){
            std::filesystem::create_directories(path.parent_path());auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
            auto sidecar=path.filename().string()+".source-"+std::to_string(stamp)+".zip";doc.datapack->write(path.parent_path()/sidecar);data["source_pack"]=sidecar;
        }
        atomicJson(path,data);
    }
    void save(const std::filesystem::path& path,const PresetDocument& doc,int64_t seed,const PresetJson& view){
        auto previous=currentFile;currentFile=path;try{PresetDocument saved=doc;saved.markSaved();write(path,saved,seed,view);doc.markSaved();}catch(...){currentFile=previous;throw;}
    }
    PresetJson load(const std::filesystem::path& path,PresetDocument& doc,int64_t& seed,bool recovery=false){
        flush();auto data=read(path);if(data.value("format","")!="ftf-surface-project.v1")throw std::runtime_error("Unsupported project format");
        PresetDocument candidate;candidate.data=data.at("preset");for(auto section:{"world","terrain","climate","rivers"})if(!candidate.data.contains(section)||!candidate.data[section].is_object())throw std::runtime_error("Invalid project preset");
        auto restoredSeed=data.at("world_seed").get<int64_t>();auto view=data.at("view");if(!view.is_object())throw std::runtime_error("Invalid project view");
        candidate.name=data.value("name","Restored preset");candidate.source=data.value("source_preset","");candidate.worldSeed=restoredSeed;candidate.savedData=data.value("saved_preset",candidate.data);if(view.contains("domain")&&view["domain"].is_array()&&view["domain"].size()==4)candidate.previewExtent=view["domain"][0].get<float>();
        if(data.contains("source_pack")){std::filesystem::path sidecar=data.at("source_pack").get<std::string>();if(sidecar!=sidecar.filename())throw std::runtime_error("Invalid project datapack path");candidate.datapack=Datapack::read(path.parent_path()/sidecar);}
        TerrainParams validation;candidate.apply(validation);
        doc=std::move(candidate);seed=restoredSeed;currentFile=recovery?std::filesystem::path(data.value("project_file","")):path;restorePending=false;lastFingerprint.clear();return view;
    }
    void tick(const PresetDocument& doc,int64_t seed,const PresetJson& view,double now,bool force=false){
        if(!doc.loaded()||restorePending)return;
        if(recoveryWorker.valid()){
            if(!force&&recoveryWorker.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            flush();
        }
        auto data=snapshot(doc,seed,view);auto fingerprint=data.dump();
        bool recovery=force||(now-lastRecovery>=1&&fingerprint!=lastFingerprint);
        bool archive=autosave&&now-lastAuto>=intervalSeconds;
        if(!recovery&&!archive)return;
        auto recoveryFile=root/"state/temp/last-session.ftfproject";
        auto archiveFile=autosaveFolder()/("autosave-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+".ftfproject");
        // Copy source extras before leaving the main thread; worker never reads live editor state.
        if(doc.datapack){if(!cachedRecoveryPack||cachedRecoveryPack->files!=doc.datapack->files){cachedRecoveryPack=std::make_shared<Datapack>(*doc.datapack);recoveryPackName="source-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+".zip";}}
        else{cachedRecoveryPack.reset();recoveryPackName.clear();}
        auto pack=cachedRecoveryPack;auto sidecarName=recoveryPackName;
        recoveryWorker=std::async(std::launch::async,[data=std::move(data),pack=std::move(pack),recoveryFile,archiveFile,recovery,archive,sidecarName]()mutable{
            auto commit=[&](const std::filesystem::path& path){auto snapshot=data;if(pack){std::filesystem::create_directories(path.parent_path());auto sidecar=path.parent_path()/sidecarName;if(!std::filesystem::exists(sidecar)){auto temporary=sidecar;temporary+=".tmp";pack->write(temporary);if(!MoveFileExW(temporary.c_str(),sidecar.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot commit recovery source pack");}snapshot["source_pack"]=sidecarName;}atomicJson(path,snapshot);};
            if(recovery)commit(recoveryFile);if(archive)commit(archiveFile);
        });
        if(recovery){lastRecovery=now;lastFingerprint=std::move(fingerprint);}if(archive)lastAuto=now;
        if(force)flush();
    }
};
