#pragma once
#include "surface.hpp"
#include <nlohmann/json.hpp>
void buildUpliftInputs(const nlohmann::ordered_json& preset,int64_t worldSeed,GraphSeeds& output,bool islands=true);

void buildClimateInputs(const nlohmann::ordered_json& preset,int64_t worldSeed,GraphSeeds& output,bool enabled=true);

void buildWorldFilterInputs(const nlohmann::ordered_json& preset,int64_t worldSeed,GraphSeeds& output,bool enabled=true,int ftfProfile=1);

std::vector<uint32_t> exactSurfaceNoiseWords(glm::uvec4 root,const std::vector<ExactNoiseSpec>& noises,const std::vector<std::string>& gradients);
void buildSurfaceInputs(const nlohmann::ordered_json& preset,int64_t seed,GraphSeeds& output,bool enabled=true);
