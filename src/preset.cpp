#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include "preset.hpp"
#include "library.hpp"
#include "ui_icons.hpp"
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <array>
#include <cstring>
#include <random>

static std::string readable(std::string text){
    // Sentence case: "globalVerticalScale" -> "Global vertical scale"; acronyms ("XZ", a lone "Y") stay upper case.
    auto upper=[&](size_t i){return i<text.size()&&std::isupper(static_cast<unsigned char>(text[i]));};
    std::string result;for(size_t i=0;i<text.size();++i){char c=text[i];bool word=i&&upper(i)&&!upper(i-1);if(word)result+=' ';
        result+=i==0?static_cast<char>(std::toupper(static_cast<unsigned char>(c))):word&&!upper(i+1)&&i+1<text.size()?static_cast<char>(std::tolower(static_cast<unsigned char>(c))):c;}
    return result;
}
static void finiteNumbers(const PresetJson& value){
    if(value.is_number_float()&&!std::isfinite(value.get<double>()))throw std::runtime_error("Preset contains a non-finite number");
    if(value.is_structured())for(auto& item:value)finiteNumbers(item);
}
static constexpr const char* islandOptionalFields[]={"enableArchipelago","islandDensity","islandSize","islandHeight","islandBaseScale","islandVerticalScale","islandHorizontalScale","mountainChance","volcanoChance","mountainScale","volcanismScale","volcanismHorizontalScale","mountainHorizontalScale","offshoreDepth","beachWidth","beachCoverage","macroDensityPercentage"};
static constexpr const char* erosionOptionalFields[]={"scale","strengthMultiplier","gullySharpness","flowOctaves","lacunarity","gain","phacelleScale","phacelleOffset","phacelleNormalization","slopeOnsetBase","slopeOnsetRidge","slopeOnsetOctave","roundingMin","roundingMax"};
static std::optional<PresetJson> optionalPresetDefault(const std::string& path){
    // Preset.DIRECT_CODEC's optional SurfaceSettings default, not the modern template.
    static const PresetJson surfaceDefaults={{"erosion",{{"rockVariance",30},{"rockMin",140},{"dirtVariance",40},{"dirtMin",95},{"rockSteepness",.65},{"dirtSteepness",.475},{"screeSteepness",.4}}}};
    if(path=="/surface")return surfaceDefaults;
    if(path.starts_with("/surface/")){auto key=PresetJson::json_pointer(path.substr(8));if(surfaceDefaults.contains(key))return surfaceDefaults.at(key);}

    if(path=="/erosionFilters/flowOctaves")return PresetJson(8);
    const std::pair<const char*,double> erosionDefaults[]={{"scale",.233},{"strengthMultiplier",81},{"gullySharpness",.2},{"lacunarity",.636},{"gain",.254},{"phacelleScale",1.918},{"phacelleOffset",.5},{"phacelleNormalization",0},{"slopeOnsetBase",.529},{"slopeOnsetRidge",1.559},{"slopeOnsetOctave",.76},{"roundingMin",100},{"roundingMax",50}};
    for(auto [key,value]:erosionDefaults)if(path=="/erosionFilters/"+std::string(key))return PresetJson(value);
    if(path=="/climate/coastalMoistureBoost")return PresetJson(.2);
    if(path=="/climate/temperature/distributionBias"||path=="/climate/moisture/distributionBias")return PresetJson(0.0);
    if(path=="/world/continent/continentNoiseOctaves")return PresetJson(5);
    if(path=="/world/properties/worldDepth")return PresetJson(64);
    if(path=="/island/enableArchipelago")return PresetJson(true);
    if(path=="/island/islandDensity")return PresetJson(.4);
    if(path=="/island/islandSize")return PresetJson(150.0);
    if(path=="/island/islandVerticalScale")return PresetJson(1.5);
    if(path=="/island/islandHorizontalScale")return PresetJson(5.0);
    if(path=="/island/offshoreDepth")return PresetJson(.275);
    if(path=="/island/beachWidth")return PresetJson(.225);
    if(path=="/island/beachCoverage")return PresetJson(.6);
    if(path=="/island/islandHeight"||path=="/island/islandBaseScale")return PresetJson(.1);
    if(path=="/island/mountainChance"||path=="/island/volcanoChance")return PresetJson(.5);
    if(path=="/island/mountainScale")return PresetJson(.4);
    if(path=="/island/volcanismScale")return PresetJson(.85);
    if(path=="/island/volcanismHorizontalScale"||path=="/island/mountainHorizontalScale")return PresetJson(.7);
    if(path=="/island/macroDensityPercentage")return PresetJson(1.0);
    return std::nullopt;
}
void PresetDocument::load(const std::filesystem::path& path){
    PresetJson parsed;std::optional<Datapack> pack;
    auto extension=path.extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
    if(extension==L".zip"||std::filesystem::is_directory(path)){pack=Datapack::read(path);parsed=pack->original;}
    else{std::ifstream input(path);if(!input)throw std::runtime_error("Cannot open preset: "+path.string());input>>parsed;
        if(path.filename()=="preset.json"&&std::filesystem::exists(path.parent_path()/"source.zip"))pack=Datapack::read(path.parent_path()/"source.zip");}
    for(auto section:{"world","terrain","climate","rivers"})if(!parsed.contains(section)||!parsed[section].is_object())throw std::runtime_error("Not an FTF preset: missing "+std::string(section));
    finiteNumbers(parsed);std::optional<float> extent;std::optional<int64_t> seed;auto titleUtf8=path.stem().u8string();std::string title(titleUtf8.begin(),titleUtf8.end());
    auto metadata=path.parent_path()/(path.stem().string()+".metadata.json");
    if(std::filesystem::exists(metadata)){std::ifstream meta(metadata);PresetJson info;meta>>info;if(info.contains("world_seed")&&info["world_seed"].is_number_integer())seed=info["world_seed"].get<int64_t>();if(info.contains("name")&&info["name"].is_string())title=info["name"];if(info.contains("preview_extent")&&info["preview_extent"].is_number()){float v=info["preview_extent"];if(std::isfinite(v)&&v>0)extent=v;}}
    if(!seed&&pack&&pack->files.contains("surfacelab.json")){auto info=PresetJson::parse(pack->files.at("surfacelab.json"));if(info.contains("world_seed")&&info["world_seed"].is_number_integer())seed=info["world_seed"].get<int64_t>();}
    data=std::move(parsed);source=path;name=std::move(title);worldSeed=seed;previewExtent=extent;datapack=std::move(pack);markSaved();
}
void PresetDocument::exportDatapack(const std::filesystem::path& path,const std::filesystem::path& fallback)const{
    if(!loaded())throw std::runtime_error("Import a preset first");finiteNumbers(data);
    auto pack=datapack?*datapack:Datapack::read(fallback);pack.update(data,Datapack::read(fallback.parent_path()/"ftf-default-datapack.zip"));
    PresetJson metadata={{"application","FTF Surface Lab"},{"erosion_engine","FTF 22-Oct Phacelle world filter"}};
    if(worldSeed)metadata["world_seed"]=*worldSeed;pack.files["surfacelab.json"]=metadata.dump(2)+"\n";pack.write(path);
}
void PresetDocument::exportTo(const std::filesystem::path& path)const{
    if(!loaded())throw std::runtime_error("Import an FTF preset first");finiteNumbers(data);
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    auto temp=path;temp+=".tmp";
    {std::ofstream out(temp,std::ios::binary);if(!out)throw std::runtime_error("Cannot write preset export");out<<data.dump(2)<<'\n';out.close();if(!out)throw std::runtime_error("Preset export failed");}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot finalize preset export");
}
void PresetDocument::set(const std::string& pointer,const PresetJson& value){
    finiteNumbers(value);auto key=PresetJson::json_pointer(pointer);
    bool present=data.contains(key);auto fallback=optionalPresetDefault(pointer);
    if(!present&&!fallback)throw std::runtime_error("Unknown preset field: "+pointer);
    auto& old=present?data.at(key):*fallback;if(old.is_number()!=value.is_number()||old.is_boolean()!=value.is_boolean()||old.is_string()!=value.is_string())throw std::runtime_error("Wrong value type for "+pointer);
    if(old.is_number_integer()){
        if(!value.is_number_integer()||value.get<double>()<std::numeric_limits<int32_t>::min()||value.get<double>()>std::numeric_limits<int32_t>::max())throw std::runtime_error("FTF integer fields need a signed 32-bit integer: "+pointer);
        old=value.get<int64_t>();
    }else if(old.is_number_float()){
        double number=value.get<double>();if(std::abs(number)>std::numeric_limits<float>::max())throw std::runtime_error("FTF float field exceeds its numeric type: "+pointer);old=number;
    }else{
        if(old.is_structured()||old.is_null())throw std::runtime_error("Edit a scalar preset field: "+pointer);old=value;
    }
    if(!present){if(pointer.starts_with("/surface/erosion/")&&!data.contains("surface"))data["surface"]=*optionalPresetDefault("/surface");data[key]=old;}
}
void PresetDocument::apply(TerrainParams& p)const{
    auto scalar=[&](const char* pointer,double fallback){auto key=PresetJson::json_pointer(pointer);double v=data.contains(key)?data.at(key).get<double>():fallback;if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<float>::max())throw std::runtime_error("Preview cannot represent "+std::string(pointer));return static_cast<float>(v);};
    auto next=p;
    if(p.meta.z==3){next.detail.x=scalar("/terrain/general/terrainRegionSize",2098);if(next.detail.x<1||next.detail.x>2147483520.0f)throw std::runtime_error("Preview paused: terrain region size must fit a positive FTF integer");next.land.z=scalar("/world/properties/seaLevel",63);p=next;return;}
    float globalHorizontal=scalar("/terrain/general/globalHorizontalScale",1),worldHeight=scalar("/world/properties/worldHeight",256);
    const char* names[]={"plains","steppe","dales","hills","hills","plateau","badlands","torridonian"};
    auto group="/terrain/"+std::string(names[std::min(p.meta.w,7u)])+"/";
    auto familyScalar=[&](const char* name,double fallback){return scalar((group+name).c_str(),fallback);};
    float horizontal=familyScalar("horizontalScale",1);
    if(globalHorizontal<=0||worldHeight<=0)throw std::runtime_error("Preview paused: global horizontal scale and world height must be positive");
    if(p.meta.z==2&&p.meta.w<=1){
        if(horizontal<=0||std::round(250.0f*horizontal)<4)throw std::runtime_error("Preview paused: terrain horizontal scale makes an integer noise scale smaller than one");
        if(horizontal>static_cast<float>(std::numeric_limits<int32_t>::max()/500))throw std::runtime_error("Preview paused: terrain scale exceeds the integer noise-domain limit");
    }
    next.land.x=scalar("/terrain/general/globalVerticalScale",1)*850;
    // Of these non-mountain graphs, only plains/steppe consume horizontalScale.
    next.land.y=(p.meta.w<=1?horizontal:1.0f)*1100;next.land.z=scalar("/world/properties/seaLevel",63);
    next.ftfScales={globalHorizontal,familyScalar("baseScale",1),familyScalar("verticalScale",1),std::min(worldHeight,256.0f)};
    if(p.meta.z>=4){next.land.y=1100;next.ftfScales.y=next.ftfScales.z=1;next.detail.x=scalar("/terrain/general/terrainRegionSize",2098);}
    next.climate.x=scalar("/climate/temperature/bias",0);next.climate.y=scalar("/climate/moisture/bias",0);next.climate.z=scalar("/climate/biomeShape/biomeSize",2600);
    for(auto v:{next.land,next.ftfScales,next.climate})for(int i=0;i<4;++i)if(!std::isfinite(v[i]))throw std::runtime_error("Preview paused: a derived value overflowed");
    p=next;
}
uint64_t PresetDocument::terrainSeedRoot(int64_t seed,uint32_t profile)const{
    auto ptr=PresetJson::json_pointer("/terrain/general/terrainSeedOffset");
    int64_t offset=data.contains(ptr)?data.at(ptr).get<int64_t>():0;
    if(offset<std::numeric_limits<int32_t>::min()||offset>std::numeric_limits<int32_t>::max())throw std::runtime_error("Preview paused: terrain seed offset must fit an FTF integer");
    // TerrainProvider: steppe(5), plains(5), dales(4), hills1(4), hills2(5), torridonian(9), plateau(9), badlands(6).
    const uint32_t offsets[]={5,0,10,14,18,32,41,23};
    return static_cast<uint64_t>(seed)+static_cast<uint64_t>(offset)+offsets[std::min(profile,7u)];
}
bool randomiseSeedButton(int64_t& value,bool worldSeed){
    bool clicked=ImGui::Button("##randomiseSeed",{28,24});auto a=ImGui::GetItemRectMin();auto* draw=ImGui::GetWindowDrawList();auto ink=ImGui::GetColorU32(ImGuiCol_Text);ImVec2 c{a.x+14,a.y+12};draw->AddRect({c.x-7,c.y-7},{c.x+7,c.y+7},ink,3,0,1.5f);for(auto v:{ImVec2(-4,-4),ImVec2(0,0),ImVec2(4,4),ImVec2(-4,4),ImVec2(4,-4)})draw->AddCircleFilled({c.x+v.x,c.y+v.y},1.25f,ink);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Randomise seed");if(!clicked)return false;
    static std::mt19937_64 generator(std::random_device{}());
    std::uniform_int_distribution<int64_t> distribution(worldSeed?std::numeric_limits<int64_t>::min():std::numeric_limits<int32_t>::min(),worldSeed?std::numeric_limits<int64_t>::max():std::numeric_limits<int32_t>::max());
    int64_t next;do{next=distribution(generator);}while(next==value);value=next;return true;
}
bool numberControl(const char* label,float& value,float low,float high,const char* format){   // the shared value control; typed values may leave the slider range
    bool changed=valueSlider(label,value,low,high,format,0,false);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Click the number to type a value. Suggested slider range: %g to %g",low,high);
    return changed;
}
void PresetEditor::initialize(const std::filesystem::path& appRoot,GLFWwindow* native){
    root=appRoot;window=native;auto path=root/"assets"/"ftf-editor-schema.json";
    if(std::filesystem::exists(path)){std::ifstream input(path);input>>schema;}
    if(native&&!std::filesystem::exists(root/"library"))try{
        PresetDocument bundled;bundled.load(root/"presets"/"GPU World Gen.json");
        auto source=Datapack::read(root/"assets"/"ftf-datapack-template.zip");if(source.original==bundled.data)bundled.datapack=std::move(source);
        PresetLibrary(root/"library").save(bundled,bundled.worldSeed.value_or(12345),"GPU World Gen");
    }catch(const std::exception& e){status="Initial library preset could not be added: "+std::string(e.what());}
}
std::filesystem::path PresetEditor::choose(bool saving,bool pack)const{
    std::array<wchar_t,32768> path{};std::filesystem::path initial=root/(saving?"exports":"presets");
    std::filesystem::create_directories(initial);if(saving){auto title=document.name.empty()?std::string("FTF preset"):document.name;for(char& c:title)if(static_cast<unsigned char>(c)<32||std::string("<>:\"/\\|?*").find(c)!=std::string::npos)c='_';auto file=std::filesystem::path(std::u8string(title.begin(),title.end())).wstring()+(pack?L" - edited.zip":L" - edited.json");if(file.size()<path.size())std::copy(file.begin(),file.end(),path.begin());}
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=glfwGetWin32Window(window);dialog.lpstrFile=path.data();dialog.nMaxFile=static_cast<DWORD>(path.size());
    dialog.lpstrFilter=pack?L"FTF datapack (*.zip)\0*.zip\0":saving?L"FreeTerraForged preset (*.json)\0*.json\0":L"FTF presets and datapacks\0*.json;*.zip\0JSON preset\0*.json\0ZIP datapack\0*.zip\0";dialog.lpstrDefExt=pack?L"zip":L"json";dialog.lpstrInitialDir=initial.c_str();
    dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(saving?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    return (saving?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))?std::filesystem::path(path.data()):std::filesystem::path{};
}
bool PresetEditor::toolbar(int64_t& seed,bool compact){
    bool changed=false;
    if(ImGui::Button("Preset library...")){managerOpen=true;if(!libraryName[0])strncpy_s(libraryName,document.name.c_str(),_TRUNCATE);}
    if(compact){bool loaded=libraryWindow(seed);if(document.loaded())ImGui::TextWrapped("Preset: %s",document.name.c_str());if(!status.empty())ImGui::TextWrapped("%s",status.c_str());return loaded;}
    ImGui::SameLine();
    if(ImGui::Button("Import JSON or datapack...")){try{auto path=choose(false);if(!path.empty()){document.load(path);past.clear();future.clear();if(document.worldSeed)seed=*document.worldSeed;changed=true;status="Imported "+path.filename().string();}}catch(const std::exception& e){status=e.what();}}
    if(ImGui::Button("Import datapack folder...")){
        HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);IFileOpenDialog* picker=nullptr;
        if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker)))){
            picker->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
            if(SUCCEEDED(picker->Show(glfwGetWin32Window(window)))){IShellItem* item=nullptr;if(SUCCEEDED(picker->GetResult(&item))){PWSTR name=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&name))){
                try{document.load(std::filesystem::path(name));past.clear();future.clear();if(document.worldSeed)seed=*document.worldSeed;changed=true;status="Imported datapack folder";}catch(const std::exception& e){status=e.what();}CoTaskMemFree(name);
            }item->Release();}}picker->Release();
        }else status="Cannot open folder picker";if(SUCCEEDED(init))CoUninitialize();
    }
    ImGui::SameLine();ImGui::BeginDisabled(!document.loaded());
    if(ImGui::Button("Export preset...")){try{auto path=choose(true);if(!path.empty()){document.exportTo(path);document.markSaved();status="Exported "+path.string();}}catch(const std::exception& e){status=e.what();}}ImGui::EndDisabled();
    ImGui::BeginDisabled(!document.loaded());
    if(ImGui::Button("Export FTF datapack...")){try{auto path=choose(true,true);if(!path.empty()){document.worldSeed=seed;document.exportDatapack(path,root/"assets"/"ftf-datapack-template.zip");status="Exported Minecraft 1.21.1 datapack: "+path.string();}}catch(const std::exception& e){status=e.what();}}ImGui::EndDisabled();
    changed|=libraryWindow(seed);
    if(document.loaded()){ImGui::TextWrapped("Preset: %s",document.name.c_str());ImGui::TextDisabled("Click any number to type beyond its slider range.");}
    ImGui::TextDisabled("Erosion: FTF 22-Oct Phacelle + cliff relaxation");
    if(!status.empty())ImGui::TextWrapped("%s",status.c_str());return changed;
}
std::vector<std::string> PresetEditor::keys(const PresetJson& object,const std::string& path)const{
    std::vector<std::string> result;
    static const PresetJson empty=PresetJson::object();const auto& order=schema.contains("order")?schema["order"]:empty;std::string lookup=path;
    if(!order.contains(lookup)&&path.rfind("/terrain/",0)==0)lookup="/terrain/*";
    if(order.contains(lookup))for(const auto& key:order[lookup])if(object.contains(key.get<std::string>()))result.push_back(key.get<std::string>());
    for(auto it=object.begin();it!=object.end();++it)if(std::find(result.begin(),result.end(),it.key())==result.end())result.push_back(it.key());return result;
}
std::pair<double,double> PresetEditor::range(const std::string& path,const PresetJson& value)const{
    static const PresetJson empty=PresetJson::object();const auto& ranges=schema.contains("ranges")?schema["ranges"]:empty;if(ranges.contains(path))return {ranges[path][0].get<double>(),ranges[path][1].get<double>()};
    auto key=path.substr(path.find_last_of('/')+1);
    if(key=="seedOffset"||key=="terrainSeedOffset"||key=="spawnX"||key=="spawnZ")return {-2147483648.0,2147483647.0};
    if(key=="baseScale")return {0,2};if(key=="weight"||key=="verticalScale")return {0,10};if(key=="horizontalScale")return {.01,10};
    if(key.find("Size")!=std::string::npos||key.find("Scale")!=std::string::npos||key.find("Width")!=std::string::npos)return {1,1000};
    if(key=="scale"||key=="falloff"||key=="octaves"||key=="riverCount")return {1,10};
    if(value.is_number_integer())return {0,256};return {0,1};
}
PreviewBinding PresetEditor::bindingFor(const std::string& path,const TerrainParams& params)const{
    auto result=::previewBinding(path,params);
    if(path.starts_with("/world/continent/")){
        auto type=document.data.value("world",PresetJson::object()).value("continent",PresetJson::object()).value("continentType",std::string("UPLIFT"));bool simple=type=="MULTI"||type=="SINGLE";
        auto key=path.substr(path.find_last_of('/')+1);bool ignored=key=="continentShape"?!simple:simple&&(key=="continentSkipping"||key=="continentSizeVariance"||key.starts_with("continentNoise"));
        if(ignored){result.effect=PreviewEffect::SavedOnly;result.explanation="The selected "+type+" generator ignores this setting in FTF. Preserved for other continent types.";}
    }return result;
}
bool PresetEditor::hasPreview(const PresetJson& value,const std::string& path,const TerrainParams& params)const{
    if(!value.is_structured())return bindingFor(path,params).capable();
    if(value.is_object()){for(auto it=value.begin();it!=value.end();++it)if(hasPreview(it.value(),path+"/"+it.key(),params))return true;}
    else for(size_t i=0;i<value.size();++i)if(hasPreview(value[i],path+"/"+std::to_string(i),params))return true;
    return false;
}
void PresetEditor::edited(const std::string& path,const std::string& label,const PreviewBinding& binding,TerrainParams& params){
    std::string title=label;if(path.rfind("/terrain/",0)==0){auto end=path.find('/',9);if(end!=std::string::npos)title=readable(path.substr(9,end-9))+": "+label;}
    feedbackLive=false;
    if(binding.updatesView(params,followEditedSetting)){
        if(binding.effect==PreviewEffect::World||binding.effect==PreviewEffect::WorldSurface)feedback=title+": Surface preview updated.";
        else if(binding.effect==PreviewEffect::Regions)feedback=title+": region map updated.";
        else if(binding.effect==PreviewEffect::LandMix)feedback=title+": partial land mix updated.";
        else{params.meta.w=binding.profile;feedback=title+": previewing "+std::string(terrainProfileName(binding.profile))+".";}
        feedbackLive=true;
    }else if(binding.effect==PreviewEffect::World||binding.effect==PreviewEffect::WorldSurface)feedback=title+": saved. Select Generator: FTF surface (all continents) to preview it; the current view is unchanged.";
    else if(binding.effect==PreviewEffect::LandMix)feedback=title+": saved. Select Generator: FTF land mix (partial) or FTF surface to preview it; the current view is unchanged.";
    else if(binding.effect==PreviewEffect::Regions)feedback=title+": saved. Select Generator: FTF region map to inspect it; the current view is unchanged.";
    else if(binding.effect==PreviewEffect::Terrain&&params.meta.z!=2)feedback=title+": saved. Select Generator: FTF terrain - isolated to preview it; the current view is unchanged.";
    else if(binding.capable())feedback=title+": saved. Select "+std::string(terrainProfileName(binding.profile))+" or enable Auto-select edited family.";
    else if(binding.effect==PreviewEffect::Limited)feedback=title+": normalization only below 256; full world-height processing is unfinished.";
    else if(path.ends_with("/weight"))feedback=title+": saved only. This family is not included in the partial land mix.";
    else feedback=title+": saved only. "+binding.explanation;
}
bool PresetEditor::field(const std::string& path,TerrainParams& params){
    auto finish=[&](bool changed){if(!drawingProperties&&!pendingReset.empty()){auto target=pendingReset;pendingReset.clear();changed|=resetToSaved(target);}return changed;};
    auto pointer=PresetJson::json_pointer(path);if(!document.data.contains(pointer)){
        auto fallback=optionalPresetDefault(path);if(!fallback)throw std::runtime_error("Unknown preset field: "+path);
        bool changed=node(*fallback,path,readable(path.substr(path.find_last_of('/')+1))+" (FTF default)",params);if(changed){if(path.starts_with("/surface/erosion/")&&!document.data.contains("surface"))document.data["surface"]=*optionalPresetDefault("/surface");document.data[pointer]=*fallback;}return finish(changed);
    }
    return finish(node(document.data[pointer],path,readable(path.substr(path.find_last_of('/')+1)),params));
}
bool PresetEditor::node(PresetJson& value,const std::string& path,const std::string& label,TerrainParams& params){
    if(!showAllProperties&&!path.starts_with("/rivers/branchRivers")&&!hasPreview(value,path,params))return false;
    ImGui::PushID(path.c_str());bool changed=false;
    auto binding=bindingFor(path,params);
    if(!value.is_structured()&&!value.is_null()){
        float left=ImGui::GetCursorPosX();bool previewed=binding.updatesView(params,followEditedSetting);
        ImGui::TextColored(previewed?ImVec4(.4f,.86f,.72f,1):ImVec4(.96f,.69f,.35f,1),binding.effect==PreviewEffect::Limited?"LIMITED":previewed?"PREVIEW":binding.capable()?"OTHER":"SAVED");
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",binding.explanation.c_str());
        ImGui::SameLine();ImGui::SetCursorPosX(left+76);bool selected=lastEditedPath==path;if(selected)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.8f,.36f,1));
        ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x-30);ImGui::TextWrapped("%s",label.c_str());ImGui::PopTextWrapPos();if(selected)ImGui::PopStyleColor();
        ImGui::SameLine();ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x-24);resetControl(path);

    }
    bool highlightLeaf=!value.is_structured()&&lastEditedPath==path;if(highlightLeaf)ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.26f,.24f,.09f,1));
    if(value.is_object()){
        int family=-1;if(path.starts_with("/terrain/")&&path.find('/',9)==std::string::npos)family=terrainFamilyId(path.substr(9));
        if(family>=0){terrainTypeIcon(family);ImGui::SameLine();}
        bool opened=ImGui::TreeNodeEx(label.c_str(),ImGuiTreeNodeFlags_DefaultOpen);
        if(family>=0){
            ImGui::SameLine();ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x-24-2*(23+6));   /* highlight, isolate and reset evenly spaced */
            bool selected=terrainSelection==family&&highlightTerrain;if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.5f,.36f,.1f,1));bool light=ImGui::Button("##highlightRegion",{23,19});if(selected)ImGui::PopStyleColor();auto a=ImGui::GetItemRectMin();auto* d=ImGui::GetWindowDrawList();auto ink=ImGui::GetColorU32(ImGuiCol_Text);d->AddCircle({a.x+11,a.y+8},4,ink,16,1.4f);d->AddLine({a.x+8,a.y+15},{a.x+14,a.y+15},ink,1.7f);if(ImGui::IsItemHovered())ImGui::SetTooltip("Highlight %s regions (view only)",label.c_str());if(light){highlightTerrain=!selected;terrainSelection=family;}
            ImGui::SameLine(0,6);bool isolating=terrainSelection==family&&isolateTerrain;if(isolating)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.12f,.48f,.4f,1));bool eye=ImGui::Button("##isolateRegion",{23,19});if(isolating)ImGui::PopStyleColor();a=ImGui::GetItemRectMin();d->AddEllipse({a.x+11,a.y+9},{8,4},ink,0,20,1.3f);d->AddCircleFilled({a.x+11,a.y+9},2,ink);if(ImGui::IsItemHovered())ImGui::SetTooltip("Isolate %s regions (view only; never saved to preset)",label.c_str());if(eye){isolateTerrain=!isolating;terrainSelection=family;}
        }
        ImGui::SameLine(0,6);if(family<0)ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x-24);resetControl(path);
        if(opened){
            auto visible=value;if(path=="/world/continent"&&!visible.contains("continentNoiseOctaves"))visible["continentNoiseOctaves"]=5;
            if((path=="/climate/temperature"||path=="/climate/moisture")&&!visible.contains("distributionBias"))visible["distributionBias"]=0.0;
            if(path=="/world/properties"&&!visible.contains("worldDepth"))visible["worldDepth"]=64;
            if(path=="/erosionFilters")for(const char* key:erosionOptionalFields)if(!visible.contains(key))visible[key]=*optionalPresetDefault(path+"/"+key);
            if(path=="/island")for(const char* key:islandOptionalFields)if(!visible.contains(key))visible[key]=*optionalPresetDefault(path+"/"+key);
            for(const auto& key:keys(visible,path)){
                if(path=="/climate/biomeShape"&&key.rfind("underground",0)==0)continue;
                if(value.contains(key))changed|=node(value[key],path+"/"+key,readable(key),params);
                else{auto temporary=visible[key];bool added=node(temporary,path+"/"+key,readable(key)+" (FTF default)",params);if(added){value[key]=temporary;changed=true;}}
            }ImGui::TreePop();
        }
    }else if(value.is_boolean()){bool b=value.get<bool>();if(ImGui::Checkbox("##boolean",&b)){value=b;changed=true;}}
    else if(value.is_number()){
        auto [low,high]=range(path,value);
        auto key=path.substr(path.find_last_of('/')+1);bool seedField=value.is_number_integer()&&(key=="seed"||key=="seedOffset"||key=="terrainSeedOffset");
        float numericWidth=valueBoxWidth,spacing=ImGui::GetStyle().ItemSpacing.x,buttonWidth=seedField?28+spacing:0;
        ImGui::SetNextItemWidth(std::max(35.0f,ImGui::GetContentRegionAvail().x-numericWidth-spacing-buttonWidth));
        if(value.is_number_integer()){
            int64_t number=value.get<int64_t>(),lo=static_cast<int64_t>(low),hi=static_cast<int64_t>(high),slider=applyOnRelease&&dragPath==path?int64_t(dragValue):number;
            if(ImGui::SliderScalar("##slider",ImGuiDataType_S64,&slider,&lo,&hi,"",ImGuiSliderFlags_NoInput)){if(applyOnRelease){dragPath=path;dragValue=double(slider);}else{number=slider;changed=true;}}
            if(applyOnRelease&&dragPath==path&&!ImGui::IsItemActive()){number=int64_t(dragValue);changed=number!=value.get<int64_t>();dragPath.clear();}
            ImGui::SameLine();ImGui::SetNextItemWidth(numericWidth);if(ImGui::InputScalar("##number",ImGuiDataType_S64,&number))changed=true;
            if(seedField){ImGui::SameLine();changed|=randomiseSeedButton(number);}
            if(changed){if(number>=std::numeric_limits<int32_t>::min()&&number<=std::numeric_limits<int32_t>::max())value=number;else{changed=false;status="FTF integer fields must fit a signed 32-bit integer.";}}
        }else{
            double number=value.get<double>(),slider=applyOnRelease&&dragPath==path?dragValue:number;
            if(ImGui::SliderScalar("##slider",ImGuiDataType_Double,&slider,&low,&high,"",ImGuiSliderFlags_NoInput)){if(applyOnRelease){dragPath=path;dragValue=slider;}else{number=slider;changed=true;}}
            if(applyOnRelease&&dragPath==path&&!ImGui::IsItemActive()){number=dragValue;changed=number!=value.get<double>();dragPath.clear();}
            ImGui::SameLine();ImGui::SetNextItemWidth(numericWidth);if(ImGui::InputDouble("##number",&number,0,0,"%.9g"))changed=true;
            if(changed){if(std::isfinite(number)&&std::abs(number)<=std::numeric_limits<float>::max())value=number;else{changed=false;status="FTF float fields need a finite 32-bit floating-point value.";}}
        }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\nClick to type beyond the suggested range: %g to %g\nFTF numeric type limits apply.",binding.explanation.c_str(),low,high);
    }else if(auto choices=[&]()->std::vector<const char*>{   // FTF enum fields are dropdowns; an unknown value in a preset stays selectable
            if(!value.is_string())return {};const auto key=path.substr(path.find_last_of('/')+1);
            if(path=="/world/continent/continentType")return {"MULTI","SINGLE","MULTI_IMPROVED","UPLIFT"};
            if(path=="/world/continent/continentShape")return {"EUCLIDEAN","MANHATTAN","NATURAL"};
            if(path=="/world/properties/spawnType")return {"CONTINENT_CENTER","ISLANDS","USER_SELECTED","WORLD_ORIGIN"};
            if(key=="type")return {"SIMPLEX","SIMPLEX2","SIMPLEX_RIDGE","PERLIN","PERLIN2","PERLIN_RIDGE","RIDGE","BILLOW","CUBIC"}; // the noise types the datapack export maps
            return {};}();!choices.empty()){
        auto selected=value.get<std::string>();ImGui::SetNextItemWidth(-1);
        if(std::find_if(choices.begin(),choices.end(),[&](const char* c){return selected==c;})==choices.end())choices.push_back(nullptr);
        if(ImGui::BeginCombo("##choice",selected.c_str())){for(const char* type:choices){const char* name=type?type:selected.c_str();bool active=selected==name;if(ImGui::Selectable(name,active)&&!active){value=std::string(name);changed=true;}if(active)ImGui::SetItemDefaultFocus();}ImGui::EndCombo();}
    }else if(value.is_string()){
        std::array<char,2048> text{};auto original=value.get<std::string>();std::copy_n(original.c_str(),std::min(original.size(),text.size()-1),text.data());
        ImGui::SetNextItemWidth(-1);if(ImGui::InputText("##text",text.data(),text.size())){value=std::string(text.data());changed=true;}
    }else if(value.is_array()){
        bool open=ImGui::TreeNode(label.c_str());ImGui::SameLine();ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x-24);resetControl(path);if(open){for(size_t i=0;i<value.size();++i)changed|=node(value[i],path+"/"+std::to_string(i),std::to_string(i),params);ImGui::TreePop();}
    }
    if(highlightLeaf)ImGui::PopStyleColor();
    if(changed&&!value.is_structured()){lastEditedPath=path;edited(path,label,binding,params);}
    ImGui::PopID();return changed;
}
bool PresetEditor::properties(TerrainParams& params){
    if(!document.loaded())return false;if(document.savedData.is_null())document.markSaved();auto before=document.data;bool changed=false;drawingProperties=true;
    ImGui::Checkbox("Show all FTF preset properties",&showAllProperties);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Show every preserved FTF field. Amber SAVED fields can be edited and exported but do not affect this preview.");
    ImGui::Checkbox("Apply on release",&applyOnRelease);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Off: the terrain follows a slider while you drag it. On: the value is applied once you let go of the slider.");
    if(params.meta.z==2){ImGui::Checkbox("Auto-select edited family (isolated)",&followEditedSetting);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Within the isolated terrain view, select the family being edited. Never changes Generator or opens diagnostics. Turn off to keep the selected family fixed.");}
    else ImGui::TextDisabled("View mode stays fixed while editing.");
    ImGui::TextWrapped(showAllProperties?"PREVIEW affects the current view. SAVED is export only. FTF surface includes all continent types, oceans, islands and Minecraft surface biome colors. Rivers, lakes, wetlands and world filters are connected.":"Showing connected controls. Use FTF surface for continent/ocean settings and combined terrain. Enable all properties for export-only settings.");
    auto sectionHeader=[&](const char* title,const std::string& path,ImGuiTreeNodeFlags flags=0){bool open=false;if(ImGui::BeginTable(("##header"+path).c_str(),2,ImGuiTableFlags_SizingStretchProp)){ImGui::TableSetupColumn("",ImGuiTableColumnFlags_WidthStretch);ImGui::TableSetupColumn("",ImGuiTableColumnFlags_WidthFixed,24);ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);if(lastEditedPath==path)ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(.36f,.29f,.12f,1));open=sectionIconHeader(title,flags);if(lastEditedPath==path)ImGui::PopStyleColor();ImGui::TableSetColumnIndex(1);resetControl(path);ImGui::EndTable();}return open;};
    const std::pair<const char*,const char*> sections[]={{"world","World"},{"underground","Underground"},{"climate","Climate"},{"terrain","Terrain"},{"rivers","Rivers"},{"island","Islands"},{"erosionFilters","Erosion filters"},{"surface","Surface and scree"},{"miscellaneous","Miscellaneous"}};
    for(auto [key,title]:sections){

        if(std::strcmp(key,"surface")==0&&!document.data.contains("surface")){
            if((showAllProperties||params.meta.z==5)&&sectionHeader(title,"/"+std::string(key))){if(document.data.contains("miscellaneous"))for(auto field:{"screeDecorator","erosionDecorator"})if(document.data["miscellaneous"].contains(field))changed|=node(document.data["miscellaneous"][field],"/miscellaneous/"+std::string(field),"Erosion decorator",params);changed|=this->field("/surface",params);}
        }else if(std::strcmp(key,"underground")==0){
            if(showAllProperties&&sectionHeader(title,"/"+std::string(key))){if(document.data.contains("caves"))changed|=node(document.data["caves"],"/caves","Caves",params);
                auto ptr=PresetJson::json_pointer("/climate/biomeShape");if(document.data.contains(ptr))for(const auto& property:keys(document.data[ptr],"/climate/biomeShape"))if(property.rfind("underground",0)==0)changed|=node(document.data[ptr][property],"/climate/biomeShape/"+property,readable(property),params);}
        }else if(document.data.contains(key)&&(showAllProperties||hasPreview(document.data[key],"/"+std::string(key),params))&&sectionHeader(title,"/"+std::string(key),std::strcmp(key,"terrain")==0?ImGuiTreeNodeFlags_DefaultOpen:0)){
            if(std::strcmp(key,"surface")==0&&document.data.contains("miscellaneous")){
                for(auto field:{"screeDecorator","erosionDecorator"})if(document.data["miscellaneous"].contains(field))changed|=node(document.data["miscellaneous"][field],"/miscellaneous/"+std::string(field),"Erosion decorator",params);
            }
            auto visible=document.data[key];
            if(std::strcmp(key,"climate")==0&&!visible.contains("coastalMoistureBoost"))visible["coastalMoistureBoost"]=.2;
            if(std::strcmp(key,"erosionFilters")==0)for(const char* field:erosionOptionalFields)if(!visible.contains(field))visible[field]=*optionalPresetDefault("/erosionFilters/"+std::string(field));
            if(std::strcmp(key,"island")==0)for(const char* field:islandOptionalFields)if(!visible.contains(field))visible[field]=*optionalPresetDefault("/island/"+std::string(field));
            for(auto& child:keys(visible,"/"+std::string(key))){
                if(std::strcmp(key,"miscellaneous")==0&&(child=="screeDecorator"||child=="erosionDecorator"))continue;
                if(document.data[key].contains(child))changed|=node(document.data[key][child],"/"+std::string(key)+"/"+child,readable(child),params);
                else {auto temporary=visible[child];bool added=node(temporary,"/"+std::string(key)+"/"+child,readable(child)+" (FTF default)",params);if(added){document.data[key][child]=temporary;changed=true;}}
                if(std::strcmp(key,"rivers")==0&&child=="riverCount"&&document.data.contains("flow"))changed|=node(document.data["flow"],"/flow","Flow",params);
            }
        }
    }
    if(showAllProperties&&sectionIconHeader("Additional preserved fields"))for(auto it=document.data.begin();it!=document.data.end();++it){bool known=false;for(auto section:sections)known|=it.key()==section.first;if(it.key()=="caves"||it.key()=="flow")known=true;if(!known)changed|=node(it.value(),"/"+it.key(),readable(it.key()),params);}
    drawingProperties=false;if(!pendingReset.empty()){auto path=pendingReset;pendingReset.clear();changed|=resetToSaved(path,false);editing=false;}
    if(changed&&document.data!=before){if(!editing){if(past.size()>=100)past.erase(past.begin());past.push_back(std::move(before));}future.clear();editing=ImGui::IsAnyItemActive();}
    if(!ImGui::IsAnyItemActive())editing=false;return changed;
}
bool PresetEditor::undo(){if(past.empty())return false;future.push_back(document.data);auto diff=PresetJson::diff(document.data,past.back());document.data=past.back();past.pop_back();if(!diff.empty())lastEditedPath=diff[0].value("path","");return true;}
bool PresetEditor::redo(){if(future.empty())return false;past.push_back(document.data);auto diff=PresetJson::diff(document.data,future.back());document.data=future.back();future.pop_back();if(!diff.empty())lastEditedPath=diff[0].value("path","");return true;}

