#pragma once
#include "cpu_world.hpp"
#include <nlohmann/json.hpp>
#include <memory>
namespace ftf {
struct RiverConfig {bool main{};int order{},bedWidth{},bankWidth{},length{};float bedHeight{},minBank{},maxBank{},fade{};};
struct RiverLine {glm::vec2 a{},b{},delta{},normal{};float length{},length2{};glm::vec4 bounds{};RiverLine()=default;RiverLine(glm::vec2 start,glm::vec2 end);};
struct RiverWarp {uint32_t seed{};float lower{},upper{},frequency{},scale{};};
struct Wetland {glm::vec2 a{},b{};float radius{};uint32_t seed{};};
struct RiverNode {RiverLine river;RiverWarp warp;RiverConfig config;float fade{},valleySize{},curveLower{},curveUpper{};std::vector<Wetland> wetlands;std::vector<std::unique_ptr<RiverNode>> children;};
struct RiverMap {glm::ivec2 center{};std::vector<std::unique_ptr<RiverNode>> roots;};
class RiverNetworks {
public:
    void update(const nlohmann::ordered_json& preset,int64_t seed,const GraphSeeds& inputs,const TerrainParams& params,bool enabled=true);
    std::vector<uint32_t> buffer;
    std::vector<RiverMap> maps;
    bool changed{};
    nlohmann::ordered_json describe()const;
private:
    std::string key;
};
}
