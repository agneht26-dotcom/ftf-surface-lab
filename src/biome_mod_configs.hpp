#pragma once

#include <sstream>
#include <string>

namespace biomemodcfg {


struct RegionsUnexploredConfig {
    int overworldRegionWeight = 8;
    bool enableCustomRegions = true;
    bool enableSurfaceRules = true;
    bool toggleVanillaBiomeReplacements = true;
    bool enableCaveBiomes = true;
};

inline std::string toToml(const RegionsUnexploredConfig& c) {
    std::ostringstream os;
    os << "# Regions Unexplored (1.21.1) regions_unexplored-common.toml\n"
       << "[worldgen]\n"
       << "  overworld_region_weight = " << c.overworldRegionWeight << "\n"
       << "  enable_custom_regions = " << (c.enableCustomRegions ? "true" : "false") << "\n"
       << "  enable_surface_rules = " << (c.enableSurfaceRules ? "true" : "false") << "\n"
       << "  toggle_vanilla_biome_replacements = " << (c.toggleVanillaBiomeReplacements ? "true" : "false") << "\n"
       << "  enable_cave_biomes = " << (c.enableCaveBiomes ? "true" : "false") << "\n";
    return os.str();
}

} // namespace biomemodcfg

