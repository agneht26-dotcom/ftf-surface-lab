#pragma once
#include "surface.hpp"
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
class WorldSampler {
public:
    explicit WorldSampler(const GraphSeeds& inputs):g(inputs){}
    WorldPoint sample(float x,float z,bool restrictSingle=true)const;
    glm::ivec2 centerForCell(glm::ivec2 cell)const;
    float distanceToOcean(int x,int z,float dx,float dz)const;
    glm::vec2 point(glm::ivec2 cell)const;
private:
    const GraphSeeds& g;
    glm::vec2 closest(glm::vec2 p,glm::ivec2& winner)const;
    float coast(float x,float z,float edge,float frequency)const;
    float warpNoise(float x,float z,uint32_t seed,int scale)const;
};
}
