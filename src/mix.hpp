#pragma once
#include "surface.hpp"
#include <nlohmann/json.hpp>

// Land-stage preview: the original provider, optionally with Heightmap chains.
// Continents, hydrology, world filters and biome selection are separate stages.
void buildLandMix(const nlohmann::ordered_json& preset,int64_t worldSeed,GraphSeeds& output,bool chains=true);
