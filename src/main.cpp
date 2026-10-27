#include <windows.h>
#include <dbghelp.h>
#include "surface.hpp"
#include "edit_preview.hpp"
#include "app_info.hpp"
#include "ui_icons.hpp"
#include "project.hpp"
#include "dock_panels.hpp"
#include "viewport_icons.hpp"
#include "trees.hpp"
#include "library.hpp"
#include <commdlg.h>
#include <shellapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include "window_chrome.hpp"
#include "preset.hpp"
#include "mix.hpp"
#include "world.hpp"
#include "rivers.hpp"
#include "biome_census.hpp"
#include "mesh_heights.hpp"
#include <imgui_impl_glfw.h>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <thread>

using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
struct Options {
    int testBiome=-1,biomeColors=3;int frames=0;uint32_t grid=2048,profile=0;bool benchmark=false,hidden=false,primitive=false,plains=true,regions=false,mix=false,exercise=false,profileExercise=false,weightExercise=false,noPreset=false,presetOnly=false,seedSpecified=false,originSpecified=false;
    std::string sliderEdit,previewTrace;int sliderStartFrame=30,sliderEndFrame=120;bool maskExercise=false,refineCancelTest=false;
    std::string report,screenshot,uiScreenshot,samples,preset,exportPreset,exportDatapack,inputs,cellSamples,biomeSamples,riverInputs;std::vector<std::string> edits;
    int materialDebug=-1;   // --material-debug k: 24-bit slice k of the material column context instead of block ids
    bool exerciseCopy=false;   // --exercise-copy: K, click the viewport, report the copied text (report "copy_tool")
    int pickCheck=0;     // --pick-check N: N x N cursor picks, old coarse vs mesh march vs a full-resolution reference (report "pick_check")
    int countBiomes=0;   // --count-biomes N: hidden biome count at an N-sample grid (report "biome_count")
    bool exitTest=false,graphicsTest=false,poseTest=false,fovSpecified=false;int exitAction=0;int cullTest=0;float fovTest=60;std::string openProject;bool noFiner=false,noMountainCache=false,library=false,testMaterialIDs=false,distanceLod=false,exerciseSaveAs=false,exercisePanels=false,exercisePanelRestore=false,noAdaptiveDetail=false;int exerciseBiomeOff=0;int findBiome=-1;uint32_t biomeMods=0;float findRadius=0;
    int previewMenu=0;bool lodWait=false;
    std::string atmosphere; // test: comma list of water,fog,clouds forced on
    bool profileSteps=false;int orbitFrom=-1,orbitTo=-1,shortcutTest=0;bool exerciseReset2=false;int windowW=1440,windowH=900; /* test: --window-size for tall UI screenshots */uint32_t directLimit=0,biomeBand=1; // test: --biome-band workgroup rows per biome-pass band in progressive builds (0 = whole grid); directLimit: largest grid regenerated in one submission when the editing preview is off (0 = 1024 interactive, unlimited hidden)
    bool providerOnly=false,exerciseDetail=false,exerciseCenter=false,exerciseDetailOff=false,exerciseZoom=false,ambientOcclusion=false,cloudShadows=false;float shadowTime=-1,aoIntensity=1,shadowIntensity=.34f,shadowSpeed=1;std::string motionStart,testSessionRoot;bool testRestore=false,exerciseDetailZoom=false,exerciseFileMenu=false,exerciseLowOrbit=false,testRestoreAccept=false,previewViewMenu=false,previewLodMenu=false,warmup=false,exerciseLoading=false,windowed=false,animate=false,fullscreenTest=false,exercisePoints=false,exerciseFly=false,exerciseWalk=false,walkAt=false,cameraSet=false,noTrees=false,previewTest=false,noEditPreview=false,noSelective=false,showTrees=false,exerciseStreaming=false,noDistanceLod=false,exerciseEscape=false,exerciseZoomGradual=false;std::string ftfVersion;int walkHold=32;float walkTurn=.6f,walkDt=1.0f/30.0f;bool walkTrace=false;float walkAtX=0,walkAtZ=0,cameraDistance=1,cameraPitch=.58f,panX=0,panZ=0;int animateUntil=0;float detailRadius=0;std::string detailSamples;
    bool uplift=false,upliftMap=false,extentSpecified=false,sunShadows=false,exerciseHover=false,exerciseReset=false,exerciseSpawn=false,exerciseSpawnHistory=false,exerciseMeasure=false,exercisePan=false,exercisePanVertical=false;int testHighlight=-1,testIsolate=-1;float sunAzimuth=305,sunElevation=53,sunIntensity=1,sunShadowStrength=.65f;
    bool oceanFloor=false,noHeightHighlight=false,generatorSpecified=false;
    bool noIslands=false,noClimate=false,noWorldFilters=false,noRivers=false;
    std::string exerciseEdit="/terrain/steppe/weight=0.0";
    float extent=8192,scale=1100,originX=0,originZ=0;int64_t seed=12345;
};
static Options arguments(int argc,char** argv){
    Options o;for(int i=1;i<argc;++i){std::string key=argv[i];if(key=="--primitive"||key=="--plains"||key=="--demo"||key=="--regions"||key=="--mix"||key=="--uplift"||key=="--uplift-map"||key=="--terrain")o.generatorSpecified=true;auto value=[&](){if(i+1>=argc)throw std::runtime_error("Missing argument after "+key);return std::string(argv[++i]);};
        if(key=="--exercise-refine-cancel"){o.refineCancelTest=true;continue;}
        if(key=="--exercise-slider"){o.sliderEdit=value();continue;}
        if(key=="--slider-start-frame"){o.sliderStartFrame=std::stoi(value());continue;}
        if(key=="--slider-end-frame"){o.sliderEndFrame=std::stoi(value());continue;}
        if(key=="--preview-trace"){o.previewTrace=value();continue;}
        if(key=="--exercise-biome-mask"){o.maskExercise=true;continue;}
        if(key=="--editing-preview"){o.previewTest=true;continue;}
        if(key=="--no-editing-preview"){o.noEditPreview=true;continue;}
        if(key=="--no-selective-updates"){o.noSelective=true;continue;}
        if(key=="--atmosphere"){o.atmosphere=value();continue;}if(key=="--profile-steps"){o.profileSteps=true;continue;}if(key=="--biome-band"){o.biomeBand=uint32_t(std::stoul(value()));continue;}if(key=="--window-size"){o.windowW=std::stoi(value());o.windowH=std::stoi(value());continue;}if(key=="--exercise-reset"){o.exerciseReset2=true;continue;}if(key=="--shortcut-test"){o.shortcutTest=std::stoi(value());continue;}if(key=="--orbit-test"){o.orbitFrom=std::stoi(value());o.orbitTo=std::stoi(value());continue;}if(key=="--direct-limit"){o.directLimit=uint32_t(std::stoul(value()));continue;}
        if(key=="--walk-hold"){o.walkHold=std::stoi(value());continue;}if(key=="--walk-turn"){o.walkTurn=std::stof(value());continue;}if(key=="--walk-trace"){o.walkTrace=true;continue;}if(key=="--walk-dt"){o.walkDt=std::stof(value());continue;}
        if(key=="--count-biomes"){o.countBiomes=std::stoi(value());continue;}if(key=="--pick-check"){o.pickCheck=std::stoi(value());continue;}if(key=="--exercise-copy"){o.exerciseCopy=true;continue;}if(key=="--no-finer"){o.noFiner=true;continue;}if(key=="--open-project"){o.openProject=value();continue;}if(key=="--exit-action"){o.exitAction=std::stoi(value());o.exitTest=true;}else if(key=="--exercise-exit-prompt")o.exitTest=true;else if(key=="--exercise-graphics-restore")o.graphicsTest=true;else if(key=="--exercise-fly-pose")o.poseTest=true;else if(key=="--culling-test")o.cullTest=std::stoi(value());else if(key=="--fov"){o.fovTest=std::stof(value());o.fovSpecified=true;}else if(key=="--biome-mods")o.biomeMods=std::stoul(value());else if(key=="--exercise-biome-off")o.exerciseBiomeOff=1;else if(key=="--exercise-biome-checkbox-off")o.exerciseBiomeOff=2;else if(key=="--exercise-panel-restore"){o.exercisePanels=true;o.exercisePanelRestore=true;}else if(key=="--no-adaptive-detail")o.noAdaptiveDetail=true;else if(key=="--exercise-panels")o.exercisePanels=true;else if(key=="--exercise-save-as")o.exerciseSaveAs=true;else if(key=="--distance-lod")o.distanceLod=true;else if(key=="--find-biome")o.findBiome=std::stoi(value());else if(key=="--find-radius")o.findRadius=std::stof(value());else if(key=="--highlight-terrain")o.testHighlight=std::stoi(value());else if(key=="--isolate-terrain")o.testIsolate=std::stoi(value());else if(key=="--exercise-spawn")o.exerciseSpawn=true;else if(key=="--exercise-spawn-history"){o.exerciseSpawn=true;o.exerciseSpawnHistory=true;}else if(key=="--exercise-measure")o.exerciseMeasure=true;else if(key=="--exercise-pan")o.exercisePan=true;else if(key=="--exercise-pan-vertical"){o.exercisePan=true;o.exercisePanVertical=true;}else if(key=="--exercise-reset-camera"){o.exerciseCenter=true;o.exerciseReset=true;}else if(key=="--sun-azimuth")o.sunAzimuth=std::stof(value());else if(key=="--sun-elevation")o.sunElevation=std::stof(value());else if(key=="--sun-intensity")o.sunIntensity=std::stof(value());else if(key=="--terrain-shadows")o.sunShadows=true;else if(key=="--sun-shadow-strength")o.sunShadowStrength=std::stof(value());else if(key=="--exercise-hover")o.exerciseHover=true;else if(key=="--preview-view-menu")o.previewViewMenu=true;else if(key=="--preview-lod-menu")o.previewLodMenu=true;else if(key=="--animate")o.animate=true;else if(key=="--animate-until"){o.animate=true;o.animateUntil=std::stoi(value());}else if(key=="--fullscreen-test")o.fullscreenTest=true;else if(key=="--exercise-points")o.exercisePoints=true;else if(key=="--exercise-fly")o.exerciseFly=true;else if(key=="--camera"){o.cameraSet=true;o.cameraDistance=std::stof(value());o.cameraPitch=std::stof(value());}else if(key=="--pan"){o.cameraSet=true;o.panX=std::stof(value());o.panZ=std::stof(value());}else if(key=="--exercise-streaming")o.exerciseStreaming=true;else if(key=="--trees")o.showTrees=true;else if(key=="--no-trees")o.noTrees=true;else if(key=="--ftf-version")o.ftfVersion=value();else if(key=="--lod-wait")o.lodWait=true;else if(key=="--preview-menu")o.previewMenu=std::stoi(value());else if(key=="--no-distance-lod")o.noDistanceLod=true;else if(key=="--exercise-escape")o.exerciseEscape=true;else if(key=="--exercise-zoom-gradual")o.exerciseZoomGradual=true;else if(key=="--exercise-walk")o.exerciseWalk=true;else if(key=="--walk-at"){o.walkAt=true;o.walkAtX=std::stof(value());o.walkAtZ=std::stof(value());}else if(key=="--windowed")o.windowed=true;else if(key=="--exercise-loading")o.exerciseLoading=true;else if(key=="--warmup"){o.warmup=true;o.hidden=true;o.frames=1;}else if(key=="--exercise-low-orbit")o.exerciseLowOrbit=true;else if(key=="--test-restore-accept"){o.testRestore=true;o.testRestoreAccept=true;}else if(key=="--test-session-root")o.testSessionRoot=value();else if(key=="--test-restore")o.testRestore=true;else if(key=="--exercise-file-menu")o.exerciseFileMenu=true;else if(key=="--exercise-detail-zoom"){o.exerciseDetail=true;o.exerciseDetailZoom=true;}else if(key=="--ao-intensity")o.aoIntensity=std::stof(value());else if(key=="--shadow-intensity")o.shadowIntensity=std::stof(value());else if(key=="--shadow-speed")o.shadowSpeed=std::stof(value());else if(key=="--motion-start")o.motionStart=value();else if(key=="--exercise-detail-off"){o.exerciseDetail=true;o.exerciseDetailOff=true;}else if(key=="--exercise-zoom")o.exerciseZoom=true;else if(key=="--ambient-occlusion")o.ambientOcclusion=true;else if(key=="--cloud-shadows")o.cloudShadows=true;else if(key=="--shadow-time")o.shadowTime=std::stof(value());else if(key=="--exercise-center")o.exerciseCenter=true;else if(key=="--detail-radius")o.detailRadius=std::stof(value());else if(key=="--detail-samples")o.detailSamples=value();else if(key=="--exercise-detail")o.exerciseDetail=true;else if(key=="--no-height-highlight")o.noHeightHighlight=true;else if(key=="--test-material-ids")o.testMaterialIDs=true;else if(key=="--material-debug"){o.testMaterialIDs=true;o.materialDebug=std::stoi(value());}else if(key=="--test-biome")o.testBiome=std::stoi(value());else if(key=="--library")o.library=true;else if(key=="--frames")o.frames=std::stoi(value());else if(key=="--resolution")o.grid=std::stoul(value());
        else if(key=="--benchmark")o.benchmark=true;else if(key=="--hidden")o.hidden=true;else if(key=="--primitive")o.primitive=true;else if(key=="--plains")o.plains=true;else if(key=="--demo")o.plains=false;
        else if(key=="--report")o.report=value();else if(key=="--screenshot")o.screenshot=value();else if(key=="--ui-screenshot")o.uiScreenshot=value();else if(key=="--samples")o.samples=value();else if(key=="--cell-samples")o.cellSamples=value();else if(key=="--biome-samples")o.biomeSamples=value();else if(key=="--biome-colors"){auto mode=value();if(mode!="map"&&mode!="grass"&&mode!="foliage"&&mode!="blocks")throw std::runtime_error("Biome colors: map, grass, foliage or blocks");o.biomeColors=mode=="grass"?1:mode=="foliage"?2:mode=="blocks"?3:0;}else if(key=="--river-inputs")o.riverInputs=value();
        else if(key=="--exercise")o.exercise=true;
        else if(key=="--exercise-profiles")o.profileExercise=true;
        else if(key=="--exercise-weights")o.weightExercise=true;
        else if(key=="--exercise-edit"){o.weightExercise=true;o.exerciseEdit=value();}
        else if(key=="--no-mountain-cache")o.noMountainCache=true;
        else if(key=="--provider-only")o.providerOnly=true;
        else if(key=="--uplift")o.uplift=true;
        else if(key=="--uplift-map")o.upliftMap=true;
        else if(key=="--ocean-floor")o.oceanFloor=true;
        else if(key=="--no-islands")o.noIslands=true;
        else if(key=="--no-climate")o.noClimate=true;
        else if(key=="--no-world-filters")o.noWorldFilters=true;
        else if(key=="--no-rivers")o.noRivers=true;
        else if(key=="--regions")o.regions=true;
        else if(key=="--mix")o.mix=true;else if(key=="--inputs")o.inputs=value();
        else if(key=="--terrain"){auto name=value();const std::vector<std::string> profiles{"plains","steppe","dales","hills1","hills2","plateau","badlands","torridonian"};auto it=std::find(profiles.begin(),profiles.end(),name);if(it==profiles.end())throw std::runtime_error("Unknown terrain profile: "+name);o.profile=static_cast<uint32_t>(it-profiles.begin());}
        else if(key=="--no-preset")o.noPreset=true;else if(key=="--preset-only")o.presetOnly=true;
        else if(key=="--preset")o.preset=value();else if(key=="--export-preset")o.exportPreset=value();else if(key=="--export-datapack")o.exportDatapack=value();else if(key=="--set")o.edits.push_back(value());
        else if(key=="--extent"){o.extent=std::stof(value());o.extentSpecified=true;}else if(key=="--scale")o.scale=std::stof(value());
        else if(key=="--origin-x"){o.originX=std::stof(value());o.originSpecified=true;}else if(key=="--origin-z"){o.originZ=std::stof(value());o.originSpecified=true;}else if(key=="--seed"){o.seed=std::stoll(value());o.seedSpecified=true;}
        else if(key=="--help"){
            std::cout<<"FTFSurface [--resolution 64..8192] [--preset FILE | --no-preset] [--seed SIGNED_64_BIT]\n"
                "Export: --export-preset FILE or --export-datapack FILE [--set /json/path=JSON_VALUE] [--preset-only]\n"
                "Measure: --benchmark --frames 360 --hidden --report out/report.json\n"
                "Capture: --screenshot out/scene.ppm --ui-screenshot out/interface.ppm --samples out/field.bin\n"
                "Generators: FTF surface by default; --plains, --demo, --primitive, --regions, --mix, --uplift, --uplift-map\n"
                "Biomes: --biome-colors map|grass|foliage|blocks --biome-samples out/biomes.bin\n"
                "GPU inputs: --inputs out/inputs.bin --river-inputs out/rivers.bin --cell-samples out/cells.bin\n"
                "Reference stages: --no-mountain-cache --provider-only --no-islands --no-rivers --no-climate --no-world-filters\n"
                "Isolated terrain: --terrain plains|steppe|dales|hills1|hills2|plateau|badlands|torridonian\n"
                "Domain: --extent 8192 --scale 1100 --origin-x 0 --origin-z 0\n";std::exit(0);}
        else throw std::runtime_error("Unknown option: "+key);
    }
    if(o.grid<64||o.grid>8192||(o.grid&(o.grid-1))!=0)throw std::runtime_error("Resolution must be a power of two from 64 through 8192");
    if(o.countBiomes<0||o.countBiomes>4096)throw std::runtime_error("Invalid biome count grid");
    if(o.findBiome>4095||!std::isfinite(o.findRadius)||o.findRadius<0)throw std::runtime_error("Invalid biome search options");
    if(o.frames<0||!std::isfinite(o.extent)||o.extent<=0||!std::isfinite(o.scale)||o.scale<=0||!std::isfinite(o.originX)||!std::isfinite(o.originZ))throw std::runtime_error("Invalid numeric option");
    if(o.weightExercise&&((!o.mix&&!o.uplift)||o.frames<=30))throw std::runtime_error("Edit exercise needs --mix or --uplift and more than 30 frames");
    if(o.weightExercise&&o.exerciseEdit.find('=')==std::string::npos)throw std::runtime_error("Use --exercise-edit /preset/path=JSON_VALUE");
    if(o.hidden&&o.frames==0)throw std::runtime_error("Hidden runs need a finite --frames count");return o;
}
static double percentile(std::vector<double> values,double q){if(values.empty())return 0;std::sort(values.begin(),values.end());return values[std::min(values.size()-1,static_cast<size_t>(q*(values.size()-1)))];}
static uint32_t ftfSeed(uint64_t root){
    if(root<=0x7fffffffull||root>=0xffffffff80000000ull)return static_cast<uint32_t>(root);
    root=(root^(root>>33))*static_cast<uint64_t>(-49064778989728563ll);
    root=(root^(root>>33))*static_cast<uint64_t>(-4265267296055464877ll);root^=root>>33;return static_cast<uint32_t>(root^(root>>32));
}
static void updateGraphInputs(TerrainParams& p,GraphSeeds& graph,uint64_t root){
    for(uint32_t i=0;i<graph.values.size();++i)graph.values[i]=ftfSeed(root+i);
    if(p.meta.z>=3){p.seeds0={ftfSeed(root+789124)+7u,ftfSeed(root+8934),ftfSeed(root+8935),0};graph.regionSeeds=p.seeds0;return;}
    for(int i=0;i<4;++i)p.seeds0[i]=ftfSeed(root+i);
    p.seeds1.x=ftfSeed(root+4);
    p.seeds1.y=static_cast<uint32_t>(std::max(4.0f,std::round(250.0f*(p.land.y/1100.0f))));
}
static std::filesystem::path projectDialog(GLFWwindow* window,const std::filesystem::path& root,bool saving,const std::string& name="Untitled"){
    std::array<wchar_t,32768> path{};auto initial=root/"projects";std::filesystem::create_directories(initial);
    if(saving){auto title=name;for(char& c:title)if(std::string("<>:\"/\\|?*").find(c)!=std::string::npos)c='_';auto file=std::filesystem::path(title+".ftfproject").wstring();if(file.size()<path.size())std::copy(file.begin(),file.end(),path.begin());}
    OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=glfwGetWin32Window(window);d.lpstrFile=path.data();d.nMaxFile=static_cast<DWORD>(path.size());d.lpstrInitialDir=initial.c_str();d.lpstrDefExt=L"ftfproject";
    d.lpstrFilter=saving?L"Surface Lab project (*.ftfproject)\0*.ftfproject\0":L"Projects, presets and datapacks\0*.ftfproject;*.json;*.zip\0Surface Lab project\0*.ftfproject\0FTF preset or datapack\0*.json;*.zip\0";
    d.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(saving?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(!(saving?GetSaveFileNameW(&d):GetOpenFileNameW(&d)))return {};
    std::filesystem::path chosen(path.data());if(saving&&chosen.extension()!=L".ftfproject")chosen+=L".ftfproject";return chosen;
}
static std::filesystem::path stateDirectory;
static void saveSettings(const TerrainParams& p,int64_t worldSeed){
    Json j={{"schema","ftf-surface-lab.validation.v1"},{"notice","Surface Lab scene settings; not a complete FTF preset"},{"generator",p.meta.z},{"seed",p.meta.y},{"resolution",p.meta.x},
        {"world_seed",worldSeed},{"terrain_profile",p.meta.w},{"ftf_scales",{p.ftfScales.x,p.ftfScales.y,p.ftfScales.z,p.ftfScales.w}},
        {"extent",p.domain.x},{"origin_x",p.domain.y},{"origin_z",p.domain.z},{"amplitude",p.land.x},{"scale",p.land.y},{"sea",p.land.z},{"continents",p.land.w},
        {"mountains",p.detail.x},{"detail",p.detail.y},{"roughness",p.detail.z},{"smoothing",p.detail.w},{"temperature",p.climate.x},{"moisture",p.climate.y},{"climate_scale",p.climate.z}};
    std::filesystem::create_directories(stateDirectory);std::ofstream out(stateDirectory/"scene.json");if(!out)throw std::runtime_error("Cannot save scene.json");out<<j.dump(2);
}
static TerrainParams loadSettings(int64_t& worldSeed){
    std::ifstream input(stateDirectory/"scene.json");if(!input)throw std::runtime_error("No saved scene yet");Json j;input>>j;
    if(j.at("schema")!="ftf-surface-lab.validation.v1")throw std::runtime_error("This is not a Surface Lab scene file");
    TerrainParams p;p.meta.y=j.at("seed");p.meta.x=j.at("resolution");p.meta.z=j.value("generator",0u);
    worldSeed=j.value("world_seed",static_cast<int64_t>(p.meta.y));
    if(j.contains("ftf_scales"))for(int i=0;i<4;++i)p.ftfScales[i]=j["ftf_scales"].at(i).get<float>();
    p.meta.w=j.value("terrain_profile",0u);if(p.meta.w>7)throw std::runtime_error("Unsupported saved terrain profile");
    if(p.meta.z!=0&&p.meta.z!=2&&p.meta.z!=3)throw std::runtime_error("Unsupported saved generator");
    p.domain={j.at("extent").get<float>(),j.at("origin_x").get<float>(),j.at("origin_z").get<float>(),0};
    p.land={j.at("amplitude").get<float>(),j.at("scale").get<float>(),j.at("sea").get<float>(),j.at("continents").get<float>()};
    p.detail={j.at("mountains").get<float>(),j.at("detail").get<float>(),j.at("roughness").get<float>(),j.at("smoothing").get<float>()};
    p.climate={j.at("temperature").get<float>(),j.at("moisture").get<float>(),j.at("climate_scale").get<float>(),0};
    for(auto vector:{p.domain,p.land,p.detail,p.climate,p.ftfScales})for(int i=0;i<4;++i)if(!std::isfinite(vector[i]))throw std::runtime_error("Scene contains non-finite settings");
    if(p.meta.x<64||p.meta.x>8192||(p.meta.x&(p.meta.x-1)))throw std::runtime_error("Unsupported saved resolution");
    if(p.domain.x<=0||p.land.y<=0||p.climate.z<=0||p.ftfScales.x<=0||p.ftfScales.w<=0)throw std::runtime_error("Saved domain scales must be positive");
    return p;
}
// Stress-test animation: a copy of the preset with a dozen real FTF settings swept over time. The document itself is never touched,
// so turning the animation off restores every value exactly and nothing animated can be saved or exported.
// FTF versions the preview can imitate. `profile` selects the filter/terrain behaviour of that build (see buildWorldFilterInputs).
struct FtfVersion {const char* id;const char* label;const char* note;int profile;};
static const FtfVersion ftfVersions[]={
    {"1.0.0014","1.0.0014  (latest unstable, 24 Oct)","Chunk-based world filters: retuned Phacelle erosion (300-block gullies, wider slope gate), terrain ceiling applied before rivers.",1},
    {"1.0.0013","1.0.0013  (unstable, 23-24 Oct)","Same terrain and erosion as 1.0.0014.",1},
    {"1.0.0010","1.0.0010  (unstable, 22 Oct)","Tile filters: Phacelle erosion with 96-block gullies, terrain ceiling only after the filters.",0}
};
static constexpr int ftfVersionCount=3;
static int ftfVersionIndexFor(const std::string& id){for(int i=0;i<ftfVersionCount;++i)if(id==ftfVersions[i].id)return i;return 0;}
static constexpr int animatedSettingCount=12;
static PresetJson animatedPreset(const PresetJson& original,double phase){
    struct Target {const char* path;double amplitude,frequency,offset;bool additive;double low,high;};
    static const Target targets[animatedSettingCount]={
        {"/terrain/general/globalVerticalScale",.40,.13,0.0,false,.2,3},{"/terrain/general/globalHorizontalScale",.25,.09,1.3,false,1,12},
        {"/terrain/mountains/verticalScale",.45,.17,2.1,false,.3,4},{"/terrain/plains/verticalScale",.45,.11,.7,false,.5,12},
        {"/terrain/hills/verticalScale",.40,.15,3.4,false,.5,6},{"/terrain/plateau/verticalScale",.40,.19,4.2,false,.5,8},
        {"/terrain/hills/weight",.55,.07,5.0,false,.5,12},{"/world/properties/seaLevel",14,.08,.4,true,30,110},
        {"/filters/erosion/erosionRate",.7,.12,2.6,false,.02,.5},{"/world/continent/continentScale",.18,.05,1.9,false,1200,9000},
        {"/climate/biomeShape/biomeSize",.35,.10,3.9,false,200,2400},{"/climate/temperature/bias",.20,.06,.9,true,-.5,.5}};
    PresetJson copy=original;
    for(const auto& t:targets){
        auto key=PresetJson::json_pointer(t.path);if(!original.contains(key)||!original.at(key).is_number())continue;
        double base=original.at(key).get<double>(),wave=std::sin(6.283185307179586*(phase*t.frequency)+t.offset);
        double value=std::clamp(t.additive?base+t.amplitude*wave:base*(1+t.amplitude*wave),std::min(t.low,base),std::max(t.high,base));
        if(original.at(key).is_number_integer())copy[key]=static_cast<int64_t>(std::llround(value));else copy[key]=value;
    }
    return copy;
}
static void theme(){
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();s.WindowRounding=7;s.FrameRounding=4;s.GrabRounding=4;s.FramePadding={9,6};s.ItemSpacing={9,9};s.WindowPadding={16,16};
    s.Colors[ImGuiCol_WindowBg]={.065f,.077f,.093f,1};s.Colors[ImGuiCol_FrameBg]={.12f,.15f,.18f,1};s.Colors[ImGuiCol_FrameBgHovered]={.17f,.24f,.27f,1};
    s.Colors[ImGuiCol_SliderGrab]={.27f,.71f,.63f,1};s.Colors[ImGuiCol_SliderGrabActive]={.40f,.9f,.77f,1};s.Colors[ImGuiCol_CheckMark]={.4f,.85f,.7f,1};
    s.Colors[ImGuiCol_Button]={.13f,.27f,.27f,1};s.Colors[ImGuiCol_ButtonHovered]={.18f,.4f,.37f,1};s.Colors[ImGuiCol_Header]={.13f,.22f,.25f,1};
}
// Crash log: on an unhandled exception, write the call stack (module + offset) to state/crash-log.txt and stderr.
// Map FTFSurface offsets with build/FTFSurface.map.
static LONG WINAPI writeCrashLog(EXCEPTION_POINTERS* e){
    HANDLE process=GetCurrentProcess(),thread=GetCurrentThread();SymInitialize(process,nullptr,TRUE);
    CONTEXT context=*e->ContextRecord;STACKFRAME64 frame{};frame.AddrPC={context.Rip,0,AddrModeFlat};frame.AddrFrame={context.Rbp,0,AddrModeFlat};frame.AddrStack={context.Rsp,0,AddrModeFlat};
    std::string text="FTF Surface Lab crash, code 0x"+[&]{char b[16];std::snprintf(b,sizeof(b),"%08lX",e->ExceptionRecord->ExceptionCode);return std::string(b);}()+"\n";
    for(int i=0;i<48&&StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)&&frame.AddrPC.Offset;++i){
        DWORD64 base=SymGetModuleBase64(process,frame.AddrPC.Offset);char name[MAX_PATH]="?";if(base)GetModuleFileNameA(HMODULE(base),name,MAX_PATH);
        const char* shortName=std::strrchr(name,'\\');char line[400];std::snprintf(line,sizeof(line),"  %s+0x%llx\n",shortName?shortName+1:name,(unsigned long long)(frame.AddrPC.Offset-base));text+=line;}
    std::fputs(text.c_str(),stderr);std::fflush(stderr);
    try{std::filesystem::create_directories("state");std::ofstream("state/crash-log.txt",std::ios::app)<<text;}catch(...){}
    return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc,char** argv){
    SetUnhandledExceptionFilter(writeCrashLog);
    try{
        Options options=arguments(argc,argv);
        std::array<wchar_t,32768> executable{};GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        auto appRoot=std::filesystem::path(executable.data()).parent_path();PresetEditor editor;int64_t worldSeed=options.seed;
        auto initialPreset=options.preset.empty()?appRoot/"presets"/"Modern - Upscaled Community Preset.json":std::filesystem::path(options.preset);
        if(options.preset.empty()&&!std::filesystem::exists(initialPreset))initialPreset=appRoot/"assets"/"ftf-presets"/"modern-default.json";   // the shareable package ships only FTF's own default presets
        if(!options.preset.empty()||(!options.noPreset&&!options.primitive&&options.plains&&std::filesystem::exists(initialPreset))){
            editor.document.load(initialPreset);if(editor.document.worldSeed&&!options.seedSpecified)worldSeed=*editor.document.worldSeed;

        }
        if(!options.generatorSpecified&&editor.document.loaded())options.uplift=true;
        for(auto& edit:options.edits){auto equals=edit.find('=');if(equals==std::string::npos)throw std::runtime_error("Use --set /preset/path=JSON_VALUE");editor.document.set(edit.substr(0,equals),PresetJson::parse(edit.substr(equals+1)));}
        if((options.mix||options.uplift||options.upliftMap)&&!editor.document.loaded())throw std::runtime_error("Land/surface views require an imported FTF preset");
        if(options.presetOnly){if(options.exportPreset.empty()&&options.exportDatapack.empty())throw std::runtime_error("--preset-only needs --export-preset or --export-datapack");editor.document.worldSeed=worldSeed;if(!options.exportPreset.empty())editor.document.exportTo(options.exportPreset);if(!options.exportDatapack.empty())editor.document.exportDatapack(options.exportDatapack,appRoot/"assets"/"ftf-datapack-template.zip");std::cout<<"FTF export complete\n";return 0;}
        glfwSetErrorCallback([](int code,const char* text){std::fprintf(stderr,"GLFW %d: %s\n",code,text);});
        if(!glfwInit())throw std::runtime_error("GLFW initialization failed");if(!glfwVulkanSupported())throw std::runtime_error("Vulkan loader/driver not available");
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);glfwWindowHint(GLFW_VISIBLE,options.hidden?GLFW_FALSE:GLFW_TRUE);
        // Interactive launches start fullscreen (borderless-style: the desktop video mode, no mode switch). F11 toggles; --windowed opts out.
        const bool interactiveLaunch=!options.hidden&&!options.benchmark&&options.frames==0;GLFWmonitor* fullscreenMonitor=options.fullscreenTest&&!options.windowed?glfwGetPrimaryMonitor():nullptr;const GLFWvidmode* fullscreenMode=fullscreenMonitor?glfwGetVideoMode(fullscreenMonitor):nullptr;
        if(fullscreenMode){glfwWindowHint(GLFW_RED_BITS,fullscreenMode->redBits);glfwWindowHint(GLFW_GREEN_BITS,fullscreenMode->greenBits);glfwWindowHint(GLFW_BLUE_BITS,fullscreenMode->blueBits);glfwWindowHint(GLFW_REFRESH_RATE,fullscreenMode->refreshRate);}
        glfwWindowHint(GLFW_AUTO_ICONIFY,GLFW_FALSE);glfwWindowHint(GLFW_DECORATED,GLFW_FALSE);
        GLFWwindow* window=glfwCreateWindow(fullscreenMode?fullscreenMode->width:options.windowW,fullscreenMode?fullscreenMode->height:options.windowH,"FTF Surface Lab",fullscreenMode?fullscreenMonitor:nullptr,nullptr);
        int windowedX=80,windowedY=60,windowedW=1440,windowedH=900;if(!window)throw std::runtime_error("Window creation failed");
        WindowChrome chrome(window,glfwGetWin32Window(window));
        Gpu gpu;gpu.initialize(window);stateDirectory=gpu.shaderRoot.parent_path()/"state";IMGUI_CHECKVERSION();ImGui::CreateContext();
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        if(std::filesystem::exists("C:/Windows/Fonts/segoeui.ttf"))io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",17);
        theme();gpu.initializeGui();Surface surface(gpu);uiSectionsStartCollapsed=interactiveLaunch;surface.profileSteps=options.profileSteps;EditPreview editPreview(surface);editPreview.enabled=(interactiveLaunch||options.previewTest)&&!options.noEditPreview;editPreview.directLimit=options.directLimit?options.directLimit:interactiveLaunch?1024u:(1u<<30);surface.selectiveGeneration=!options.noSelective;surface.useMountainCache=!options.noMountainCache;
        // GPU pipelines compile in parallel; the window shows progress instead of freezing (slow only when the shaders changed).
        bool loadingShot=false;auto loadingStart=Clock::now();auto loadingFrame=[&](uint32_t done,uint32_t total){
            if(options.hidden&&!options.exerciseLoading)return;glfwPollEvents();auto loadingCommand=gpu.begin();if(!loadingCommand)return;
            ImGui_ImplVulkan_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();auto* v=ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(v->GetCenter(),ImGuiCond_Always,{.5f,.5f});ImGui::SetNextWindowSize({480,0});
            ImGui::Begin("Loading",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextColored({.4f,.86f,.72f,1},"FTF Surface Lab");ImGui::TextWrapped("Preparing GPU shaders (%u of %u). This is slow only the first time after the shaders change or the graphics driver cache is cleared.",done,total);
            char loadingText[64];std::snprintf(loadingText,sizeof(loadingText),"%u of %u  |  %.0f s",done,total,std::chrono::duration<double>(Clock::now()-loadingStart).count());ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImVec4(.27f,.71f,.63f,1));ImGui::ProgressBar(total?float(done)/float(total):0.0f,{-1,0},loadingText);ImGui::PopStyleColor();ImGui::End();ImGui::Render();gpu.end(ImGui::GetDrawData());
            if(options.exerciseLoading&&!loadingShot&&done>0&&!options.uiScreenshot.empty()){loadingShot=true;gpu.screenshotUi(ImGui::GetDrawData(),options.uiScreenshot+".loading.ppm");}
        };
        surface.initialize(nullptr,loadingFrame,options.warmup);
        if(options.warmup){std::cout<<"GPU shader warm-up complete"<<std::endl;surface.destroy();ImGui_ImplVulkan_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();gpu.destroy();glfwDestroyWindow(window);glfwTerminate();return 0;}editor.initialize(appRoot,window);if(options.library)editor.showLibrary();
        if(options.testHighlight>=0){editor.terrainSelection=options.testHighlight;editor.highlightTerrain=true;}if(options.testIsolate>=0){editor.terrainSelection=options.testIsolate;editor.isolateTerrain=true;}
        TerrainParams params;params.meta.x=options.grid;params.meta.y=static_cast<uint32_t>(worldSeed);params.meta.z=options.primitive?1:options.upliftMap?6:options.uplift?5:(options.mix?4:options.regions?3:(options.plains?2:0));params.meta.w=options.profile;
        if(options.regions)params.detail.x=2098;
        params.domain={options.extent,options.originX,options.originZ,0};params.land.y=options.scale;
        if(editor.document.loaded()){if(!options.originSpecified){auto spawn=editor.document.spawn();params.domain.y=spawn.x;params.domain.z=spawn.y;}if(editor.document.previewExtent&&!options.extentSpecified)params.domain.x=*editor.document.previewExtent;}
        Surface middleSurface(gpu);bool middleInitialized=false,middleActive=false;TerrainParams middlePrevious{};uint64_t middleRevision=0,middleBaseRevision=0;uint32_t middleBoundGrid=0;ftf::RiverNetworks middleRivers;DrawParams middleDraw{};
        Surface detailSurface(gpu);bool detailInitialized=false,spawnTool=false,measureTool=false,centerTool=false,cameraCentered=false,detailTool=false,detailPlaced=options.detailRadius>0;
        glm::vec2 detailCenter{};float detailRadius=options.detailRadius>0?options.detailRadius:2048;float detailScreenRadius=120,detailSurfaceHeight=0;bool screenDetail=false,adaptiveDetailMesh=!options.noAdaptiveDetail;float detailRenderRadius=detailRadius;glm::vec4 detailScreen{};bool detailCoverageLimited=false;uint32_t boundCoarseGrid=0,boundDetailGrid=0;int detailGrid=1024;uint64_t detailRevision=1,detailBaseRevision=0;TerrainParams previousDetail{};
        ftf::RiverNetworks detailRivers;
        // Background finer detail: the full-resolution patch and ring are built progressively in back surfaces and swapped in when complete,
        // so moving the camera or settling a slider never stalls a frame on a whole 2048 x 2048 generation.
        Surface detailBack(gpu),middleBack(gpu);bool backInitialized=false;ftf::RiverNetworks detailRiversBack,middleRiversBack;
        struct FinerKey {TerrainParams local{},mid{};bool midActive=false;uint64_t base=0;};FinerKey frontKey{},backKey{};GraphSeeds backSeeds{};bool frontFull=false,backJob=false,swapReady=false;double finerJobStart=0;int finerJobFrame=0;Json finerJobs=Json::array();uint64_t backRevision=1ull<<40;
        auto sameGeometry=[](const FinerKey& a,const FinerKey& b){return a.midActive==b.midActive&&a.local.meta.x==b.local.meta.x&&a.local.domain==b.local.domain&&(!a.midActive||(a.mid.meta.x==b.mid.meta.x&&a.mid.domain==b.mid.domain));};
        auto sameKey=[](const FinerKey& a,const FinerKey& b){return a.midActive==b.midActive&&a.base==b.base&&!std::memcmp(&a.local,&b.local,sizeof(a.local))&&(!a.midActive||!std::memcmp(&a.mid,&b.mid,sizeof(a.mid)));};
        ftf::RiverNetworks riverNetworks;TerrainParams previous=params;GraphSeeds graphSeeds,previousSeeds;uint64_t revision=1;int frameNumber=0;
        float yaw=.7f,pitch=options.cameraSet?options.cameraPitch:.58f,distance=options.cameraSet?options.cameraDistance:1.0f,exaggeration=1.0f,cameraPanY=0;glm::vec2 pan=options.cameraSet?glm::vec2(options.panX,options.panZ):glm::vec2(0);int layer=0,targetHz=12,biomeColors=options.biomeColors;PresetJson biomeLegend;{std::ifstream info(appRoot/"assets"/"biomes-1.21.1.json");info>>biomeLegend;}PresetJson blockLegend;{std::ifstream info(appRoot/"assets"/"materials-1.21.1.json");info>>blockLegend;}char blockSearch[96]{};
        float animateSpeed=2.0f;double animatePhase=0;bool wasAnimating=false;TerrainParams animateBackup{};
        bool topDown=options.regions||options.upliftMap,lighting=true,animate=options.benchmark||options.animate,automatic=false,showMetrics=true,editing=false,showOceanFloor=options.oceanFloor,showHeightLimit=!options.noHeightHighlight;
        struct AtmosphereUi {bool water=false,fog=false,clouds=false;float ripple=1,reflection=1,fogDensity=.25f,fogFalloff=.5f,fogHeight=63,fogOpacity=1,cloudOpacity=1,cloudCoverage=.5f,cloudDensity=1,cloudBase=350,cloudThickness=140,cloudScale=1500,cloudSpeed=6;} atmo; // render-only: realistic water, volumetric fog and clouds (all off by default)
        bool ambientOcclusion=options.ambientOcclusion,cloudShadows=options.cloudShadows;float aoIntensity=std::clamp(options.aoIntensity,0.0f,2.0f),shadowIntensity=std::clamp(options.shadowIntensity,0.0f,1.0f),shadowSpeed=std::clamp(options.shadowSpeed,0.0f,4.0f);float cameraHeight=0;glm::vec3 lastCameraCenter{};
        bool sunDrag=false;   // Ctrl+Shift+RMB drag turns the sun (azimuth only), latched at drag start
        float sunAzimuth=options.sunAzimuth,sunElevation=options.sunElevation,sunIntensity=options.sunIntensity,sunShadowStrength=options.sunShadowStrength,sunShadowSoftness=.35f,sunShadowRange=1500;bool sunShadows=options.sunShadows,openToolSettingsRequested=false;
        struct Waypoint {std::string name;glm::vec3 p;uint32_t color;};std::vector<Waypoint> points;bool pointTool=false,copyTool=false;float sunBefore=-1;glm::vec2 panBefore{};std::string copiedText;double copiedAt=-10;   /* copyTool [K]: LMB copies "X Y Z" of the clicked spot */int renamingPoint=-1;char pointNameBuffer[64]{};int highlightedPoint=-1;double lastHeightRefresh=-10;bool flyMode=false;float cameraFov=options.fovTest,flyOrbitRadius=1;glm::vec3 flyPos{};float flyYaw=0,flyPitch=0,flySpeed=0;
        std::vector<uint32_t> previousRiverWords;uint32_t requestedResolution=options.grid;uint32_t biomeModMask=options.biomeMods;int ftfVersionIndex=options.ftfVersion.empty()?0:ftfVersionIndexFor(options.ftfVersion);bool wholeMapTrees=true;bool frustumCulling=options.cullTest!=1,distanceCulling=options.cullTest==2,backfaceCulling=options.cullTest==3,treeFrustumCulling=true;float cullDistance=options.cullTest==2?1000.0f:50000.0f;bool treesEnabled=options.showTrees,treesPause=!options.showTrees;float treeDensity=1,treeRadius=600,treeLod=140,treePauseDelay=.6f;uint32_t treeMask=0xfff;double treesResumeAt=0;uint64_t treeLastRevision=0;bool treesPausedNow=false,treesTooFar=false;
        bool walkGrid=true;bool walkMode=false,walkFlying=false,walkMouseCaptured=false,walkOnGround=false,walkInWater=false,walkLookReset=true,detailAuto=false,hasUserDetailBackup=false;glm::vec3 walkPos{};float walkYaw=0,walkPitch=0,walkVelY=0,walkSpeedScale=1,walkFov=70,savedExaggeration=1;int lodBoostSamples=2048;double lastSpaceTap=-10,autoMoveTime=-10;glm::dvec2 walkLastCursor{};
        std::vector<float> walkHeights;uint64_t walkHeightsRevision=0,heightCopyRevision=0;TerrainParams walkPatch{};std::vector<float> baseWalkHeights;TerrainParams baseWalkParams{},baseWalkPending{};uint64_t baseWalkCopies=0,baseWalkFieldRevision=0;glm::vec2 baseWalkCenter{1e30f,1e30f},patchWindowCenter{1e30f,1e30f}; // baseWalk: a 65 x 65 window of the whole-world grid under the player (collision without a finer patch)
        glm::vec2 lastAutoCamera{},userDetailCenterBackup{};float autoRadiusBackup=2048,userDetailRadiusBackup=2048;int autoGridBackup=1024,userDetailGridBackup=1024;size_t pointsRoundTrip[2]{0,0};std::string pointsRoundTripName;glm::vec2 pointsCentered{};
        std::vector<glm::vec3> measurePoints;double measureHorizontal=0,measureDirect=0,measureSurface=0;glm::vec3 spawnMarker{};bool hasSpawnMarker=false;
        glm::vec2 previousPresetSpawn=editor.document.spawn();std::string toolHint;
        struct HoverInfo {bool valid=false;glm::vec3 position{};SurfaceInspection sample{};std::string biome,block,terrain;} hover;
        MeshHeightTiles baseTiles,middleTiles,detailTiles;glm::mat4 meshVp{1};glm::vec2 meshViewport{1,1};PresetJson pickCheckReport;   // full-resolution mesh heights (mesh_heights.hpp)
        std::vector<float> baseHeightCache,detailHeightCache;uint64_t baseHeightRevision=0,detailHeightRevision=0;double lastHoverTime=-1;
        float terrainHeightLimit=0,terrainSqueezeStart=0;bool showAboveHeight=false;float aboveHeight=200;std::string highestStatus;std::string status;std::vector<TerrainParams> undo,redo;
        Json previewTrace=Json::array();bool refineCancelExercised=false;double sliderOriginal=0;uint32_t renderedThisFrame=0;
        std::vector<double> gpuTimes,genTimes,filterTimes,drawTimes,frameTimes,cacheTimes,worldTimes,oceanTimes,composeTimes;StageTiming lastTiming{},lastDrawTiming{};bool leftCollapsed=false,rightCollapsed=false,viewportCollapsed=false;ImVec2 rightPanelSize{};int previousTool=-1;
        std::map<std::string,std::vector<double>> stallTimes; // CPU stall bookkeeping reported as `stalls`
        auto stallScope=[&](const char* name,auto&& work){auto began=Clock::now();work();stallTimes[name].push_back(std::chrono::duration<double,std::milli>(Clock::now()-began).count());};
        Clock::time_point lastFrame=Clock::now();double elapsed=0,smoothedFrame=16.7;int goodFrames=0,lastQualityChange=-1000;
        bool observedSpawnUndo=false,shortcutUndoObserved=false,resetObserved=false;bool shortcutHeld[4]{};glm::vec2 initialSpawn=editor.document.spawn();
        uint64_t generatedFrames=0;uint32_t lastSubmitted=0;
        ProjectSession project(options.testSessionRoot.empty()?appRoot:std::filesystem::path(options.testSessionRoot));const bool persistentSession=!options.hidden&&!options.benchmark&&options.frames==0;
        if(!persistentSession&&!options.testRestore)project.restorePending=false;
        DockPanels dock;std::string savedDockLayout;auto dockLayoutFile=appRoot/"state/docked-panels.json";const Json defaultDock=dock.snapshot();
        if(persistentSession&&std::filesystem::exists(dockLayoutFile))try{std::ifstream f(dockLayoutFile);Json j;f>>j;dock.load(j);}catch(...){}
        savedDockLayout=dock.snapshot().dump();bool observedPanelHide=false;ImVec2 biomeToolbarButton{};

        glm::vec2 restoreTestClick{};bool restoreDialogRequested=false;double sessionTick=0;
        Surface biomeSurface(gpu);bool biomeInitialized=false,biomeTool=false,biomeSearchRequested=false,biomeSearchWaiting=false,biomeHighlight=false;
        int biomeChoice=0,biomeSearchGrid=1024;float biomeRadius=0;bool biomeUseWorldSize=true;char biomeSearchText[96]{};std::string biomeSearchStatus="Choose a biome and search";std::optional<SurfaceInspection> biomeResult;
        TerrainParams biomeParams{};ftf::RiverNetworks biomeRivers;uint64_t biomeRevision=0;std::string biomeContentFingerprint;
        // Biome count (right panel): one scan of the preview world on the biome-search surface, against what the selection tables can output.
        const auto biomeTableWords=loadBiomeTableWords(appRoot/"assets");bool censusRequested=false,censusWaiting=false;int censusGrid=1024;TerrainParams censusParams{};
        std::string censusFingerprint,censusStatus;std::vector<bool> censusPossible;std::optional<BiomeCensus> census;
        bool distanceLod=!options.noDistanceLod;bool lodAdaptive=true,lodWaitForStop=!options.hidden||options.lodWait;double lodResumeAt=0;float lastCameraSignature[14]{};float lodFinest=1.0f,lodCoarsest=64.0f,lodAdaptiveSpacing=0;int lodAdaptiveState=0;float lodBandFraction=.12f;int lodBandCount=6;float lodBandWidth[8]={.12f,.12f,.12f,.12f,.12f,.12f,.12f,.12f};int lodBandDetail[8]={0,1,2,3,4,5,5,5};float finerPatchRadius=0,finerRingScale=2;bool finerLiveFull=false;int finerEditGrid=512,finerDragMode=0; /* while values change: full finer detail (may lag) or a preview patch of finerEditGrid samples */ /* band widths are fractions of the world size; finerPatchRadius 0 = automatic */double detailLastEdit=0;bool finerInCameraModes=true,treesHideMoving=true,walkPickTool=false;int fpsLimit=60; /* Settings: viewport FPS lock (0 = off); spare frame time goes to terrain building */double cameraMovedAt=-10;float finerProgress=-1;const char* finerLabel=""; // finerProgress<0: no finer-detail bar
        auto graphicsState=[&](){return PresetJson{{"editing_preview",{{"enabled",editPreview.enabled},{"cap",editPreview.cap},{"budget_ms",editPreview.budgetMs}}},{"tree_defaults_version",2},{"fps_limit",fpsLimit},{"atmosphere",{{"water",atmo.water},{"fog",atmo.fog},{"clouds",atmo.clouds},{"ripple",atmo.ripple},{"reflection",atmo.reflection},{"fog_density",atmo.fogDensity},{"fog_falloff",atmo.fogFalloff},{"fog_height",atmo.fogHeight},{"fog_opacity",atmo.fogOpacity},{"cloud_opacity",atmo.cloudOpacity},{"cloud_coverage",atmo.cloudCoverage},{"cloud_density",atmo.cloudDensity},{"cloud_base",atmo.cloudBase},{"cloud_thickness",atmo.cloudThickness},{"cloud_scale",atmo.cloudScale},{"cloud_speed",atmo.cloudSpeed}}},{"lighting",lighting},{"ao",ambientOcclusion},{"ao_strength",aoIntensity},{"clouds",cloudShadows},{"cloud_strength",shadowIntensity},{"cloud_speed",shadowSpeed},{"sun",{sunAzimuth,sunElevation,sunIntensity,sunShadowStrength,sunShadowSoftness,sunShadowRange}},{"sun_shadows",sunShadows},{"fov",cameraFov},{"frustum_culling",frustumCulling},{"distance_culling",distanceCulling},{"cull_distance",cullDistance},{"backface_culling",backfaceCulling},{"tree_culling",treeFrustumCulling},{"whole_map_trees",wholeMapTrees},{"walk_fov",walkFov},{"tree_settings",{{"enabled",treesEnabled},{"pause",treesPause},{"hide_moving",treesHideMoving},{"density",treeDensity},{"radius",treeRadius},{"lod",treeLod},{"delay",treePauseDelay},{"mask",treeMask}}}};};
        bool graphicsPersistent=persistentSession||(options.graphicsTest&&!options.testSessionRoot.empty());auto graphicsFile=(options.graphicsTest&&!options.testSessionRoot.empty()?project.root:appRoot)/"state/graphics.json";
        bool migrateLegacyGraphics=persistentSession&&!std::filesystem::exists(graphicsFile);
        auto applyGraphics=[&](const auto& g){if(g.contains("editing_preview")){auto a=g["editing_preview"];editPreview.enabled=a.value("enabled",true)&&!options.noEditPreview;{uint32_t savedCap=a.value("cap",256u);editPreview.cap=savedCap>=1024?1024:savedCap>=512?512:256;}editPreview.budgetMs=std::clamp(a.value("budget_ms",8.0f),2.0f,16.0f);}if(g.contains("atmosphere")){auto a=g["atmosphere"];atmo.water=a.value("water",false);atmo.fog=a.value("fog",false);atmo.clouds=a.value("clouds",false);atmo.ripple=std::clamp(a.value("ripple",1.0f),0.0f,2.0f);atmo.reflection=std::clamp(a.value("reflection",1.0f),0.0f,1.5f);atmo.fogDensity=std::clamp(a.value("fog_density",.25f),0.0f,4.0f);atmo.fogFalloff=std::clamp(a.value("fog_falloff",.5f),0.0f,4.0f);atmo.fogHeight=std::clamp(a.value("fog_height",63.0f),-100.0f,2000.0f);atmo.fogOpacity=std::clamp(a.value("fog_opacity",1.0f),0.0f,1.0f);atmo.cloudOpacity=std::clamp(a.value("cloud_opacity",1.0f),0.0f,1.0f);atmo.cloudCoverage=std::clamp(a.value("cloud_coverage",.5f),0.0f,1.0f);atmo.cloudDensity=std::clamp(a.value("cloud_density",1.0f),0.0f,3.0f);atmo.cloudBase=std::clamp(a.value("cloud_base",350.0f),0.0f,5000.0f);atmo.cloudThickness=std::clamp(a.value("cloud_thickness",140.0f),20.0f,1500.0f);atmo.cloudScale=std::clamp(a.value("cloud_scale",1500.0f),200.0f,10000.0f);atmo.cloudSpeed=std::clamp(a.value("cloud_speed",6.0f),0.0f,200.0f);}
                lighting=g.value("lighting",true);ambientOcclusion=g.value("ao",false);aoIntensity=g.value("ao_strength",1.0f);cloudShadows=g.value("clouds",false);shadowIntensity=g.value("cloud_strength",.34f);shadowSpeed=g.value("cloud_speed",1.0f);sunShadows=g.value("sun_shadows",false);cameraFov=std::clamp(g.value("fov",60.0f),5.0f,150.0f);if(g.contains("sun")&&g["sun"].size()==6){auto a=g["sun"];sunAzimuth=a[0];sunElevation=a[1];sunIntensity=a[2];sunShadowStrength=a[3];sunShadowSoftness=a[4];sunShadowRange=a[5];}frustumCulling=g.value("frustum_culling",true);distanceCulling=g.value("distance_culling",false);cullDistance=g.value("cull_distance",50000.0f);backfaceCulling=g.value("backface_culling",false);treeFrustumCulling=g.value("tree_culling",true);wholeMapTrees=g.value("whole_map_trees",true);walkFov=g.value("walk_fov",70.0f);if(g.contains("tree_settings")){auto t=g["tree_settings"];treesEnabled=g.value("tree_defaults_version",0)>=2&&t.value("enabled",false)&&!options.noTrees;treesPause=t.value("pause",true);treesHideMoving=t.value("hide_moving",true);treeDensity=t.value("density",1.0f);treeRadius=t.value("radius",600.0f);treeLod=t.value("lod",140.0f);treePauseDelay=t.value("delay",.6f);treeMask=t.value("mask",0xfffu)&0xfffu;}
            fpsLimit=g.value("fps_limit",60);if(fpsLimit!=0)fpsLimit=std::clamp(fpsLimit,20,240);};
        const auto defaultGraphics=graphicsState();   // Settings > Reset
        if(graphicsPersistent&&std::filesystem::exists(graphicsFile))try{applyGraphics(ProjectSession::read(graphicsFile));}catch(const std::exception& e){editor.status=e.what();}
        if(options.atmosphere.find("water")!=std::string::npos)atmo.water=true;if(options.atmosphere.find("fog")!=std::string::npos)atmo.fog=true;if(options.atmosphere.find("clouds")!=std::string::npos)atmo.clouds=true;
        if(options.fovSpecified)cameraFov=options.fovTest;auto initialGraphics=graphicsState();std::string savedGraphics=graphicsState().dump();
        bool closeRequested=false,closeAfterSave=false,resetSettingsRequested=false;ImVec2 settingsPos{};uint64_t saveSerial=0,closeSaveSerial=0;ImVec2 exitCancelPoint{};bool exitPromptObserved=false,exitCancelObserved=false,graphicsRestoreObserved=false;float poseError=0,poseDirectionError=0;
        glfwSetWindowUserPointer(window,&closeRequested);glfwSetWindowCloseCallback(window,[](GLFWwindow* w){*static_cast<bool*>(glfwGetWindowUserPointer(w))=true;glfwSetWindowShouldClose(w,GLFW_FALSE);});
        auto captureView=[&](){
            auto vec=[](const auto& v){return PresetJson::array({v[0],v[1],v[2],v[3]});};
            return PresetJson{{"meta",vec(params.meta)},{"domain",vec(params.domain)},{"land",vec(params.land)},{"detail",vec(params.detail)},{"climate",vec(params.climate)},{"ftf_scales",vec(params.ftfScales)},
                {"camera",{yaw,pitch,distance,exaggeration,pan.x,pan.y,cameraHeight}},{"camera_pan_y",cameraPanY},{"camera_centered",cameraCentered},{"top_down",topDown},{"lighting",lighting},{"layer",layer},{"biome_colors",biomeColors},{"automatic_resolution",automatic},{"distance_lod",distanceLod},{"lod_band_fraction",lodBandFraction},{"lod_bands",[&]{auto a=Json::array();for(int i=0;i<lodBandCount;++i)a.push_back({{"width",lodBandWidth[i]},{"detail",lodBandDetail[i]}});return a;}()},{"finer_patch_radius",finerPatchRadius},{"finer_live_full",finerLiveFull},{"finer_drag_mode",finerDragMode},{"finer_edit_grid",finerEditGrid},{"finer_ring_scale",finerRingScale},{"lod_boost_samples",lodBoostSamples},{"lod_adaptive",lodAdaptive},{"lod_wait_stop",lodWaitForStop},{"finer_in_camera_modes",finerInCameraModes},{"lod_finest",lodFinest},{"lod_coarsest",lodCoarsest},{"ftf_version",ftfVersions[ftfVersionIndex].id},{"biome_mods",biomeModMask},{"trees",{{"enabled",treesEnabled},{"pause",treesPause},{"hide_moving",treesHideMoving},{"density",treeDensity},{"radius",treeRadius},{"lod",treeLod},{"delay",treePauseDelay},{"mask",treeMask}}},{"ocean_floor",showOceanFloor},{"height_highlight",showHeightLimit},
                {"ambient_occlusion",ambientOcclusion},{"cloud_shadows",cloudShadows},{"ao_intensity",aoIntensity},{"shadow_intensity",shadowIntensity},{"shadow_speed",shadowSpeed},
                {"sun",{sunAzimuth,sunElevation,sunIntensity,sunShadowStrength,sunShadowSoftness,sunShadowRange}},{"sun_shadows",sunShadows},{"detail_placed",detailPlaced},{"detail_center",{detailCenter.x,detailCenter.y}},{"detail_screen_radius",detailScreenRadius},{"detail_world_radius",detailRadius},{"detail_radius_units","blocks"},{"detail_surface_height",detailSurfaceHeight},{"detail_resolution",detailGrid},{"adaptive_detail_mesh",adaptiveDetailMesh},
                {"points",[&]{auto list=PresetJson::array();for(auto& w:points)list.push_back({{"name",w.name},{"x",w.p.x},{"y",w.p.y},{"z",w.p.z},{"color",w.color}});return list;}()}};
        };
        auto restoreView=[&](const PresetJson& view,bool keepHistory=false){
            if(migrateLegacyGraphics&&restoreDialogRequested){lighting=view.value("lighting",true);ambientOcclusion=view.value("ambient_occlusion",false);cloudShadows=view.value("cloud_shadows",false);aoIntensity=view.value("ao_intensity",1.0f);shadowIntensity=view.value("shadow_intensity",.34f);shadowSpeed=view.value("shadow_speed",1.0f);sunShadows=view.value("sun_shadows",false);if(view.contains("sun")&&view["sun"].size()==6){auto a=view["sun"];sunAzimuth=a[0];sunElevation=a[1];sunIntensity=a[2];sunShadowStrength=a[3];sunShadowSoftness=a[4];sunShadowRange=a[5];}if(view.contains("trees")){auto t=view["trees"];treesEnabled=false;treesPause=t.value("pause",true);treesHideMoving=t.value("hide_moving",true);treeDensity=t.value("density",1.0f);treeRadius=t.value("radius",600.0f);treeLod=t.value("lod",140.0f);treePauseDelay=t.value("delay",.6f);treeMask=t.value("mask",0xfffu)&0xfffu;}migrateLegacyGraphics=false;}

            auto unpack=[&](const char* name,auto& v){if(view.contains(name)&&view[name].is_array()&&view[name].size()==4)for(int i=0;i<4;++i)v[i]=view[name][i].template get<std::remove_reference_t<decltype(v[i])>>();};
            unpack("meta",params.meta);unpack("domain",params.domain);unpack("land",params.land);unpack("detail",params.detail);unpack("climate",params.climate);unpack("ftf_scales",params.ftfScales);
            if(params.meta.x<64||params.meta.x>8192||(params.meta.x&(params.meta.x-1)))params.meta.x=2048;if(params.meta.z>6)params.meta.z=5;
            if(view.contains("camera")&&view["camera"].size()==7){auto c=view["camera"];yaw=c[0];pitch=std::clamp(c[1].get<float>(),-1.565f,1.565f);distance=std::clamp(c[2].get<float>(),1e-12f,4.0f);exaggeration=c[3];pan={c[4].get<float>(),c[5].get<float>()};cameraHeight=c[6];}
            biomeModMask=view.value("biome_mods",0u)&7u;
            previousPresetSpawn=editor.document.spawn();
            cameraPanY=view.value("camera_pan_y",0.0f);
            cameraCentered=view.value("camera_centered",false);topDown=view.value("top_down",false);layer=view.value("layer",0);biomeColors=view.value("biome_colors",3);automatic=view.value("automatic_resolution",false);showOceanFloor=view.value("ocean_floor",false);showHeightLimit=view.value("height_highlight",true);
            distanceLod=view.contains("lod_adaptive")?view.value("distance_lod",true):true; // projects saved before adaptive resolution existed had the view off by default
            lodAdaptive=view.value("lod_adaptive",true);lodWaitForStop=view.value("lod_wait_stop",true);finerInCameraModes=view.value("finer_in_camera_modes",true);lodFinest=std::clamp(view.value("lod_finest",1.0f),.25f,64.0f);lodCoarsest=std::clamp(view.value("lod_coarsest",64.0f),lodFinest,512.0f);
            if(view.contains("ftf_version")&&view["ftf_version"].is_string())ftfVersionIndex=ftfVersionIndexFor(view["ftf_version"].get<std::string>());
            lodBoostSamples=std::clamp(view.value("lod_boost_samples",2048),1024,4096);
            if(view.contains("lod_bands")&&view["lod_bands"].is_array()&&!view["lod_bands"].empty()){lodBandCount=std::min<int>(8,int(view["lod_bands"].size()));for(int i=0;i<lodBandCount;++i){lodBandWidth[i]=std::clamp(view["lod_bands"][i].value("width",.12f),.002f,1.0f);lodBandDetail[i]=std::clamp(view["lod_bands"][i].value("detail",std::min(i,5)),0,5);}}
            finerPatchRadius=std::max(0.0f,view.value("finer_patch_radius",0.0f));finerLiveFull=view.value("finer_live_full",false);finerDragMode=std::clamp(view.value("finer_drag_mode",finerLiveFull?2:0),0,2);finerEditGrid=std::clamp(view.value("finer_edit_grid",512),256,2048);finerRingScale=std::clamp(view.value("finer_ring_scale",2.0f),1.25f,4.0f);lodBandFraction=view.value("lod_band_fraction",.12f);if(!std::isfinite(lodBandFraction)||lodBandFraction<=0)lodBandFraction=.12f;
            biomeResult.reset();biomeHighlight=false;biomeTool=false;biomeSearchStatus="Choose a biome and search";
            adaptiveDetailMesh=view.value("adaptive_detail_mesh",true);
            detailPlaced=view.value("detail_placed",false);screenDetail=false;detailRadius=view.value("detail_world_radius",2048.0f);if(!std::isfinite(detailRadius)||detailRadius<16||detailRadius>100000)detailRadius=2048;detailRenderRadius=detailRadius;detailSurfaceHeight=view.value("detail_surface_height",0.0f);detailScreenRadius=view.value("detail_screen_radius",120.0f);if(!std::isfinite(detailScreenRadius)||detailScreenRadius<20||detailScreenRadius>600)detailScreenRadius=120;detailGrid=view.value("detail_resolution",1024);if(detailGrid!=256&&detailGrid!=512&&detailGrid!=1024&&detailGrid!=2048)detailGrid=1024;
            if(view.contains("detail_center")&&view["detail_center"].size()==2)detailCenter={view["detail_center"][0].get<float>(),view["detail_center"][1].get<float>()};detailTool=centerTool=spawnTool=measureTool=pointTool=false;measurePoints.clear();measureHorizontal=measureDirect=measureSurface=0;hasSpawnMarker=false;++revision;if(!keepHistory)editor.clearHistory();
            points.clear();renamingPoint=-1;if(view.contains("points")&&view["points"].is_array())for(auto& item:view["points"])try{glm::vec3 position{item.at("x").get<float>(),item.value("y",0.0f),item.at("z").get<float>()};if(std::isfinite(position.x)&&std::isfinite(position.y)&&std::isfinite(position.z))points.push_back({item.value("name",std::string("Point")).substr(0,60),position,item.value("color",0xffd24au)});}catch(...){}
        };
        // Height of the terrain mesh as drawn at world x,z: the base field, the intermediate ring and the finer patch, each at
        // full resolution and blended into the one below exactly like terrain.vert (ring: radial blend; patch: radial, or the
        // screen circle when the patch follows the screen). Not included: local river/lake water surfaces in the FTF land
        // view (the readout gives the river bed there) and coarser far LOD bands. fresh: read the newest field now.
        auto meshHeight=[&](glm::vec2 world,bool fresh)->std::optional<float>{
            if(params.meta.z==3||params.meta.z==6)return params.land.z;
            if(!surface.fieldRevision)return std::nullopt;const float water=showOceanFloor?-1e30f:params.land.z;
            baseTiles.track(surface,elapsed,fresh);auto h=baseTiles.height(world,water);if(!h)return std::nullopt;
            if(middleActive&&middleInitialized&&middleSurface.fieldRevision){middleTiles.track(middleSurface,elapsed,fresh);
                if(auto m=middleTiles.height(world,water)){float r=glm::length(world-middleTiles.center()),R=middleTiles.domain.x*.5f;h=std::lerp(*m,*h,glm::smoothstep(R*.70f,R*.94f,r));}}
            if(detailPlaced&&detailInitialized&&detailSurface.fieldRevision){detailTiles.track(detailSurface,elapsed,fresh);
                if(auto f=detailTiles.height(world,water)){glm::vec2 offset=world-detailTiles.center();float R=detailTiles.domain.x*.5f,blend=glm::smoothstep(R*.70f,R*.94f,glm::length(offset));
                    if(screenDetail&&detailScreen.w>0){auto clip=meshVp*glm::vec4(world.x-params.domain.y,*f*exaggeration,world.y-params.domain.z,1);
                        if(clip.w>0){glm::vec2 pixel=(glm::vec2(clip)/clip.w*.5f+.5f)*meshViewport;float edge=std::max(std::abs(offset.x),std::abs(offset.y))/R;
                            blend=std::max(glm::smoothstep(detailScreen.z*.70f,detailScreen.z*.94f,glm::length(pixel-glm::vec2(detailScreen))),glm::smoothstep(.88f,.96f,edge));}}
                    h=std::lerp(*f,*h,blend);}}
            return h;};
        // Highest sample of the preview field, read in bands of about 32 MB (a single copy of an 8192 field is ~1 GB).
        auto findHighest=[&]()->std::optional<std::pair<size_t,float>>{
            const uint32_t G=surface.resolution;if(!surface.fieldRevision||G<2)return std::nullopt;const uint32_t rows=std::clamp((32u<<20)/(G*16u),1u,G);
            float best=-1e30f;size_t index=0;bool any=false;
            for(uint32_t y=0;y<G;y+=rows){uint32_t n=std::min(rows,G-y);auto band=surface.heightWindows({glm::ivec4(0,int(y),int(G),int(n))});
                for(size_t i=0;i<band[0].size();++i)if(band[0][i]>best){best=band[0][i];index=size_t(y)*G+i;any=true;}}
            if(!any)return std::nullopt;return std::make_pair(index,best);};
        auto sampleGround=[&](glm::vec2 world,float& out){
            if(surface.fieldRevision&&(baseHeightCache.empty()||(baseHeightRevision!=surface.fieldRevision&&elapsed-lastHeightRefresh>.4))){stallScope("pick heights (sync readback)",[&]{baseHeightCache=surface.pickHeights();});baseHeightRevision=surface.fieldRevision;lastHeightRefresh=elapsed;}
            auto h=meshHeight(world,false);if(!h)return false;out=*h;return true;};
        auto orbitCenter=[&](){glm::vec3 c{pan.x,params.land.z+params.land.x*.13f,pan.y};if(cameraCentered)c.y=cameraHeight*exaggeration;if(params.meta.z==3||params.meta.z==6)c.y=params.land.z;c.y+=cameraPanY;return c;};
        auto flyDirection=[&](){return glm::vec3(std::sin(flyYaw)*std::cos(flyPitch),std::sin(flyPitch),std::cos(flyYaw)*std::cos(flyPitch));};
        // Fly camera (Unreal-style): enters from the current orbit pose and hands a matching orbit pose back when it ends.
        auto enterFly=[&](){auto c=orbitCenter();float radius=std::max(std::max(.001f,glm::length(c)*.000001f),params.domain.x*.83f*distance);
            glm::vec3 eye=c+glm::vec3(std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch))*radius;glm::vec3 d=glm::normalize(c-eye);
            flyOrbitRadius=radius;flyPos=eye;flyPitch=std::asin(std::clamp(d.y,-1.0f,1.0f));flyYaw=std::atan2(d.x,d.z);if(flySpeed<=0)flySpeed=std::clamp(params.domain.x/25.0f,10.0f,4000.0f);flyMode=true;};
        auto leaveFly=[&](){glm::vec3 d=flyDirection();float radius=flyOrbitRadius;distance=radius/(params.domain.x*.83f);glm::vec3 c=flyPos+d*radius;
            pan={c.x,c.z};cameraHeight=c.y/std::max(.001f,exaggeration);cameraCentered=true;cameraPanY=0;pitch=std::asin(std::clamp(-d.y,-1.0f,1.0f));yaw=std::atan2(-d.x,-d.z);flyMode=false;};
        auto resetCamera=[&](){yaw=.7f;pitch=.58f;distance=1;pan={};cameraPanY=0;cameraCentered=false;if(flyMode)enterFly();};
        auto releaseAutoPatch=[&](){if(!detailAuto)return;detailAuto=false;frontFull=backJob=swapReady=false;detailPlaced=false;detailRadius=autoRadiusBackup;detailGrid=autoGridBackup;};
        auto setCursorCapture=[&](bool capture){walkMouseCaptured=capture;walkLookReset=true;if(!options.hidden)glfwSetInputMode(window,GLFW_CURSOR,capture?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);};
        // Walk mode (Minecraft-style): the player drops onto the terrain at an absolute world position (x,z).
        auto enterWalk=[&](glm::vec2 xz){
            if(flyMode)leaveFly();
            if(!walkMode){savedExaggeration=exaggeration;exaggeration=1;
                if(detailPlaced&&!detailAuto){userDetailCenterBackup=detailCenter;userDetailRadiusBackup=detailRadius;userDetailGridBackup=detailGrid;hasUserDetailBackup=true;detailPlaced=false;}
                walkYaw=yaw+3.14159265f;walkPitch=-.05f;walkHeights.clear();walkHeightsRevision=0;heightCopyRevision=0;baseWalkHeights.clear();baseWalkCenter={1e30f,1e30f};}
            float half=params.domain.x*.49f;walkPos.x=std::clamp(xz.x,params.domain.y-half,params.domain.y+half);walkPos.z=std::clamp(xz.y,params.domain.z-half,params.domain.z+half);
            float ground=params.land.z;sampleGround({walkPos.x,walkPos.z},ground);walkPos.y=ground;walkVelY=0;walkFlying=false;walkMode=true;setCursorCapture(true);};
        auto leaveWalk=[&](){if(!walkMode)return;walkMode=false;walkFlying=false;setCursorCapture(false);exaggeration=savedExaggeration;releaseAutoPatch();
            {glm::vec2 local{walkPos.x-params.domain.y,walkPos.z-params.domain.z};pan=local;cameraHeight=walkPos.y;cameraCentered=true;cameraPanY=0;yaw=walkYaw-3.14159265f;pitch=std::clamp(-walkPitch,.25f,1.2f);distance=std::clamp(120.0f/(params.domain.x*.83f),1e-5f,1.0f);} // orbit around where you stood, same heading
            if(hasUserDetailBackup){detailCenter=userDetailCenterBackup;detailRadius=userDetailRadiusBackup;detailGrid=userDetailGridBackup;detailPlaced=true;hasUserDetailBackup=false;}walkHeights.clear();};
        // Centre the orbit camera on a saved point; far points move the preview window itself (like "View found location").
        auto centerOnPoint=[&](const Waypoint& w){glm::vec2 local{w.p.x-params.domain.y,w.p.z-params.domain.z};
            if(std::abs(local.x)>params.domain.x*.45f||std::abs(local.y)>params.domain.x*.45f){params.domain.y=w.p.x;params.domain.z=w.p.z;local={0,0};}
            pan=local;cameraHeight=w.p.y;{float ground=0;if(sampleGround({w.p.x,w.p.z},ground))cameraHeight=ground;}cameraCentered=true;cameraPanY=0;};
        auto presetLoaded=[&](){biomeResult.reset();biomeHighlight=false;biomeTool=false;biomeSearchStatus="Choose a biome and search";auto spawn=editor.document.spawn();previousPresetSpawn=spawn;params.domain.y=spawn.x;params.domain.z=spawn.y;if(editor.document.previewExtent)params.domain.x=*editor.document.previewExtent;resetCamera();detailPlaced=false;measurePoints.clear();measureHorizontal=measureDirect=measureSurface=0;hasSpawnMarker=false;detailTool=centerTool=spawnTool=measureTool=pointTool=copyTool=false;points.clear();renamingPoint=-1;++revision;};
        auto undoPreset=[&](bool redo){bool changed=redo?editor.redo():editor.undo();if(changed){previousPresetSpawn=editor.document.spawn();hasSpawnMarker=false;
            if(editor.lastEditedPath.starts_with("/world/properties/spawn")&&!baseHeightCache.empty()){auto uv=(previousPresetSpawn-glm::vec2(params.domain.y,params.domain.z))/params.domain.x+.5f;if(uv.x>=0&&uv.x<=1&&uv.y>=0&&uv.y<=1){int x=int(std::round(uv.x*128)),z=int(std::round(uv.y*128));float h=baseHeightCache[z*129+x];if(!showOceanFloor)h=std::max(h,params.land.z);spawnMarker={previousPresetSpawn.x,h,previousPresetSpawn.y};hasSpawnMarker=true;}}
        }};
        auto updateMeasurement=[&](){measureHorizontal=measureDirect=measureSurface=0;for(size_t i=1;i<measurePoints.size();++i){auto a=measurePoints[i-1],b=measurePoints[i];measureHorizontal+=glm::length(glm::vec2(b.x-a.x,b.z-a.z));measureDirect+=glm::length(b-a);auto last=a;for(int j=1;j<=64;++j){auto point=glm::mix(a,b,float(j)/64);if(j<64&&!baseHeightCache.empty()){auto uv=(glm::vec2(point.x,point.z)-glm::vec2(params.domain.y,params.domain.z))/params.domain.x+.5f;auto q=glm::clamp(uv,glm::vec2(0),glm::vec2(1))*128.0f;int x=std::min(127,int(q.x)),z=std::min(127,int(q.y));float u=q.x-x,v=q.y-z;point.y=std::lerp(std::lerp(baseHeightCache[z*129+x],baseHeightCache[z*129+x+1],u),std::lerp(baseHeightCache[(z+1)*129+x],baseHeightCache[(z+1)*129+x+1],u),v);if(!showOceanFloor)point.y=std::max(point.y,params.land.z);}measureSurface+=glm::length(point-last);last=point;}}};
        auto disableBiomeTool=[&](){biomeTool=false;biomeHighlight=false;biomeResult.reset();biomeSearchRequested=biomeSearchWaiting=false;biomeSearchStatus="Choose a biome and search";};
        const PresetJson defaultView=captureView();   // Settings > Reset: the view as the app starts (launcher options included)
        auto resetAppSettings=[&](){   // view, quality, finer detail, atmosphere, trees, camera, panels and FPS lock; keeps the preset, project content (FTF version, biome mods), points and history
            PresetJson view=defaultView;auto now=captureView();for(const char* keep:{"points","ftf_version","biome_mods"})if(now.contains(keep))view[keep]=now[keep];
            restoreView(view,true);applyGraphics(defaultGraphics);dock.load(defaultDock);walkSpeedScale=1;
            if(walkMode)leaveWalk();if(flyMode)leaveFly();++revision;editor.status="App settings reset to defaults";};
        auto savePreferences=[&](){try{project.savePreferences();}catch(const std::exception& e){editor.status=e.what();}};
        bool newPresetRequested=false;char newPresetName[160]{};ImVec2 saveAsTestButton{};bool saveAsPopupObserved=false;
        auto saveProject=[&](bool asNew){if(asNew){newPresetRequested=true;strncpy_s(newPresetName,(editor.document.name+" copy").c_str(),_TRUNCATE);return;}try{
            PresetLibrary library(project.root/"library");for(auto& entry:library.list())if(std::filesystem::weakly_canonical(entry.path)==std::filesystem::weakly_canonical(editor.document.source)){
                if(entry.builtin||entry.customDefault){newPresetRequested=true;strncpy_s(newPresetName,(editor.document.name+" copy").c_str(),_TRUNCATE);return;}
                library.save(editor.document,worldSeed,entry.name,entry.id);if(project.currentFile.empty())project.currentFile=project.root/"projects"/(entry.id+".ftfproject");break;
            }
            auto path=project.currentFile;if(asNew||path.empty())path=projectDialog(window,appRoot,true,asNew?editor.document.name+" copy":editor.document.name);
            if(path.empty())return;if(asNew&&!project.currentFile.empty()&&_wcsicmp(std::filesystem::weakly_canonical(path).c_str(),std::filesystem::weakly_canonical(project.currentFile).c_str())==0)throw std::runtime_error("Save as new requires a different project name.");
            project.save(path,editor.document,worldSeed,captureView());++saveSerial;editor.status="Saved project: "+path.filename().string();
        }catch(const std::exception& e){editor.status=e.what();}};
        auto openProject=[&](){try{auto path=projectDialog(window,appRoot,false);if(path.empty())return;
            auto ext=path.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
            if(ext==L".ftfproject")restoreView(project.load(path,editor.document,worldSeed));else{editor.loadDocument(path,worldSeed);project.currentFile.clear();presetLoaded();}
            project.restorePending=false;editor.status="Opened "+path.filename().string();
        }catch(const std::exception& e){editor.status=e.what();}};
        auto paletteLegends=[&](){
                if(sectionIconHeader(biomeColors==3?"Biome tint legend":"Biome color legend")){ImGui::BeginChild("Biome legend",ImVec2(0,150),true);const char* key=biomeColors==0?"map":biomeColors==2?"foliage":"grass";for(auto& b:biomeLegend["biomes"]){uint32_t rgb=b[key];ImGui::ColorButton(("##biome"+std::to_string(b["id"].get<int>())).c_str(),ImVec4(((rgb>>16)&255)/255.0f,((rgb>>8)&255)/255.0f,(rgb&255)/255.0f,1),ImGuiColorEditFlags_NoTooltip,ImVec2(13,13));ImGui::SameLine();ImGui::TextUnformatted(b["name"].get<std::string>().c_str());}ImGui::EndChild();}
                if(biomeColors==3)ImGui::TextWrapped("Minecraft block colours with biome tints and FTF scree. Surface materials and strata are sampled on the mesh; textures and individual blocks are not drawn.");
                if(sectionIconHeader("Minecraft block colour reference")){ImGui::InputText(labelAbove("Find block").c_str(),blockSearch,sizeof(blockSearch));ImGui::TextDisabled("Representative colours; biome-tinted blocks vary by location.");ImGui::BeginChild("Block palette",ImVec2(0,180),true);std::string needle=blockSearch;std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});for(auto& block:blockLegend["blocks"]){auto name=block["name"].get<std::string>();if(name.find(needle)==std::string::npos)continue;uint32_t rgb=block["color"];glm::vec3 c{((rgb>>16)&255)/255.0f,((rgb>>8)&255)/255.0f,(rgb&255)/255.0f};int tint=block["tint"];if(tint){uint32_t t=tint==1?0x91bd59:tint==2?0x77ab2f:tint==3?0x3f76e4:tint==4?0x80a755:0x619961;c*=glm::vec3{((t>>16)&255)/255.0f,((t>>8)&255)/255.0f,(t&255)/255.0f};}ImGui::ColorButton(("##block"+name).c_str(),ImVec4(c.x,c.y,c.z,block["source"]=="invisible"?0.0f:1.0f),ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_AlphaPreview,ImVec2(13,13));ImGui::SameLine();ImGui::TextUnformatted((name+(block["source"]=="invisible"?" (invisible)":"")).c_str());}ImGui::EndChild();}
        };
        while(!glfwWindowShouldClose(window)&&(options.frames==0||frameNumber<options.frames)){
            auto start=Clock::now();double dt=std::chrono::duration<double>(start-lastFrame).count();lastFrame=start;elapsed+=dt;
            if(biomeResult&&biomeContentFingerprint!=editor.document.data.dump()+std::to_string(worldSeed)+":"+std::to_string(biomeModMask)+":"+std::to_string(ftfVersionIndex)){biomeResult.reset();biomeHighlight=false;biomeSearchStatus="Preset or seed changed; search again";}
            if(censusWaiting){try{census=summarizeBiomeCensus(biomeLegend,censusPossible,biomeSurface.generatedBiomes(),censusParams.domain.x,int(censusParams.meta.x));censusStatus.clear();}catch(const std::exception& e){census.reset();censusStatus=e.what();}censusWaiting=false;}
            if(biomeSearchWaiting){try{biomeResult=biomeSurface.findBiome(uint32_t(biomeChoice),{biomeParams.domain.y,biomeParams.domain.z},biomeRadius);biomeHighlight=biomeResult.has_value();biomeSearchStatus=biomeResult?"Found at "+std::to_string(int(biomeResult->extra.x))+", "+std::to_string(int(biomeResult->extra.y)):"Not found at this search resolution";}catch(const std::exception& e){biomeSearchStatus=e.what();}biomeSearchWaiting=false;}
            glfwPollEvents();auto cmd=gpu.begin();if(!cmd){glfwWaitEventsTimeout(.02);continue;}
            smoothedFrame=smoothedFrame*.95+dt*1000*.05;
            if(!surface.frames.empty()&&surface.frames.size()==gpu.swap.ImageCount){auto t=surface.timing(gpu.swap.FrameIndex);if(editPreview.initialized&&editPreview.worker.frames.size()==gpu.swap.ImageCount){auto bg=editPreview.worker.timing(gpu.swap.FrameIndex);if(bg.valid){t.total+=bg.total;t.generate+=bg.generate;t.filter+=bg.filter;t.generated|=bg.generated;}}if(t.valid){lastDrawTiming=t;if(t.generated)lastTiming=t;if(frameNumber>40){gpuTimes.push_back(t.total);drawTimes.push_back(t.draw);if(t.generated){genTimes.push_back(t.generate);filterTimes.push_back(t.filter);cacheTimes.push_back(t.cache);worldTimes.push_back(t.world);oceanTimes.push_back(t.ocean);composeTimes.push_back(t.compose);}}}}
            ImGui_ImplVulkan_NewFrame();ImGui_ImplGlfw_NewFrame();
            // While the mouse is captured for walking, the UI must neither see the cursor nor react to Space/Enter on a focused widget.
            if(walkMode&&walkMouseCaptured){io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);io.ConfigFlags&=~ImGuiConfigFlags_NavEnableKeyboard;}else io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
            if(interactiveLaunch&&ImGui::IsKeyPressed(ImGuiKey_F11,false)){
                if(glfwGetWindowMonitor(window))glfwSetWindowMonitor(window,nullptr,windowedX,windowedY,windowedW,windowedH,0);
                else{glfwGetWindowPos(window,&windowedX,&windowedY);glfwGetWindowSize(window,&windowedW,&windowedH);auto* m=glfwGetPrimaryMonitor();auto* md=glfwGetVideoMode(m);glfwSetWindowMonitor(window,m,0,0,md->width,md->height,md->refreshRate);}
                chrome.refresh();
            }
            if(options.exercisePanels){auto press=[&](ImVec2 p){io.AddMousePosEvent(p.x,p.y);io.AddMouseButtonEvent(0,true);};auto release=[&](){io.AddMouseButtonEvent(0,false);};
                if(frameNumber==5||frameNumber==9||frameNumber==18||(options.exercisePanelRestore&&frameNumber==33))press({dock.left.pos.x+14,dock.top+14});
                if(frameNumber==6||frameNumber==10||frameNumber==19||frameNumber==22||frameNumber==26||frameNumber==30||(options.exercisePanelRestore&&(frameNumber==34||frameNumber==38||frameNumber==42||frameNumber==46)))release();
                if(frameNumber==13)press({dock.left.pos.x+dock.leftWidth+6,400});if(frameNumber==14)io.AddMousePosEvent(dock.left.pos.x+dock.leftWidth+66,400);if(frameNumber==15)release();
                if(frameNumber==21||(options.exercisePanelRestore&&frameNumber==37))press({dock.right.pos.x+14,dock.top+14});
                if(frameNumber==25||(options.exercisePanelRestore&&frameNumber==41))press({dock.bottom.pos.x+14,dock.bottom.pos.y+14});
                if(frameNumber==29||(options.exercisePanelRestore&&frameNumber==45))press({dock.viewport.pos.x+14,dock.top+14});
            }
            if(options.exerciseBiomeOff){if(frameNumber==12){auto p=biomeToolbarButton;io.AddMousePosEvent(p.x,p.y);io.AddMouseButtonEvent(0,true);}if(frameNumber==13)io.AddMouseButtonEvent(0,false);}

            if(options.exerciseSaveAs){if(frameNumber==5){io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiMod_Shift,true);io.AddKeyEvent(ImGuiKey_S,true);}if(frameNumber==6){io.AddKeyEvent(ImGuiKey_S,false);io.AddKeyEvent(ImGuiMod_Shift,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);}if(frameNumber==9&&!options.testSessionRoot.empty()){io.AddMousePosEvent(saveAsTestButton.x,saveAsTestButton.y);io.AddMouseButtonEvent(0,true);}if(frameNumber==10)io.AddMouseButtonEvent(0,false);}
            if(options.testRestoreAccept){if(frameNumber==4)io.AddMousePosEvent(restoreTestClick.x,restoreTestClick.y);if(frameNumber==5)io.AddMouseButtonEvent(0,true);if(frameNumber==6)io.AddMouseButtonEvent(0,false);}
            if(options.exerciseLowOrbit){if(frameNumber==4){io.AddMousePosEvent(io.DisplaySize.x*.51f,io.DisplaySize.y*.55f);io.AddMouseButtonEvent(0,true);}if(frameNumber==5)io.AddMousePosEvent(io.DisplaySize.x*.51f,100);if(frameNumber==6)io.AddMouseButtonEvent(0,false);}
            if(options.exerciseFileMenu){if(frameNumber==3){io.AddMousePosEvent(24,14);io.AddMouseButtonEvent(0,true);}if(frameNumber==4)io.AddMouseButtonEvent(0,false);}
            if(options.exerciseDetailZoom&&frameNumber==16)io.AddMouseWheelEvent(0,2);
            if(options.exerciseSpawn||options.exerciseMeasure){ImGuiKey key=options.exerciseSpawn?ImGuiKey_P:ImGuiKey_M;if(frameNumber==5)io.AddKeyEvent(key,true);if(frameNumber==6)io.AddKeyEvent(key,false);if(frameNumber==8){io.AddMousePosEvent(io.DisplaySize.x*.51f,io.DisplaySize.y*.55f);io.AddMouseButtonEvent(0,true);}if(frameNumber==9)io.AddMouseButtonEvent(0,false);if(options.exerciseMeasure){if(frameNumber==12){io.AddMousePosEvent(io.DisplaySize.x*.60f,io.DisplaySize.y*.64f);io.AddMouseButtonEvent(0,true);}if(frameNumber==13)io.AddMouseButtonEvent(0,false);}}
            if(options.exerciseReset2){   /* test: change app settings and the preset, reset, check defaults back and the preset edit kept */
                if(frameNumber==5){fpsLimit=30;lodBandCount=3;atmo.fog=true;params.meta.x=512;editor.placeSpawn(initialSpawn+glm::vec2(64,0));}
                if(frameNumber==8)resetAppSettings();if(frameNumber==11)resetSettingsRequested=true;   /* leaves the confirmation open for the UI screenshot */
                if(frameNumber==10)resetObserved=fpsLimit==60&&lodBandCount==6&&!atmo.fog&&params.meta.x==defaultView["meta"][0].get<uint32_t>()&&editor.document.spawn()!=initialSpawn&&editor.historySize()>1;}
            if(options.shortcutTest){   /* test: undo with Ctrl+Z inside walk (1) or fly (2) */
                if(frameNumber==4){if(options.shortcutTest==1)enterWalk(editor.document.spawn());else enterFly();}
                if(frameNumber==8)editor.placeSpawn(initialSpawn+glm::vec2(96,0));
                if(frameNumber==12){io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_LeftCtrl,true);io.AddKeyEvent(ImGuiKey_Z,true);}if(frameNumber==13){io.AddKeyEvent(ImGuiKey_Z,false);io.AddKeyEvent(ImGuiKey_LeftCtrl,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);}
                if(frameNumber==16)shortcutUndoObserved=editor.document.spawn()==initialSpawn&&(walkMode||flyMode);}
            if(options.exerciseSpawnHistory){if(frameNumber==13){io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_Z,true);}if(frameNumber==14){io.AddKeyEvent(ImGuiKey_Z,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);}if(frameNumber==17){io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiMod_Shift,true);io.AddKeyEvent(ImGuiKey_Z,true);}if(frameNumber==18){io.AddKeyEvent(ImGuiKey_Z,false);io.AddKeyEvent(ImGuiMod_Shift,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);}}
            if(options.exercisePan){if(frameNumber==4){io.AddMousePosEvent(io.DisplaySize.x*.51f,io.DisplaySize.y*.5f);io.AddMouseButtonEvent(1,true);}if(frameNumber==5)io.AddMousePosEvent(io.DisplaySize.x*.51f+(options.exercisePanVertical?0:80),io.DisplaySize.y*.5f+(options.exercisePanVertical?80:0));if(frameNumber==6)io.AddMouseButtonEvent(1,false);}
            if(options.exerciseCopy){if(frameNumber==5)io.AddKeyEvent(ImGuiKey_K,true);if(frameNumber==6)io.AddKeyEvent(ImGuiKey_K,false);if(frameNumber==8){io.AddMousePosEvent(io.DisplaySize.x*.51f,io.DisplaySize.y*.6f);io.AddMouseButtonEvent(0,true);}if(frameNumber==9)io.AddMouseButtonEvent(0,false);
                if(frameNumber==12){sunBefore=sunAzimuth;panBefore=pan;io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_LeftCtrl,true);io.AddKeyEvent(ImGuiMod_Shift,true);io.AddKeyEvent(ImGuiKey_LeftShift,true);io.AddMousePosEvent(io.DisplaySize.x*.5f,io.DisplaySize.y*.6f);}
                if(frameNumber==13)io.AddMouseButtonEvent(1,true);if(frameNumber>=14&&frameNumber<=18)io.AddMousePosEvent(io.DisplaySize.x*.5f+40.0f*(frameNumber-13),io.DisplaySize.y*.6f);
                if(frameNumber==19){io.AddMouseButtonEvent(1,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddKeyEvent(ImGuiKey_LeftCtrl,false);io.AddKeyEvent(ImGuiMod_Shift,false);io.AddKeyEvent(ImGuiKey_LeftShift,false);}}
            if(options.exercisePoints){if(frameNumber==5)io.AddKeyEvent(ImGuiKey_B,true);if(frameNumber==6)io.AddKeyEvent(ImGuiKey_B,false);
                if(frameNumber==8){io.AddMousePosEvent(io.DisplaySize.x*.51f,io.DisplaySize.y*.55f);io.AddMouseButtonEvent(0,true);}if(frameNumber==9)io.AddMouseButtonEvent(0,false);
                if(frameNumber==12){io.AddMousePosEvent(io.DisplaySize.x*.60f,io.DisplaySize.y*.64f);io.AddMouseButtonEvent(0,true);}if(frameNumber==13)io.AddMouseButtonEvent(0,false);
                if(frameNumber==16){io.AddMousePosEvent(io.DisplaySize.x*.43f,io.DisplaySize.y*.50f);io.AddMouseButtonEvent(0,true);}if(frameNumber==17)io.AddMouseButtonEvent(0,false);}
            if(options.exercisePoints&&frameNumber==30&&!points.empty()){auto file=std::filesystem::path(options.report.empty()?"out/points-roundtrip.json":options.report).parent_path()/"points-roundtrip.ftfproject";std::filesystem::create_directories(file.parent_path());
                project.save(file,editor.document,worldSeed,captureView());pointsRoundTrip[0]=points.size();auto names=points[1].name;points.clear();restoreView(project.load(file,editor.document,worldSeed));pointsRoundTrip[1]=points.size();pointsRoundTripName=points.size()>1?points[1].name:std::string();(void)names;
                if(!points.empty()){centerOnPoint(points[0]);pointsCentered={pan.x,pan.y};}}
            if(options.exerciseWalk){if(frameNumber==4)enterWalk(options.walkAt?glm::vec2(options.walkAtX,options.walkAtZ):editor.document.loaded()?editor.document.spawn():glm::vec2(params.domain.y,params.domain.z));
                if(frameNumber==12)io.AddKeyEvent(ImGuiKey_W,true);if(frameNumber==12+options.walkHold)io.AddKeyEvent(ImGuiKey_W,false);
                if(frameNumber==18+options.walkHold)io.AddKeyEvent(ImGuiKey_Space,true);if(frameNumber==19+options.walkHold)io.AddKeyEvent(ImGuiKey_Space,false);
                if(options.exerciseEscape){if(frameNumber==70)io.AddKeyEvent(ImGuiKey_Escape,true);if(frameNumber==71)io.AddKeyEvent(ImGuiKey_Escape,false);}}
            if(options.exerciseFly){if(frameNumber==3)io.AddMousePosEvent(io.DisplaySize.x*.51f,io.DisplaySize.y*.55f);if(frameNumber==5)io.AddKeyEvent(ImGuiKey_F,true);if(frameNumber==6)io.AddKeyEvent(ImGuiKey_F,false);
                if(frameNumber==8)io.AddKeyEvent(ImGuiKey_W,true);if(frameNumber==30)io.AddKeyEvent(ImGuiKey_W,false);if(frameNumber==31)io.AddMouseWheelEvent(0,3);
                if(frameNumber==33)io.AddMouseButtonEvent(1,true);if(frameNumber==34)io.AddMousePosEvent(io.DisplaySize.x*.51f+90,io.DisplaySize.y*.55f+30);if(frameNumber==36)io.AddMouseButtonEvent(1,false);
                if(frameNumber==38)io.AddKeyEvent(ImGuiKey_E,true);if(frameNumber==44)io.AddKeyEvent(ImGuiKey_E,false);
                if(options.exerciseEscape){if(frameNumber==55)io.AddKeyEvent(ImGuiKey_Escape,true);if(frameNumber==56)io.AddKeyEvent(ImGuiKey_Escape,false);}}
            if(options.exerciseReset){if(frameNumber==15)io.AddKeyEvent(ImGuiKey_R,true);if(frameNumber==16)io.AddKeyEvent(ImGuiKey_R,false);}
            if(options.exerciseHover&&frameNumber==5)io.AddMousePosEvent(io.DisplaySize.x*.51f,io.DisplaySize.y*.55f);
            if(options.exerciseCenter){if(frameNumber==5)io.AddKeyEvent(ImGuiKey_C,true);if(frameNumber==6)io.AddKeyEvent(ImGuiKey_C,false);if(frameNumber==8){io.AddMousePosEvent(io.DisplaySize.x*.51f,io.DisplaySize.y*.55f);io.AddMouseButtonEvent(0,true);}if(frameNumber==9)io.AddMouseButtonEvent(0,false);}
            if(options.exerciseZoomGradual){if(frameNumber==3)io.AddMousePosEvent(io.DisplaySize.x*.51f,io.DisplaySize.y*.55f);if(frameNumber>=6&&frameNumber%4==0&&frameNumber<260)io.AddMouseWheelEvent(0,1);}
            if(options.exerciseZoom&&frameNumber==15){io.AddMousePosEvent(io.DisplaySize.x*.51f,io.DisplaySize.y*.55f);io.AddMouseWheelEvent(0,100);}
            if(options.exerciseDetailOff){if(frameNumber==16)io.AddKeyEvent(ImGuiKey_H,true);if(frameNumber==17)io.AddKeyEvent(ImGuiKey_H,false);}
            if(options.exerciseDetail){
                if(frameNumber==5)io.AddKeyEvent(ImGuiKey_H,true);if(frameNumber==6)io.AddKeyEvent(ImGuiKey_H,false);
                if(frameNumber==8){io.AddMousePosEvent(io.DisplaySize.x*.51f,io.DisplaySize.y*.55f);io.AddMouseButtonEvent(0,true);}if(frameNumber==9)io.AddMouseButtonEvent(0,false);
                if(frameNumber==12){io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddMouseWheelEvent(0,2);}if(frameNumber==13)io.AddKeyEvent(ImGuiMod_Ctrl,false);
            }
            if(options.exitTest&&options.exitAction==1){if(frameNumber==17){io.AddMousePosEvent(saveAsTestButton.x,saveAsTestButton.y);io.AddMouseButtonEvent(0,true);}if(frameNumber==18)io.AddMouseButtonEvent(0,false);}
            if(options.exitTest){if(frameNumber==11){io.AddMousePosEvent(exitCancelPoint.x,exitCancelPoint.y);io.AddMouseButtonEvent(0,true);}if(frameNumber==12)io.AddMouseButtonEvent(0,false);}
            ImGui::NewFrame();
            if(options.exitTest&&frameNumber==7){if(options.exitAction==1&&!options.testSessionRoot.empty())project.currentFile=project.root/"projects/exit-save-check.ftfproject";closeRequested=true;}
            if(options.poseTest&&frameNumber==7){enterFly();flyPos+=glm::vec3(123,77,-99);flyPitch=.4f;flyYaw=1.3f;auto expected=flyPos,dir=flyDirection();leaveFly();enterFly();poseError=glm::length(flyPos-expected);poseDirectionError=glm::length(flyDirection()-dir);leaveFly();}
            if(options.graphicsTest&&frameNumber==7){ambientOcclusion=true;cloudShadows=true;sunShadows=true;sunIntensity=1.7f;auto view=captureView();view["ambient_occlusion"]=false;view["cloud_shadows"]=false;view["sun_shadows"]=false;view["sun"]={0,0,0,0,0,0};restoreView(view);presetLoaded();graphicsRestoreObserved=ambientOcclusion&&cloudShadows&&sunShadows&&sunIntensity==1.7f;}
            editor.document.previewExtent=params.domain.x;const bool keysBlocked=io.WantTextInput||(walkMode&&walkMouseCaptured);
            if(options.countBiomes>0&&frameNumber==3){censusGrid=options.countBiomes;censusRequested=true;}
            if(options.findBiome>=0&&frameNumber==3){biomeChoice=std::min(options.findBiome,int(biomeLegend["biomes"].size()-1));biomeTool=true;biomeRadius=options.findRadius>0?options.findRadius:params.domain.x;biomeUseWorldSize=options.findRadius<=0;biomeSearchGrid=128;biomeSearchRequested=true;}
            if(ImGui::BeginMainMenuBar()){
                if(ImGui::BeginMenu("File")){
                    if(ImGui::MenuItem("Open project, preset or datapack...","Ctrl+O"))openProject();
                    if(ImGui::MenuItem("Open datapack folder...")){if(editor.importFolder(worldSeed)){presetLoaded();project.currentFile.clear();project.restorePending=false;}}
                    if(ImGui::MenuItem("Preset library..."))editor.showLibrary();
                    if(ImGui::MenuItem("Reset preset to last saved state",nullptr,false,editor.canReset("")))editor.resetToSaved("");ImGui::Separator();
                    if(ImGui::MenuItem("Save","Ctrl+S",false,editor.document.loaded()))saveProject(false);
                    if(ImGui::MenuItem("Save as new preset...","Ctrl+Shift+S",false,editor.document.loaded()))saveProject(true);
                    if(ImGui::BeginMenu("Export",editor.document.loaded())){if(ImGui::MenuItem("FTF preset JSON..."))editor.exportAction(false,worldSeed);if(ImGui::MenuItem("FTF datapack..."))editor.exportAction(true,worldSeed);ImGui::EndMenu();}
                    ImGui::Separator();if(ImGui::MenuItem("Autosave",nullptr,project.autosave)){project.autosave=!project.autosave;savePreferences();}
                    if(ImGui::BeginMenu("Autosave settings")){int seconds=project.intervalSeconds;if(valueSliderInt("Autosave interval",seconds,5,3600,"%d s",ImGuiSliderFlags_Logarithmic,300)){project.intervalSeconds=std::clamp(seconds,5,3600);savePreferences();}ImGui::TextDisabled("60 seconds = every 1 minute");ImGui::EndMenu();}
                    if(ImGui::MenuItem("Open autosave folder")){auto folder=project.autosaveFolder();std::filesystem::create_directories(folder);ShellExecuteW(glfwGetWin32Window(window),L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
                    ImGui::Separator();if(ImGui::MenuItem("Quit"))closeRequested=true;ImGui::EndMenu();
                }
                bool settingsOpen=ImGui::BeginMenu("Settings");{auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();settingsPos={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}if(settingsOpen){   // app-wide settings (not the preset)
                    ImGui::TextColored({.62f,.45f,.95f,1},"Viewport frame rate");
                    bool locked=fpsLimit>0;if(ImGui::Checkbox("Lock viewport FPS",&locked))fpsLimit=locked?60:0;
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("Caps how often the 3D view is drawn. Time left in each frame goes to building terrain\n(background finer detail and refinement), so a lower cap builds terrain faster.");
                    ImGui::BeginDisabled(!locked);int shown=locked?fpsLimit:60;if(valueSliderInt("Frames per second",shown,20,240,"%d fps",0,300)&&locked)fpsLimit=shown;ImGui::EndDisabled();
                    if(!locked)ImGui::TextDisabled("Off: draws as fast as the GPU allows.");
                    ImGui::Separator();if(ImGui::MenuItem("Reset all app settings..."))resetSettingsRequested=true;
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("Resolution, view, six-band LOD, finer detail, atmosphere, trees, camera, panels and FPS lock.\nYour preset, projects, library, points and undo history are kept.");
                    ImGui::EndMenu();
                }
                ImVec2 changelogPos{},modPos{},infoPos{},firstEntryPos{};bool changelogOpen=ImGui::BeginMenu("Changelog");{auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();changelogPos={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}
                if(changelogOpen){
                    ImGui::TextColored({.62f,.45f,.95f,1},"FTF Surface Lab %s%s",appVersion,appEdition);ImGui::Separator();
                    for(auto& entry:appChangelog()){
                        std::string label=std::string("Version ")+entry.version+"  -  "+entry.date;
                        bool entryOpen=ImGui::BeginMenu(label.c_str());if(&entry==&appChangelog().front()){auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();firstEntryPos={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}
                        if(entryOpen){ImGui::PushTextWrapPos(560);ImGui::TextColored({.40f,.86f,.72f,1},"%s",entry.title);for(auto* item:entry.items)ImGui::BulletText("%s",item);ImGui::PopTextWrapPos();ImGui::EndMenu();}
                    }
                    ImGui::EndMenu();
                }
                bool infoOpen=ImGui::BeginMenu("Info");{auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();infoPos={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}if(infoOpen){   // what matches the game exactly and what is an approximation (tools/gen_app_info.py)
                    ImGui::PushTextWrapPos(600);ImGui::TextColored({.62f,.45f,.95f,1},"How close is the preview to the game?");ImGui::TextDisabled("Compared with Minecraft 1.21.1 + FTF at the same seed and preset.");ImGui::Separator();
                    for(auto& group:appInfo()){
                        const std::string title=group.title;ImVec4 color=title.rfind("Exact",0)==0?ImVec4{.40f,.86f,.55f,1}:title.rfind("Approximate",0)==0?ImVec4{.96f,.69f,.35f,1}:ImVec4{.62f,.66f,.70f,1};
                        ImGui::TextColored(color,"%s",group.title);
                        for(auto& row:group.rows){ImGui::Bullet();ImGui::SameLine();ImGui::TextColored({.90f,.94f,.95f,1},"%s",row.name);ImGui::Indent(22);ImGui::TextDisabled("%s",row.status);ImGui::Unindent(22);}
                        ImGui::Spacing();
                    }
                    ImGui::PopTextWrapPos();ImGui::EndMenu();
                }
                bool modOpen=ImGui::BeginMenu("Mod support");{auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();modPos={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}
                if(modOpen){
                    ImGui::PushTextWrapPos(560);
                    for(auto& group:appModSupport()){
                        ImGui::TextColored({.96f,.69f,.35f,1},"%s",group.title);
                        for(auto& row:group.rows){ImGui::Bullet();ImGui::SameLine();ImGui::TextColored({.90f,.94f,.95f,1},"%s",row.name);ImGui::Indent(22);ImGui::TextDisabled("%s",row.status);ImGui::Unindent(22);}
                        ImGui::Spacing();
                    }
                    ImGui::PopTextWrapPos();ImGui::EndMenu();
                }
                if(options.previewMenu){if(frameNumber==6){auto mp=options.previewMenu==2?modPos:options.previewMenu==4?infoPos:options.previewMenu==5?settingsPos:changelogPos;io.AddMousePosEvent(mp.x,mp.y);io.AddMouseButtonEvent(0,true);}if(frameNumber==7)io.AddMouseButtonEvent(0,false);if(options.previewMenu==3&&frameNumber==10)io.AddMousePosEvent(firstEntryPos.x,firstEntryPos.y);}
                const float captionStart=ImGui::GetCursorPosX();
                ImGui::TextDisabled("%s",project.currentFile.empty()?"Unsaved project":project.currentFile.filename().string().c_str());
                chrome.caption(captionStart,ImGui::GetWindowWidth()-128,ImGui::GetWindowHeight());
                ImGui::SameLine(std::max(ImGui::GetCursorPosX()+8,ImGui::GetWindowWidth()-124));
                auto windowControl=[&](const char* label,ImVec4 color,const char* hint){ImGui::PushStyleColor(ImGuiCol_Button,color);bool hit=ImGui::Button((std::string("##window")+label).c_str(),{34,20});ImGui::PopStyleColor();auto a=ImGui::GetItemRectMin();ImVec2 c{a.x+17,a.y+10};auto* d=ImGui::GetWindowDrawList();auto ink=IM_COL32(245,248,250,255);if(label[0]=='_')d->AddLine({c.x-5,c.y+4},{c.x+5,c.y+4},ink,1.6f);else if(label[0]=='[')d->AddRect({c.x-5,c.y-5},{c.x+5,c.y+5},ink,0,0,1.5f);else{d->AddLine({c.x-4,c.y-4},{c.x+4,c.y+4},ink,1.7f);d->AddLine({c.x-4,c.y+4},{c.x+4,c.y-4},ink,1.7f);}if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",hint);return hit;};
                if(windowControl("_",{.55f,.40f,.10f,1},"Minimize"))glfwIconifyWindow(window);ImGui::SameLine(0,4);
                if(windowControl("[]",{.12f,.42f,.29f,1},"Maximize or restore")){if(glfwGetWindowMonitor(window))glfwSetWindowMonitor(window,nullptr,windowedX,windowedY,windowedW,windowedH,0);else if(glfwGetWindowAttrib(window,GLFW_MAXIMIZED))glfwRestoreWindow(window);else glfwMaximizeWindow(window);chrome.refresh();}ImGui::SameLine(0,4);
                if(windowControl("X",{.65f,.16f,.19f,1},"Exit (asks to save)"))closeRequested=true;
                ImGui::EndMainMenuBar();
            }
            {   // File/history shortcuts in every navigation mode. In walk and fly they are read straight from the keyboard (the camera
                // owns the mouse and keys there, and the UI layer did not see them reliably); elsewhere through ImGui (tests inject them).
                const bool cameraMode=(walkMode||flyMode)&&!options.hidden;bool ctrl=io.KeyCtrl,shift=io.KeyShift,keyO=false,keyS=false,keyZ=false,keyY=false;
                if(cameraMode){auto down=[&](int k){return glfwGetKey(window,k)==GLFW_PRESS;};ctrl=down(GLFW_KEY_LEFT_CONTROL)||down(GLFW_KEY_RIGHT_CONTROL);shift=down(GLFW_KEY_LEFT_SHIFT)||down(GLFW_KEY_RIGHT_SHIFT);
                    bool now[4]={down(GLFW_KEY_O),down(GLFW_KEY_S),down(GLFW_KEY_Z),down(GLFW_KEY_Y)};keyO=now[0]&&!shortcutHeld[0];keyS=now[1]&&!shortcutHeld[1];keyZ=now[2]&&!shortcutHeld[2];keyY=now[3]&&!shortcutHeld[3];for(int i=0;i<4;++i)shortcutHeld[i]=now[i];}
                else{for(bool& held:shortcutHeld)held=false;keyO=ImGui::IsKeyPressed(ImGuiKey_O,false);keyS=ImGui::IsKeyPressed(ImGuiKey_S,false);keyZ=ImGui::IsKeyPressed(ImGuiKey_Z,false);keyY=ImGui::IsKeyPressed(ImGuiKey_Y,false);}
                if(!project.restorePending&&ctrl){if(!io.WantTextInput&&keyO)openProject();if(!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)&&keyS)saveProject(shift);if(!io.WantTextInput&&(keyZ||keyY)){bool again=shift||keyY;if(editor.document.loaded())undoPreset(again);else if(auto& from=again?redo:undo;!from.empty()){(again?undo:redo).push_back(params);params=from.back();from.pop_back();}}}
            }
            if(closeRequested){editor.dismissLibrary();ImGui::OpenPopup("Save before exiting?");closeRequested=false;}
            if(resetSettingsRequested){ImGui::OpenPopup("Reset all app settings?");resetSettingsRequested=false;}
            if(ImGui::BeginPopupModal("Reset all app settings?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
                ImGui::TextUnformatted("Put every app setting back to its default?");ImGui::Spacing();
                ImGui::TextColored({.96f,.69f,.35f,1},"Reset:");ImGui::SameLine();ImGui::TextUnformatted("resolution and view, six-band LOD, finer detail, atmosphere and lighting,");
                ImGui::TextUnformatted("        trees, camera and navigation, panel layout, FPS lock.");
                ImGui::TextColored({.40f,.86f,.55f,1},"Kept: ");ImGui::SameLine();ImGui::TextUnformatted("your preset and its values, projects, library, points, FTF version, biome mods, undo history.");
                ImGui::Spacing();if(ImGui::Button("Reset settings",{150,0})){resetAppSettings();ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel",{110,0})||ImGui::IsKeyPressed(ImGuiKey_Escape,false))ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if(ImGui::BeginPopupModal("Save before exiting?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
                exitPromptObserved=true;ImGui::TextUnformatted("Save your current preset and project before exiting?");
                ImGui::BeginDisabled(!editor.document.loaded()||project.restorePending);if(ImGui::Button("Save and exit")){closeAfterSave=true;closeSaveSerial=saveSerial;ImGui::CloseCurrentPopup();saveProject(false);}{auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();if(options.exitAction==1)exitCancelPoint={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}ImGui::EndDisabled();
                ImGui::SameLine();if(ImGui::Button("Exit without saving")){glfwSetWindowShouldClose(window,GLFW_TRUE);ImGui::CloseCurrentPopup();}{auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();if(options.exitAction==2)exitCancelPoint={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}
                ImGui::SameLine();if(ImGui::Button("Cancel")){exitCancelObserved=true;if(project.restorePending)restoreDialogRequested=false;ImGui::CloseCurrentPopup();}{auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();if(options.exitAction==0)exitCancelPoint={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}ImGui::EndPopup();
            }
            if(closeAfterSave){if(saveSerial>closeSaveSerial){glfwSetWindowShouldClose(window,GLFW_TRUE);closeAfterSave=false;}else if(!newPresetRequested&&!ImGui::IsPopupOpen("Save as new preset")){closeAfterSave=false;closeRequested=true;}}
            if(newPresetRequested){ImGui::OpenPopup("Save as new preset");newPresetRequested=false;}
            if(ImGui::BeginPopupModal("Save as new preset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
                saveAsPopupObserved=true;ImGui::InputText(labelAbove("New name",320).c_str(),newPresetName,sizeof(newPresetName));
                bool saveNewClicked=ImGui::Button("Save new preset");auto saveMin=ImGui::GetItemRectMin(),saveMax=ImGui::GetItemRectMax();saveAsTestButton={(saveMin.x+saveMax.x)*.5f,(saveMin.y+saveMax.y)*.5f};if(saveNewClicked){try{
                    PresetLibrary library(project.root/"library");std::string name=newPresetName;
                    for(auto& entry:library.list())if(entry.name==name)throw std::runtime_error("Choose a new, unused preset name.");
                    auto entry=library.save(editor.document,worldSeed,name);editor.document.source=entry.path;editor.document.name=entry.name;
                    project.currentFile=project.root/"projects"/(entry.id+".ftfproject");project.save(project.currentFile,editor.document,worldSeed,captureView());++saveSerial;editor.status="Saved new preset: "+name;ImGui::CloseCurrentPopup();
                }catch(const std::exception& e){editor.status=e.what();}}
                ImGui::SameLine();if(ImGui::Button("Cancel")){closeAfterSave=false;ImGui::CloseCurrentPopup();}ImGui::TextWrapped("%s",editor.status.c_str());ImGui::EndPopup();
            }
            if(project.restorePending&&!restoreDialogRequested){ImGui::OpenPopup("Restore last project?");restoreDialogRequested=true;}
            if(ImGui::BeginPopupModal("Restore last project?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
                ImGui::TextUnformatted("Restore the preset and view from your last session?");
                bool restoreClicked=ImGui::Button("Restore");if(options.testRestoreAccept&&frameNumber==2){auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();restoreTestClick={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}
                if(restoreClicked){try{restoreView(project.load(project.recoveryPath(),editor.document,worldSeed,true));editor.status="Restored last working project";ImGui::CloseCurrentPopup();}catch(const std::exception& e){editor.status=e.what();}}
                ImGui::SameLine();if(ImGui::Button("Start fresh")){migrateLegacyGraphics=false;project.restorePending=false;ImGui::CloseCurrentPopup();}if(!editor.status.empty())ImGui::TextWrapped("%s",editor.status.c_str());ImGui::EndPopup();
            }
            auto* mainViewport=ImGui::GetMainViewport();dock.layout(mainViewport);dock.splitters();dock.layout(mainViewport);
            bool actionsVisible=dock.begin("Project actions",dock.bottom,dock.bottomHidden,"v","^");if(actionsVisible){
            auto nextAction=[&](const char* label){float next=ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2;float edge=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x;if(ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+next<=edge)ImGui::SameLine();};
            if(ImGui::Checkbox("Autosave",&project.autosave))savePreferences();nextAction("Save");ImGui::BeginDisabled(!editor.document.loaded());
            if(ImGui::Button("Save"))saveProject(false);nextAction("Save as new");if(ImGui::Button("Save as new"))saveProject(true);nextAction("Export preset");
            if(ImGui::Button("Export preset"))editor.exportAction(false,worldSeed);nextAction("Export datapack");if(ImGui::Button("Export datapack"))editor.exportAction(true,worldSeed);ImGui::EndDisabled();}dock.end(actionsVisible);

            const ImGuiViewport* vp=ImGui::GetMainViewport();
            bool changed=false;TerrainParams before=params;bool leftVisible=dock.begin("Surface controls",dock.left,dock.leftHidden,"<",">");leftCollapsed=dock.leftHidden;if(leftVisible){
            sectionBar("FTF version");ImGui::SetNextItemWidth(-1);
            if(ImGui::BeginCombo("##ftfVersion",ftfVersions[ftfVersionIndex].label)){for(int i=0;i<ftfVersionCount;++i){if(ImGui::Selectable(ftfVersions[i].label,i==ftfVersionIndex))ftfVersionIndex=i;if(i==ftfVersionIndex)ImGui::SetItemDefaultFocus();}ImGui::EndCombo();}
            ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("%s",ftfVersions[ftfVersionIndex].note);ImGui::PopStyleColor();ImGui::Spacing();
            sectionBar(params.meta.z==5?"Preset":params.meta.z==3||params.meta.z==6?"Preset (diagnostic view)":"Preset (development view)");
            editor.document.previewExtent=params.domain.x;if(editor.toolbar(worldSeed,true)){presetLoaded();if(params.meta.z==0||params.meta.z==1)params.meta.z=5;}
            ImGui::TextUnformatted("World seed");ImGui::SetNextItemWidth(std::max(100.0f,ImGui::GetContentRegionAvail().x-28-ImGui::GetStyle().ItemSpacing.x));ImGui::InputScalar("##worldseed",ImGuiDataType_S64,&worldSeed);ImGui::SameLine();ImGui::PushID("worldSeedRandomise");randomiseSeedButton(worldSeed,true);ImGui::PopID();
            if(editor.document.loaded()){
                if(params.meta.z==5)ImGui::TextWrapped("FTF surface: selected continent generator, terrain, oceans, islands, rivers and world filters. Minecraft surface biomes are selected from FTF climate and terrain parameters.");
                else if(params.meta.z==6)ImGui::TextWrapped("Continent map: edge and identity; Uplift additionally supplies a water table and size modifier. Layer choices show edge, water table or size. Property edits keep this diagnostic view selected.");
                else if(params.meta.z==4)ImGui::TextWrapped(options.providerOnly?"Land provider only: all regional terrain types, including volcanoes. Chain blending disabled for stage comparison.":"Land stage: regional terrain, volcanoes and mountain chains before continent/ocean placement. Select FTF surface to add world layout.");
                else if(params.meta.z==3)ImGui::TextWrapped("Diagnostic view: region identities and border weights. Use Generator to return to terrain. Property edits do not change the view mode.");
                else ImGui::TextWrapped("Single-family preview. Choose FTF land mix (partial) in Generator to combine the eight implemented families and preview their weights. Auto-selection below applies only to isolated families.");
                if(params.meta.z==2&&params.meta.w>1)ImGui::TextDisabled("FTF ignores horizontalScale in this graph.");
                if(params.meta.z==2&&(params.meta.w==1||params.meta.w==2||params.meta.w>=6))ImGui::TextDisabled("FTF ignores globalVerticalScale in this graph.");
                auto historyButton=[&](bool redo){
                    bool clicked=ImGui::Button(redo?"##redoPreset":"##undoPreset",{34,28});auto a=ImGui::GetItemRectMin();auto* list=ImGui::GetWindowDrawList();ImVec2 c{a.x+17,a.y+15};float direction=redo?-1.0f:1.0f;ImU32 ink=ImGui::GetColorU32(ImGuiCol_Text);
                    list->PathClear();for(int i=0;i<=20;++i){float angle=-2.45f+i*(4.1f/20);list->PathLineTo({c.x+direction*std::cos(angle)*7,c.y+std::sin(angle)*7});}list->PathStroke(ink,0,1.8f);
                    list->AddTriangleFilled({c.x-direction*9,c.y-7},{c.x-direction*1,c.y-7},{c.x-direction*7,c.y-1},ink);
                    if(ImGui::IsItemHovered())ImGui::SetTooltip(redo?"Redo preset edit [Ctrl+Shift+Z]":"Undo preset edit [Ctrl+Z]");return clicked;
                };
                if(historyButton(false))undoPreset(false);ImGui::SameLine();if(historyButton(true))undoPreset(true);
                editor.historyPanel();
                if(ImGui::Button("Reset preset to last saved state"))editor.resetToSaved("");
                auto oldMode=params.meta.z;int oldTerrain=editor.terrainSelection;bool oldHighlight=editor.highlightTerrain,oldIsolate=editor.isolateTerrain;editor.properties(params);if((oldTerrain!=editor.terrainSelection||oldHighlight!=editor.highlightTerrain||oldIsolate!=editor.isolateTerrain)&&(editor.highlightTerrain||editor.isolateTerrain)&&params.meta.z!=4&&params.meta.z!=5)params.meta.z=5;if(oldMode!=params.meta.z){layer=0;topDown=params.meta.z==3;}
            }
            ImGui::Spacing();ImGui::TextWrapped("%s",status.c_str());}dock.end(leftVisible);
            terrainHeightLimit=0;auto heightPath=PresetJson::json_pointer("/world/properties/worldHeight");if(editor.document.loaded()&&editor.document.data.contains(heightPath)&&editor.document.data[heightPath].is_number())terrainHeightLimit=editor.document.data[heightPath].get<float>();if(!std::isfinite(terrainHeightLimit)||terrainHeightLimit<=0)terrainHeightLimit=0;
            {float sea=63;auto seaPath=PresetJson::json_pointer("/world/properties/seaLevel");if(editor.document.loaded()&&editor.document.data.contains(seaPath)&&editor.document.data[seaPath].is_number())sea=editor.document.data[seaPath].get<float>();
             int available=std::max(1,int(terrainHeightLimit)-int(sea)),head=std::min(16,std::max(1,available/8)),band=std::min(112,std::max(1,available/2));terrainSqueezeStart=terrainHeightLimit-float(head+band);} // FTF TerrainCeiling compresses everything above this
            bool rightVisible=dock.begin("View and quality",dock.right,dock.rightHidden,">","<");rightCollapsed=dock.rightHidden;rightPanelSize={dock.rightWidth,dock.right.size.y};
            if(!keysBlocked&&ImGui::IsKeyPressed(ImGuiKey_H,false)){if(io.KeyShift)openToolSettingsRequested=true;}
            detailTool=false;if(detailPlaced&&!detailAuto)detailPlaced=false;   /* the manual High detail tool is gone; adaptive finer detail replaces it */
            if(!keysBlocked&&!io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_R,false))resetCamera();
            if(!keysBlocked&&ImGui::IsKeyPressed(ImGuiKey_Escape,false)&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)){detailTool=centerTool=spawnTool=measureTool=biomeTool=pointTool=copyTool=false;}
            if(!keysBlocked&&!io.KeyCtrl&&!topDown&&ImGui::IsKeyPressed(ImGuiKey_F,false)){if(flyMode)leaveFly();else enterFly();}
            if(!io.WantTextInput&&ImGui::IsKeyPressed(ImGuiKey_Escape,false)&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)){walkPickTool=false;if(walkMode)leaveWalk();else if(flyMode)leaveFly();} // Esc leaves walk mode and the fly camera
            if(!io.WantTextInput&&!io.KeyCtrl&&!topDown&&ImGui::IsKeyPressed(ImGuiKey_G,false)){if(walkMode)leaveWalk();else if(hover.valid)enterWalk({hover.position.x,hover.position.z});else{auto spawn=editor.document.loaded()?editor.document.spawn():glm::vec2(params.domain.y,params.domain.z);enterWalk(spawn);}}
            if(!keysBlocked&&!io.KeyCtrl&&!io.KeyShift&&ImGui::IsKeyPressed(ImGuiKey_T,false))treesEnabled=!treesEnabled;
            if(!keysBlocked&&!io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_B,false)){pointTool=!pointTool;if(pointTool){detailTool=centerTool=spawnTool=measureTool=false;disableBiomeTool();}}
            if(!keysBlocked&&!io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_N,false)){if(biomeTool)disableBiomeTool();else{biomeTool=true;detailTool=centerTool=spawnTool=measureTool=pointTool=false;}}
            if(!keysBlocked&&!io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_C,false)){centerTool=!centerTool;detailTool=spawnTool=measureTool=false;}
            if(!keysBlocked&&!io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_P,false)){spawnTool=!spawnTool;detailTool=centerTool=measureTool=false;}
            if(!keysBlocked&&!io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_M,false)){measureTool=!measureTool;detailTool=centerTool=spawnTool=false;}
            if(measureTool&&!keysBlocked){if(ImGui::IsKeyPressed(ImGuiKey_Backspace,false)&&!measurePoints.empty()){measurePoints.pop_back();updateMeasurement();}if(ImGui::IsKeyPressed(ImGuiKey_Enter,false))measureTool=false;}
            if(!keysBlocked&&!io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_K,false)){copyTool=!copyTool;if(copyTool){detailTool=centerTool=spawnTool=measureTool=pointTool=walkPickTool=false;disableBiomeTool();}}
            if(centerTool||spawnTool||measureTool||biomeTool||pointTool||walkPickTool)copyTool=false;
            if(detailTool||centerTool||spawnTool||measureTool||biomeTool)pointTool=false;if(detailTool||centerTool||spawnTool||measureTool||pointTool)biomeTool=false;
            if(!biomeTool)disableBiomeTool();
            if(rightVisible){
            int selectedTool=detailTool?1:centerTool?2:spawnTool?3:measureTool?4:biomeTool?5:pointTool?6:0;if(selectedTool!=previousTool){ImGui::SetScrollY(0);previousTool=selectedTool;}
            sectionBar("Selected tool");
            if(biomeTool){
                ImGui::TextUnformatted("Biome finder [N]");ImGui::InputText(labelAbove("Search name").c_str(),biomeSearchText,sizeof(biomeSearchText));
                if(ImGui::BeginCombo(labelAbove("Biome").c_str(),biomeLegend["biomes"][biomeChoice]["name"].get<std::string>().c_str())){
                    std::string needle=biomeSearchText;std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return char(std::tolower(c));});
                    for(size_t i=0;i<biomeLegend["biomes"].size();++i){if(!biomeLegend["biomes"][i].value("surface_available",true)||biomeLegend["biomes"][i].value("canonical_id",int(i))!=int(i))continue;auto name=biomeLegend["biomes"][i]["name"].get<std::string>();if(name.find(needle)!=std::string::npos&&ImGui::Selectable(name.c_str(),int(i)==biomeChoice)){biomeChoice=int(i);biomeHighlight=false;biomeResult.reset();}}
                    ImGui::EndCombo();
                }
                ImGui::Checkbox("Radius follows world size",&biomeUseWorldSize);if(biomeUseWorldSize)biomeRadius=params.domain.x;
                ImGui::BeginDisabled(biomeUseWorldSize);float requestedBiomeRadius=biomeRadius;if(numberControl("Search radius",requestedBiomeRadius,16,1000000,"%.0f blocks")&&std::isfinite(requestedBiomeRadius)&&requestedBiomeRadius>=16)biomeRadius=requestedBiomeRadius;ImGui::EndDisabled();
                int quality=biomeSearchGrid==512?0:biomeSearchGrid==1024?1:2;if(biomeSearchGrid<512)ImGui::Text("Test search grid: %d",biomeSearchGrid);else if(ImGui::Combo(labelAbove("Search samples").c_str(),&quality,"512\0" "1024\0" "2048\0"))biomeSearchGrid=512<<quality;
                ImGui::TextWrapped("Circle around preview center. Searches generated samples; small biomes may need more samples.");
                ImGui::BeginDisabled(biomeSearchWaiting||params.meta.z!=5);if(ImGui::Button("Find biome")){biomeSearchRequested=true;biomeResult.reset();biomeHighlight=false;biomeSearchStatus="Generating search area...";}ImGui::EndDisabled();
                ImGui::Checkbox("Highlight found biome",&biomeHighlight);ImGui::TextWrapped("%s",biomeSearchStatus.c_str());
                if(biomeResult&&ImGui::Button("View found location")){params.domain.y=biomeResult->extra.x;params.domain.z=biomeResult->extra.y;resetCamera();++revision;}
            }
            if(measureTool){ImGui::TextUnformatted("Measure [M]");ImGui::Text("%zu points",measurePoints.size());if(ImGui::Button("Clear measurement")){measurePoints.clear();updateMeasurement();}}
            else if(spawnTool)ImGui::TextWrapped("Set spawn [P]: click the terrain to set the preset spawn.");
            else if(centerTool)ImGui::TextWrapped("Center camera [C]: click the terrain to set the orbit center.");
            else if(biomeTool){}else ImGui::TextWrapped("Navigate [Esc]: orbit, pan and zoom. Select a tool to edit its settings here.");
            if(sectionIconHeader("Points (this project)",ImGuiTreeNodeFlags_DefaultOpen)){
                if(ImGui::Button(pointTool?"Stop pinning [B]":"Pin points [B]")){pointTool=!pointTool;if(pointTool){detailTool=centerTool=spawnTool=measureTool=false;disableBiomeTool();}}
                ImGui::SameLine();if(ImGui::Button("Pin view centre")){char name[40];std::snprintf(name,sizeof(name),"Point %zu",points.size()+1);static const uint32_t palette[]={0xffd24a,0x4ad2ff,0xff6b5e,0x7be07b,0xc58bff,0xff9f4a,0x4aa3ff,0xff7bc8};points.push_back({name,{lastCameraCenter.x+params.domain.y,lastCameraCenter.y/std::max(.001f,exaggeration),lastCameraCenter.z+params.domain.z},palette[points.size()%8]});highlightedPoint=int(points.size())-1;}
                ImGui::SameLine();ImGui::BeginDisabled(points.empty());if(ImGui::Button("Clear all"))ImGui::OpenPopup("Clear all points?");ImGui::EndDisabled();
                if(ImGui::BeginPopupModal("Clear all points?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::Text("Remove all %zu points of this project?",points.size());if(ImGui::Button("Remove all")){points.clear();renamingPoint=-1;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
                if(points.empty()){ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("No points yet. Press B, click the terrain to pin a place, then click its name here to centre the camera on it. Points are saved with the project.");ImGui::PopStyleColor();}
                int removePoint=-1;
                for(int i=0;i<int(points.size());++i){auto& w=points[i];ImGui::PushID(i);
                    ImGui::ColorButton("##color",ImVec4(((w.color>>16)&255)/255.0f,((w.color>>8)&255)/255.0f,(w.color&255)/255.0f,1),ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_NoPicker,ImVec2(14,14));ImGui::SameLine();
                    float actions=ImGui::CalcTextSize("Walk").x+ImGui::CalcTextSize("Rename").x+ImGui::CalcTextSize("X").x+ImGui::GetStyle().FramePadding.x*6+ImGui::GetStyle().ItemSpacing.x*3;
                    if(renamingPoint==i){ImGui::SetNextItemWidth(std::max(60.0f,ImGui::GetContentRegionAvail().x-actions));if(ImGui::IsWindowAppearing()||ImGui::IsAnyItemActive()==false)ImGui::SetKeyboardFocusHere();
                        if(ImGui::InputText("##rename",pointNameBuffer,sizeof(pointNameBuffer),ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll)){if(pointNameBuffer[0])w.name=pointNameBuffer;renamingPoint=-1;}
                        else if(ImGui::IsKeyPressed(ImGuiKey_Escape,false))renamingPoint=-1;}
                    else{if(ImGui::Selectable(w.name.c_str(),i==highlightedPoint,0,ImVec2(std::max(60.0f,ImGui::GetContentRegionAvail().x-actions),0))){highlightedPoint=i;centerOnPoint(w);}
                        if(ImGui::IsItemHovered())ImGui::SetTooltip("Click: centre the camera here\nX %.0f  Y %.0f  Z %.0f",w.p.x,w.p.y,w.p.z);}
                    ImGui::SameLine();if(ImGui::SmallButton("Walk"))enterWalk({w.p.x,w.p.z});if(ImGui::IsItemHovered())ImGui::SetTooltip("Drop in here and walk");
                    ImGui::SameLine();if(renamingPoint==i){if(ImGui::SmallButton("Done"))renamingPoint=-1;}else if(ImGui::SmallButton("Rename")){renamingPoint=i;std::snprintf(pointNameBuffer,sizeof(pointNameBuffer),"%s",w.name.c_str());}
                    ImGui::SameLine();if(ImGui::SmallButton("X"))removePoint=i;
                    ImGui::PopID();}
                if(removePoint>=0){points.erase(points.begin()+removePoint);renamingPoint=-1;highlightedPoint=-1;}
            }
            if(sectionIconHeader("Walk mode [G]",ImGuiTreeNodeFlags_DefaultOpen)){
                valueSlider("Walk speed",walkSpeedScale,.25f,4,"%.2fx");ImGui::Checkbox("Block grid (every block and every 16 blocks)",&walkGrid);
                ImGui::Checkbox("Finer detail in walk and fly modes",&finerInCameraModes);
                if(!walkMode){
                    if(ImGui::Button(walkPickTool?"Click the terrain... (Esc cancels)":"Pick where to drop in"))walkPickTool=!walkPickTool;
                    if(ImGui::Button("Drop in at world spawn"))enterWalk(editor.document.loaded()?editor.document.spawn():glm::vec2(params.domain.y,params.domain.z));
                    ImGui::SameLine();if(ImGui::Button("At view centre"))enterWalk({lastCameraCenter.x+params.domain.y,lastCameraCenter.z+params.domain.z});
                    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("Walk the terrain like Minecraft to feel its scale. Use the walk tool and click the terrain, hover the terrain and press G, or use Walk on a point. The terrain around you uses the adaptive finer detail of the six-band view.");ImGui::PopStyleColor();
                }else{
                    ImGui::Text("Position %.1f, %.1f, %.1f",walkPos.x,walkPos.y,walkPos.z);ImGui::Text("State: %s",walkFlying?"flying":walkInWater?"swimming":walkOnGround?"on ground":"airborne");
                    if(detailInitialized&&detailAuto)ImGui::Text("Terrain patch: %d x %d samples, %.2f blocks per sample",detailGrid,detailGrid,detailRenderRadius*2/float(std::max(2,detailGrid-1)));
                    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("WASD walk, Space jump (double-tap: fly, Space up, Shift down), Ctrl sprint, Shift sneak, mouse look. Esc leaves walk mode.");ImGui::PopStyleColor();
                    if(ImGui::Button("Exit walk mode [G]"))leaveWalk();
                }
            }
            biomeModMask=0;   /* shareable edition: FTF + vanilla Minecraft only, no biome-mod data is shipped */
            if(options.countBiomes>0)ImGui::SetNextItemOpen(true,ImGuiCond_Always);   // hidden count runs show the section (UI screenshot)
            if(sectionIconHeader("Biomes generated")){
                if(options.countBiomes>0)ImGui::SetScrollHereY(0);
                ImGui::TextWrapped("Which biomes appear in the preview world: %.0f x %.0f blocks around the preview centre (Preview world size).",params.domain.x,params.domain.x);
                int censusQuality=censusGrid==512?0:censusGrid==1024?1:2;if(censusGrid<512||censusGrid>2048)ImGui::Text("Test scan grid: %d",censusGrid);else if(ImGui::Combo(labelAbove("Scan samples").c_str(),&censusQuality,"512\0" "1024\0" "2048\0"))censusGrid=512<<censusQuality;
                ImGui::TextDisabled("One sample every %.0f blocks",params.domain.x/float(std::max(1,censusGrid-1)));
                ImGui::BeginDisabled(censusWaiting||censusRequested||biomeSearchWaiting||params.meta.z!=5);if(ImGui::Button("Count biomes")){censusRequested=true;census.reset();censusStatus="Generating the preview world...";}ImGui::EndDisabled();
                if(params.meta.z!=5)ImGui::TextDisabled("Needs the FTF surface generator");
                if(!censusStatus.empty())ImGui::TextWrapped("%s",censusStatus.c_str());
                if(census&&!censusWaiting){
                    if(censusFingerprint!=editor.document.data.dump()+std::to_string(worldSeed)+":"+std::to_string(biomeModMask)+":"+std::to_string(ftfVersionIndex)+":"+std::to_string(params.domain.x)+","+std::to_string(params.domain.y)+","+std::to_string(params.domain.z))ImGui::TextColored({1,.75f,.3f,1},"Settings or world size changed since this count; count again.");
                    ImGui::Text("%d / %d biomes generated",census->generated,census->possible);
                    for(auto& g:census->groups)if(g.possible>0)ImGui::BulletText("%s: %d / %d",g.pack.c_str(),g.generated,g.possible);
                    int missing=census->possible-census->generated;
                    if(missing==0)ImGui::TextColored({.45f,.85f,.45f,1},"Every possible biome generated");
                    else if((options.countBiomes>0?(ImGui::SetNextItemOpen(true,ImGuiCond_Always),true):true)&&ImGui::TreeNode("census_missing","Not generated (%d)",missing)){for(auto& g:census->groups){if(g.missing.empty())continue;ImGui::TextDisabled("%s",g.pack.c_str());for(auto& name:g.missing)ImGui::BulletText("%s",name.c_str());}ImGui::TreePop();}
                    if(!census->unreachable.empty()&&ImGui::TreeNode("census_unreachable","Cannot generate with these settings (%d)",int(census->unreachable.size()))){ImGui::TextWrapped("No climate table in use can select these, so no world size will show them.");for(auto& name:census->unreachable)ImGui::BulletText("%s",name.c_str());ImGui::TreePop();}
                    if(!census->unexpected.empty()){std::string list;for(auto& name:census->unexpected)list+=(list.empty()?"":", ")+name;ImGui::TextWrapped("Also found outside the tables: %s",list.c_str());}
                }
                ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("Biome selection follows Minecraft's climate tree on FTF's climate. Very small biomes can fall between samples: use more samples to catch them.");
                ImGui::PopStyleColor();
            }
            sectionBar("Generator");
            int generator=params.meta.z>=2?static_cast<int>(params.meta.z)-1:0;
            ImGui::SetNextItemWidth(-1);
            if(ImGui::Combo("##Generator",&generator,"Validation landscape\0FTF terrain - isolated\0FTF region map - diagnostic\0FTF land mix (partial)\0FTF surface (all continents)\0FTF continent map - diagnostic\0")){params.meta.z=generator==0?0:generator+1;layer=0;topDown=params.meta.z==3||params.meta.z==6;if(params.meta.z==3)params.detail.x=2098;if(params.meta.z>=4&&!editor.document.loaded()){params.meta.z=2;status="Import an FTF preset to use land/surface views.";}changed=true;}
            if(params.meta.z==2){int profile=static_cast<int>(params.meta.w);if(ImGui::Combo(labelAbove("Terrain family").c_str(),&profile,"Plains\0Steppe\0Dales\0Hills 1\0Hills 2\0Plateau\0Badlands\0Torridonian\0")){params.meta.w=profile;changed=true;}}
            if((params.meta.z==5||params.meta.z==6)&&editor.document.data.value("world",PresetJson::object()).value("continent",PresetJson::object()).value("continentType",std::string())=="SINGLE"){
                ImGui::TextWrapped("Single continent surrounds the world origin.");if(ImGui::Button("View near world origin")){params.domain.y=params.domain.z=0;changed=true;}
            }
            if(sectionIconHeader("Resolution and performance",ImGuiTreeNodeFlags_DefaultOpen)){
                int index=0;while((64u<<index)<params.meta.x&&index<7)++index;
                const char* grids[]={"64 x 64","128 x 128","256 x 256","512 x 512","1024 x 1024","2048 x 2048","4096 x 4096","8192 x 8192 (warning: 4.25 GiB)"};
                if(index==7)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.3f,.3f,1));bool gridOpen=ImGui::BeginCombo(labelAbove("Grid").c_str(),grids[index]);if(index==7)ImGui::PopStyleColor();
                if(gridOpen){for(int choice=0;choice<8;++choice){if(choice==7)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.3f,.3f,1));if(ImGui::Selectable(grids[choice],choice==index)){index=choice;params.meta.x=64u<<index;changed=true;}if(choice==7){ImGui::PopStyleColor();if(ImGui::IsItemHovered())ImGui::SetTooltip("8K: 4.25 GiB of terrain fields plus GPU overhead. Generation can pause the view; realtime updates are not guaranteed.");}if(choice==index)ImGui::SetItemDefaultFocus();}ImGui::EndCombo();}
                if(params.meta.x==8192){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.3f,.3f,1));ImGui::TextWrapped("Warning: 8K uses 4.25 GiB of terrain storage plus GPU overhead. Updates may be slow or fail if GPU memory is low.");ImGui::PopStyleColor();}
                ImGui::TextDisabled("Final terrain fields: %.0f MiB",Surface::estimatedFieldBytes(params.meta.x)/1048576.0);ImGui::TextDisabled("Stage reuse cache: %.0f MiB",(surface.stageCacheBytes()+editPreview.worker.stageCacheBytes())/1048576.0);
                if(params.meta.x>1024)ImGui::TextWrapped("High-resolution grids trade update rate for detail.");
                changed|=numberControl("Preview world size",params.domain.x,256,65536,"%.0f blocks");
                ImGui::TextDisabled("%.1f blocks per sample",params.domain.x/(params.meta.x-1));
                ImGui::Checkbox("Automatic resolution",&automatic);ImGui::Checkbox("Metrics",&showMetrics);
                if(automatic)ImGui::TextDisabled("Automatic quality uses 64 through 2048.");
                ImGui::TextDisabled("Automatic resolution target: 12 FPS");
                ImGui::Checkbox("Responsive editing preview",&editPreview.enabled);if(ImGui::IsItemHovered())ImGui::SetTooltip("Drag at up to 12 preview updates/sec, then refine in small GPU jobs. The selected Grid is your saved/final quality.");
                if(editPreview.enabled){int c=editPreview.cap>=1024?2:editPreview.cap==512?1:0;if(ImGui::Combo(labelAbove("Editing quality").c_str(),&c,"256 x 256 (faster)\0" "512 x 512\0" "1024 x 1024 (sharper)\0"))editPreview.cap=c==2?1024:c==1?512:256;valueSlider("Refinement budget per frame",editPreview.budgetMs,2,16,"%.0f ms");ImGui::TextWrapped("%s",editPreview.status().c_str());}
            }
            if(terrainHeightLimit>0&&(params.meta.z==2||params.meta.z==4||params.meta.z==5)&&sectionIconHeader("Height limit",ImGuiTreeNodeFlags_DefaultOpen)){
                ImGui::Checkbox("Highlight height overflow",&showHeightLimit);
                ImGui::TextWrapped("FTF preset height limit: Y %.0f. FTF never lets terrain pass it: everything above Y %.0f is squeezed into the last blocks below the limit (TerrainCeiling).",terrainHeightLimit,terrainSqueezeStart);
                if(showHeightLimit)ImGui::TextColored({1,.3f,.3f,1},"Red hatching: terrain squeezed by the ceiling (above Y %.0f)",terrainSqueezeStart);
            }
            if((params.meta.z==2||params.meta.z==4||params.meta.z==5)&&sectionIconHeader("Heights")){
                ImGui::Checkbox("Highlight terrain above Y",&showAboveHeight);valueSlider("##aboveHeight",aboveHeight,-64,4096,"%.0f");
                if(showAboveHeight)ImGui::TextColored({1,.8f,.25f,1},"Amber hatching: terrain above Y %.0f",aboveHeight);
                if(ImGui::Button("Find highest point")){auto t0=std::chrono::steady_clock::now();uint32_t G=surface.resolution;
                    if(auto top=findHighest()){size_t best=top->first;float hx=(float(best%G)/float(G-1)-.5f)*params.domain.x+params.domain.y,hz=(float(best/G)/float(G-1)-.5f)*params.domain.x+params.domain.z,hy=top->second;
                        std::erase_if(points,[](const Waypoint& w){return w.name=="Highest point"&&w.color==0xff5e5eu;});renamingPoint=highlightedPoint=-1;   /* one marker: a new search replaces the old one */
                        points.push_back({"Highest point",{hx,hy,hz},0xff5e5eu});centerOnPoint(points.back());
                        char text[160];std::snprintf(text,sizeof(text),"Highest: Y %.1f at X %.0f, Z %.0f (%u x %u grid, %.0f blocks per sample; %.0f ms)",hy,hx,hz,G,G,params.domain.x/float(G-1),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count());highestStatus=text;}}
                if(!highestStatus.empty())ImGui::TextWrapped("%s",highestStatus.c_str());
            }
            if(sectionIconHeader("View and scene",ImGuiTreeNodeFlags_DefaultOpen)){
                ImGui::Checkbox("Top down",&topDown);ImGui::Checkbox("Lighting",&lighting);
                valueSlider("Vertical field of view (orbit, fly and walk)",cameraFov,20,120,"%.0f\xC2\xB0");
                ImGui::SeparatorText("Culling");ImGui::Checkbox("Terrain frustum culling",&frustumCulling);ImGui::Checkbox("Terrain distance culling",&distanceCulling);if(distanceCulling)numberControl("Maximum draw distance",cullDistance,100,1000000,"%.0f blocks");ImGui::Checkbox("Terrain backface culling",&backfaceCulling);ImGui::TextWrapped("Culling changes rendering only. Trees use the same frustum and distance culling as the terrain. Distance culling can hide distant terrain.");
                ImGui::Checkbox("Terrain ambient occlusion",&ambientOcclusion);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Heightfield horizon occlusion: adds depth to valleys and crevices. Display only.");
                ImGui::BeginDisabled(!ambientOcclusion);valueSlider("Ambient occlusion intensity",aoIntensity,0,2);ImGui::EndDisabled();
                ImGui::BeginDisabled(atmo.clouds);bool shownShadows=cloudShadows&&!atmo.clouds;if(ImGui::Checkbox("Cloud shadows",&shownShadows))cloudShadows=shownShadows;ImGui::EndDisabled();if(atmo.clouds&&cloudShadows)ImGui::TextDisabled("(returns when clouds are off)");
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Soft procedural cloud shadows drift over the terrain. Display only.");
                ImGui::BeginDisabled(!cloudShadows);
                valueSlider("Cloud shadow intensity",shadowIntensity,0,1);
                valueSlider("Cloud shadow speed",shadowSpeed,0,4,"%.2fx");ImGui::EndDisabled();
                ImGui::SeparatorText("Atmosphere (display only)");
                ImGui::Checkbox("Realistic water",&atmo.water);if(ImGui::IsItemHovered())ImGui::SetTooltip("Animated ripples, sky reflection with Fresnel, sun glint, depth absorption and shore foam on the sea. Display only; off by default.");
                if(atmo.water){valueSlider("Ripple strength",atmo.ripple,0,2);valueSlider("Reflection strength",atmo.reflection,0,1.5f);}
                ImGui::Checkbox("Volumetric fog",&atmo.fog);if(ImGui::IsItemHovered())ImGui::SetTooltip("Height fog thickest in valleys and over the sea, tinted by the sky and sun. Display only; off by default.");
                if(atmo.fog){valueSlider("Fog density",atmo.fogDensity,0,4);valueSlider("Fog height falloff",atmo.fogFalloff,0,4);valueSlider("Fog opacity",atmo.fogOpacity,0,1);valueSlider("Fog base height",atmo.fogHeight,-100,600,"%.0f blocks");}
                ImGui::Checkbox("Clouds",&atmo.clouds);if(ImGui::IsItemHovered())ImGui::SetTooltip("A flat layer of drifting 2D clouds at the cloud height, seen from below or above. Display only; off by default.");
                if(atmo.clouds){valueSlider("Cloud opacity",atmo.cloudOpacity,0,1);valueSlider("Cloud coverage",atmo.cloudCoverage,0,1);valueSlider("Cloud density",atmo.cloudDensity,0,3);valueSlider("Cloud height",atmo.cloudBase,0,2000,"%.0f blocks");valueSlider("Cloud size",atmo.cloudScale,200,6000,"%.0f blocks");valueSlider("Wind speed",atmo.cloudSpeed,0,100,"%.0f blocks/s");}
                ImGui::SeparatorText("Sun lighting");
                valueSlider("Sun azimuth",sunAzimuth,0,360,"%.0f\xC2\xB0");valueSlider("Sun elevation",sunElevation,2,89,"%.0f\xC2\xB0");valueSlider("Sun intensity",sunIntensity,0,3);
                ImGui::Checkbox("Terrain casts sun shadows",&sunShadows);ImGui::BeginDisabled(!sunShadows);
                valueSlider("Sun shadow intensity",sunShadowStrength,0,1);valueSlider("Sun shadow softness",sunShadowSoftness,0,1);valueSlider("Sun shadow reach",sunShadowRange,64,5000,"%.0f blocks");ImGui::EndDisabled();
                ImGui::BeginDisabled(params.meta.z==3||params.meta.z==6||params.meta.z==1);ImGui::Checkbox("Show ocean floor",&showOceanFloor);ImGui::EndDisabled();
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Draw seabed heights instead of flattening underwater geometry to sea level. This is a display option; the FTF preset and generated heights stay unchanged.");
                numberControl("Display vertical scale",exaggeration,.2f,3,"%.2fx");
                changed|=numberControl("Origin X",params.domain.y,-10000,10000,"%.9g");changed|=numberControl("Origin Z",params.domain.z,-10000,10000,"%.9g");
                if(ImGui::Button("Reset camera [R]"))resetCamera();if(ImGui::IsItemHovered())ImGui::SetTooltip("Back to the default orbit view");
                ImGui::BeginDisabled(editor.document.loaded());
                if(ImGui::Button("Save scene")){try{saveSettings(params,worldSeed);status="Saved to state/scene.json";}catch(const std::exception& e){status=e.what();}}
                ImGui::SameLine();if(ImGui::Button("Load scene")){try{params=loadSettings(worldSeed);changed=true;status="Scene loaded";}catch(const std::exception& e){status=e.what();}}
                ImGui::EndDisabled();
            }
            }
            dock.end(rightVisible);
            if(changed&&std::memcmp(&before,&params,sizeof(params))!=0){if(!editing){if(undo.size()>100)undo.erase(undo.begin());undo.push_back(before);}editing=ImGui::IsAnyItemActive();redo.clear();}
            if(!ImGui::IsAnyItemActive())editing=false;
            if(options.exercise){
                constexpr uint32_t grids[]={64,256,1024,128,512};params.meta.x=grids[(frameNumber/60)%5];
                if(frameNumber==120)glfwSetWindowSize(window,1280,800);
                if(frameNumber==240)glfwSetWindowSize(window,1440,900);
            }
            if(options.profileExercise){auto variant=(frameNumber/30)%(editor.document.loaded()?12:9);params.meta.z=variant>=8?variant-5:2;params.meta.w=variant>=8?0:variant;if(variant==8)params.detail.x=2098;layer=0;}
            if(options.refineCancelTest&&!refineCancelExercised&&editPreview.job&&editPreview.work.params.meta.x>editPreview.cap&&editPreview.progress()>.4){editor.document.set("/terrain/steppe/weight",7.0);refineCancelExercised=true;}
            if(options.maskExercise&&frameNumber==30)biomeModMask=7;
            if(!options.openProject.empty()&&frameNumber==2){restoreView(project.load(options.openProject,editor.document,worldSeed,true));if(options.testHighlight>=0){editor.terrainSelection=options.testHighlight;editor.highlightTerrain=true;}}   /* test flag: open a saved project (e.g. a crash report's last session) */
            if(!options.sliderEdit.empty()&&frameNumber>=options.sliderStartFrame&&frameNumber<=options.sliderEndFrame){auto equals=options.sliderEdit.find('=');if(equals==std::string::npos)throw std::runtime_error("Slider exercise requires /path=end_value");auto path=PresetJson::json_pointer(options.sliderEdit.substr(0,equals));if(frameNumber==options.sliderStartFrame)sliderOriginal=editor.document.data.at(path).get<double>();double end=std::stod(options.sliderEdit.substr(equals+1)),t=double(frameNumber-options.sliderStartFrame)/std::max(1,options.sliderEndFrame-options.sliderStartFrame);editor.document.data[path]=std::lerp(sliderOriginal,end,t);}
            if(options.weightExercise&&frameNumber==30){auto equals=options.exerciseEdit.find('=');editor.document.set(options.exerciseEdit.substr(0,equals),PresetJson::parse(options.exerciseEdit.substr(equals+1)));}
            if(automatic&&frameNumber>80&&frameNumber-lastQualityChange>120&&!options.benchmark){
                double budget=1000.0/targetHz;
                if(lastTiming.total>budget*.75&&params.meta.x>64){params.meta.x/=2;goodFrames=0;lastQualityChange=frameNumber;}
                else if(lastTiming.total<budget*.3&&smoothedFrame<budget*1.15){if(++goodFrames>360&&params.meta.x<2048){params.meta.x*=2;goodFrames=0;lastQualityChange=frameNumber;}}
                else goodFrames=0;
            }
            if(options.animateUntil>0&&frameNumber==options.animateUntil)animate=false;
            if(animate!=wasAnimating){if(animate)animateBackup=params;else if(!editor.document.loaded())params=animateBackup;wasAnimating=animate;++revision;}
            if(animate)animatePhase+=dt*animateSpeed;
            PresetJson animatedData;bool swappedAnimated=false;
            if(animate&&editor.document.loaded()){animatedData=animatedPreset(editor.document.data,animatePhase);std::swap(editor.document.data,animatedData);swappedAnimated=true;}
            uint64_t graphRoot=static_cast<uint64_t>(worldSeed);
            bool previewValid=true;
            if(editor.document.loaded()&&params.meta.z>=2){
                auto spawn=editor.document.spawn();if(spawn!=previousPresetSpawn){previousPresetSpawn=spawn;params.domain.y=spawn.x;params.domain.z=spawn.y;hasSpawnMarker=false;resetCamera();}
                try{editor.document.apply(params);if(params.meta.z==2)graphRoot=editor.document.terrainSeedRoot(worldSeed,params.meta.w);}
                catch(const std::exception& error){status=error.what();editor.feedback=status;editor.feedbackLive=false;params=previous;previewValid=false;}
            }
            if(previewValid&&(!std::isfinite(params.domain.x)||params.domain.x<=0||params.land.y<=0||params.climate.z<=0||(params.meta.z==3&&params.detail.x<1))){
                status="Preview paused: view extent and noise scales must be positive.";params=previous;previewValid=false;
            }
            if(previewValid){
                // Transient stress-test changes follow preset mapping; the preset document stays editable/exportable.
                if(animate&&!editor.document.loaded()){if(params.meta.z>=3)params.domain.y+=2.0f;else if(params.meta.z==2&&(params.meta.w==1||params.meta.w==2||params.meta.w>=6))params.ftfScales.z=1+std::sin(static_cast<float>(elapsed)*1.7f)*.25f;else params.land.x=800+std::sin(static_cast<float>(elapsed)*1.7f)*220;params.climate.x=std::sin(static_cast<float>(elapsed)*.9f)*.25f;}
                try{params.meta.y=params.meta.z==1?static_cast<uint32_t>(worldSeed):ftfSeed(static_cast<uint64_t>(worldSeed));updateGraphInputs(params,graphSeeds,graphRoot);if(params.meta.z==4||params.meta.z==5)buildLandMix(editor.document.data,worldSeed,graphSeeds,!options.providerOnly);if(params.meta.z==5||params.meta.z==6)buildUpliftInputs(editor.document.data,worldSeed,graphSeeds,!options.noIslands);if(params.meta.z==5)buildClimateInputs(editor.document.data,worldSeed,graphSeeds,!options.noClimate);else buildClimateInputs(editor.document.data,worldSeed,graphSeeds,false);graphSeeds.biomeSeeds2.w=graphSeeds.climateMeta.x?static_cast<uint32_t>(biomeColors):0u;if(options.testBiome>=0){if(options.testBiome>=static_cast<int>(biomeLegend["biomes"].size()))throw std::runtime_error("Invalid test biome ID");graphSeeds.biomeOptions.z=static_cast<float>(options.testBiome+1);}buildWorldFilterInputs(editor.document.data,worldSeed,graphSeeds,params.meta.z==5&&!options.noWorldFilters,ftfVersions[ftfVersionIndex].profile);buildSurfaceInputs(editor.document.data,worldSeed,graphSeeds,params.meta.z==5&&graphSeeds.climateMeta.x!=0u);graphSeeds.surfaceMeta.w=(showOceanFloor?1u:0u)|(biomeModMask<<1);graphSeeds.surfaceOptions.w=options.materialDebug>=0?2.0f+float(options.materialDebug):options.testMaterialIDs?1.0f:0.0f;if(params.meta.z==5)riverNetworks.update(editor.document.data,worldSeed,graphSeeds,params,!options.noRivers);}
                catch(const std::exception& error){if(options.hidden)throw;status=error.what();editor.feedback=status;editor.feedbackLive=false;params=previous;graphSeeds=previousSeeds;previewValid=false;}
            }
            if(swappedAnimated)std::swap(editor.document.data,animatedData);
            if(std::memcmp(&params,&previous,sizeof(params))!=0||std::memcmp(&graphSeeds,&previousSeeds,sizeof(graphSeeds))!=0){++revision;previous=params;previousSeeds=graphSeeds;}
            if(graphicsPersistent&&savedGraphics!=graphicsState().dump())try{ProjectSession::atomicJson(graphicsFile,graphicsState());savedGraphics=graphicsState().dump();migrateLegacyGraphics=false;}catch(const std::exception& e){editor.status=e.what();}
            if(riverNetworks.buffer!=previousRiverWords){previousRiverWords=riverNetworks.buffer;++revision;}
            requestedResolution=params.meta.x;
            editPreview.worker.selectiveGeneration=surface.selectiveGeneration;
            editPreview.plan(params,graphSeeds,riverNetworks.buffer,revision,elapsed,ImGui::IsAnyItemActive()||animate||(!options.sliderEdit.empty()&&frameNumber>=options.sliderStartFrame&&frameNumber<=options.sliderEndFrame));
            RenderSnapshotScope renderSnapshot(params,graphSeeds,revision,editPreview.display);
            dock.layout(vp);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});bool viewportVisible=dock.begin("Terrain viewport",dock.viewport,dock.viewportHidden,"v","^",ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);viewportCollapsed=dock.viewportHidden;
            observedPanelHide|=dock.leftHidden||dock.rightHidden||dock.bottomHidden||dock.viewportHidden;
            if(persistentSession&&savedDockLayout!=dock.snapshot().dump()){try{auto j=dock.snapshot();ProjectSession::atomicJson(dockLayoutFile,PresetJson::parse(j.dump()));savedDockLayout=j.dump();}catch(const std::exception& e){editor.status=e.what();}}
            if(viewportCollapsed){renderSnapshot.restore();dock.end(viewportVisible);ImGui::PopStyleVar();ImGui::Render();gpu.end(ImGui::GetDrawData());
                if(persistentSession&&elapsed-sessionTick>=1){try{project.tick(editor.document,worldSeed,captureView(),elapsed);sessionTick=elapsed;}catch(const std::exception& e){editor.status=e.what();}}
                ++frameNumber;if(!options.hidden&&!options.benchmark)std::this_thread::sleep_until(start+std::chrono::microseconds(1000000/(fpsLimit>0?fpsLimit:60)));continue;
            }
            auto size=ImGui::GetContentRegionAvail();size.x=std::max(32.0f,size.x);size.y=std::max(32.0f,size.y);uint32_t w=std::max(1u,static_cast<uint32_t>(size.x)),h=std::max(1u,static_cast<uint32_t>(size.y));
            uint32_t previousGrid=surface.resolution;
            try{surface.ensure(params.meta.x,w,h);}catch(const std::exception& error){
                if(options.hidden||!previousGrid)throw;status=error.what();params.meta.x=previousGrid;
                surface.ensure(previousGrid,w,h);++revision;previous=params;
            }
            uint32_t fi=gpu.swap.FrameIndex;
            auto corner=ImGui::GetCursorScreenPos();ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(surface.frames[fi].texture)),size);
            bool viewportHovered=ImGui::IsItemHovered();const bool flyActive=flyMode&&!topDown,walkActive=walkMode&&!topDown;
            auto afterImage=ImGui::GetCursorScreenPos();
            bool displayPopupActive=false;const float toolbarHeight=39.0f*8+12;
            ImGui::SetCursorScreenPos({corner.x+size.x-126,corner.y+14});ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{6,6});
            ImGui::BeginChild("Viewport display controls",{112,46},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar);
            auto viewButton=[&](const char* id,bool palette){bool clicked=ImGui::Button(id,{46,32});auto a=ImGui::GetItemRectMin();auto* draw=ImGui::GetWindowDrawList();auto ink=ImGui::GetColorU32(ImGuiCol_Text);
                if(palette){draw->AddCircleFilled({a.x+15,a.y+13},4,IM_COL32(230,136,92,255));draw->AddCircleFilled({a.x+22,a.y+13},4,IM_COL32(107,191,151,255));draw->AddCircleFilled({a.x+18,a.y+20},4,IM_COL32(109,157,220,255));}
                else for(int i=0;i<3;++i){float y=a.y+10+i*5;draw->AddQuad({a.x+8,y},{a.x+18,y-4},{a.x+28,y},{a.x+18,y+4},ink,1.5f);}
                draw->AddTriangleFilled({a.x+33,a.y+13},{a.x+41,a.y+13},{a.x+37,a.y+18},ink);return clicked;};
            if(viewButton("##layerMenu",false)||(!keysBlocked&&!io.KeyCtrl&&!io.KeyShift&&ImGui::IsKeyPressed(ImGuiKey_Z,false)))ImGui::OpenPopup("View layer");
            if(ImGui::IsItemHovered())ImGui::SetTooltip("View layer [Z]\nSurface, height, temperature, moisture or terrain regions");ImGui::SameLine();
            if(viewButton("##paletteMenu",true)||(!keysBlocked&&!io.KeyCtrl&&io.KeyShift&&ImGui::IsKeyPressed(ImGuiKey_Z,false)))ImGui::OpenPopup("Biome colours");
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Biome and block colours [Shift+Z]\nMinecraft blocks, FTF map, grass or foliage tint");
            if(options.previewViewMenu&&frameNumber==2)ImGui::OpenPopup("Biome colours");
            ImGui::SetNextWindowPos({corner.x+size.x-298,corner.y+64},ImGuiCond_Appearing);ImGui::SetNextWindowSize({284,0},ImGuiCond_Always);
            if(ImGui::BeginPopup("View layer")){displayPopupActive=true;ImGui::TextUnformatted("View layer [Z]");ImGui::Separator();
                std::vector<const char*> names=params.meta.z==6?std::vector<const char*>{"Continent layout","Continent edge","Water table","Continent size"}:params.meta.z==3?std::vector<const char*>{"Region identities and borders","Edge strength"}:std::vector<const char*>{"Surface colours","Height","Temperature","Moisture","Terrain regions"};
                for(int i=0;i<int(names.size());++i)if(ImGui::Selectable(names[i],layer==i))layer=i;
                if(sectionIconHeader("Highlight and isolate terrain")){
                    const int ids[]={0,1,2,3,5,6,7,9,12,104,105,106,107,108,109,110,111,112};const char* labels[]={"Plains","Steppe","Dales","Hills","Plateau","Badlands","Torridonian","Mountains","Volcano","Deep ocean","Shallow ocean","Coast","Island","Beach","Island mountain","River","Wetland","Lake"};const char* current="Choose terrain";for(int i=0;i<18;++i)if(ids[i]==editor.terrainSelection)current=labels[i];
                    if(ImGui::BeginCombo(labelAbove("Terrain type").c_str(),current)){for(int i=0;i<18;++i){ImGui::PushID(i);terrainTypeIcon(ids[i]);ImGui::SameLine();if(ImGui::Selectable(labels[i],editor.terrainSelection==ids[i]))editor.terrainSelection=ids[i];ImGui::PopID();}ImGui::EndCombo();}
                    ImGui::BeginDisabled((params.meta.z!=4&&params.meta.z!=5)||editor.terrainSelection<0);ImGui::Checkbox("Highlight selected regions",&editor.highlightTerrain);ImGui::Checkbox("Isolate selected terrain",&editor.isolateTerrain);ImGui::EndDisabled();if(ImGui::Button("Clear terrain view filter")){editor.highlightTerrain=editor.isolateTerrain=false;editor.terrainSelection=-1;}ImGui::TextDisabled("View only; never saved to the preset.");
                }
                ImGui::EndPopup();}
            ImGui::SetNextWindowPos({corner.x+size.x-330,corner.y+64},ImGuiCond_Appearing);ImGui::SetNextWindowSize({316,0},ImGuiCond_Always);
            if(ImGui::BeginPopup("Biome colours")){displayPopupActive=true;ImGui::TextUnformatted("Biome and block colours [Shift+Z]");ImGui::Separator();const char* names[]={"FTF biome map","Biome grass and water tint","Biome foliage and water tint","Minecraft surface blocks"};
                ImGui::BeginDisabled(params.meta.z!=5);for(int i=0;i<4;++i)if(ImGui::Selectable(names[i],biomeColors==i))biomeColors=i;ImGui::EndDisabled();if(layer!=0)ImGui::TextDisabled("Use the Surface colours layer for this palette.");if(params.meta.z==5)paletteLegends();ImGui::EndPopup();}
            ImGui::EndChild();
            // Trees: a view toggle with its own settings dropdown, left of the LOD view.
            const bool narrowView=size.x<900;ImGui::SetCursorScreenPos(narrowView?ImVec2{corner.x+size.x-14-94-104,corner.y+158}:ImVec2{corner.x+size.x-228-104,corner.y+14});
            ImGui::BeginChild("Viewport tree view",{94,46},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar);{
                if(treesEnabled)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.12f,.48f,.40f,1));bool treeClicked=ImGui::Button("##treeView",{46,32});if(treesEnabled)ImGui::PopStyleColor();
                auto a=ImGui::GetItemRectMin();auto* draw=ImGui::GetWindowDrawList();auto ink=ImGui::GetColorU32(ImGuiCol_Text);ImVec2 c{a.x+23,a.y+16};
                draw->AddTriangle({c.x,c.y-12},{c.x-7,c.y-2},{c.x+7,c.y-2},ink,1.5f);draw->AddTriangle({c.x,c.y-6},{c.x-10,c.y+6},{c.x+10,c.y+6},ink,1.5f);draw->AddRectFilled({c.x-1.5f,c.y+6},{c.x+1.5f,c.y+12},ink);
                if(treeClicked)treesEnabled=!treesEnabled;
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Trees [T]\nWhole-map canopies or nearby blocky trees; configurable in the dropdown");ImGui::SameLine();
                if(ImGui::Button("##treeMenu",{22,32}))ImGui::OpenPopup("Tree settings");
                auto b=ImGui::GetItemRectMin();draw->AddTriangleFilled({b.x+6,b.y+13},{b.x+16,b.y+13},{b.x+11,b.y+19},ink);if(ImGui::IsItemHovered())ImGui::SetTooltip("Tree settings");
            }
            ImGui::SetNextWindowPos({corner.x+size.x-14-330,corner.y+64},ImGuiCond_Appearing);ImGui::SetNextWindowSize({330,0},ImGuiCond_Always);
            if(ImGui::BeginPopup("Tree settings")){displayPopupActive=true;ImGui::TextUnformatted("Trees [T]");ImGui::Separator();
                ImGui::Checkbox("Show trees",&treesEnabled);ImGui::Checkbox("Entire-map overview trees",&wholeMapTrees);
                ImGui::Checkbox("Hide while the camera moves",&treesHideMoving);ImGui::Checkbox("Hide while I adjust settings",&treesPause);ImGui::BeginDisabled(!treesPause);valueSlider("Trees come back after",treePauseDelay,.1f,3.0f,"%.1f s");ImGui::EndDisabled();
                valueSlider("Tree density (vanilla = 1)",treeDensity,.1f,3.0f,"%.2fx");
                ImGui::BeginDisabled(wholeMapTrees);valueSlider("Tree draw distance",treeRadius,150.0f,3000.0f,"%.0f blocks",ImGuiSliderFlags_Logarithmic);
                valueSlider("Detailed trees within",treeLod,40.0f,400.0f,"%.0f blocks");ImGui::EndDisabled();
                ImGui::SeparatorText("Tree kinds");
                for(uint32_t t=0;t<treeTypeCount;++t){bool on=(treeMask>>t)&1u;if(ImGui::Checkbox(treeTypeName(t),&on))treeMask=on?(treeMask|(1u<<t)):(treeMask&~(1u<<t));}
                if(ImGui::SmallButton("All"))treeMask=0xfff;ImGui::SameLine();if(ImGui::SmallButton("None"))treeMask=0;
                ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped(!surface.treesAvailable()?"Tree data is missing from this install.":params.meta.z!=5?"Trees need the FTF surface generator.":treesTooFar?"Enable entire-map overview trees to see vegetation across the map.":treesPausedNow?"Hidden while settings change; they return in a moment.":treesEnabled?(wholeMapTrees?"World-scale block-shaped trees; distant trees use compact voxel silhouettes.":"Nearby blocky trees use vanilla 1.21.1 vegetation features."):"Trees are off.");
                ImGui::PopStyleColor();ImGui::EndPopup();}
            ImGui::EndChild();
            // Six-band camera LOD is a separate view of its own (not a tool): toggle icon plus a settings dropdown beside it.
            ImGui::SetCursorScreenPos(narrowView?ImVec2{corner.x+size.x-14-94,corner.y+158}:ImVec2{corner.x+size.x-228,corner.y+14});
            ImGui::BeginChild("Viewport LOD view",{94,46},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar);{
                if(distanceLod)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.12f,.48f,.40f,1));bool lodClicked=ImGui::Button("##lodView",{46,32});if(distanceLod)ImGui::PopStyleColor();
                auto a=ImGui::GetItemRectMin();auto* draw=ImGui::GetWindowDrawList();auto ink=ImGui::GetColorU32(ImGuiCol_Text);ImVec2 c{a.x+23,a.y+16};
                for(int i=0;i<3;++i){float r=3.0f+i*3.4f;draw->AddRect({c.x-r,c.y-r},{c.x+r,c.y+r},ink,0,0,1.4f);}draw->AddLine({c.x-12,c.y},{c.x+12,c.y},ink,1.2f);draw->AddLine({c.x,c.y-12},{c.x,c.y+12},ink,1.2f);
                if(lodClicked||(!keysBlocked&&!io.KeyCtrl&&!io.KeyShift&&ImGui::IsKeyPressed(ImGuiKey_L,false)))distanceLod=!distanceLod;
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Six-band camera LOD view [L]\nNear terrain at full detail, farther bands use fewer triangles");ImGui::SameLine();
                if(ImGui::Button("##lodMenu",{22,32})||(options.previewLodMenu&&frameNumber==2))ImGui::OpenPopup("LOD view settings");
                auto b=ImGui::GetItemRectMin();draw->AddTriangleFilled({b.x+6,b.y+13},{b.x+16,b.y+13},{b.x+11,b.y+19},ink);if(ImGui::IsItemHovered())ImGui::SetTooltip("Camera LOD settings");
            }
            ImGui::SetNextWindowPos({corner.x+size.x-14-400,corner.y+64},ImGuiCond_Appearing);ImGui::SetNextWindowSize({400,0},ImGuiCond_Always);
            if(ImGui::BeginPopup("LOD view settings")){displayPopupActive=true;ImGui::TextUnformatted("Six-band camera LOD [L]");ImGui::Separator();
                ImGui::Checkbox("Enable six distance bands",&distanceLod);ImGui::BeginDisabled(!distanceLod);
                valueSliderInt("Bands",lodBandCount,1,8);
                {static const char* details="1x (full)\0" "2x\0" "4x\0" "8x\0" "16x\0" "32x\0";
                 if(ImGui::BeginTable("##lodBands",3,ImGuiTableFlags_SizingStretchProp)){ImGui::TableSetupColumn("Band",ImGuiTableColumnFlags_WidthFixed,38);ImGui::TableSetupColumn("Width (share of the world)",ImGuiTableColumnFlags_WidthStretch,2.4f);ImGui::TableSetupColumn("Detail",ImGuiTableColumnFlags_WidthStretch,1.0f);ImGui::TableHeadersRow();
                    for(int i=0;i<lodBandCount;++i){ImGui::PushID(i);ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::Text("%d",i+1);ImGui::TableSetColumnIndex(1);valueSlider("##w",lodBandWidth[i],.005f,.5f,"%.3f",ImGuiSliderFlags_Logarithmic);
                        ImGui::TableSetColumnIndex(2);ImGui::SetNextItemWidth(-1);ImGui::Combo("##d",&lodBandDetail[i],details);ImGui::PopID();}
                    ImGui::EndTable();}
                 if(ImGui::SmallButton("Reset bands")){lodBandCount=6;for(int i=0;i<8;++i){lodBandWidth[i]=.12f;lodBandDetail[i]=std::min(i,5);}}}
                lodBandFraction=lodBandWidth[0];
                ImGui::TextWrapped("Bands go outward from the camera: each has a width (fraction of the world size, widened with camera height) and a triangle detail (1x full ... 32x coarser per edge). Past the last band its detail continues. Band borders are stitched without cracks.");
                ImGui::SeparatorText("Near-camera resolution");
                ImGui::Checkbox("Adaptive: finer while zooming in",&lodAdaptive);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("The nearest band is regenerated on a denser grid as the camera gets closer, in power-of-two steps, within the limits below.");
                ImGui::BeginDisabled(!lodAdaptive);ImGui::Checkbox("Apply only after the camera stops",&lodWaitForStop);if(ImGui::IsItemHovered())ImGui::SetTooltip("Like the trees: the finer detail is hidden while you move the camera and returns a moment after you stop.");
                if(valueSlider("Finest resolution (blocks per sample)",lodFinest,.25f,16.0f,"%.2f",ImGuiSliderFlags_Logarithmic))lodCoarsest=std::max(lodCoarsest,lodFinest);
                if(valueSlider("Coarsest resolution (blocks per sample)",lodCoarsest,1.0f,256.0f,"%.0f",ImGuiSliderFlags_Logarithmic))lodFinest=std::min(lodFinest,lodCoarsest);
                ImGui::EndDisabled();
                int samplesIndex=lodBoostSamples>=4096?2:lodBoostSamples>=2048?1:0;ImGui::BeginDisabled(!lodAdaptive);
                if(ImGui::Combo(labelAbove("Patch samples").c_str(),&samplesIndex,"1024\0" "2048\0" "4096 (1.1 GB)\0"))lodBoostSamples=1024<<samplesIndex;
                ImGui::TextWrapped("Generates the nearest band again on a denser grid that follows the camera, so close terrain gets more detail than the whole-world grid. Resolution is the distance between samples: smaller is finer. Uses a near patch plus an intermediate ring at lower resolution. Both currently regenerate when the camera settles in a new place. A hand-placed High detail patch takes priority.");
                if(lodAdaptiveState==1&&detailInitialized)ImGui::TextWrapped("Near: %d x %d samples, %.2f blocks per sample, radius %.0f (plus an intermediate detail ring)",detailGrid,detailGrid,detailRenderRadius*2/float(std::max(2,detailGrid-1)),detailRenderRadius);
                else if(lodAdaptiveState==2)ImGui::TextWrapped("Now: not needed at this distance (the whole-world grid is already as fine as %.1f blocks per sample).",params.domain.x/float(std::max(2u,params.meta.x-1)));
                else if(lodAdaptiveState==4)ImGui::TextWrapped("Now: waiting for the camera to stop moving.");
                                else if(!distanceLod)ImGui::TextUnformatted("Turn the six-band view on to use adaptive resolution.");
                ImGui::EndDisabled();
                ImGui::Text("Triangles drawn: %llu",(unsigned long long)surface.drawnTriangles);ImGui::EndDisabled();
                ImGui::SeparatorText("Adaptive finer sizes");{float shown=finerPatchRadius;if(valueSlider("Patch radius (0 = automatic)",shown,0,8192,"%.0f blocks",ImGuiSliderFlags_Logarithmic))finerPatchRadius=shown<16?0:shown;}
                valueSlider("Ring width (times the patch width)",finerRingScale,1.25f,4,"%.2fx");if(ImGui::IsItemHovered())ImGui::SetTooltip("The intermediate ring covers this many times the patch width with the same number of samples, so its spacing is this many times the patch spacing (always finer than the bands).");
                ImGui::Combo(labelAbove("While dragging a slider").c_str(),&finerDragMode,"Close bands first, full detail\0" "Preview patch\0" "Full detail every change\0");if(ImGui::IsItemHovered())ImGui::SetTooltip("Close bands first: the finer patch and its ring rebuild at full detail with the newest values in the background (about 1-2 s each), then the rest of the world refines.\nPreview patch: a smaller patch on every change, full detail after you let go.\nFull detail every change: rebuilds the full patch synchronously on every change (can lag).");finerLiveFull=finerDragMode==2;
                ImGui::BeginDisabled(finerDragMode!=1);{int c=finerEditGrid>=2048?3:finerEditGrid>=1024?2:finerEditGrid>=512?1:0;if(ImGui::Combo(labelAbove("Preview patch size").c_str(),&c,"256\0" "512\0" "1024\0" "2048\0"))finerEditGrid=256<<c;}ImGui::EndDisabled();
                ImGui::Checkbox("Finer detail in walk and fly modes",&finerInCameraModes);if(ImGui::IsItemHovered())ImGui::SetTooltip("Minecraft (walk) and Unreal (fly) navigation: generate the near patch and its ring around you. Off: only the whole-world grid (walk collision then uses it too).");
                ImGui::EndPopup();}
            ImGui::EndChild();ImGui::PopStyleVar();ImGui::SetCursorScreenPos(afterImage);
            ImGui::SetCursorScreenPos({corner.x+12,corner.y+14});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{6,6});
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,7);
            ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.045f,.075f,.095f,.95f));
            ImGui::BeginChild("Viewport tools",{54,toolbarHeight},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
            if(toolIconButton("##navigateTool",ToolIcon::Navigate,!detailTool&&!centerTool&&!spawnTool&&!measureTool&&!biomeTool&&!pointTool&&!copyTool)){detailTool=centerTool=spawnTool=measureTool=biomeTool=copyTool=false;}
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Navigate [Esc]\nLMB/MMB: orbit | RMB: pan | Wheel: zoom | R: reset");
            if(toolIconButton("##centerTool",ToolIcon::Center,centerTool)){centerTool=!centerTool;detailTool=spawnTool=measureTool=false;}
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Center camera [C]\nLMB: set orbit center | MMB: orbit | R: reset");
            if(toolIconButton("##spawnTool",ToolIcon::Spawn,spawnTool)){spawnTool=!spawnTool;detailTool=centerTool=measureTool=false;}if(ImGui::IsItemHovered())ImGui::SetTooltip("Set spawn [P]\nLMB place preset spawn | MMB orbit | RMB pan");
            if(toolIconButton("##measureTool",ToolIcon::Measure,measureTool)){measureTool=!measureTool;detailTool=centerTool=spawnTool=false;}if(ImGui::IsItemHovered())ImGui::SetTooltip("Measure distance [M]\nLMB add points | Backspace remove last | Enter finish | MMB orbit");
            if(toolIconButton("##biomeTool",ToolIcon::Biome,biomeTool)){if(biomeTool)disableBiomeTool();else biomeTool=true;detailTool=centerTool=spawnTool=measureTool=pointTool=false;}{auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();biomeToolbarButton={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}if(ImGui::IsItemHovered())ImGui::SetTooltip("Biome finder [N]\nFind a biome near the view centre | click again to close and clear results");
            if(toolIconButton("##pointTool",ToolIcon::Pin,pointTool)){pointTool=!pointTool;if(pointTool){detailTool=centerTool=spawnTool=measureTool=false;disableBiomeTool();}}if(ImGui::IsItemHovered())ImGui::SetTooltip("Points [B]\nLMB: pin a named point for this project | list in the right panel | MMB orbit");
            if(toolIconButton("##copyTool",ToolIcon::Copy,copyTool)){copyTool=!copyTool;if(copyTool){detailTool=centerTool=spawnTool=measureTool=pointTool=walkPickTool=false;disableBiomeTool();}}if(ImGui::IsItemHovered())ImGui::SetTooltip("Copy coordinates [K]\nLMB: copy X Y Z of the clicked spot (block coordinates, Y on top of the ground; paste after /tp) | MMB orbit");
            ImGui::Separator();
            ImGui::BeginDisabled(topDown);if(toolIconButton("##flyMode",ToolIcon::Fly,flyActive)){if(flyMode)leaveFly();else enterFly();}ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Fly camera [F]\nUnreal-style: RMB look, WASD move, Q/E down/up, wheel = speed, Shift fast, Ctrl slow");
            ImGui::BeginDisabled(topDown);if(toolIconButton("##walkMode",ToolIcon::Walk,walkActive||walkPickTool)){if(walkMode)leaveWalk();else walkPickTool=!walkPickTool;}ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Walk mode [G]\nDrop in at the world spawn (or press G over the terrain to drop in there) and walk like Minecraft\nWASD, Space jump (double-tap: fly), Ctrl sprint, Shift sneak, Esc leaves walk mode");
            if(openToolSettingsRequested){dock.rightHidden=false;ImGui::SetWindowFocus("View and quality");openToolSettingsRequested=false;}
            if(!biomeTool)disableBiomeTool();
            ImGui::EndChild();ImGui::PopStyleColor();ImGui::PopStyleVar(2);
            ImGui::SetCursorScreenPos(afterImage);
            bool overToolbar=io.MousePos.x>=corner.x+12&&io.MousePos.x<=corner.x+66&&io.MousePos.y>=corner.y+14&&io.MousePos.y<=corner.y+14+toolbarHeight;
            bool placeDetail=false,centerClick=false,spawnClick=false,measureClick=false,pointClick=false,copyClick=false;bool hoverAllowed=viewportHovered&&!overToolbar&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)&&!(walkMode&&walkMouseCaptured);bool inspectHover=hoverAllowed&&!walkActive&&params.meta.z==5&&graphSeeds.climateMeta.x&&elapsed-lastHoverTime>.12;
            if(!hoverAllowed)hover.valid=false;
            if(walkMode&&!walkMouseCaptured&&viewportHovered&&!overToolbar&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)&&ImGui::IsMouseClicked(ImGuiMouseButton_Left))setCursorCapture(true);
            if(viewportHovered&&!overToolbar&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)&&!walkMode){
                placeDetail=detailTool&&ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                centerClick=centerTool&&ImGui::IsMouseClicked(ImGuiMouseButton_Left);spawnClick=spawnTool&&ImGui::IsMouseClicked(ImGuiMouseButton_Left);measureClick=measureTool&&ImGui::IsMouseClicked(ImGuiMouseButton_Left);pointClick=pointTool&&ImGui::IsMouseClicked(ImGuiMouseButton_Left);copyClick=copyTool&&ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                if(walkPickTool&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&hover.valid){walkPickTool=false;enterWalk({hover.position.x,hover.position.z});}
                if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)&&io.KeyCtrl&&io.KeyShift)sunDrag=true;
                if(sunDrag&&io.MouseDelta.x!=0){sunAzimuth=std::fmod(sunAzimuth+io.MouseDelta.x*.25f,360.0f);if(sunAzimuth<0)sunAzimuth+=360;}
                if(detailTool&&io.KeyCtrl&&io.MouseWheel!=0){detailRadius=std::clamp(detailRadius*std::exp(io.MouseWheel*.12f),16.0f,100000.0f);}
                if(flyActive){if(io.MouseDown[1]&&!sunDrag){flyYaw-=io.MouseDelta.x*.0035f;flyPitch=std::clamp(flyPitch-io.MouseDelta.y*.0035f,-1.55f,1.55f);}if(io.MouseWheel!=0&&!io.KeyCtrl)flySpeed=std::clamp(flySpeed*std::exp(io.MouseWheel*.18f),1.0f,params.domain.x*2);}
                else if((!detailTool&&!centerTool&&!spawnTool&&!measureTool&&!pointTool&&!copyTool&&!walkPickTool&&ImGui::IsMouseDragging(ImGuiMouseButton_Left))||ImGui::IsMouseDragging(ImGuiMouseButton_Middle)){yaw-=io.MouseDelta.x*.006f;pitch=std::clamp(pitch+io.MouseDelta.y*.006f,-1.565f,1.565f);}
                if(!flyActive&&!sunDrag&&ImGui::IsMouseDragging(ImGuiMouseButton_Right)){
                    float scale=topDown?params.domain.x*1.1f*distance/size.x:2*params.domain.x*.83f*distance*std::tan(glm::radians(cameraFov*.5f))/size.y;
                    glm::vec2 right=topDown?glm::vec2(1,0):glm::vec2(std::cos(yaw),-std::sin(yaw));glm::vec2 up=topDown?glm::vec2(0,-1):glm::vec2(-std::sin(yaw)*std::sin(pitch),-std::cos(yaw)*std::sin(pitch));
                    pan+=(-right*io.MouseDelta.x+up*io.MouseDelta.y)*scale;if(!topDown)cameraPanY+=std::cos(pitch)*io.MouseDelta.y*scale;
                }
                if(!flyActive&&!(detailTool&&io.KeyCtrl))distance=std::max(1e-12f,std::min(4.0f,distance*std::exp(-io.MouseWheel*.12f)));
                if(!detailTool&&!centerTool&&!spawnTool&&!measureTool&&!biomeTool&&!pointTool&&!copyTool&&!flyActive&&ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){pan={};cameraPanY=0;distance=1;cameraCentered=false;}
            }
            if(sunDrag&&!io.MouseDown[1])sunDrag=false;
            if(flyActive&&(viewportHovered||io.MouseDown[1])&&!keysBlocked&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)){
                glm::vec3 forward=flyDirection(),right=glm::normalize(glm::cross(forward,glm::vec3(0,1,0))),move{};
                if(ImGui::IsKeyDown(ImGuiKey_W))move+=forward;if(ImGui::IsKeyDown(ImGuiKey_S))move-=forward;if(ImGui::IsKeyDown(ImGuiKey_D))move+=right;if(ImGui::IsKeyDown(ImGuiKey_A))move-=right;if(ImGui::IsKeyDown(ImGuiKey_E))move.y+=1;if(ImGui::IsKeyDown(ImGuiKey_Q))move.y-=1;
                if(glm::length(move)>0)flyPos+=glm::normalize(move)*flySpeed*float(std::min(dt,.1))*(io.KeyShift?4.0f:io.KeyCtrl?.25f:1.0f);
            }
            if(screenDetail&&detailTool&&!detailPlaced&&viewportHovered&&!overToolbar){auto* brush=ImGui::GetWindowDrawList();brush->AddCircle(io.MousePos,detailScreenRadius,IM_COL32(120,240,209,210),96,1.6f);}
            if(detailPlaced&&screenDetail&&detailScreen.w>0){auto* brush=ImGui::GetWindowDrawList();brush->AddCircle({corner.x+detailScreen.x,corner.y+detailScreen.y},detailScreenRadius,IM_COL32(55,224,190,235),96,1.7f);}
            if(hover.valid&&hoverAllowed&&measurePoints.empty()&&!measureTool){auto* hud=ImGui::GetWindowDrawList();ImVec2 pos{corner.x+14,corner.y+size.y-119};hud->AddRectFilled(pos,{pos.x+std::min(520.0f,size.x-28),pos.y+106},IM_COL32(11,19,25,235),7);char line[240];float yy=pos.y+10;
                for(auto text:{std::string("Biome: ")+hover.biome,std::string("Terrain: ")+hover.terrain,std::string("Block: ")+hover.block}){hud->AddText({pos.x+12,yy},IM_COL32(218,234,230,255),text.c_str());yy+=20;}
                std::snprintf(line,sizeof(line),"X %.1f  Y %.1f  Z %.1f | Ground %.1f",hover.position.x,hover.position.y,hover.position.z,hover.sample.values.x);hud->AddText({pos.x+12,yy},IM_COL32(157,193,185,255),line);
            }
            bool feedbackVisible=!editor.feedback.empty();float feedbackHeight=feedbackVisible?ImGui::CalcTextSize(editor.feedback.c_str(),nullptr,false,380.0f).y+15.0f:0.0f;
            float previewStatusHeight=editPreview.enabled||editPreview.busy()?(editPreview.busy()?34.0f:24.0f):0.0f;float overlayHeight=(showMetrics?127.0f:57.0f)+feedbackHeight+previewStatusHeight;
            auto* overlay=ImGui::GetWindowDrawList();overlay->AddRectFilled({corner.x+80,corner.y+14},{corner.x+491,corner.y+overlayHeight},IM_COL32(11,19,25,225),7);
            overlay->AddText({corner.x+94,corner.y+25},IM_COL32(184,224,211,255),params.meta.z==5?"FTF surface: terrain, water and biomes":params.meta.z==6?"FTF continent map (diagnostic)":params.meta.z==4?"FTF land stage (before continents)":params.meta.z==3?"FTF region map (diagnostic)":"Validation terrain (not full FTF)");
            if(showMetrics){char text[256];std::snprintf(text,sizeof(text),"%u x %u  |  GPU %.2f ms  |  frame %.1f ms",params.meta.x,params.meta.x,lastDrawTiming.total,smoothedFrame);overlay->AddText({corner.x+94,corner.y+51},IM_COL32(219,228,232,255),text);
                std::snprintf(text,sizeof(text),"Compute %.2f ms  |  draw %.2f ms",lastTiming.generate,lastDrawTiming.draw);overlay->AddText({corner.x+94,corner.y+76},IM_COL32(145,166,178,255),text);
                overlay->AddText({corner.x+94,corner.y+101},IM_COL32(145,166,178,255),"Drag: orbit  |  Right drag: pan  |  Wheel: zoom");}
            if(editPreview.enabled||editPreview.busy())overlay->AddText({corner.x+94,corner.y+(showMetrics?126.0f:56.0f)},IM_COL32(132,225,191,255),editPreview.status().c_str());
            if(editPreview.busy()){   // loading bar: how far the full-resolution terrain has come
                float top=corner.y+(showMetrics?146.0f:76.0f),left=corner.x+94,width=380;float fraction=editPreview.job?std::clamp(editPreview.progress(),0.0f,1.0f):0.0f;
                overlay->AddRectFilled({left,top},{left+width,top+7},IM_COL32(28,48,56,255),3);
                overlay->AddRectFilled({left,top},{left+std::max(6.0f,width*fraction),top+7},editPreview.job?IM_COL32(72,214,170,255):IM_COL32(236,190,92,255),3);}
            if(finerProgress>=0&&!topDown){   // finer-detail loading bar along the bottom of the 3D viewport
                ImVec2 vpMin=ImGui::GetWindowPos(),vpSize=ImGui::GetWindowSize();float left=vpMin.x+16,right=vpMin.x+vpSize.x-16,bottom=vpMin.y+vpSize.y-12;
                overlay->AddRectFilled({left-6,bottom-27},{right+6,bottom+5},IM_COL32(11,19,25,200),6);overlay->AddText({left,bottom-24},IM_COL32(184,224,211,255),finerLabel);
                overlay->AddRectFilled({left,bottom-6},{right,bottom},IM_COL32(28,48,56,255),3);overlay->AddRectFilled({left,bottom-6},{left+std::max(6.0f,(right-left)*finerProgress),bottom},IM_COL32(72,214,170,255),3);}
            if(feedbackVisible)overlay->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{corner.x+94,corner.y+(showMetrics?132.0f:62.0f)+previewStatusHeight},editor.feedbackLive?IM_COL32(133,224,188,255):IM_COL32(246,185,105,255),editor.feedback.c_str(),nullptr,380.0f);
            dock.end(viewportVisible);ImGui::PopStyleVar();
            if(params.meta.z==5)surface.updateRivers(editPreview.display.rivers);
            if(walkMode&&topDown)leaveWalk();
            // collision: the finer patch where it covers the player, else the whole-world grid window under the player, else the coarse pick grid
            auto gridHeight=[&](const std::vector<float>& hv,const TerrainParams& P,glm::vec2 xz,float& out){uint32_t G=P.meta.x;if(G<2||hv.size()!=size_t(G)*G)return false;
                glm::vec2 uv=(xz-glm::vec2(P.domain.y,P.domain.z))/P.domain.x+.5f;if(uv.x<0||uv.x>1||uv.y<0||uv.y>1)return false;
                auto q=uv*float(G-1);int x=std::min(int(G)-2,int(q.x)),z=std::min(int(G)-2,int(q.y));float a=q.x-x,b=q.y-z;
                out=std::lerp(std::lerp(hv[size_t(z)*G+x],hv[size_t(z)*G+x+1],a),std::lerp(hv[size_t(z+1)*G+x],hv[size_t(z+1)*G+x+1],a),b);if(!showOceanFloor)out=std::max(out,params.land.z);return true;};
            auto walkGround=[&](glm::vec2 xz){float g=params.land.z;
                if(detailAuto&&gridHeight(walkHeights,walkPatch,xz,g))return g;if(gridHeight(baseWalkHeights,baseWalkParams,xz,g))return g;
                if(!sampleGround(xz,g))g=params.land.z;return g;};
            // Minecraft-like footprint: the player is a 0.6 x 0.6 block column standing on the highest ground under it, so the
            // camera never sinks into a rising slope ahead (a point-sized player let the view clip into hills).
            auto walkFootprint=[&](glm::vec2 xz){float g=walkGround(xz);for(glm::vec2 o:{glm::vec2(.3f,.3f),glm::vec2(-.3f,.3f),glm::vec2(.3f,-.3f),glm::vec2(-.3f,-.3f)})g=std::max(g,walkGround(xz+o));return g;};
            if(walkActive){
                stallScope("walk heights collect",[&]{std::vector<float> got;TerrainParams gotParams;uint64_t gotRevision=0;if(surface.collectHeights(gpu.swap.FrameIndex,got,gotParams,gotRevision)){baseWalkHeights=std::move(got);baseWalkParams=gotParams;}});
                if(detailInitialized&&detailAuto){   // collision heights arrive asynchronously (a few frames after the patch was generated); the last valid field stays in use meanwhile
                    stallScope("walk heights collect",[&]{std::vector<float> got;TerrainParams gotParams;uint64_t gotRevision=0;if(detailSurface.collectHeights(gpu.swap.FrameIndex,got,gotParams,gotRevision)&&gotRevision>=walkHeightsRevision){walkHeights=std::move(got);walkHeightsRevision=gotRevision;walkPatch=gotParams;}});}
                const float sdt=options.exerciseWalk?options.walkDt:float(std::min(dt,.05));const bool captured=walkMouseCaptured||options.hidden;
                double cx=0,cy=0;if(!options.hidden)glfwGetCursorPos(window,&cx,&cy);
                if(walkMouseCaptured&&!options.hidden){if(walkLookReset){walkLastCursor={cx,cy};walkLookReset=false;}walkYaw-=float(cx-walkLastCursor.x)*.0022f;walkPitch=std::clamp(walkPitch-float(cy-walkLastCursor.y)*.0022f,-1.55f,1.55f);walkLastCursor={cx,cy};}
                if(options.exerciseWalk){if(frameNumber==20)walkYaw+=options.walkTurn;if(frameNumber==24)walkPitch=-.25f;}
                glm::vec2 forward{std::sin(walkYaw),std::cos(walkYaw)},right{-std::cos(walkYaw),std::sin(walkYaw)},wish{};
                const bool typing=io.WantTextInput;
                if(captured&&!typing){if(ImGui::IsKeyDown(ImGuiKey_W))wish+=forward;if(ImGui::IsKeyDown(ImGuiKey_S))wish-=forward;if(ImGui::IsKeyDown(ImGuiKey_D))wish+=right;if(ImGui::IsKeyDown(ImGuiKey_A))wish-=right;}
                glm::vec2 here{walkPos.x,walkPos.z};float ground=walkFootprint(here);walkInWater=!walkFlying&&ground<=params.land.z+.01f;
                const bool sprint=io.KeyCtrl&&captured,sneak=io.KeyShift&&!walkFlying&&captured;
                float speed=walkFlying?(sprint?21.9f:10.9f):walkInWater?2.2f:sprint?5.612f:sneak?1.3f:4.317f;speed*=walkSpeedScale;
                if(glm::length(wish)>0){glm::vec2 move=glm::normalize(wish)*speed*sdt;
                    // substeps of at most 0.1 block keep the step-up rule independent of the frame rate; on the ground the feet follow
                    // the slope between substeps (rises up to 0.6 block are stepped, like Minecraft; higher ledges need a jump)
                    const int substeps=std::max(1,int(std::ceil(glm::length(move)/.1f)));const glm::vec2 stepMove=move/float(substeps);
                    auto tryMove=[&](glm::vec2 delta){glm::vec2 target=here+delta;float g=walkFootprint(target);float rise=g-walkPos.y;
                        if(walkFlying||rise<=(walkPos.y>ground+.05f?1.05f:.6f)){walkPos.x=target.x;walkPos.z=target.y;here=target;if(!walkFlying&&rise>0&&walkPos.y<=ground+.05f)walkPos.y=g;ground=g;return true;}return false;};
                    for(int i=0;i<substeps;++i)if(!tryMove(stepMove)&&!tryMove({stepMove.x,0}))tryMove({0,stepMove.y});}
                float halfWorld=params.domain.x*.49f;walkPos.x=std::clamp(walkPos.x,params.domain.y-halfWorld,params.domain.y+halfWorld);walkPos.z=std::clamp(walkPos.z,params.domain.z-halfWorld,params.domain.z+halfWorld);
                ground=walkFootprint({walkPos.x,walkPos.z});
                const bool spaceDown=captured&&!typing&&ImGui::IsKeyDown(ImGuiKey_Space),spacePressed=captured&&!typing&&ImGui::IsKeyPressed(ImGuiKey_Space,false);
                if(spacePressed){if(elapsed-lastSpaceTap<.3){walkFlying=!walkFlying;walkVelY=0;lastSpaceTap=-10;}else lastSpaceTap=elapsed;}
                if(walkFlying){float vertical=(spaceDown?1.0f:0.0f)-((io.KeyShift&&captured)?1.0f:0.0f);walkPos.y=std::max(ground,walkPos.y+vertical*7.0f*walkSpeedScale*sdt);}
                else if(walkInWater){walkVelY=0;walkPos.y=ground;}
                else{if(walkPos.y<=ground+.001f&&spaceDown)walkVelY=8.8f;walkVelY=std::max(walkVelY-32.0f*sdt,-78.0f);walkPos.y+=walkVelY*sdt;if(walkPos.y<=ground){walkPos.y=ground;if(walkVelY<0)walkVelY=0;}}
                walkOnGround=walkPos.y<=ground+.02f;
                if(options.walkTrace)std::fprintf(stderr,"walk %llu %.3f %.3f %.3f ground %.3f heights %zu\n",(unsigned long long)frameNumber,walkPos.x,walkPos.y,walkPos.z,ground,walkHeights.size());
            }
            if(options.exerciseStreaming&&frameNumber==39)detailLastEdit=-10;
            if(int(frameNumber)>=options.orbitFrom&&int(frameNumber)<options.orbitTo)yaw+=.004f;   /* test: orbit the camera between these frames */
            if(options.exerciseStreaming&&frameNumber>40&&frameNumber<72){if(walkActive)walkPos.x+=64.0f;else if(flyActive)flyPos.x+=512.0f;}
            {   // camera motion: finer detail waits for the camera to stop (same idea as the trees)
                const float signature[14]={yaw,pitch,distance,pan.x,pan.y,cameraPanY,flyPos.x,flyPos.y+flyPos.z,flyYaw,flyPitch,walkPos.x,walkPos.y+walkPos.z,walkYaw,walkPitch};bool moved=false;
                for(int i=0;i<14;++i){if(std::abs(signature[i]-lastCameraSignature[i])>1e-5f*std::max(std::abs(signature[i]),1e-3f))moved=true;lastCameraSignature[i]=signature[i];}
                if(moved){lodResumeAt=elapsed+.4;cameraMovedAt=elapsed;}
            }
            glm::vec2 originXZ{params.domain.y,params.domain.z};
            glm::vec2 cameraGround=walkActive?glm::vec2(walkPos.x,walkPos.z)-originXZ:flyActive?glm::vec2(flyPos.x,flyPos.z):pan+glm::vec2(std::sin(yaw),std::cos(yaw))*(topDown?0.0f:params.domain.x*.83f*distance*std::cos(pitch));
            {   // The near-camera patch: walk mode (1 block per sample) or the LOD view's resolution boost. A hand-placed High detail patch always wins outside walk mode.
                bool wantAuto=false;float wantRadius=0;int wantGrid=0;bool immediate=false;glm::vec2 wantCenter=(walkActive||flyActive?cameraGround:pan)+originXZ; /* orbit: around what you look at (under the eye it could sit behind the view) */float baseSpacing=params.domain.x/float(std::max(2u,params.meta.x-1));
                if(!topDown&&distanceLod&&lodAdaptive&&params.meta.z>=2){
                    float target=1e9f;
                    {   // aim for about 1.5 screen pixels per sample at the distance of the view target, in power-of-two steps within the user's limits
                        float viewDistance=walkActive?1.62f:flyActive?flyPos.y:params.domain.x*.83f*distance;if(flyActive){float g=0;if(sampleGround({flyPos.x+params.domain.y,flyPos.z+params.domain.z},g))viewDistance=std::max(1.0f,flyPos.y-g);}
                        float wanted=1.5f*.89f*std::max(1.0f,viewDistance)/float(std::max(1u,h));float stepped=std::exp2(std::round(std::log2(std::max(.01f,wanted))));
                        target=std::clamp(stepped,lodFinest,std::max(lodFinest,lodCoarsest));lodAdaptiveSpacing=target;if(target>baseSpacing/1.12f)target=1e9f; // no boost where the whole-world grid is already as fine
                    }
                    float band=target>1e8f?0.0f:params.domain.x*std::max(.005f,lodBandFraction);int grid=256;while(grid<lodBoostSamples&&2*band/target>grid-1)grid*=2;
                    float radius=std::min({band,(grid-1)*target*.5f,params.domain.x*.45f});if(finerPatchRadius>0&&target<1e8f){float want=finerPatchRadius;grid=256;while(grid<lodBoostSamples&&2*want/target>grid-1)grid*=2;radius=std::min({want,(grid-1)*target*.5f,params.domain.x*.45f});}if(radius>1&&(finerInCameraModes||!(flyActive||walkActive))){wantAuto=true;wantGrid=grid;wantRadius=radius;}}
                if(options.noFiner)wantAuto=false;   /* test flag: bands only, no finer patch or ring */
                const bool waitingForStop=wantAuto&&!walkActive&&lodWaitForStop&&elapsed<lodResumeAt;if(waitingForStop)wantAuto=false;
                lodAdaptiveState=!(distanceLod&&lodAdaptive)||topDown?0:(detailPlaced&&!detailAuto)?3:waitingForStop?4:wantAuto?1:2;
                if(wantAuto&&(!detailPlaced||detailAuto)){
                    float snap=std::max(1.0f,wantRadius*.25f);
                    if(!detailAuto){autoRadiusBackup=detailRadius;autoGridBackup=detailGrid;detailAuto=true;detailPlaced=true;detailCenter=glm::round(wantCenter/snap)*snap;autoMoveTime=-10;lastAutoCamera=wantCenter;}
                    if(glm::length(wantCenter-lastAutoCamera)>wantRadius*.03f){autoMoveTime=elapsed;lastAutoCamera=wantCenter;}
                    bool settled=immediate||options.exerciseStreaming||elapsed-autoMoveTime>.25;
                    if(settled&&(glm::length(wantCenter-detailCenter)>wantRadius*.25f||wantRadius!=detailRadius||wantGrid!=detailGrid))detailCenter=glm::round(wantCenter/snap)*snap;
                    detailRadius=wantRadius;detailGrid=wantGrid;detailPlaced=true;
                }else if(!wantAuto&&detailAuto&&!waitingForStop)releaseAutoPatch();   /* while the camera moves, keep the last patch on screen and let its build finish (it used to be thrown away on every orbit/fly move) */
                finerProgress=-1;   // bottom loading bar: waiting for the camera to stop, settling, then generating (shown until the patch is on screen)
                if(waitingForStop){finerProgress=.45f*std::clamp(1.0f-float(lodResumeAt-elapsed)/.4f,0.0f,1.0f);finerLabel="Finer detail: waiting for the camera to stop";}
                else if(detailAuto&&!immediate&&elapsed-autoMoveTime<.25){finerProgress=.45f+.25f*std::clamp(float(elapsed-autoMoveTime)/.25f,0.0f,1.0f);finerLabel="Finer detail: settling";}
            }
            float eyeAltitude=0;if(!topDown&&!walkActive){if(flyActive){float g=0;eyeAltitude=sampleGround({flyPos.x+params.domain.y,flyPos.z+params.domain.z},g)?std::max(0.0f,flyPos.y-g):std::max(0.0f,flyPos.y);}else eyeAltitude=params.domain.x*.83f*distance*std::sin(pitch);}
            surface.distanceLod=distanceLod;surface.lodAltitude=eyeAltitude;surface.lodBandSize=std::max(params.domain.x*std::max(.005f,lodBandFraction),eyeAltitude*.9f);{const float firstWidth=params.domain.x*std::max(.002f,lodBandWidth[0]),grow=std::max(1.0f,eyeAltitude*.9f/firstWidth);float end=0;surface.lodBands=uint32_t(std::clamp(lodBandCount,1,8));for(uint32_t i=0;i<surface.lodBands;++i){end+=params.domain.x*std::max(.002f,lodBandWidth[i])*grow;surface.lodBandEnd[i]=end;surface.lodBandLevel[i]=uint32_t(std::clamp(lodBandDetail[i],0,5));}}surface.lodCamera=cameraGround;
            if(!std::isfinite(cameraFov))cameraFov=60;cameraFov=std::clamp(cameraFov,5.0f,150.0f);
            DrawParams draw;float extent=params.domain.x;glm::vec3 atmoEye{};glm::vec2 atmoClip{1,1};glm::vec3 center=orbitCenter();
            if(flyActive)center=flyPos+flyDirection()*std::max(50.0f,flySpeed);
            glm::vec3 walkEye=walkPos+glm::vec3(0,1.62f,0)-glm::vec3(params.domain.y,0,params.domain.z),walkLook{std::sin(walkYaw)*std::cos(walkPitch),std::sin(walkPitch),std::cos(walkYaw)*std::cos(walkPitch)};
            if(walkActive)center=walkEye+walkLook*50.0f;lastCameraCenter=center;
            if(topDown){center.y=0;float span=std::max(.001f,extent*.55f*distance);draw.vp=glm::ortho(-span,span,-span*h/w,span*h/w,1.0f,extent*10.0f)*glm::lookAt(center+glm::vec3(0,extent*4,0),center,glm::vec3(0,0,-1));}
            else if(walkActive){atmoEye=walkEye;atmoClip={.08f,std::min(extent*20.0f,3.0e6f)};draw.vp=glm::perspective(glm::radians(cameraFov),static_cast<float>(w)/h,.08f,std::min(extent*20.0f,3.0e6f))*glm::lookAt(walkEye,walkEye+walkLook,glm::vec3(0,1,0));}
            else if(flyActive){atmoEye=flyPos;atmoClip={std::max(.05f,flySpeed*.0008f),extent*20.0f};draw.vp=glm::perspective(glm::radians(cameraFov),static_cast<float>(w)/h,std::max(.05f,flySpeed*.0008f),extent*20.0f)*glm::lookAt(flyPos,flyPos+flyDirection(),glm::vec3(0,1,0));}
            else{float radius=std::max(std::max(.001f,glm::length(center)*.000001f),extent*.83f*distance);glm::vec3 eye=center+glm::vec3(std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch))*radius;
                draw.vp=glm::perspective(glm::radians(cameraFov),static_cast<float>(w)/h,std::max(.00001f,std::min(extent*.0002f,radius*.001f)),extent*20.0f)*glm::lookAt(eye,center,glm::vec3(0,1,0));atmoEye=eye;atmoClip={std::max(.00001f,std::min(extent*.0002f,radius*.001f)),extent*20.0f};}
            // Flip the entire clip-space Y row for Vulkan, including view terms.
            for(int column=0;column<4;++column)draw.vp[column][1]*=-1;
            {auto& a=surface.atmosphere;const float ex=exaggeration;const bool on=!topDown; // volumetric clouds replace the old cloud shadows at draw time only (the saved setting is kept)
               
                a.water={on&&atmo.water?1.0f:0.0f,atmo.ripple,atmo.reflection,params.land.z*ex};a.fog={on&&atmo.fog?1.0f:0.0f,atmo.fogDensity,atmo.fogFalloff,atmo.fogHeight*ex};
                a.cloud={on&&atmo.clouds?1.0f:0.0f,atmo.cloudCoverage,atmo.cloudDensity,atmo.cloudBase*ex};a.cloud2={atmo.cloudThickness*ex,atmo.cloudSpeed,atmo.cloudScale,ex};
                a.inverseVP=glm::mat4(glm::inverse(glm::dmat4(draw.vp)*glm::translate(glm::dmat4(1.0),glm::dvec3(atmoEye))));a.clip={atmoClip,0,0};a.eye={atmoEye,static_cast<float>(std::fmod(options.shadowTime>=0?double(options.shadowTime):elapsed,100000.0))};a.domain={extent,static_cast<float>(params.meta.x),atmo.fogOpacity,atmo.cloudOpacity};}
            const bool heightViews=params.meta.z==2||params.meta.z==4||params.meta.z==5;draw.limits={terrainSqueezeStart,aboveHeight,params.domain.y,params.domain.z}; /* x: ceiling squeeze start, y: amber threshold; flags in meta.y bits 2048 / 4096 */draw.view={extent,params.land.z,exaggeration,lighting?1.0f:0.0f};draw.meta={params.meta.x,static_cast<uint32_t>(layer)|(showOceanFloor?256u:0u)|(ambientOcclusion?512u:0u)|(cloudShadows&&!atmo.clouds?1024u:0u)|(showHeightLimit&&terrainHeightLimit>0&&heightViews?2048u:0u)|(showAboveHeight&&heightViews?4096u:0u)|(static_cast<uint32_t>(params.ftfScales.w)<<16),params.meta.z,(static_cast<uint32_t>(std::fmod((options.shadowTime>=0?double(options.shadowTime):elapsed)*10.0,16777216.0))<<8)};
            if(surface.fieldRevision!=revision)++generatedFrames;
            meshVp=draw.vp;meshViewport={float(w),float(h)};
            // Ray (from the viewport camera, relative to the preview centre) against the mesh: a coarse march on the 129 x 129
            // base samples finds roughly where, then a march at the finest drawn spacing from a few coarse cells before that
            // point finds the first crossing of the full-resolution mesh (meshHeight). The coarse grid can flatten a ridge in
            // front of its hit, so the fine march starts where the ray is above the fine mesh and extends if it finds nothing.
            auto coarseHit=[&](glm::vec3 rayStart,glm::vec3 ray)->std::optional<float>{
                if(baseHeightCache.size()!=129*129)return std::nullopt;
                auto delta=[&](float t){auto p=rayStart+ray*t;glm::vec2 uv=glm::vec2(p.x,p.z)/extent+.5f;if(uv.x<0||uv.x>1||uv.y<0||uv.y>1)return 1e20f;
                    auto q=uv*128.0f;int x=std::min(127,int(q.x)),z=std::min(127,int(q.y));float a=q.x-x,b=q.y-z;auto& v=baseHeightCache;
                    float height=std::lerp(std::lerp(v[z*129+x],v[z*129+x+1],a),std::lerp(v[(z+1)*129+x],v[(z+1)*129+x+1],a),b);if(!showOceanFloor)height=std::max(height,params.land.z);return p.y-height*exaggeration;};
                float previous=0;for(int step=1;step<=4096;++step){float t=extent*20*step/4096.0f;if(delta(t)<=0&&delta(previous)<1e19f){float lo=previous,hi=t;for(int i=0;i<24;++i){float mid=(lo+hi)*.5f;(delta(mid)>0?lo:hi)=mid;}return (lo+hi)*.5f;}previous=t;}
                return std::nullopt;};
            auto meshHit=[&](glm::vec3 rayStart,glm::vec3 ray)->std::optional<glm::vec3>{
                auto tc=coarseHit(rayStart,ray);if(!tc)return std::nullopt;
                const glm::vec2 origin{params.domain.y,params.domain.z};auto worldAt=[&](float t){auto p=rayStart+ray*t;return glm::vec2(p.x,p.z)+origin;};
                auto fine=[&](float t){auto p=rayStart+ray*t;auto h=meshHeight(glm::vec2(p.x,p.z)+origin,true);return h?p.y-*h*exaggeration:1e20f;};
                float horizontal=std::max(glm::length(glm::vec2(ray.x,ray.z)),.02f),coarseCell=extent/128.0f;
                glm::vec2 coarsePoint=worldAt(*tc);float cell=extent/float(std::max(2u,surface.resolution)-1);
                if(middleActive&&middleTiles.valid()&&glm::length(coarsePoint-middleTiles.center())<middleTiles.domain.x*.5f)cell=std::min(cell,middleTiles.spacing());
                if(detailPlaced&&detailTiles.valid()&&glm::length(coarsePoint-detailTiles.center())<detailTiles.domain.x*.5f)cell=std::min(cell,detailTiles.spacing());
                float back=3*coarseCell/horizontal,start=std::max(0.0f,*tc-back);
                for(int i=0;i<8&&start>0&&fine(start)<=0;++i){back*=2;start=std::max(0.0f,*tc-back);}
                float end=*tc+back;
                for(int round=0;round<4;++round){
                    for(auto* tiles:{&baseTiles,&middleTiles,&detailTiles})if(tiles->valid())tiles->prefetchSegment(worldAt(start),worldAt(end));
                    float dt=std::max(.5f*cell/horizontal,(end-start)/8192.0f),previous=start;
                    for(float t=start+dt;t<=end+dt*.5f;t+=dt){if(fine(t)<=0&&fine(previous)<1e19f){float lo=previous,hi=t;for(int i=0;i<24;++i){float mid=(lo+hi)*.5f;(fine(mid)>0?lo:hi)=mid;}return rayStart+ray*((lo+hi)*.5f);}previous=t;}
                    start=end;end+=2*back;}
                return rayStart+ray**tc;};
            auto mouseRay=[&](glm::vec2 pixel,glm::vec3& rayStart,glm::vec3& ray){auto inverse=glm::inverse(draw.vp);glm::vec2 mouse{(pixel.x-corner.x)/size.x*2-1,(pixel.y-corner.y)/size.y*2-1};
                auto near4=inverse*glm::vec4(mouse,0,1),far4=inverse*glm::vec4(mouse,.5f,1);rayStart=glm::vec3(near4)/near4.w;ray=glm::normalize(glm::vec3(far4)/far4.w-rayStart);};
            if((placeDetail||centerClick||spawnClick||measureClick||pointClick||copyClick||inspectHover)&&surface.fieldRevision){
                if(baseHeightRevision!=surface.fieldRevision||baseHeightCache.empty()){stallScope("pick heights (sync readback)",[&]{baseHeightCache=surface.pickHeights();});baseHeightRevision=surface.fieldRevision;}
                bool fineAtCursor=detailPlaced&&detailInitialized&&detailSurface.fieldRevision&&(!screenDetail||(detailScreen.w>0&&glm::length(glm::vec2(io.MousePos.x-corner.x,io.MousePos.y-corner.y)-glm::vec2(detailScreen))<detailScreenRadius));
                glm::vec3 rayStart,ray;mouseRay({io.MousePos.x,io.MousePos.y},rayStart,ray);
                // Where the ray meets the mesh as drawn (meshHeight, full resolution); the 129 x 129 base samples only say where to look.
                bool found=false;glm::vec3 hit{};
                if(auto picked=meshHit(rayStart,ray)){glm::vec3 point=*picked;if(centerClick){pan={point.x,point.z};cameraHeight=point.y/std::max(.001f,exaggeration);cameraCentered=true;centerTool=false;}else if(spawnClick){editor.placeSpawn({point.x+params.domain.y,point.z+params.domain.z});previousPresetSpawn=editor.document.spawn();spawnMarker={previousPresetSpawn.x,point.y/std::max(.001f,exaggeration),previousPresetSpawn.y};hasSpawnMarker=true;spawnTool=false;}else if(measureClick){measurePoints.push_back({point.x+params.domain.y,point.y/std::max(.001f,exaggeration),point.z+params.domain.z});updateMeasurement();}else if(pointClick){static const uint32_t palette[]={0xffd24a,0x4ad2ff,0xff6b5e,0x7be07b,0xc58bff,0xff9f4a,0x4aa3ff,0xff7bc8};char name[40];std::snprintf(name,sizeof(name),"Point %zu",points.size()+1);points.push_back({name,{point.x+params.domain.y,point.y/std::max(.001f,exaggeration),point.z+params.domain.z},palette[points.size()%8]});highlightedPoint=int(points.size())-1;status="Pinned "+points.back().name;}else if(copyClick){glm::vec3 spot{point.x+params.domain.y,point.y/std::max(.001f,exaggeration),point.z+params.domain.z};char text[96];std::snprintf(text,sizeof(text),"%d %d %d",int(std::floor(spot.x)),int(std::ceil(spot.y)),int(std::floor(spot.z)));ImGui::SetClipboardText(text);copiedText=text;copiedAt=elapsed;status=std::string("Copied coordinates: ")+text;
                }else if(placeDetail){releaseAutoPatch();detailCenter={point.x+params.domain.y,point.z+params.domain.z};detailPlaced=true;detailSurfaceHeight=point.y/std::max(.001f,exaggeration);detailLastEdit=elapsed;}hit=point;found=true;}
                if(inspectHover){lastHoverTime=elapsed;hover.valid=found;if(found){
                    hover.position={hit.x+params.domain.y,hit.y/std::max(.001f,exaggeration),hit.z+params.domain.z};
                    Surface* target=&surface;if(fineAtCursor&&glm::length(glm::vec2(hover.position.x,hover.position.z)-detailCenter)<detailRenderRadius)target=&detailSurface;
                    stallScope("hover inspect (sync readback)",[&]{hover.sample=target->inspect({hover.position.x,hover.position.z});});auto ids=hover.sample.ids;
                    hover.biome=ids.x<biomeLegend["biomes"].size()?biomeLegend["biomes"][ids.x]["name"].get<std::string>():"unavailable";
                    hover.block=ids.z<blockLegend["blocks"].size()?blockLegend["blocks"][ids.z]["name"].get<std::string>():"unavailable";
                    const char* roles[]={"Land","Badlands","Mountain chain","Volcano","Deep ocean","Shallow ocean","Coast","Island","Beach","Island mountain","River","Wetland","Lake"};hover.terrain=ids.y<13?roles[ids.y]:"Unknown";
                    const char* families[]={"Plains","Steppe","Dales","Hills 1","Hills 2","Plateau","Badlands","Torridonian","Badlands plateau","Mountains low","Mountains","Mountains high","Volcano"};if(ids.w<graphSeeds.mixMeta.x&&ids.y<=1){auto choice=graphSeeds.choices[ids.w];if(choice.y<13){hover.terrain=families[choice.y];if(choice.w&&choice.z<13)hover.terrain+=std::string(" + ")+families[choice.z];}}
                }}
                if(!found&&(placeDetail||centerClick||spawnClick||measureClick||pointClick||copyClick))status="Click a visible part of the terrain.";
            }
            if(options.pickCheck>0&&frameNumber==options.frames-2&&surface.fieldRevision&&surface.resolution<=2048){   // test: cursor accuracy (base surface only; run with --no-finer)
                if(baseHeightCache.size()!=129*129)baseHeightCache=surface.pickHeights();auto field=surface.heightField();const uint32_t G=surface.resolution;const float spacing=extent/float(G-1);
                auto reference=[&](float x,float z){glm::vec2 g=(glm::vec2(x,z)/extent+.5f)*float(G-1);if(g.x<0||g.y<0||g.x>G-1||g.y>G-1)return 1e30f;glm::ivec2 a=glm::min(glm::ivec2(g),glm::ivec2(int(G)-2));glm::vec2 f=g-glm::vec2(a);
                    auto at=[&](int i,int j){float v=field[size_t(a.y+j)*G+a.x+i];return showOceanFloor?v:std::max(v,params.land.z);};float h00=at(0,0),h10=at(1,0),h01=at(0,1),h11=at(1,1);
                    return f.x+f.y<=1?h00+(h10-h00)*f.x+(h01-h00)*f.y:h11+(h01-h11)*(1-f.x)+(h10-h11)*(1-f.y);};
                std::vector<double> oldXZ,oldY,newXZ,newY;int missed=0;
                for(int j=0;j<options.pickCheck;++j)for(int i=0;i<options.pickCheck;++i){glm::vec2 pixel{corner.x+size.x*(.1f+.8f*(i+.5f)/options.pickCheck),corner.y+size.y*(.1f+.8f*(j+.5f)/options.pickCheck)};glm::vec3 rayStart,ray;mouseRay(pixel,rayStart,ray);
                    float horizontal=std::max(glm::length(glm::vec2(ray.x,ray.z)),.02f),rayStep=.2f*spacing/horizontal,last=0;std::optional<glm::vec3> truth;
                    auto delta=[&](float t){auto p=rayStart+ray*t;float h=reference(p.x,p.z);return h>1e29f?1e30f:p.y-h*exaggeration;};
                    for(float t=rayStep;t<extent*20&&!truth;t+=rayStep){if(delta(t)<=0&&delta(last)<1e29f){float lo=last,hi=t;for(int k=0;k<30;++k){float mid=(lo+hi)*.5f;(delta(mid)>0?lo:hi)=mid;}truth=rayStart+ray*((lo+hi)*.5f);}last=t;}
                    auto coarse=coarseHit(rayStart,ray);auto mesh=meshHit(rayStart,ray);if(!truth||!coarse||!mesh){++missed;continue;}
                    glm::vec3 old=rayStart+ray**coarse;auto xz=[&](glm::vec3 a){return double(glm::length(glm::vec2(a.x-truth->x,a.z-truth->z)));};
                    oldXZ.push_back(xz(old));oldY.push_back(std::abs(old.y-truth->y)/exaggeration);newXZ.push_back(xz(*mesh));newY.push_back(std::abs(mesh->y-truth->y)/exaggeration);}
                auto stats=[](std::vector<double> v){if(v.empty())return PresetJson{};std::sort(v.begin(),v.end());double sum=0;for(double x:v)sum+=x;return PresetJson{{"mean",sum/v.size()},{"median",v[v.size()/2]},{"p90",v[v.size()*9/10]},{"max",v.back()}};};
                auto top=findHighest();size_t fullBest=size_t(std::max_element(field.begin(),field.end())-field.begin());
                pickCheckReport={{"highest_bands",top?PresetJson{top->first,top->second}:PresetJson{}},{"highest_full",{fullBest,field[fullBest]}},{"rays",options.pickCheck*options.pickCheck},{"missed",missed},{"grid",G},{"blocks_per_sample",spacing},{"old_xz",stats(oldXZ)},{"old_y",stats(oldY)},{"new_xz",stats(newXZ)},{"new_y",stats(newY)}};}
            if(detailTool&&(ImGui::IsAnyItemActive()||io.MouseWheel!=0))detailLastEdit=elapsed;
            {static uint64_t editSeen=0;if(detailAuto&&revision!=editSeen){if(editSeen)detailLastEdit=elapsed;editSeen=revision;}} /* settings changed: the finer patch uses 256 samples until they settle */
            detailScreen={};detailCoverageLimited=false;
            if(detailPlaced&&screenDetail){
                glm::vec3 position{detailCenter.x-params.domain.y,detailSurfaceHeight*exaggeration,detailCenter.y-params.domain.z};
                auto clip=draw.vp*glm::vec4(position,1);
                if(clip.w>0){
                    auto projectPixel=[&](glm::vec3 p){auto v=draw.vp*glm::vec4(p,1);return (glm::vec2(v)/v.w*.5f+.5f)*glm::vec2(float(w),float(h));};
                    auto pixel=projectPixel(position);float delta=std::max(1.0f,extent/1024.0f);
                    auto dx=(projectPixel(position+glm::vec3(delta,0,0))-pixel)/delta,dz=(projectPixel(position+glm::vec3(0,0,delta))-pixel)/delta;
                    float aa=glm::dot(dx,dx),bb=glm::dot(dx,dz),cc=glm::dot(dz,dz);float smallest=std::sqrt(std::max(1e-8f,.5f*(aa+cc-std::sqrt((aa-cc)*(aa-cc)+4*bb*bb))));
                    float needed=detailScreenRadius/smallest*1.2f;detailRadius=std::pow(1.08f,std::ceil(std::log(std::max(1.0f,needed))/std::log(1.08f)));
                    float maximum=std::min(extent*.45f,extent/float(params.meta.x-1)*float(detailGrid-1)*.45f);detailCoverageLimited=detailRadius>maximum;
                    detailScreen={pixel.x,pixel.y,detailScreenRadius,1};
                }
            }
            surface.scaleCue=walkActive&&walkGrid?glm::vec4(walkPos.x,walkPos.y,walkPos.z,1):glm::vec4(0);detailSurface.scaleCue=surface.scaleCue;surface.screenFocus=detailScreen;surface.viewportSize={float(w),float(h),0,0};detailSurface.screenFocus=detailScreen;detailSurface.viewportSize=surface.viewportSize;
            glm::uvec4 regionMask{};
            if(editor.terrainSelection>=0&&(editor.highlightTerrain||editor.isolateTerrain)){
                int selected=editor.terrainSelection;auto familyMatches=[&](uint32_t family){return selected==3?(family==3||family==4):selected==6?(family==6||family==8):selected==9?(family>=9&&family<=11):family==uint32_t(selected);};
                for(uint32_t choice=0;choice<std::min(64u,graphSeeds.mixMeta.x);++choice){auto node=graphSeeds.choices[choice];if(familyMatches(node.y)||(node.w&&familyMatches(node.z)))regionMask[choice/32]|=1u<<(choice%32);}
                regionMask.z=(editor.highlightTerrain?1u:0u)|(editor.isolateTerrain?2u:0u);regionMask.w=uint32_t(selected);
            }
            if(biomeTool&&biomeHighlight&&biomeResult&&params.meta.z==5)regionMask={0,0,1,uint32_t(200+biomeChoice)};
            surface.biomeSearchArea={biomeParams.domain.y,biomeParams.domain.z,biomeParams.domain.x*.5f,1};detailSurface.biomeSearchArea=surface.biomeSearchArea;
            surface.terrainMask=regionMask;detailSurface.terrainMask=regionMask;
            float az=glm::radians(sunAzimuth),el=glm::radians(sunElevation);surface.sun={std::sin(az)*std::cos(el),std::sin(el),std::cos(az)*std::cos(el),sunIntensity};surface.sunShadow={sunShadowStrength,sunShadowRange,sunShadowSoftness,sunShadows?1.0f:0.0f};detailSurface.sun=surface.sun;detailSurface.sunShadow=surface.sunShadow;
            surface.effects={aoIntensity,shadowIntensity,options.shadowTime>=0?options.shadowTime:static_cast<float>(elapsed),shadowSpeed};detailSurface.effects=surface.effects;
            if(biomeSearchRequested){biomeContentFingerprint=editor.document.data.dump()+std::to_string(worldSeed)+":"+std::to_string(biomeModMask)+":"+std::to_string(ftfVersionIndex);biomeSearchRequested=false;try{
                if(!biomeInitialized){biomeSurface.initialize(&surface);biomeInitialized=true;}
                biomeParams=params;biomeParams.meta.x=biomeSearchGrid;biomeParams.domain.x=std::clamp(biomeRadius,16.0f,10000000.0f)*2;biomeRadius=biomeParams.domain.x*.5f;
                biomeSurface.ensure(biomeSearchGrid,1,1);biomeRivers.update(editor.document.data,worldSeed,graphSeeds,biomeParams,!options.noRivers);biomeSurface.updateRivers(biomeRivers.buffer);
                biomeSurface.record(cmd,fi,biomeParams,graphSeeds,draw,++biomeRevision,nullptr,nullptr,true);biomeSearchWaiting=true;
            }catch(const std::exception& e){biomeSearchStatus=e.what();}}
            else if(censusRequested){censusRequested=false;censusFingerprint=editor.document.data.dump()+std::to_string(worldSeed)+":"+std::to_string(biomeModMask)+":"+std::to_string(ftfVersionIndex)+":"+std::to_string(params.domain.x)+","+std::to_string(params.domain.y)+","+std::to_string(params.domain.z);try{
                if(!biomeInitialized){biomeSurface.initialize(&surface);biomeInitialized=true;}
                censusParams=params;censusParams.meta.x=uint32_t(censusGrid);censusPossible=possibleBiomes(biomeTableWords,biomeModMask);
                biomeSurface.ensure(censusGrid,1,1);biomeRivers.update(editor.document.data,worldSeed,graphSeeds,censusParams,!options.noRivers);biomeSurface.updateRivers(biomeRivers.buffer);
                biomeSurface.record(cmd,fi,censusParams,graphSeeds,draw,++biomeRevision,nullptr,nullptr,true);censusWaiting=true;
            }catch(const std::exception& e){censusStatus=e.what();}}
            DrawParams localDraw=draw;bool renderDetail=false;middleActive=false;middleSurface.finerBand=nullptr;detailSurface.detailHole={};TerrainParams treeDetailLocal{};
            if(detailPlaced&&(detailAuto||!editPreview.busy())&&(!screenDetail||detailScreen.w>0)){   /* adaptive detail stays on while values change (a lighter grid until they settle) */
                // Keep the local grid genuinely denser than the overview, including at 8K.
                float maxRadius=extent/float(params.meta.x-1)*float(detailGrid-1)*.45f;
                detailRenderRadius=std::clamp(detailRadius,1.0f,std::max(1.0f,std::min(extent*.45f,maxRadius)));detailCoverageLimited=detailRenderRadius<detailRadius;
                TerrainParams local=params;local.meta.x=detailGrid;local.domain={detailRenderRadius*2,detailCenter.x,detailCenter.y,0};
                // Automatic zoom detail gets an intermediate ring at twice the sample spacing (same grid over twice the width): patch > ring > six bands.
                auto ringFor=[&](const TerrainParams& l,TerrainParams& m){if(!detailAuto||!(l.domain.x*finerRingScale*.5f<extent*.48f&&(l.domain.x*finerRingScale)/float(std::max(128u,l.meta.x)-1)<extent/float(std::max(2u,surface.resolution-1))/1.12f))return false;m=l;m.meta.x=std::max(128u,l.meta.x);m.domain.x=l.domain.x*finerRingScale;return true;};
                const bool editingNow=elapsed-detailLastEdit<.20;bool displayOnly=false;
                try{
                    const bool closeFirst=finerDragMode==0;   /* slider drags: the patch and ring at full detail first (newest values), the rest of the world after */
                    if(detailAuto&&(closeFirst||!editingNow)&&local.meta.x>256){   /* background job toward the wanted full-resolution patch and ring */
                        // The job follows the newest values (the edit preview's latest snapshot), not the coarse display snapshot drawn meanwhile.
                        const bool latestKnown=editPreview.latest.revision!=0;TerrainParams values=latestKnown?editPreview.latest.params:params;values.meta.x=local.meta.x;values.domain=local.domain;
                        FinerKey want{values,{},false,latestKnown?editPreview.latest.target:revision};want.midActive=ringFor(values,want.mid);
                        if(!(frontFull&&sameKey(frontKey,want))){
                            // Values changed while a job runs: let it finish and swap in, then start one with the newest values (a drag never restarts it forever).
                            const bool coalesce=closeFirst&&backJob&&sameGeometry(backKey,want);
                            if(!backJob||(!sameKey(backKey,want)&&!coalesce)){
                                if(!backInitialized){detailBack.initialize(&surface);middleBack.initialize(&surface);backInitialized=true;}
                                vkCheck(vkDeviceWaitIdle(gpu.device));detailBack.cancelGeneration();middleBack.cancelGeneration();   /* rare: a new target; back descriptor sets may still be in flight */
                                detailBack.ensure(want.local.meta.x,1,1);if(want.midActive)middleBack.ensure(want.mid.meta.x,1,1);
                                backSeeds=latestKnown?editPreview.latest.seeds:graphSeeds;
                                if(want.local.meta.z==5){stallScope("detail river networks (CPU)",[&]{detailRiversBack.update(editor.document.data,worldSeed,backSeeds,want.local,!options.noRivers);detailBack.updateRivers(detailRiversBack.buffer);
                                    if(want.midActive){middleRiversBack.update(editor.document.data,worldSeed,backSeeds,want.mid,!options.noRivers);middleBack.updateRivers(middleRiversBack.buffer);}});}
                                if(want.midActive)middleBack.bindCoarse(surface);detailBack.bindCoarse(want.midActive?middleBack:surface);   /* the swap carries these bindings to the front pair */
                                backKey=want;backJob=true;swapReady=false;++backRevision;finerJobStart=elapsed;finerJobFrame=frameNumber;}
                            for(Surface* b:{&detailBack,&middleBack}){b->progressiveGeneration=true;b->generationBudgetMs=std::clamp(.72f*1000.0f/float(fpsLimit>0?fpsLimit:60),6.0f,40.0f);   /* ~12 ms at 60 fps; a lower FPS lock gives terrain more time */b->profileSteps=surface.profileSteps;b->biomeBandGroups=options.biomeBand;b->effects=surface.effects;b->sun=surface.sun;b->sunShadow=surface.sunShadow;b->terrainMask=surface.terrainMask;b->biomeSearchArea=surface.biomeSearchArea;b->scaleCue=surface.scaleCue;b->viewportSize=surface.viewportSize;}
                            if(detailBack.fieldRevision!=backRevision)detailBack.record(cmd,fi,backKey.local,backSeeds,draw,backRevision,nullptr,nullptr,true);
                            else if(backKey.midActive&&middleBack.fieldRevision!=backRevision)middleBack.record(cmd,fi,backKey.mid,backSeeds,draw,backRevision,nullptr,nullptr,true);
                            swapReady=detailBack.fieldRevision==backRevision&&(!backKey.midActive||middleBack.fieldRevision==backRevision);
                            float done=detailBack.fieldRevision==backRevision?(backKey.midActive?.5f+.5f*middleBack.generationProgress():1.0f):(backKey.midActive?.5f:1.0f)*detailBack.generationProgress();
                            finerProgress=.5f+.5f*done;finerLabel="Finer detail: building in the background";
                        }else backJob=false;
                        if(frontFull){displayOnly=true;local=frontKey.local;}   /* keep showing the last complete patch meanwhile */
                        else local.meta.x=256;   /* nothing complete yet: a quick 256 patch now, the full one follows */
                    }else if(editingNow&&detailGrid>256){local.meta.x=uint32_t(finerDragMode==2?detailGrid:std::min(detailGrid,finerEditGrid));backJob=false;}   /* while dragging: the chosen preview size, or full detail live */
                    if(!displayOnly)frontFull=false;
                    detailRenderRadius=local.domain.x*.5f;
                    if(!detailInitialized){detailSurface.initialize(&surface);detailInitialized=true;}
                    if(!displayOnly){detailSurface.progressiveGeneration=false;detailSurface.ensure(local.meta.x,1,1);
                        if(detailBaseRevision!=revision||std::memcmp(&local,&previousDetail,sizeof(local))){++detailRevision;detailBaseRevision=revision;previousDetail=local;}
                        if(params.meta.z==5){stallScope("detail river networks (CPU)",[&]{detailRivers.update(editor.document.data,worldSeed,graphSeeds,local,!options.noRivers);});stallScope("detail river upload",[&]{if(detailSurface.updateRivers(detailRivers.buffer))++detailRevision;});}}
                    TerrainParams mid{};bool ringOn=displayOnly?frontKey.midActive:ringFor(local,mid);if(displayOnly&&ringOn)mid=frontKey.mid;
                    if(ringOn){
                        if(!middleInitialized){middleSurface.initialize(&surface);middleInitialized=true;}
                        if(!displayOnly){middleSurface.progressiveGeneration=false;middleSurface.ensure(mid.meta.x,1,1);
                            if(middleBaseRevision!=revision||std::memcmp(&mid,&middlePrevious,sizeof(mid))){middlePrevious=mid;middleBaseRevision=revision;++middleRevision;}
                            stallScope("middle river networks (CPU)",[&]{middleRivers.update(editor.document.data,worldSeed,graphSeeds,mid,!options.noRivers);});stallScope("middle river upload",[&]{if(middleSurface.updateRivers(middleRivers.buffer))++middleRevision;});}
                        if(middleBoundGrid!=surface.resolution||boundCoarseGrid!=middleSurface.resolution||boundDetailGrid!=detailSurface.resolution){vkCheck(vkDeviceWaitIdle(gpu.device));middleSurface.bindCoarse(surface);detailSurface.bindCoarse(middleSurface);middleBoundGrid=surface.resolution;boundCoarseGrid=middleSurface.resolution;boundDetailGrid=detailSurface.resolution;}
                        middleDraw=draw;middleDraw.view.x=mid.domain.x;middleDraw.meta.x=mid.meta.x;middleDraw.limits.z=mid.domain.y;middleDraw.limits.w=mid.domain.z;
                        auto offset=glm::vec2(mid.domain.y,mid.domain.z)-originXZ;middleDraw.focus={offset.x,offset.y,extent,mid.domain.x*.5f};
                        middleSurface.effects=surface.effects;middleSurface.sun=surface.sun;middleSurface.sunShadow=surface.sunShadow;middleSurface.terrainMask=surface.terrainMask;middleSurface.biomeSearchArea=surface.biomeSearchArea;middleSurface.scaleCue=surface.scaleCue;middleSurface.viewportSize=surface.viewportSize;middleSurface.screenFocus={};middleSurface.detailHole={local.domain.y,local.domain.z,detailRenderRadius,0};   /* the hole follows the patch actually drawn (the last complete one while a new one builds), else the gap shows sky */
                        middleSurface.frustumCull=frustumCulling;middleSurface.backfaceCull=backfaceCulling;middleSurface.distanceLod=false;middleSurface.lodCamera={0,0};middleSurface.lodBandSize=mid.domain.x*.175f;
                        middleSurface.record(cmd,fi,mid,graphSeeds,middleDraw,middleRevision,nullptr,nullptr,true);middleActive=true;detailSurface.detailHole.w=extent;
                    }else if(boundCoarseGrid!=surface.resolution||boundDetailGrid!=detailSurface.resolution||middleBoundGrid){vkCheck(vkDeviceWaitIdle(gpu.device));detailSurface.bindCoarse(surface);boundCoarseGrid=surface.resolution;boundDetailGrid=detailSurface.resolution;middleBoundGrid=0;}
                    localDraw.view.x=local.domain.x;localDraw.meta.x=local.meta.x;localDraw.limits.z=local.domain.y;localDraw.limits.w=local.domain.z;
                    glm::vec2 offset=glm::vec2(local.domain.y,local.domain.z)-glm::vec2(params.domain.y,params.domain.z);
                    localDraw.focus={offset.x,offset.y,middleActive?middlePrevious.domain.x:extent,detailRenderRadius};
                    detailSurface.frustumCull=frustumCulling;detailSurface.backfaceCull=backfaceCulling;detailSurface.distanceLod=adaptiveDetailMesh&&!detailAuto;detailSurface.lodCamera={0,0};detailSurface.lodBandSize=detailRenderRadius*.35f;
                    detailSurface.record(cmd,fi,local,graphSeeds,localDraw,detailRevision,nullptr,nullptr,true);
                    if(walkActive&&detailAuto&&local.meta.x>=129&&detailSurface.resolution==local.meta.x){   /* collision: a 129 x 129 window of the patch under the player (the whole 2048 patch took ~360 ms to read back) */
                        const uint32_t G=local.meta.x;const float spacing=local.domain.x/float(G-1);glm::vec2 here{walkPos.x,walkPos.z};
                        if(heightCopyRevision!=detailRevision||glm::length(here-patchWindowCenter)>spacing*24){
                            glm::vec2 uv=(here-glm::vec2(local.domain.y,local.domain.z))/local.domain.x+.5f;glm::ivec2 c=glm::clamp(glm::ivec2(glm::round(uv*float(G-1))),glm::ivec2(64),glm::ivec2(int(G)-65));
                            TerrainParams window=local;window.meta.x=129;window.domain.x=spacing*128;window.domain.y=local.domain.y+(float(c.x)/float(G-1)-.5f)*local.domain.x;window.domain.z=local.domain.z+(float(c.y)/float(G-1)-.5f)*local.domain.x;
                            detailSurface.recordHeightCopy(cmd,fi,window,detailRevision,c-64,129);heightCopyRevision=detailRevision;patchWindowCenter={window.domain.y,window.domain.z};}}
                    if(middleActive){middleSurface.finerBand=&detailSurface;middleSurface.finerBandDraw=localDraw;}
                    draw.focus={offset.x,offset.y,extent,-(middleActive?middlePrevious.domain.x*.5f:detailRenderRadius)};   /* the base hole matches the ring that is drawn (ring width slider) */renderDetail=true;treeDetailLocal=local;
                    if(detailAuto&&finerProgress<0&&(detailSurface.fieldRevision!=detailRevision||(middleActive&&middleSurface.fieldRevision!=middleRevision))){finerProgress=detailSurface.fieldRevision!=detailRevision?.75f:.9f;finerLabel="Finer detail: generating";}
                }catch(const std::exception& error){status=error.what();detailPlaced=false;}
            }
            overlay->PushClipRect(corner,{corner.x+size.x,corner.y+size.y},true);
            auto projectWorld=[&](glm::vec3 world,ImVec2& pixel){auto clip=draw.vp*glm::vec4(world.x-params.domain.y,world.y*exaggeration,world.z-params.domain.z,1);if(clip.w<=0)return false;auto p=(glm::vec2(clip)/clip.w*.5f+.5f)*glm::vec2(size.x,size.y);pixel={corner.x+p.x,corner.y+p.y};return std::isfinite(pixel.x)&&std::isfinite(pixel.y);};
            if(biomeTool&&biomeResult&&std::abs(biomeResult->extra.x-params.domain.y)<=extent*.5f&&std::abs(biomeResult->extra.y-params.domain.z)<=extent*.5f){ImVec2 p;if(projectWorld({biomeResult->extra.x,biomeResult->values.x,biomeResult->extra.y},p)){overlay->AddCircle(p,18,IM_COL32(255,210,60,255),32,3);overlay->AddText({p.x+22,p.y},IM_COL32(255,230,120,255),"Biome found");}}
            if(editor.document.loaded()&&(params.meta.z==5||params.meta.z==4||params.meta.z==2)){auto spawn=editor.document.spawn();float ground=0;ImVec2 p;if(sampleGround(spawn,ground)&&projectWorld({spawn.x,ground,spawn.y},p))drawPlayerIcon(overlay,p,1.5f,"Spawn");}
            if(walkActive){ImVec2 mid{corner.x+size.x*.5f,corner.y+size.y*.5f};overlay->AddLine({mid.x-9,mid.y},{mid.x+9,mid.y},IM_COL32(255,255,255,220),2);overlay->AddLine({mid.x,mid.y-9},{mid.x,mid.y+9},IM_COL32(255,255,255,220),2);
                char line[200];std::snprintf(line,sizeof(line),"Walk  X %.1f  Y %.1f  Z %.1f  |  %s%s",walkPos.x,walkPos.y,walkPos.z,walkFlying?"flying":walkInWater?"swimming":walkOnGround?"on ground":"airborne",walkMouseCaptured?"":"  |  mouse released: click the view");
                auto textSize=ImGui::CalcTextSize(line);ImVec2 at{corner.x+size.x*.5f-textSize.x*.5f,corner.y+size.y-46};overlay->AddRectFilled({at.x-8,at.y-4},{at.x+textSize.x+8,at.y+textSize.y+4},IM_COL32(11,19,25,225),6);overlay->AddText(at,IM_COL32(236,244,240,255),line);}
            for(int i=0;i<int(points.size());++i){auto& w=points[i];glm::vec3 position=w.p;float ground=0;if(sampleGround({w.p.x,w.p.z},ground))position.y=ground;ImVec2 p;if(projectWorld(position,p))drawPinIcon(overlay,p,w.color,11,w.name.c_str(),i==highlightedPoint);}
            if(elapsed-copiedAt<1.6&&!copiedText.empty()){std::string text="Copied "+copiedText;ImVec2 at{io.MousePos.x+16,io.MousePos.y+14},extentText=ImGui::CalcTextSize(text.c_str());overlay->AddRectFilled({at.x-6,at.y-4},{at.x+extentText.x+6,at.y+extentText.y+4},IM_COL32(11,19,25,230),5);overlay->AddText(at,IM_COL32(120,230,160,255),text.c_str());}
            if(!measurePoints.empty()){
                ImVec2 last{};bool haveLast=false;for(size_t i=0;i<measurePoints.size();++i){ImVec2 p;if(projectWorld(measurePoints[i],p)){if(haveLast)overlay->AddLine(last,p,IM_COL32(255,215,80,255),2.5f);overlay->AddCircleFilled(p,4,IM_COL32(255,235,150,255));char n[24];std::snprintf(n,sizeof(n),"%zu",i+1);overlay->AddText({p.x+7,p.y-14},IM_COL32(255,240,200,255),n);last=p;haveLast=true;}else haveLast=false;}
                ImVec2 card{corner.x+14,corner.y+size.y-139};overlay->AddRectFilled(card,{card.x+420,card.y+126},IM_COL32(11,19,25,238),7);char text[160];float y=card.y+10;std::snprintf(text,sizeof(text),"Measure [M] | %zu points | blocks (~metres)",measurePoints.size());overlay->AddText({card.x+12,y},IM_COL32(255,225,132,255),text);y+=22;
                for(auto entry:{std::pair<const char*,double>{"Horizontal",measureHorizontal},{"Direct 3D",measureDirect},{"Surface (sampled)",measureSurface}}){std::snprintf(text,sizeof(text),"%s: %.2f",entry.first,entry.second);overlay->AddText({card.x+12,y},IM_COL32(216,234,228,255),text);y+=20;}overlay->AddText({card.x+12,y},IM_COL32(148,181,172,255),"Backspace: remove last | Enter: finish | Clear: right panel");
            }
            if(editor.highlightTerrain||editor.isolateTerrain){char text[128];std::snprintf(text,sizeof(text),"Terrain filter: %s | %s | view only",terrainTypeName(editor.terrainSelection),editor.isolateTerrain?"isolated":"highlighted");overlay->AddText({corner.x+80,corner.y+145},IM_COL32(255,207,93,255),text);}
            overlay->PopClipRect();
            toolHint=walkPickTool?"Walk: click the terrain where you want to drop in | Esc cancel":copyTool?"Copy coordinates [K]: LMB copy X Y Z of the clicked spot | MMB orbit | RMB pan | Esc navigate":pointTool?"Points [B]: LMB pin a point | click a name in the right panel to centre on it | MMB orbit | Esc navigate":biomeTool?"Biome finder [N]: search name, radius and quality in the right panel | Esc navigate":displayPopupActive?"Viewport display: Z layers | Shift+Z biome and block colours | L six-band LOD | Esc close":detailTool?"High detail [H]: LMB place/move | Ctrl+wheel world radius | H disable | Shift+H settings | MMB orbit | R reset":spawnTool?"Set spawn [P]: LMB place preset spawn | MMB orbit | RMB pan | Esc navigate":measureTool?"Measure [M]: LMB add point | Backspace remove | Enter finish | Clear: right panel | MMB orbit":centerTool?"Center camera [C]: LMB set orbit center | MMB orbit | RMB pan | Wheel zoom | R reset":"Navigate [Esc]: LMB/MMB orbit | RMB pan | Ctrl+Shift+RMB turn sun | Wheel zoom | C center | P spawn | M measure | B points | N biome finder | Ctrl+Z undo | Ctrl+Shift+Z redo | R reset";
            if(walkActive){toolHint=walkMouseCaptured?"Walk [G]: WASD move | Space jump (double-tap: fly) | Ctrl sprint | Shift sneak/down | mouse look | Ctrl+Z undo, Ctrl+S save | Esc or G exit":"Walk [G]: mouse released - click the view to look again | Esc or G exit";}
            else if(flyActive){char t[300];std::snprintf(t,sizeof(t),"Fly camera [F]: RMB look | WASD move | Q/E down/up | Wheel speed (%.0f blocks/s) | Shift fast, Ctrl slow | Ctrl+Z undo, Ctrl+S save | Esc or F exit",flySpeed);toolHint=t;}
            ImGui::SetNextWindowPos({vp->Pos.x,vp->Pos.y+vp->Size.y-28});ImGui::SetNextWindowSize({vp->Size.x,28});ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12,4});ImGui::Begin("Tool shortcut hints",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoSavedSettings);ImGui::TextUnformatted(toolHint.c_str());ImGui::End();ImGui::PopStyleVar();
            {   // Preview trees: only when they can be seen, and not while settings are being changed.
                if(revision!=treeLastRevision){treeLastRevision=revision;treesResumeAt=elapsed+treePauseDelay;}
                if(treesPause&&(ImGui::IsAnyItemActive()||animate))treesResumeAt=elapsed+treePauseDelay;
                if(treesHideMoving&&elapsed-cameraMovedAt<.05)treesResumeAt=std::max(treesResumeAt,elapsed+treePauseDelay); // trees step aside while the camera moves
                glm::vec2 focus=walkActive?glm::vec2(walkPos.x,walkPos.z):flyActive?glm::vec2(flyPos.x+params.domain.y,flyPos.z+params.domain.z):glm::vec2(pan.x+params.domain.y,pan.y+params.domain.z);
                float cameraReach=walkActive?0.0f:flyActive?std::max(0.0f,flyPos.y):params.domain.x*.83f*distance;
                treesTooFar=!wholeMapTrees&&cameraReach>std::max(3000.0f,treeRadius*4);treesPausedNow=(treesPause||treesHideMoving)&&elapsed<treesResumeAt;
                surface.trees.enabled=treesEnabled&&surface.treesAvailable()&&params.meta.z==5&&layer==0&&!treesTooFar&&!treesPausedNow&&treeMask!=0;
                surface.trees.camera=cameraGround+originXZ;{   // Entire-map mode follows the view: the window is centred on what the camera looks at and sized to the visible range (instances are real-size, so a bigger window only thins them)
                    float lookDistance=walkActive?0.0f:flyActive?std::max(0.0f,flyPos.y)*2.0f:params.domain.x*.83f*distance;
                    surface.trees.center=wholeMapTrees?focus:focus;surface.trees.radius=wholeMapTrees?std::clamp(std::max(treeRadius,lookDistance*1.5f),treeRadius,params.domain.x*.72f):treeRadius;
                    glm::vec2 heading=walkActive?glm::vec2(std::sin(walkYaw),std::cos(walkYaw)):flyActive?glm::vec2(std::sin(flyYaw),std::cos(flyYaw)):glm::vec2(-std::sin(yaw),-std::cos(yaw));
                    float horizontal=walkActive||flyActive?1.0f:std::cos(pitch);surface.trees.viewTest=frustumCulling&&!topDown&&horizontal>.35f;
                    {float vfov=glm::radians(cameraFov),aspect=std::max(1.0f,io.DisplaySize.x)/std::max(1.0f,io.DisplaySize.y);float halfH=std::atan(std::tan(vfov*.5f)*aspect);surface.trees.viewCos=std::cos(std::min(3.0f,halfH+.35f));} /* cone = the real horizontal view plus a margin */surface.trees.viewDirection=glm::length(heading)>1e-4f?glm::normalize(heading):glm::vec2(0,1);}surface.trees.wholeMap=wholeMapTrees;surface.treeFrustumCull=frustumCulling;surface.trees.density=treeDensity;surface.trees.lodDistance=treeLod;surface.trees.typeMask=treeMask;
                surface.treeDetail=renderDetail?&detailSurface:nullptr;surface.treeRing=renderDetail&&middleActive?&middleSurface:nullptr;surface.treeRingParams=middlePrevious;surface.treeDetailRadius=renderDetail?detailRenderRadius:0.0f;surface.treeDetailParams=treeDetailLocal;
            }
            surface.frustumCull=frustumCulling;surface.distanceCull=distanceCulling;surface.maxDrawDistance=std::max(1.0f,cullDistance);surface.backfaceCull=backfaceCulling;
            renderedThisFrame=surface.resolution;
            editPreview.hold=backJob&&finerDragMode==0;   /* close bands first: the full-resolution base refinement waits for the finer job */
            editPreview.worker.profileSteps=surface.profileSteps;editPreview.worker.biomeBandGroups=options.biomeBand;editPreview.record(cmd,fi,draw);
            if(walkActive&&surface.fieldRevision&&params.meta.x>=65&&surface.resolution==params.meta.x){   // keep a 65 x 65 window of the whole-world grid under the player for collision
                const uint32_t G=params.meta.x;const float spacing=params.domain.x/float(G-1);glm::vec2 here{walkPos.x,walkPos.z};
                if(surface.fieldRevision!=baseWalkFieldRevision||glm::length(here-baseWalkCenter)>spacing*12){
                    glm::vec2 uv=(here-glm::vec2(params.domain.y,params.domain.z))/params.domain.x+.5f;glm::ivec2 c=glm::clamp(glm::ivec2(glm::round(uv*float(G-1))),glm::ivec2(32),glm::ivec2(int(G)-33));
                    TerrainParams window=params;window.meta.x=65;window.domain.x=spacing*64;window.domain.y=params.domain.y+(float(c.x)/float(G-1)-.5f)*params.domain.x;window.domain.z=params.domain.z+(float(c.y)/float(G-1)-.5f)*params.domain.x;
                    surface.recordHeightCopy(cmd,fi,window,++baseWalkCopies,c-32,65);baseWalkFieldRevision=surface.fieldRevision;baseWalkCenter={window.domain.y,window.domain.z};}}
            surface.record(cmd,fi,params,graphSeeds,draw,revision,renderDetail?(middleActive?&middleSurface:&detailSurface):nullptr,renderDetail?(middleActive?&middleDraw:&localDraw):nullptr);renderSnapshot.restore();ImGui::Render();gpu.end(ImGui::GetDrawData());lastSubmitted=fi;
            if(swapReady&&backJob){   /* the background patch and ring are complete: swap them in (bindings travel with the descriptor sets) */
                if(!detailInitialized){detailSurface.initialize(&surface);detailInitialized=true;}if(backKey.midActive&&!middleInitialized){middleSurface.initialize(&surface);middleInitialized=true;}   /* a never-initialized front would hand the back surface null buffers (crash in the next background job) */
                detailSurface.exchangeTerrain(detailBack);if(backKey.midActive)middleSurface.exchangeTerrain(middleBack);
                frontKey=backKey;frontFull=true;backJob=swapReady=false;if(finerJobs.size()<64)finerJobs.push_back({{"seconds",elapsed-finerJobStart},{"frames",frameNumber-finerJobFrame},{"grid",backKey.local.meta.x},{"ring",backKey.midActive?backKey.mid.meta.x:0u}});detailRevision=detailSurface.fieldRevision;previousDetail=frontKey.local;detailBaseRevision=frontKey.base;
                if(frontKey.midActive){middleRevision=middleSurface.fieldRevision;middlePrevious=frontKey.mid;middleBaseRevision=frontKey.base;middleBoundGrid=surface.resolution;boundCoarseGrid=middleSurface.resolution;}else{middleBoundGrid=0;boundCoarseGrid=surface.resolution;}
                boundDetailGrid=detailSurface.resolution;heightCopyRevision=0;}
            if(editPreview.promote()){if(backJob){vkCheck(vkDeviceWaitIdle(gpu.device));if(backKey.midActive)middleBack.bindCoarse(surface);else detailBack.bindCoarse(surface);}   /* the base field moved: the job's ring (or patch) blends against the new one */
                boundCoarseGrid=boundDetailGrid=middleBoundGrid=0;baseHeightRevision=detailHeightRevision=walkHeightsRevision=heightCopyRevision=0;treeLastRevision=0;}
            if(!options.previewTrace.empty())previewTrace.push_back({{"frame",frameNumber},{"time",elapsed},{"frame_ms",dt*1000},{"draw_resolution",renderedThisFrame},{"target_resolution",requestedResolution},{"target_revision",revision},{"display_target",editPreview.display.target},{"busy",editPreview.busy()},{"job",editPreview.job},{"progress",editPreview.progress()},{"finer_job",backJob},{"finer_front",frontFull},{"detail_placed",detailPlaced},{"finer_progress",finerProgress},{"finer_dispatches",backInitialized?detailBack.generationDispatches+middleBack.generationDispatches:0}});
            editor.document.previewExtent=params.domain.x;
            if(persistentSession&&elapsed-sessionTick>=1){try{project.tick(editor.document,worldSeed,captureView(),elapsed);sessionTick=elapsed;}catch(const std::exception& e){editor.status=e.what();}}
            if(!options.motionStart.empty()){if(frameNumber==1)surface.screenshot(fi,options.motionStart);std::this_thread::sleep_for(std::chrono::milliseconds(30));}
            if(options.exerciseSpawnHistory&&frameNumber==15)observedSpawnUndo=editor.document.spawn()==initialSpawn;
            if(frameNumber>40)frameTimes.push_back(dt*1000);++frameNumber;
            if(!options.benchmark&&!options.hidden&&fpsLimit>0)std::this_thread::sleep_until(start+std::chrono::microseconds(1000000/fpsLimit));
        }
        vkCheck(vkDeviceWaitIdle(gpu.device));
        if(!options.previewTrace.empty()){auto path=std::filesystem::path(options.previewTrace);if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());std::ofstream(path)<<previewTrace.dump(2);}
        if(!options.exportPreset.empty())editor.document.exportTo(options.exportPreset);
        if(persistentSession)try{project.tick(editor.document,worldSeed,captureView(),elapsed,true);}catch(const std::exception& e){std::cerr<<"Recovery save failed: "<<e.what()<<"\n";}
        if(!options.exportDatapack.empty()){editor.document.worldSeed=worldSeed;editor.document.exportDatapack(options.exportDatapack,appRoot/"assets"/"ftf-datapack-template.zip");}
        if(!options.screenshot.empty())surface.screenshot(lastSubmitted,options.screenshot);
        if(!options.uiScreenshot.empty())gpu.screenshotUi(ImGui::GetDrawData(),options.uiScreenshot);
        if(!options.samples.empty()){
            auto samples=surface.samples(lastSubmitted);if(!std::all_of(samples.begin(),samples.end(),[](auto& s){return std::isfinite(s.field[0]);}))throw std::runtime_error("Non-finite terrain output");
            auto file=std::filesystem::path(options.samples);if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());
            std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(samples.data()),samples.size()*sizeof(TerrainSample));
        }
        if(detailInitialized&&!options.detailSamples.empty()){auto values=detailSurface.samples(lastSubmitted);std::filesystem::path file(options.detailSamples);if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());std::ofstream output(file,std::ios::binary);output.write(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(TerrainSample));}
        if(!options.cellSamples.empty())surface.exportCells(options.cellSamples);if(!options.biomeSamples.empty())surface.exportCells(options.biomeSamples,true);
        if(!options.riverInputs.empty()){auto file=std::filesystem::path(options.riverInputs);if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(riverNetworks.buffer.data()),riverNetworks.buffer.size()*sizeof(uint32_t));}
        if(!options.inputs.empty()){auto file=std::filesystem::path(options.inputs);if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(&graphSeeds),sizeof(graphSeeds));}
        const char* profileNames[]={"plains","steppe","dales","hills1","hills2","plateau","badlands","torridonian"};
        RECT chromeClient{};GetClientRect(glfwGetWin32Window(window),&chromeClient);POINT dragPoint{LONG((chrome.drag.left+chrome.drag.right)/2),15},edgePoint{1,150};ClientToScreen(glfwGetWin32Window(window),&dragPoint);ClientToScreen(glfwGetWin32Window(window),&edgePoint);auto dragHit=SendMessageW(glfwGetWin32Window(window),WM_NCHITTEST,0,MAKELPARAM(dragPoint.x,dragPoint.y)),edgeHit=SendMessageW(glfwGetWin32Window(window),WM_NCHITTEST,0,MAKELPARAM(edgePoint.x,edgePoint.y));
        Json report={{"shortcut_undo",shortcutUndoObserved},{"settings_reset",resetObserved},{"timing_scope","Generation timings include all compute/filter phases plus background refinement; draw is visible rendering. CPU frame intervals include waits/CPU work."},{"window_controls",{{"native_caption",bool(GetWindowLongPtrW(glfwGetWin32Window(window),GWL_STYLE)&WS_CAPTION)},{"colored_buttons",3},{"caption_drag_hit",dragHit==HTCAPTION},{"edge_resize_hit",edgeHit==HTLEFT}}},{"editing_preview",{{"enabled",editPreview.enabled},{"target_resolution",requestedResolution},{"actual_resolution",surface.resolution},{"busy",editPreview.busy()},{"status",editPreview.status()},{"promotions",editPreview.promotions},{"coarse_updates",editPreview.coarseUpdates},{"worker_cancellations",editPreview.worker.cancelledGenerations},{"cancel_exercised",refineCancelExercised},{"worker_progress",editPreview.progress()}}},{"selective_updates",{{"enabled",surface.selectiveGeneration},{"last_stage",generationStageName(surface.lastGenerationStage)},{"terrain_runs",surface.stageRuns[0]+editPreview.worker.stageRuns[0]},{"climate_runs",surface.stageRuns[1]+editPreview.worker.stageRuns[1]},{"filter_runs",surface.stageRuns[2]+editPreview.worker.stageRuns[2]},{"biome_runs",surface.stageRuns[3]+editPreview.worker.stageRuns[3]},{"material_runs",surface.stageRuns[4]+editPreview.worker.stageRuns[4]}}},{"application","FTF Surface Lab"},{"biome_mod_mask",biomeModMask},{"panels",{{"docked",true},{"observed_hide",observedPanelHide},{"left_width",dock.leftWidth},{"actions_hidden",dock.bottomHidden},{"left_collapsed",leftCollapsed},{"right_collapsed",rightCollapsed},{"viewport_collapsed",viewportCollapsed},{"right_size",{rightPanelSize.x,rightPanelSize.y}}}},{"save_as_popup_observed",saveAsPopupObserved},{"saved_project",project.currentFile.string()},{"pick_check",pickCheckReport},{"copy_tool",{{"tool_on",copyTool},{"copied",copiedText},{"clipboard",[&]{const char* c=ImGui::GetClipboardText();return std::string(c?c:"");}()},{"hover_y",hover.valid?hover.position.y:0.0f},{"sun_before",sunBefore},{"sun_after",sunAzimuth},{"pan_moved",glm::length(pan-panBefore)}}},{"biome_count",[&]{PresetJson out={{"status",censusStatus},{"counted",census.has_value()}};if(census){out["generated"]=census->generated;out["possible"]=census->possible;out["grid"]=census->grid;out["world_size"]=census->worldSize;PresetJson missing=PresetJson::array(),groups=PresetJson::array();for(auto& g:census->groups){groups.push_back({{"pack",g.pack},{"generated",g.generated},{"possible",g.possible}});for(auto& name:g.missing)missing.push_back(g.pack+": "+name);}out["groups"]=groups;out["missing"]=missing;out["unreachable"]=census->unreachable;out["unexpected"]=census->unexpected;}return out;}()},{"biome_search",{{"status",biomeSearchStatus},{"found",biomeResult.has_value()},{"active",biomeTool},{"waiting",biomeSearchWaiting},{"requested",biomeSearchRequested},{"biome",biomeChoice},{"radius",biomeRadius},{"highlight",biomeHighlight}}},{"finer_jobs",finerJobs},{"intermediate_detail_band",{{"active",middleActive},{"resolution",middleActive?middlePrevious.meta.x:0},{"radius",middleActive?middlePrevious.domain.x*.5f:0}}},{"distance_lod",distanceLod},{"lod_patches",surface.lodPatches},{"drawn_triangles",surface.drawnTriangles},{"exit_prompt_observed",exitPromptObserved},{"exit_cancel_observed",exitCancelObserved},{"graphics_initial",initialGraphics},{"graphics_restore_preserved",graphicsRestoreObserved},{"fly_pose_error",poseError},{"fly_direction_error",poseDirectionError},{"camera_fov",cameraFov},{"culled_patches",surface.culledPatches},{"whole_map_trees",wholeMapTrees},{"tree_visuals","Source feature graph/height/colours; custom voxel silhouettes approximate branching"},{"camera_yaw",yaw},{"spawn_undo_observed",observedSpawnUndo},{"terrain_filter",{{"selection",editor.terrainSelection},{"highlight",editor.highlightTerrain},{"isolate",editor.isolateTerrain}}},{"last_edited_property",editor.lastEditedPath},{"pan",{pan.x,pan.y,cameraPanY}},{"spawn",{editor.document.spawn().x,editor.document.spawn().y}},{"measure_points",measurePoints.size()},{"points",[&]{auto list=Json::array();for(auto& w:points)list.push_back({{"name",w.name},{"x",w.p.x},{"y",w.p.y},{"z",w.p.z}});return list;}()},{"point_tool",pointTool},{"walk",{{"active",walkMode},{"position",{walkPos.x,walkPos.y,walkPos.z}},{"yaw",walkYaw},{"flying",walkFlying},{"on_ground",walkOnGround},{"patch_samples",walkPatch.meta.x},{"patch_auto",detailAuto},{"patch_radius",detailRenderRadius},{"patch_grid",detailGrid},{"heights",walkHeights.size()}}},{"lod_adaptive",{{"enabled",lodAdaptive},{"state",lodAdaptiveState},{"spacing",lodAdaptiveSpacing},{"finest",lodFinest},{"coarsest",lodCoarsest}}},{"ftf_version",ftfVersions[ftfVersionIndex].id},{"trees",{{"enabled",treesEnabled},{"drawn",surface.trees.enabled},{"available",surface.treesAvailable()},{"paused",treesPausedNow},{"too_far",treesTooFar},{"resume_in",treesResumeAt-elapsed},{"revision",revision},{"item_active",ImGui::IsAnyItemActive()}}},{"fly",{{"active",flyMode},{"position",{flyPos.x,flyPos.y,flyPos.z}},{"yaw",flyYaw},{"pitch",flyPitch},{"speed",flySpeed}}},{"points_roundtrip",{pointsRoundTrip[0],pointsRoundTrip[1]}},{"points_roundtrip_name",pointsRoundTripName},{"points_centered_pan",{pointsCentered.x,pointsCentered.y}},{"measure_horizontal",measureHorizontal},{"measure_direct",measureDirect},{"measure_surface",measureSurface},{"hover",{{"valid",hover.valid},{"biome",hover.biome},{"terrain",hover.terrain},{"block",hover.block},{"position",{hover.position.x,hover.position.y,hover.position.z}},{"height",hover.sample.values.x},{"biome_id",hover.sample.ids.x},{"block_id",hover.sample.ids.z}}},{"sun_azimuth",sunAzimuth},{"sun_elevation",sunElevation},{"sun_intensity",sunIntensity},{"sun_shadows",sunShadows},{"sun_shadow_strength",sunShadowStrength},{"restore_prompt",project.restorePending},{"autosave_interval_seconds",project.intervalSeconds},{"camera_pitch",pitch},{"camera_centered",cameraCentered},{"camera_distance_factor",distance},{"camera_orbit_center",{lastCameraCenter.x,lastCameraCenter.y,lastCameraCenter.z}},{"ambient_occlusion",ambientOcclusion},{"cloud_shadows",cloudShadows},{"ao_intensity",aoIntensity},{"shadow_intensity",shadowIntensity},{"shadow_speed",shadowSpeed},{"shadow_animation_seconds",surface.effects.z},{"detail",{{"placed",detailPlaced},{"tool_active",detailTool},{"center",{detailCenter.x,detailCenter.y}},{"radius",detailAuto?autoRadiusBackup:detailRadius},{"auto_patch",detailAuto},{"render_radius",detailRenderRadius},{"screen_radius_pixels",detailScreenRadius},{"screen_space",screenDetail},{"coverage_limited",detailCoverageLimited},{"resolution",detailAuto?autoGridBackup:detailGrid},{"actual_resolution",detailSurface.resolution},{"adaptive_triangles",adaptiveDetailMesh},{"drawn_triangles",detailSurface.drawnTriangles},{"revision",detailRevision},{"base_revision",revision},{"spacing",detailRadius*2/(detailGrid-1)},{"base_spacing",params.domain.x/(params.meta.x-1)}}},{"terrain_model",params.meta.z==6?"FTF continent diagnostic: edge, water table, size and identity":params.meta.z==5?"FTF surface: all four continent types, terrain/islands/rivers/world filters and Minecraft 1.21.1 surface biome selection; no blocks.":params.meta.z==4?"FTF land stage: terrain-provider heights, volcanoes, regional borders and mountain chains; continents/oceans/hydrology/world filters/final biomes excluded":params.meta.z==1?"FTF Perlin2 primitive only":params.meta.z==3?"FTF RegionModule diagnostic: identity, edge, center X/Z; no terrain selection":params.meta.z==2?"Isolated FTF terrain populator; no continent/region blending, hydrology, tile filters or verified biomes":"Validation composition using FTF Perlin2; not full FTF terrain or biome selection"},
            {"rivers_enabled",params.meta.z==5&&!options.noRivers},{"world_filters_enabled",params.meta.z==5&&!options.noWorldFilters},{"biome_selection","Minecraft 1.21.1 Overworld surface RTree + FTF cell climate/continentalness/river roles"},{"biome_color_mode",biomeColors},{"surface_materials_enabled",params.meta.z==5&&biomeColors==3&&!options.noClimate},{"block_palette_count",blockLegend["block_count"]},
            {"ftf_climate_enabled",params.meta.z==5&&!options.noClimate},{"archipelagos_enabled",params.meta.z==5&&graphSeeds.islandMeta.x!=0},
            {"river_map_count",riverNetworks.buffer.size()>=16?riverNetworks.buffer[0]:0},{"river_root_count",riverNetworks.buffer.size()>=16?riverNetworks.buffer[1]:0},{"river_segment_count",riverNetworks.buffer.size()>=16?riverNetworks.buffer[2]:0},{"wetland_count",riverNetworks.buffer.size()>=16?riverNetworks.buffer[3]:0},
            {"mix_choice_count",graphSeeds.mixMeta.x},{"mix_weighted_slots",graphSeeds.mixMeta.y},
            {"mountain_chains",(params.meta.z==4||params.meta.z==5)&&graphSeeds.mountainMeta.w!=0},
            {"uplift_continents",(params.meta.z==5||params.meta.z==6)&&(static_cast<int>(graphSeeds.continentNoise.w)&3)==0},{"continent_type",editor.document.loaded()?editor.document.data.at(PresetJson::json_pointer("/world/continent/continentType")).get<std::string>():"none"},
            {"show_ocean_floor",showOceanFloor},{"height_overflow_highlight",showHeightLimit},{"terrain_height_limit_y",terrainHeightLimit},{"terrain_height_limit_source","/world/properties/worldHeight"},{"world_origin",{params.domain.y,params.domain.z}},{"utility_menu","right"},
            {"mountain_cache_samples",surface.mountainCacheSamples},{"mountain_cache_capacity_mib",16},
            {"terrain_profile",params.meta.z==2?profileNames[params.meta.w]:"none"},
            {"gpu",gpu.properties.deviceName},{"resolution",params.meta.x},{"automatic_resolution_target_fps",targetHz},{"world_extent",params.domain.x},{"viewport",{surface.width,surface.height}},
            {"terrain_storage_mib",surface.fieldBytes/1048576.0},{"max_storage_buffer_range",gpu.properties.limits.maxStorageBufferRange},{"patch_instances",surface.patchCount},
            {"world_seed",worldSeed},{"imported_preset",editor.document.loaded()?editor.document.source.string():""},
            {"erosion_engine",params.meta.z==5&&!options.noWorldFilters?"FTF 22-Oct Phacelle + cliff relaxation":"inactive"},
            {"frames",frameNumber},{"generation_submissions",generatedFrames},{"animated_settings",animate},{"validation_layer_active",gpu.validationActive},{"validation_errors",gpu.validationErrors},
            {"mixed_resolution_exercise",options.exercise},
            {"profile_switch_exercise",options.profileExercise},
            {"gpu_samples",gpuTimes.size()},{"gpu_total_median_ms",percentile(gpuTimes,.5)},{"gpu_total_p95_ms",percentile(gpuTimes,.95)},
            {"generation_median_ms",percentile(genTimes,.5)},{"filter_median_ms",percentile(filterTimes,.5)},{"draw_median_ms",percentile(drawTimes,.5)},
            {"cache_median_ms",percentile(cacheTimes,.5)},{"world_map_median_ms",percentile(worldTimes,.5)},{"ocean_median_ms",percentile(oceanTimes,.5)},{"composition_median_ms",percentile(composeTimes,.5)},
            {"frame_median_ms",percentile(frameTimes,.5)},{"frame_p95_ms",percentile(frameTimes,.95)}};
        {Json stalls=Json::object();for(auto& entry:stallTimes){double total=0,peak=0;for(double v:entry.second){total+=v;peak=std::max(peak,v);}stalls[entry.first]={{"count",entry.second.size()},{"total_ms",total},{"max_ms",peak}};}report["stalls"]=stalls;}
        if(options.profileSteps){Json steps=Json::object();auto add=[&](const Surface& s,const char* who){for(auto& e:s.stepProfile)steps[std::string(who)+": "+e.first]={{"total_ms",e.second.first},{"count",e.second.second},{"mean_ms",e.second.first/std::max(1u,e.second.second)}};};add(surface,"visible");if(editPreview.initialized)add(editPreview.worker,"worker");if(backInitialized){add(detailBack,"finer");add(middleBack,"ring");}report["step_profile"]=steps;}
        std::cout<<report.dump(2)<<"\n";
        if(!options.report.empty()){auto file=std::filesystem::path(options.report);if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());std::ofstream out(file);out<<report.dump(2);}
        editPreview.destroy();if(backInitialized){detailBack.destroy();middleBack.destroy();}if(middleInitialized)middleSurface.destroy();if(biomeInitialized)biomeSurface.destroy();if(detailInitialized)detailSurface.destroy();surface.destroy();ImGui_ImplVulkan_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();gpu.destroy();glfwDestroyWindow(window);glfwTerminate();
        return report["validation_errors"].get<uint32_t>()?2:0;
    }catch(const std::exception& error){std::cerr<<"FTF Surface Lab: "<<error.what()<<"\n";return 1;}
}
