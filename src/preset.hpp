#pragma once
#include "surface.hpp"
#include "preview.hpp"
#include "datapack.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <set>

using PresetJson=nlohmann::ordered_json;
struct PresetDocument {
    PresetJson data;
    mutable PresetJson savedData;
    void markSaved()const{savedData=data;}
    std::filesystem::path source;
    std::string name;
    std::optional<int64_t> worldSeed;
    std::optional<float> previewExtent;
    glm::vec2 spawn()const;
    std::optional<Datapack> datapack;
    bool loaded()const{return data.is_object()&&!data.empty();}
    void load(const std::filesystem::path& path);
    void exportTo(const std::filesystem::path& path) const;
    void exportDatapack(const std::filesystem::path& path,const std::filesystem::path& fallback)const;
    void set(const std::string& pointer,const PresetJson& value);
    void apply(TerrainParams& params)const;
    uint64_t terrainSeedRoot(int64_t seed,uint32_t profile) const;
};

bool randomiseSeedButton(int64_t& value,bool worldSeed=false);

// Suggested slider limits never constrain the single-click numeric input.
bool numberControl(const char* label,float& value,float low,float high,const char* format="%.5g");

class PresetEditor {
public:
    PresetDocument document;
    std::string status;
    std::string feedback;
    bool feedbackLive{},showAllProperties=false,followEditedSetting=true,applyOnRelease=false;std::string dragPath;double dragValue=0; // applyOnRelease: sliders commit when released
    std::string lastEditedPath;
    int terrainSelection=-1;bool highlightTerrain=false,isolateTerrain=false;
    bool resetToSaved(const std::string& path,bool history=true);
    bool canReset(const std::string& path)const;
    bool placeSpawn(glm::vec2 point);
    void initialize(const std::filesystem::path& appRoot,GLFWwindow* window);
    bool toolbar(int64_t& seed,bool compact=false);
    void loadDocument(const std::filesystem::path& path,int64_t& seed);
    bool importFolder(int64_t& seed);
    void exportAction(bool pack,int64_t seed);
    void clearHistory(){past.clear();future.clear();lastEditedPath.clear();terrainSelection=-1;highlightTerrain=isolateTerrain=false;}
    void showLibrary(){managerOpen=true;}
    void dismissLibrary(){managerOpen=false;}
    bool properties(TerrainParams& params);
    // Reusable scalar editor; the property browser and UI interaction checks use the same widget.
    bool field(const std::string& path,TerrainParams& params);
    bool undo();
    bool redo();
    bool historyPanel();
    bool jumpHistory(size_t index);
    size_t historyPosition()const{return past.size();}
    size_t historySize()const{return past.size()+future.size()+1;}
private:
    PresetJson schema;
    std::filesystem::path root;
    GLFWwindow* window{};
    std::vector<PresetJson> past,future;
    bool editing{};
    std::string pendingReset;
    bool drawingProperties=false;
    bool resetControl(const std::string& path);
    bool managerOpen{};
    char libraryName[160]{},librarySearch[160]{};
    std::string librarySelection,libraryTemplateName;
    PresetJson savedLibraryData;
    std::optional<int64_t> savedLibrarySeed;
    bool libraryWindow(int64_t& seed);
    bool node(PresetJson& value,const std::string& path,const std::string& label,TerrainParams& params);
    bool hasPreview(const PresetJson& value,const std::string& path,const TerrainParams& params)const;
    PreviewBinding bindingFor(const std::string& path,const TerrainParams& params)const;
    void edited(const std::string& path,const std::string& label,const PreviewBinding& binding,TerrainParams& params);
    std::vector<std::string> keys(const PresetJson& object,const std::string& path)const;
    std::filesystem::path choose(bool saving,bool pack=false)const;
    std::pair<double,double> range(const std::string& path,const PresetJson& value)const;
};