void PresetEditor::loadDocument(const std::filesystem::path& path,int64_t& seed){document.load(path);clearHistory();if(document.worldSeed)seed=*document.worldSeed;}
bool PresetEditor::importFolder(int64_t& seed){bool changed=false;
        HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);IFileOpenDialog* picker=nullptr;
        if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker)))){
            picker->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
            if(SUCCEEDED(picker->Show(glfwGetWin32Window(window)))){IShellItem* item=nullptr;if(SUCCEEDED(picker->GetResult(&item))){PWSTR name=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&name))){
                try{document.load(std::filesystem::path(name));past.clear();future.clear();if(document.worldSeed)seed=*document.worldSeed;changed=true;status="Imported datapack folder";}catch(const std::exception& e){status=e.what();}CoTaskMemFree(name);
            }item->Release();}}picker->Release();
        }else status="Cannot open folder picker";if(SUCCEEDED(init))CoUninitialize();
return changed;
}
void PresetEditor::exportAction(bool pack,int64_t seed){try{auto path=choose(true,pack);if(!path.empty()){document.worldSeed=seed;if(pack)document.exportDatapack(path,root/"assets"/"ftf-datapack-template.zip");else document.exportTo(path);document.markSaved();status="Exported "+path.string();}}catch(const std::exception& e){status=e.what();}}

