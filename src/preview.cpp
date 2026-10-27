#include "preview.hpp"
#include <algorithm>

const char* terrainProfileName(uint32_t profile){
    const char* names[]={"Plains","Steppe","Dales","Hills 1","Hills 2","Plateau","Badlands","Torridonian"};return names[std::min(profile,7u)];
}
PreviewBinding previewBinding(const std::string& path,const TerrainParams& p){
    const auto current=std::min(p.meta.w,7u);
    auto terrain=[&](uint32_t profile,const std::string& explanation){return PreviewBinding{(p.meta.z==4||p.meta.z==5)?PreviewEffect::LandMix:PreviewEffect::Terrain,profile,explanation};};
    if(path.rfind("/world/continent/",0)==0)return {PreviewEffect::World,current,"Changes the selected FTF continent generator. MULTI/SINGLE use Continent Shape; UPLIFT/MULTI_IMPROVED use Euclidean geometry. Skipping, size variance and fractal continent-noise controls belong to the improved/Uplift modes."};
    if(path=="/world/controlPoints/shallowOcean"||path=="/world/controlPoints/inland")return {PreviewEffect::World,current,"Changes Uplift coastal shaping and ocean/land interpolation."};
    bool islandField=false;
    if(path.rfind("/island/",0)==0){auto key=path.substr(8);for(const char* supported:{"enableArchipelago","islandDensity","islandSize","islandHeight","islandBaseScale","islandVerticalScale","islandHorizontalScale","mountainChance","volcanoChance","mountainScale","volcanismScale","volcanismHorizontalScale","mountainHorizontalScale","offshoreDepth","beachWidth","beachCoverage","macroDensityPercentage"})islandField|=key==supported;}
    if(path=="/world/controlPoints/islandCoast"||islandField)return {PreviewEffect::WorldSurface,current,"Changes source archipelago generation after ocean/land blending. The Uplift surface view evaluates these settings; the continent map shows the base continents."};
    if(path=="/world/controlPoints/deepOcean"||path=="/world/controlPoints/coast"||path=="/world/properties/oceanDepth"||path=="/world/properties/worldDepth")return {PreviewEffect::WorldSurface,current,"Changes source ocean depths or ocean/land interpolation in the Uplift surface view."};
    if(path=="/world/properties/spawnX"||path=="/world/properties/spawnZ")return {PreviewEffect::World,current,"Sets the preset spawn and recenters the preview there. Spawn coordinates are exported; preview extent is saved separately."};
    if(path=="/world/properties/seaLevel"&&(p.meta.z==5||p.meta.z==6))return {PreviewEffect::World,current,"Changes water level, Uplift water-table normalization and source ocean heights."};
    if(path=="/terrain/general/terrainRegionSize")return {(p.meta.z==4||p.meta.z==5)?PreviewEffect::LandMix:PreviewEffect::Regions,0,"Changes region layout in the land/Uplift surface or diagnostic map. It does not change an isolated family's height."};
    if(path=="/terrain/general/fancyMountains"||path=="/terrain/general/legacyMountainScaling"||path=="/terrain/general/mountainVariety")return {PreviewEffect::LandMix,0,"Changes mountain geometry in the partial land mix. Fancy mountains includes the original cellular erosion; variety selects low, center or high settings by region."};
    if(path.rfind("/terrain/mountains/",0)==0)return {PreviewEffect::LandMix,0,path=="/terrain/mountains/weight"?"Changes regional mountain selection. FTF's separate mountain-chain blend still runs at zero Mountain Weight.":"Changes regional mountains and mountain chains. This FTF build applies family vertical scale twice to nonlegacy chains."};
    if(path=="/terrain/volcano/weight")return {PreviewEffect::LandMix,0,"Changes volcano selection in the combined land preview. FTF rounds weight ratios to repeated selection slots."};
    if(path.rfind("/terrain/volcano/",0)==0)return {PreviewEffect::SavedOnly,current,"The supplied VolcanoPopulator consumes only weight; its base/vertical/horizontal scale fields are preserved for export and have no effect in FTF itself."};
    if(path=="/terrain/general/globalHorizontalScale"||path=="/terrain/general/terrainSeedOffset"||path=="/world/properties/seaLevel")return terrain(current,"Changes the terrain preview.");
    if(path=="/terrain/general/globalVerticalScale"){
        bool used=current==0||current==3||current==4||current==5;
        return terrain(used?current:0,used?"Changes this family's global height scale.":"This family ignores global vertical scale in FTF. Auto-select edited family can preview Plains while in the isolated terrain view.");
    }
    if(path=="/rivers/mainRivers/bedDepth"||path=="/rivers/lakes/depth")return {PreviewEffect::WorldSurface,current,"Changes the underwater bed while water stays at its local level. Enable View / scene / Show ocean floor to inspect depth. Lake Depth retains the supplied FTF build's normalized-depth formula."};
    if(path=="/rivers/seedOffset"||path=="/rivers/riverCount"||path.rfind("/rivers/mainRivers/",0)==0||path=="/rivers/branchRivers/fade"||path.rfind("/rivers/wetlands/",0)==0)return {PreviewEffect::WorldSurface,current,"Changes the source river/lake/wetland surface stage. Native network generation and GPU carving run in the Uplift surface without changing Generator."};
    if(path.rfind("/rivers/branchRivers/",0)==0)return {PreviewEffect::SavedOnly,current,"This FTF build derives fork width/depth/banks from the parent river. It consumes branch Fade, but these branch settings are not used by its generator. Preserved for export."};
    if(path.rfind("/rivers/lakes/",0)==0){if(path=="/rivers/lakes/minBankHeight"||path=="/rivers/lakes/maxBankHeight")return {PreviewEffect::SavedOnly,current,"UpliftRiverCarver does not consume the lake bank-height settings in the supplied build. Preserved for export."};return {PreviewEffect::WorldSurface,current,"Changes the source plateau-dependent widening/depth of lakes along rivers. Only eligible plateaus within the configured start-distance range widen."};}
    if(path=="/world/controlPoints/beach")return {PreviewEffect::WorldSurface,current,"Changes the source beach-detection threshold after erosion in the Uplift surface."};
    if(path.rfind("/erosionFilters/",0)==0){auto key=path.substr(16);for(const char* supported:{"scale","strengthMultiplier","gullySharpness","flowOctaves","lacunarity","gain","phacelleScale","phacelleOffset","phacelleNormalization","slopeOnsetBase","slopeOnsetRidge","slopeOnsetOctave","roundingMin","roundingMax"})if(key==supported)return {PreviewEffect::WorldSurface,current,"Changes the source world erosion pass after climate. Neighbor samples use world-block distances interpolated from the chosen preview resolution."};}
    if(path.rfind("/surface/erosion/",0)==0||path=="/miscellaneous/erosionDecorator"||path=="/miscellaneous/screeDecorator"||path=="/miscellaneous/strataDecorator"||path=="/miscellaneous/strataRegionSize"||path=="/miscellaneous/naturalSnowDecorator"||path=="/miscellaneous/smoothLayerDecorator")return {PreviewEffect::WorldSurface,current,"Changes Minecraft surface block colours, scree or exposed rock in the Minecraft surface / blocks colour view. Geometry remains a height mesh."};
    if(path=="/miscellaneous/plainStoneErosion")return {PreviewEffect::SavedOnly,current,"The supplied ScreeFeature does not read plainStoneErosion. Preserved for export, as in FTF."};
    bool climateField=path=="/climate/altitudeCoolingStrength"||path=="/climate/rainShadowStrength"||path=="/climate/coastalMoistureBoost";
    for(const char* group:{"temperature","moisture"})for(const char* key:{"seedOffset","scale","falloff","min","max","bias","distributionBias"})climateField|=path=="/climate/"+std::string(group)+"/"+key;
    for(const char* key:{"biomeSize","biomeWarpScale","biomeWarpStrength","macroNoiseSize"})climateField|=path=="/climate/biomeShape/"+std::string(key);
    for(const char* key:{"type","scale","octaves","gain","lacunarity","strength"})climateField|=path=="/climate/biomeEdgeShape/"+std::string(key);
    if(climateField&&p.meta.z==5)return {PreviewEffect::WorldSurface,current,"Changes FTF climate values or biome regions, followed by Minecraft surface-biome selection and colors."};
    if(path=="/climate/temperature/bias"||path=="/climate/moisture/bias"||path=="/climate/biomeShape/biomeSize")return terrain(current,"Changes temporary climate colors in this pre-climate development view. The Uplift surface evaluates FTF climate values.");
    if(path=="/world/properties/worldHeight")return {(p.meta.z==5||p.meta.z==6)?PreviewEffect::World:PreviewEffect::Limited,current,"These FTF height stages normalize with min(worldHeight,256). Values above 256 do not rescale the base terrain, but World Height also changes FTF altitude cooling and rain shadow in the Uplift surface."};
    if(path.rfind("/terrain/",0)==0){
        auto end=path.find('/',9);if(end!=std::string::npos){
            auto family=path.substr(9,end-9),property=path.substr(end+1);
            const char* families[]={"plains","steppe","dales","hills","hills","plateau","badlands","torridonian"};
            for(uint32_t i=0;i<8;++i)if(family==families[i]){
                if(property=="weight")return {PreviewEffect::LandMix,i,"Changes region selection in FTF land mix (partial). FTF rounds weights to repeated slots, so tiny edits can retain the same layout. Isolated previews do not use weights."};
                if(property=="baseScale"||property=="verticalScale"||(property=="horizontalScale"&&i<=1)){
                    auto profile=family=="hills"&&(current==3||current==4)?current:i;
                    return terrain(profile,"Changes "+std::string(terrainProfileName(profile))+". Auto-select edited family can select it within the isolated terrain view; it never changes Generator.");
                }
                if(property=="horizontalScale")return {PreviewEffect::SavedOnly,current,"This graph does not use horizontalScale in the supplied FTF build. Saved for export only."};
            }
        }
    }
    return {PreviewEffect::SavedOnly,current,"This generation stage is not connected to the preview. The value is preserved for preset export."};
}
