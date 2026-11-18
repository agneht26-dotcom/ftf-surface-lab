#pragma once
#include "surface.hpp"
#include <array>
#include <vector>
namespace ftf {
uint32_t seedInt(uint64_t seed);
float sine(float angle);
float cosine(float angle);
float simplex(float x,float z,uint32_t seed,bool second=false);
float perlinNode(float x,float z,uint32_t seed,int scale,int octaves=1);
float map(float value,float lo,float hi);
int floor(float value);
int round(float value);
struct WorldPoint {float edge{};glm::ivec2 center{},cell{};float size{1};};
struct VoronoiPolygon {
    int cellX{INT32_MIN},cellY{INT32_MIN},edgeCount{};
    float seedX{},seedY{},centroidX{},centroidY{};
    std::array<int,24> ncx{},ncy{};
    std::array<float,24> dx{},dy{},mx{},my{},nx{},ny{},tx{},ty{},len{},lo{},hi{},tMid{},halfLen{},hc{},mTan{};
    std::array<bool,24> isBridged{},isTectonicActive{};
};
class WorldSampler {
public:
    explicit WorldSampler(const GraphSeeds& inputs):g(inputs){}
    WorldPoint sample(float x,float z,bool restrictSingle=true)const;
    glm::ivec2 centerForCell(glm::ivec2 cell)const;
    float distanceToOcean(int x,int z,float dx,float dz)const;
    glm::vec2 point(glm::ivec2 cell)const;
    bool upliftBridged()const;
    bool isInBridgedSector(int cx,int cz,float dx,float dz)const;
    bool isInBridgedSector(float rawX,float rawZ)const;
    const VoronoiPolygon& getVoronoi(int cellX,int cellY)const;
    bool shouldSkip(int cx,int cy)const;
private:
    const GraphSeeds& g;
    mutable std::vector<VoronoiPolygon> voronoiCache;
    glm::vec2 closest(glm::vec2 p,glm::ivec2& winner)const;
    float coast(float x,float z,float edge,float frequency)const;
    float warpNoise(float x,float z,uint32_t seed,int scale,int octaves,float lacunarity,float gain)const;
    glm::vec2 warpUplift(float rawX,float rawZ,bool includeMacro)const;
    float continentSizeMod(glm::ivec2 cell)const;
    float bridgeRoll(int ax,int ay,int bx,int by)const;
    float tectonicRoll(int ax,int ay,int bx,int by)const;
    bool canBridge(int cx,int cy)const;
    bool isBridged(int ax,int ay,int bx,int by)const;
    float boundaryDistanceSq(float x,float y,int cellX,int cellY)const;
    void populateVoronoi(int cellX,int cellY,VoronoiPolygon& v)const;
};
bool buildVoronoiBuffer(const TerrainParams& params,const GraphSeeds& g,VoronoiBufferKey& key,std::vector<uint32_t>& words);
}