glm::vec2 PresetDocument::spawn()const{auto coord=[&](const char* name){auto p=PresetJson::json_pointer(std::string("/world/properties/")+name);return data.contains(p)&&data[p].is_number()?data[p].get<float>():0.0f;};return {coord("spawnX"),coord("spawnZ")};}

static std::vector<std::string> savedResetPaths(const PresetDocument& doc,const std::string& path){
    if(path=="/surface")return {"/surface","/miscellaneous/screeDecorator","/miscellaneous/erosionDecorator"};
    if(path=="/rivers")return {"/rivers","/flow"};
    if(path=="/underground"){std::vector<std::string> result{"/caves"};for(const PresetJson* data:std::array<const PresetJson*,2>{&doc.data,&doc.savedData}){auto p=PresetJson::json_pointer("/climate/biomeShape");if(data->contains(p))for(auto it=(*data)[p].begin();it!=(*data)[p].end();++it)if(it.key().starts_with("underground"))result.push_back("/climate/biomeShape/"+it.key());}return result;}
    return {path};
}
bool PresetEditor::canReset(const std::string& path)const{
    if(document.savedData.is_null())return false;
    for(const auto& item:savedResetPaths(document,path)){if(item.empty()){if(document.data!=document.savedData)return true;continue;}auto p=PresetJson::json_pointer(item);
        if(item.starts_with("/surface/erosion/")&&!document.savedData.contains("surface")){auto fallback=optionalPresetDefault(item);if(fallback&&document.data.contains(p)&&document.data[p]!=*fallback)return true;continue;}
        if(document.savedData.contains(p)?!document.data.contains(p)||document.data[p]!=document.savedData[p]:document.data.contains(p))return true;
    }return false;
}
bool PresetEditor::resetToSaved(const std::string& path,bool history){
    if(!canReset(path))return false;auto before=document.data;
    for(const auto& item:savedResetPaths(document,path)){
        if(item.empty()){document.data=document.savedData;continue;}auto p=PresetJson::json_pointer(item);
        if(document.savedData.contains(p))document.data[p]=document.savedData[p];
        else if(item.starts_with("/surface/")&&!document.savedData.contains("surface")){auto fallback=optionalPresetDefault(item);if(fallback&&document.data.contains("surface"))document.data[p]=*fallback;if(document.data.contains("surface")&&document.data["surface"]==*optionalPresetDefault("/surface"))document.data.erase("surface");}
        else{auto slash=item.find_last_of('/');auto parent=PresetJson::json_pointer(item.substr(0,slash));if(document.data.contains(parent)){auto& container=document.data[parent];if(container.is_array()){size_t index=std::stoull(item.substr(slash+1));if(index<container.size())container.erase(index);}else container.erase(item.substr(slash+1));}}
    }
    if(history){if(past.size()>=100)past.erase(past.begin());past.push_back(std::move(before));future.clear();}lastEditedPath=path;status="Reset to last saved preset value";return true;
}
bool PresetEditor::resetControl(const std::string& path){
    ImGui::PushID(path.c_str());ImGui::BeginDisabled(!canReset(path));bool clicked=ImGui::Button("##resetSaved",{22,19});auto a=ImGui::GetItemRectMin();auto* draw=ImGui::GetWindowDrawList();auto ink=ImGui::GetColorU32(ImGuiCol_Text);ImVec2 c{a.x+11,a.y+10};draw->PathArcTo(c,5,-2.5f,2.1f,16);draw->PathStroke(ink,0,1.4f);draw->AddTriangleFilled({c.x-6,c.y-5},{c.x,c.y-5},{c.x-5,c.y},ink);ImGui::EndDisabled();if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Reset to the preset's last explicit save/load");ImGui::PopID();if(clicked)pendingReset=path;return clicked;
}
bool PresetEditor::placeSpawn(glm::vec2 point){auto before=document.data;document.set("/world/properties/spawnX",int32_t(std::round(point.x)));document.set("/world/properties/spawnZ",int32_t(std::round(point.y)));if(document.data.contains(PresetJson::json_pointer("/world/properties/spawnType")))document.set("/world/properties/spawnType","USER_SELECTED");if(document.data==before)return false;past.push_back(std::move(before));future.clear();lastEditedPath="/world/properties/spawnZ";status="Placed preset spawn";return true;}

// Reuse undo/redo so jumping retains every intermediate state and its redo branch.
bool PresetEditor::jumpHistory(size_t index){
    if(index>=historySize()||index==past.size())return false;
    while(past.size()>index)undo();while(past.size()<index)redo();return true;
}
bool PresetEditor::historyPanel(){
    bool changed=false;
    if(sectionIconHeader("Edit history")){
        ImGui::TextDisabled("Click any state to restore it");
        ImGui::BeginChild("History states",{0,180},true);
        size_t current=past.size(),total=historySize();
        auto state=[&](size_t i)->const PresetJson&{return i<current?past[i]:i==current?document.data:future[total-1-i];};
        for(size_t i=0;i<total;++i){std::string label=i?"Edit "+std::to_string(i):"Initial state";
            if(i){auto diff=PresetJson::diff(state(i-1),state(i));if(!diff.empty()){auto path=diff[0].value("path","");label+="  "+path;}}
            if(ImGui::Selectable((label+"##history"+std::to_string(i)).c_str(),i==current)){changed=jumpHistory(i);break;}
        }ImGui::EndChild();
    }return changed;
}
