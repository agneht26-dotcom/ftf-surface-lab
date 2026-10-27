#pragma once
#include <cstring>

// Exact byte comparisons, deliberately conservative for geometry dependencies.
// Climate can change coastal terrain roles; filters must restart from checkpoints.
enum class GenerationStage : uint32_t { Terrain,Climate,Filters,Biomes,Materials,None };
inline const char* generationStageName(GenerationStage s){static const char* n[]={"terrain","climate","filters","biomes","materials","none"};return n[uint32_t(s)];}
struct GenerationInputs {
    TerrainParams params{};GraphSeeds graph{};uint64_t rivers=0;
    static GraphSeeds terrainKey(GraphSeeds g){
        g.climateMeta={};g.climateSeeds0={};g.climateSeeds1={};g.climateSeeds2={};
        g.climateShape={};g.climateEdge={};g.climateEffects={};g.temperature0={};g.temperature1={};g.moisture0={};g.moisture1={};
        g.filter0={};g.filter1={};g.filter2={};g.filter3={};g.filter4={};g.filter5.x=0;g.filterMeta={};
        g.biomeSeeds2.z=g.biomeSeeds2.w=0;g.biomeOptions.z=0;
        g.scree0={};g.scree1={};g.surfaceOptions={};g.surfaceMeta={};return g;
    }
    static GraphSeeds climateKey(GraphSeeds g){
        // Compare everything still relevant through climate; downstream-only fields are cleared.
        g.filter0={};g.filter1={};g.filter2={};g.filter3={};g.filter4={};g.filter5.x=0;g.filterMeta={};
        g.biomeSeeds2.z=g.biomeSeeds2.w=0;g.biomeOptions.z=0;
        g.scree0={};g.scree1={};g.surfaceOptions={};g.surfaceMeta={};return g;
    }
    static GraphSeeds filterKey(GraphSeeds g){g.biomeSeeds2.z=g.biomeSeeds2.w=0;g.biomeOptions.z=0;g.scree0={};g.scree1={};g.surfaceOptions={};g.surfaceMeta={};return g;}
    static GraphSeeds biomeKey(GraphSeeds g){g.biomeSeeds2.w=0;g.scree0={};g.scree1={};g.surfaceOptions={};g.surfaceMeta.x=g.surfaceMeta.y=g.surfaceMeta.z=0;g.surfaceMeta.w>>=1;return g;}
    GenerationStage difference(const GenerationInputs& next)const{
        auto differs=[](const auto& a,const auto& b){return std::memcmp(&a,&b,sizeof(a))!=0;};
        auto oldParams=params,newParams=next.params;if(graph.climateMeta.x&&next.graph.climateMeta.x){oldParams.climate={};newParams.climate={};}
        if(params.meta.z!=5||next.params.meta.z!=5||differs(oldParams,newParams)||rivers!=next.rivers||differs(terrainKey(graph),terrainKey(next.graph)))return GenerationStage::Terrain;
        if(differs(climateKey(graph),climateKey(next.graph)))return GenerationStage::Climate;
        if(differs(filterKey(graph),filterKey(next.graph)))return GenerationStage::Filters;
        if(differs(biomeKey(graph),biomeKey(next.graph)))return GenerationStage::Biomes;
        if(differs(graph,next.graph))return GenerationStage::Materials;
        return GenerationStage::None;
    }
};
