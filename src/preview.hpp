#pragma once
#include "surface.hpp"
#include <string>

enum class PreviewEffect { SavedOnly, Terrain, Regions, LandMix, Limited, World, WorldSurface };
struct PreviewBinding {
    PreviewEffect effect{PreviewEffect::SavedOnly};
    uint32_t profile{};
    std::string explanation;
    bool capable()const{return effect==PreviewEffect::Terrain||effect==PreviewEffect::Regions||effect==PreviewEffect::LandMix||effect==PreviewEffect::World||effect==PreviewEffect::WorldSurface;}
    bool active(const TerrainParams& params)const{
        return (effect==PreviewEffect::Terrain&&params.meta.z==2&&params.meta.w==profile)||(effect==PreviewEffect::Regions&&params.meta.z==3)||(effect==PreviewEffect::LandMix&&(params.meta.z==4||params.meta.z==5))||(effect==PreviewEffect::World&&(params.meta.z==5||params.meta.z==6))||(effect==PreviewEffect::WorldSurface&&params.meta.z==5);
    }
    bool updatesView(const TerrainParams& params,bool followFamily)const{
        return active(params)||(effect==PreviewEffect::Terrain&&params.meta.z==2&&followFamily);
    }
};
PreviewBinding previewBinding(const std::string& path,const TerrainParams& params);
const char* terrainProfileName(uint32_t profile);
