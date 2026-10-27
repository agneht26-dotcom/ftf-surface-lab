#pragma once
// Heights of the drawn terrain mesh at any world x,z, at full resolution (cursor readout, clicks, pins and markers).
// A surface's height field is read in 64-cell tiles on demand and kept until that field changes. Between samples the
// height follows terrain.vert: each sample is raised to the water level first, then the cell's two triangles
// (split along the (1,0)-(0,1) diagonal) interpolate it.
#include "surface.hpp"
#include <algorithm>
#include <optional>
#include <unordered_map>
#include <vector>

struct MeshHeightTiles {
    static constexpr int Cells=64;   // one tile: 65 x 65 samples (64 cells plus the shared edge)
    struct Tile {int w=0,h=0;std::vector<float> v;};
    Surface* surface=nullptr;uint64_t revision=~0ull;uint32_t resolution=0;glm::vec3 domain{};double flushed=-1;
    std::unordered_map<uint32_t,Tile> tiles;

    // fresh=false keeps tiles of an older field for up to maxAge seconds, so markers drawn every frame do not read the GPU
    // every frame while a slider drags; a moved or resized field always starts over.
    void track(Surface& s,double now,bool fresh,double maxAge=.4){
        glm::vec3 d{s.lastParams.domain.x,s.lastParams.domain.y,s.lastParams.domain.z};
        bool moved=&s!=surface||s.resolution!=resolution||d!=domain;
        if(moved||(s.fieldRevision!=revision&&(fresh||now-flushed>maxAge))){tiles.clear();flushed=now;revision=s.fieldRevision;}
        surface=&s;resolution=s.resolution;domain=d;
    }
    bool valid()const{return surface&&resolution>=2&&domain.x>0;}
    float spacing()const{return domain.x/float(resolution-1);}
    glm::vec2 center()const{return {domain.y,domain.z};}
    bool grid(glm::vec2 world,glm::vec2& g)const{
        auto uv=(world-center())/domain.x+.5f;if(uv.x<0||uv.x>1||uv.y<0||uv.y>1)return false;g=uv*float(resolution-1);return true;}
    static uint32_t key(glm::ivec2 t){return uint32_t(t.y)*65536u+uint32_t(t.x);}
    glm::ivec2 cellOf(glm::vec2 g)const{return glm::clamp(glm::ivec2(glm::floor(g)),glm::ivec2(0),glm::ivec2(int(resolution)-2));}

    // Reads every missing tile in the list with one GPU copy.
    void prefetch(const std::vector<glm::ivec2>& list){
        if(!valid())return;std::vector<glm::ivec4> windows;std::vector<uint32_t> keys;
        for(auto t:list){auto k=key(t);if(tiles.count(k)||std::find(keys.begin(),keys.end(),k)!=keys.end())continue;
            int x=t.x*Cells,z=t.y*Cells;if(x<0||z<0||x>=int(resolution)-1||z>=int(resolution)-1)continue;
            windows.push_back({x,z,std::min(Cells+1,int(resolution)-x),std::min(Cells+1,int(resolution)-z)});keys.push_back(k);}
        if(windows.empty())return;auto data=surface->heightWindows(windows);
        for(size_t i=0;i<keys.size();++i)tiles[keys[i]]={windows[i].z,windows[i].w,std::move(data[i])};
    }
    // Tiles under the straight line a-b (world x,z), so a ray march reads them in one copy.
    void prefetchSegment(glm::vec2 a,glm::vec2 b){
        if(!valid())return;std::vector<glm::ivec2> list;float length=glm::length(b-a)/spacing();int steps=std::clamp(int(length/(Cells*.5f))+1,1,4096);
        for(int i=0;i<=steps;++i){glm::vec2 g;if(grid(glm::mix(a,b,float(i)/steps),g)){auto t=cellOf(g)/Cells;if(std::find(list.begin(),list.end(),t)==list.end())list.push_back(t);}}
        prefetch(list);
    }
    // Mesh height of this surface (raw FTF height raised to waterLevel per sample, like terrain.vert).
    std::optional<float> height(glm::vec2 world,float waterLevel){
        glm::vec2 g;if(!valid()||!grid(world,g))return std::nullopt;
        auto a=cellOf(g);glm::vec2 t=glm::clamp(g-glm::vec2(a),glm::vec2(0),glm::vec2(1));auto tc=a/Cells;auto k=key(tc);
        auto it=tiles.find(k);if(it==tiles.end()){prefetch({tc});it=tiles.find(k);if(it==tiles.end())return std::nullopt;}
        const auto& tile=it->second;int lx=a.x-tc.x*Cells,lz=a.y-tc.y*Cells;
        auto at=[&](int x,int z){return std::max(tile.v[size_t(lz+z)*tile.w+size_t(lx+x)],waterLevel);};
        float h00=at(0,0),h10=at(1,0),h01=at(0,1),h11=at(1,1);
        return t.x+t.y<=1?h00+(h10-h00)*t.x+(h01-h00)*t.y:h11+(h01-h11)*(1-t.x)+(h10-h11)*(1-t.y);
    }
};
