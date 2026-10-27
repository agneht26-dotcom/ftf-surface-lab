#include <windows.h>
#include <objbase.h>
#include "library.hpp"
#include "ui_icons.hpp"
#include <fstream>
#include <algorithm>
#include <chrono>

namespace {
std::string newId(){GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot allocate preset ID");wchar_t str[40]{};StringFromGUID2(guid,str,40);std::string id;for(int i=1;i<=36;++i)id+=static_cast<char>(str[i]);return id;}
void validName(const std::string& n){if(n.empty()||n.size()>150||n.find_first_not_of(" \t\r\n")==std::string::npos)throw std::runtime_error("Enter a preset name (1 to 150 bytes)");}
PresetJson readMeta(const std::filesystem::path& p){std::ifstream in(p);PresetJson j;in>>j;return j;}
void writeMeta(const std::filesystem::path& path,const PresetJson& j){auto temp=path;temp+=L".tmp";std::ofstream out(temp,std::ios::binary);out<<j.dump(2)<<'\n';out.close();if(!out||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot save library metadata");}
void validateEntry(const std::filesystem::path& root,const LibraryEntry& e){if(e.path.parent_path().parent_path()!=root||e.path.filename()!="preset.json"||e.id!=e.path.parent_path().filename().string())throw std::runtime_error("Invalid library entry");}
}
std::vector<LibraryEntry> PresetLibrary::list()const{
    std::vector<LibraryEntry> result;
    auto builtinRoot=root.parent_path()/"assets"/"ftf-presets",manifest=builtinRoot/"manifest.json";
    PresetJson builtins=PresetJson::object();if(std::filesystem::exists(manifest))builtins=readMeta(manifest);
    if(builtins.contains("templates"))for(auto& item:builtins.at("templates")){
        auto file=std::filesystem::path(item.at("file").get<std::string>());if(file!=file.filename())continue;
        if(std::filesystem::is_regular_file(builtinRoot/file))result.push_back({"builtin-"+item.at("id").get<std::string>(),"FTF - "+item.at("name").get<std::string>(),builtinRoot/file,false,true});
    }
    if(std::filesystem::exists(root))
    for(auto& dir:std::filesystem::directory_iterator(root))if(dir.is_directory()&&!dir.is_symlink()&&dir.path().filename()!="trash"&&dir.path().filename().string().size()==36){
        auto meta=dir.path()/"preset.metadata.json",preset=dir.path()/"preset.json";
        if(!std::filesystem::exists(meta)||!std::filesystem::exists(preset))continue;
        try{auto j=readMeta(meta);result.push_back({dir.path().filename().string(),j.at("name"),preset,j.value("favorite",false),false,j.value("custom_default",false)});}catch(...){/* An incomplete save is not a selectable preset. */}
    }
    std::stable_sort(result.begin(),result.end(),[](auto& a,auto& b){if(a.favorite!=b.favorite)return a.favorite>b.favorite;if(a.builtin!=b.builtin)return a.builtin>b.builtin;return a.builtin?false:a.name<b.name;});return result;
}
LibraryEntry PresetLibrary::save(const PresetDocument& doc,int64_t seed,const std::string& name,const std::string& id)const{
    if(!doc.loaded())throw std::runtime_error("Import a preset first");validName(name);
    std::string key=id.empty()?newId():id;
    if(key.find_first_not_of("0123456789abcdefABCDEF-")!=std::string::npos||key.size()!=36)throw std::runtime_error("Invalid library ID");
    auto path=root/key;std::filesystem::create_directories(root);bool fav=false;
    if(std::filesystem::exists(path/"preset.metadata.json")){auto metadata=readMeta(path/"preset.metadata.json");if(metadata.value("custom_default",false))throw std::runtime_error("Custom Defaults are templates. Save a new preset with a new name.");fav=metadata.value("favorite",false);}
    auto pending=root/(".pending-"+newId());std::filesystem::create_directory(pending);
    if(doc.datapack)doc.datapack->write(pending/"source.zip");
    doc.exportTo(pending/"preset.json");PresetJson metadata={{"name",name},{"world_seed",seed},{"favorite",fav},{"format",1}};if(doc.previewExtent)metadata["preview_extent"]=*doc.previewExtent;writeMeta(pending/"preset.metadata.json",metadata);
    std::filesystem::path backup;
    if(std::filesystem::exists(path)){auto history=root/"history";std::filesystem::create_directories(history);backup=history/(key+"-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));std::filesystem::rename(path,backup);}
    try{std::filesystem::rename(pending,path);}catch(...){if(!backup.empty())std::filesystem::rename(backup,path);throw;}
    doc.markSaved();return {key,name,path/"preset.json",fav};
}
void PresetLibrary::rename(const LibraryEntry& e,const std::string& name)const{validateEntry(root,e);validName(name);auto p=e.path.parent_path()/"preset.metadata.json";auto j=readMeta(p);j["name"]=name;writeMeta(p,j);}
void PresetLibrary::promote(const LibraryEntry& e,bool enabled)const{validateEntry(root,e);auto p=e.path.parent_path()/"preset.metadata.json";auto j=readMeta(p);j["custom_default"]=enabled;writeMeta(p,j);}
void PresetLibrary::favorite(const LibraryEntry& e,bool enabled)const{validateEntry(root,e);auto p=e.path.parent_path()/"preset.metadata.json";auto j=readMeta(p);j["favorite"]=enabled;writeMeta(p,j);}
void PresetLibrary::trash(const LibraryEntry& e)const{
    validateEntry(root,e);auto dir=root/"trash";std::filesystem::create_directories(dir);
    auto destination=dir/(e.id+"-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::rename(e.path.parent_path(),destination);
}
void PresetLibrary::restoreLast()const{
    auto dir=root/"trash";if(!std::filesystem::exists(dir))throw std::runtime_error("No deleted library presets");
    std::filesystem::path latest;for(auto& e:std::filesystem::directory_iterator(dir))if(e.is_directory()&&!e.is_symlink()&&(latest.empty()||e.path().filename().string().substr(37)>latest.filename().string().substr(37)))latest=e.path();
    if(latest.empty())throw std::runtime_error("No deleted library presets");auto id=latest.filename().string().substr(0,36);
    if(std::filesystem::exists(root/id))throw std::runtime_error("A preset with this ID already exists; duplicate it before restoring");std::filesystem::rename(latest,root/id);
}
bool PresetEditor::libraryWindow(int64_t& seed){
    bool changed=false;if(!managerOpen)return false;
    if(!ImGui::IsPopupOpen("Preset library"))ImGui::OpenPopup("Preset library");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),ImGuiCond_Appearing,ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(660,570),ImGuiCond_FirstUseEver);
    if(ImGui::BeginPopupModal("Preset library",&managerOpen)){
        try{
            PresetLibrary library(root/"library");auto entries=library.list();
            ImGui::InputText(labelAbove("Search").c_str(),librarySearch,sizeof(librarySearch));
            ImGui::BeginChild("Library entries",ImVec2(0,200),true);
            for(int section=0;section<3;++section){
                const char* title=section==0?"Default":section==1?"Custom":"Custom Defaults";
                if(!ImGui::CollapsingHeader(title,section==0||uiSectionsStartCollapsed?ImGuiTreeNodeFlags_None:ImGuiTreeNodeFlags_DefaultOpen))continue;
                bool any=false;
                for(auto& e:entries){if((e.builtin?0:e.customDefault?2:1)!=section)continue;
                    std::string label=(e.favorite?"* ":"")+e.name;
                    std::string hay=label,needle=librarySearch;auto lower=[](unsigned char c){return static_cast<char>(std::tolower(c));};std::transform(hay.begin(),hay.end(),hay.begin(),lower);std::transform(needle.begin(),needle.end(),needle.begin(),lower);
                    if(hay.find(needle)==std::string::npos)continue;any=true;
                    if(ImGui::Selectable((label+"##"+e.id).c_str(),librarySelection==e.id)){librarySelection=e.id;strncpy_s(libraryName,e.name.c_str(),_TRUNCATE);}
                }
                if(!any)ImGui::TextDisabled("No presets");
            }ImGui::EndChild();
            auto selected=std::find_if(entries.begin(),entries.end(),[&](auto& e){return e.id==librarySelection;});
            ImGui::InputText(labelAbove("Name",320).c_str(),libraryName,sizeof(libraryName));
            ImGui::BeginDisabled(!document.loaded());
            if(ImGui::Button("Save current as new")){
                auto source=std::find_if(entries.begin(),entries.end(),[&](auto& e){return e.path==document.source&&(e.builtin||e.customDefault);});
                if(source!=entries.end()){libraryTemplateName=source->name;strncpy_s(libraryName,(source->name+" copy").c_str(),_TRUNCATE);ImGui::OpenPopup("Save new preset name");}
                else{auto e=library.save(document,seed,libraryName);librarySelection=e.id;savedLibraryData=document.data;savedLibrarySeed=seed;status="Saved new library preset: "+e.name;}
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(selected==entries.end());
            if(ImGui::Button("Load selected"))ImGui::OpenPopup("Load library preset?");
            ImGui::SameLine();if(ImGui::Button("Overwrite selected")){
                if(selected!=entries.end()&&(selected->builtin||selected->customDefault)){libraryTemplateName=selected->name;strncpy_s(libraryName,(selected->name+" copy").c_str(),_TRUNCATE);ImGui::OpenPopup("Save new preset name");}
                else ImGui::OpenPopup("Overwrite library preset?");
            }
            ImGui::BeginDisabled(selected!=entries.end()&&(selected->builtin||selected->customDefault));
            if(ImGui::Button("Rename")){library.rename(*selected,libraryName);status="Renamed library preset";}
            ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Duplicate")){PresetDocument copy;copy.load(selected->path);auto e=library.save(copy,copy.worldSeed.value_or(seed),selected->name+" copy");librarySelection=e.id;strncpy_s(libraryName,e.name.c_str(),_TRUNCATE);status="Duplicated library preset";}
            ImGui::SameLine();ImGui::BeginDisabled(selected!=entries.end()&&selected->builtin);if(ImGui::Button(selected!=entries.end()&&selected->favorite?"Unfavorite":"Favorite"))library.favorite(*selected,!selected->favorite);
            ImGui::SameLine();if(ImGui::Button("Delete"))ImGui::OpenPopup("Delete library preset?");
            ImGui::EndDisabled();ImGui::EndDisabled();
            ImGui::BeginDisabled(selected==entries.end()||selected->builtin);
            if(ImGui::Button(selected!=entries.end()&&selected->customDefault?"Move to Custom":"Promote to Custom Defaults")){library.promote(*selected,!selected->customDefault);status=selected->customDefault?"Moved preset to Custom":"Promoted preset to Custom Defaults";}
            ImGui::EndDisabled();
            if(ImGui::BeginPopupModal("Save new preset name",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
                ImGui::TextUnformatted("Default presets are templates. Enter a new name to save your copy.");
                ImGui::InputText(labelAbove("New name",320).c_str(),libraryName,sizeof(libraryName));
                std::string name=libraryName;auto normalized=[](std::string text){auto first=text.find_first_not_of(" \t\r\n"),last=text.find_last_not_of(" \t\r\n");if(first==std::string::npos)return std::string{};text=text.substr(first,last-first+1);std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});return text;};
                bool unique=!normalized(name).empty()&&normalized(name)!=normalized(libraryTemplateName)&&std::none_of(entries.begin(),entries.end(),[&](auto& e){return normalized(e.name)==normalized(name);});
                if(!unique)ImGui::TextDisabled("Choose a different, unused name.");
                ImGui::BeginDisabled(!unique);
                if(ImGui::Button("Save new preset")){auto e=library.save(document,seed,name);librarySelection=e.id;savedLibraryData=document.data;savedLibrarySeed=seed;status="Saved new Custom preset: "+e.name;ImGui::CloseCurrentPopup();}
                ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
            }
            if(ImGui::Button("Restore last deleted")){library.restoreLast();status="Restored library preset";}
            for(auto action:{"Load library preset?","Overwrite library preset?","Delete library preset?"})if(ImGui::BeginPopupModal(action,nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
                ImGui::TextUnformatted(std::string(action).starts_with("Load")?"Replace the current preview settings? Save current as new first to keep edits.":std::string(action).starts_with("Overwrite")?"Replace the selected library preset with current settings and seed?":"Move the selected preset to library/trash? It can be restored.");
                if(ImGui::Button("Confirm")&&selected!=entries.end()){
                    if(std::string(action).starts_with("Load")){document.load(selected->path);if(document.worldSeed)seed=*document.worldSeed;clearHistory();savedLibraryData=document.data;savedLibrarySeed=seed;changed=true;status="Loaded "+selected->name;}
                    else if(std::string(action).starts_with("Overwrite")){library.save(document,seed,libraryName,selected->id);savedLibraryData=document.data;savedLibrarySeed=seed;status="Updated library preset";}
                    else{library.trash(*selected);librarySelection.clear();status="Deleted preset; Restore last deleted is available";}
                    ImGui::CloseCurrentPopup();
                }ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
            }
            if(!savedLibraryData.is_null()&&(document.data!=savedLibraryData||savedLibrarySeed!=seed))ImGui::TextWrapped("Current settings or seed differ from the last library save/load.");
            ImGui::TextWrapped("Default and Custom Defaults are templates: saving creates a Custom preset with a new name. Promote a Custom preset to keep it as your own reusable default. Your library entries include the preset, preview seed and original datapack when available.");
        }catch(const std::exception& e){status=e.what();}
        if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
        ImGui::EndPopup();
    }return changed;
}
