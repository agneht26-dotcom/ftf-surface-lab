#pragma once
// Biome count (right panel): which biome ids the selection in shaders/biomes.comp can output for a biome-mod mask,
// read from the same table (assets/biomes-1.21.1.bin) the GPU searches.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <vector>

// The GPU table is biomes-1.21.1.bin followed by materials-1.21.1.bin (Surface::initialize); offsets and the mod footer are words of the joined table.
inline std::vector<uint32_t> loadBiomeTableWords(const std::filesystem::path& assets){
    std::vector<uint32_t> words;
    for(const char* name:{"biomes-1.21.1.bin","materials-1.21.1.bin"}){
        std::ifstream input(assets/name,std::ios::binary|std::ios::ate);if(!input)throw std::runtime_error("Missing Minecraft table: "+(assets/name).string());
        auto bytes=static_cast<size_t>(input.tellg());if(bytes<32||bytes%4)throw std::runtime_error("Invalid Minecraft table: "+(assets/name).string());
        size_t at=words.size();words.resize(at+bytes/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()+at),bytes);}
    return words;
}

inline size_t biomeTableFooter(const std::vector<uint32_t>& w){
    if(w.size()>=16&&w.back()==0x504D534Bu){size_t f=size_t(w[w.size()-3]);if(f+8<=w.size()&&w[f]==0x4654464du)return f;}
    if(w.size()>=16&&w[w.size()-8]==0x4654464du)return w.size()-8;
    return SIZE_MAX;
}

inline bool biomeTableHasFooter(const std::vector<uint32_t>& w){return biomeTableFooter(w)!=SIZE_MAX;}

inline uint32_t canonicalBiomeId(const std::vector<uint32_t>& w,uint32_t biome){   // canonicalBiome() in biomes.comp
    if(biome>=w[1])return 0xffffffffu;size_t footer=biomeTableFooter(w);if(footer==SIZE_MAX)return biome;
    uint32_t alias=w[w[footer+6]+biome];return (alias&131072u)?alias&65535u:biome;
}

inline void collectTreeLeaves(const std::vector<uint32_t>& w,uint32_t root,std::vector<bool>& out){
    std::vector<uint32_t> stack{root};
    while(!stack.empty()){uint32_t node=stack.back();stack.pop_back();size_t offset=8+size_t(node)*16;if(offset+15>=w.size())throw std::runtime_error("Biome table node out of range");
        int children=int(w[offset+15]);uint32_t first=w[offset+14];
        if(children<0){auto id=canonicalBiomeId(w,first);if(id<out.size())out[id]=true;}
        else for(int i=0;i<children;++i)stack.push_back(first+uint32_t(i));}
}

// mask bit 0: replaces the whole vanilla result with the first mod tree; bit 1: regional variant trees; bit 2: weighted replacement rules keyed on the vanilla biome.
inline std::vector<bool> possibleBiomes(const std::vector<uint32_t>& w,uint32_t mask){
    std::vector<bool> vanilla(w[1],false),out(w[1],false);
    collectTreeLeaves(w,w[2],vanilla);collectTreeLeaves(w,w[3],vanilla);
    size_t footer=biomeTableFooter(w);
    if(footer==SIZE_MAX)mask=0;
    if(mask&1u)collectTreeLeaves(w,w[footer+1],out);
    else if(mask&32u){uint32_t rg=w[footer+5];if(rg>0&&rg+24<=footer&&w[rg]==0x52474E53u&&w[rg+23]!=0xffffffffu)collectTreeLeaves(w,w[rg+23],out);else out=vanilla;}
    else out=vanilla;
    if(mask&2u)for(uint32_t v=1;v<=3;++v)collectTreeLeaves(w,w[footer+1+v],out);
    if((mask&4u)||(mask&8u)){
        uint32_t rg=w[footer+5];
        if(rg>0&&rg+24<=footer&&w[rg]==0x52474E53u){
            if(mask&4u){
                for(uint32_t i=0;i<w[rg+7];++i){auto id=canonicalBiomeId(w,w[w[rg+8]+size_t(i)*14]);if(id<out.size())out[id]=true;}
                for(uint32_t i=0;i<w[rg+22];++i){auto id=canonicalBiomeId(w,w[w[rg+10]+size_t(i)*16]);if(id<out.size())out[id]=true;}
            }
            if(mask&8u){
                for(uint32_t k=0;k<5;++k){
                    if(w[rg+11+k]!=0xffffffffu)collectTreeLeaves(w,w[rg+11+k],out);
                    if(w[rg+16+k]!=0xffffffffu)collectTreeLeaves(w,w[rg+16+k],out);
                }
                for(uint32_t i=w[rg+22];i<w[rg+9];++i){auto id=canonicalBiomeId(w,w[w[rg+10]+size_t(i)*16]);if(id<out.size())out[id]=true;}
            }
        }
    }
    return out;
}

// What the panel shows after a count: generated vs possible per pack (the legend's "pack", "minecraft" when absent).
struct BiomeCensusGroup {std::string pack;int generated=0,possible=0;std::vector<std::string> missing;};
struct BiomeCensus {int generated=0,possible=0;std::vector<BiomeCensusGroup> groups;std::vector<std::string> unreachable,unexpected;float worldSize=0;int grid=0;};

template<class Json> BiomeCensus summarizeBiomeCensus(const Json& legend,const std::vector<bool>& possible,const std::vector<uint32_t>& found,float worldSize,int grid){
    BiomeCensus c;c.worldSize=worldSize;c.grid=grid;std::vector<bool> seen(possible.size(),false);
    for(auto id:found){if(id<seen.size())seen[id]=true;if(id>=possible.size()||!possible[id])c.unexpected.push_back(id<legend["biomes"].size()?legend["biomes"][id]["name"].template get<std::string>():std::to_string(id));}
    auto group=[&](const std::string& pack)->BiomeCensusGroup&{for(auto& g:c.groups)if(g.pack==pack)return g;c.groups.push_back({pack});return c.groups.back();};
    group("minecraft");
    std::vector<std::string> packsInUse{"minecraft"};
    for(size_t i=0;i<legend["biomes"].size()&&i<possible.size();++i)if(possible[i]){auto pack=legend["biomes"][i].value("pack",std::string("minecraft"));if(std::find(packsInUse.begin(),packsInUse.end(),pack)==packsInUse.end())packsInUse.push_back(pack);}
    for(size_t i=0;i<legend["biomes"].size()&&i<possible.size();++i){
        const auto& b=legend["biomes"][i];if(!b.value("surface_available",true)||b.value("canonical_id",int(i))!=int(i))continue;
        auto name=b["name"].template get<std::string>();auto pack=b.value("pack",std::string("minecraft"));auto shortName=name.substr(name.find(':')==std::string::npos?0:name.find(':')+1);
        if(!possible[i]){if(std::find(packsInUse.begin(),packsInUse.end(),pack)!=packsInUse.end())c.unreachable.push_back(pack+": "+shortName);continue;}
        auto& g=group(pack);++g.possible;++c.possible;if(seen[i]){++g.generated;++c.generated;}else g.missing.push_back(shortName);
    }
    return c;
}
