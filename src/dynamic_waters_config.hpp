#pragma once
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Adapted from Dynamic Waters 13.1.1 (AdamNew, MPL-2.0), ported to C++ for FTF Surface Lab (2026-11);
// see LICENSES/DynamicWaters-MPL-2.0.txt (notices/DynamicWaters-MPL-2.0.txt in release packages).
//
// Dynamic Waters (13.1.1 for MC 1.21.1 NeoForge) configuration state, JSON persistence,
// NeoForge dynamicwaters-common.toml exporter, and split cache hashes.
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace dw {

enum class NormalRiverMode : int { DYNAMIC = 0, VANILLA = 1, NONE = 2 };
enum class DensityLevel : int { VERY_LOW = 0, LOW = 1, MEDIUM = 2, HIGH = 3, ULTRA = 4, EXTREME = 5, INSANE = 6 };
enum class RiverSplits : int { OFF = 0, RARE = 1, ENOUGH = 2, NORMAL = 3, OFTEN = 4 };
enum class FallenLogs : int { OFF = 0, RARE = 1, NORMAL = 2, OFTEN = 3 };
enum class TunnelFrequency : int { RARE = 0, NORMAL = 1, OFTEN = 2 };
enum class WaterfallLevel : int { CLIFFS = 0, FEW = 1, NORMAL = 2, MANY = 3 };
enum class MountainRiverIce : int { OPEN = 0, CALM = 1, FULL = 2 };
enum class IceFloes : int { OFF = 0, RARE = 1, NORMAL = 2, OFTEN = 3, MANY = 4 };

inline const char* normalRiverModeName(NormalRiverMode v) {
    static const char* k[] = {"DYNAMIC", "VANILLA", "NONE"};
    return k[int(v) >= 0 && int(v) < 3 ? int(v) : 0];
}
inline const char* densityLevelName(DensityLevel v) {
    static const char* k[] = {"VERY_LOW", "LOW", "MEDIUM", "HIGH", "ULTRA", "EXTREME", "INSANE"};
    return k[int(v) >= 0 && int(v) < 7 ? int(v) : 3];
}
inline const char* riverSplitsName(RiverSplits v) {
    static const char* k[] = {"OFF", "RARE", "ENOUGH", "NORMAL", "OFTEN"};
    return k[int(v) >= 0 && int(v) < 5 ? int(v) : 2];
}
inline double riverSplitsChance(RiverSplits v) {
    static const double k[] = {0.0, 0.1, 0.2, 0.3, 0.6};
    return k[int(v) >= 0 && int(v) < 5 ? int(v) : 2];
}
inline const char* fallenLogsName(FallenLogs v) {
    static const char* k[] = {"OFF", "RARE", "NORMAL", "OFTEN"};
    return k[int(v) >= 0 && int(v) < 4 ? int(v) : 2];
}
inline double fallenLogsChance(FallenLogs v) {
    static const double k[] = {0.0, 0.15, 0.4, 0.85};
    return k[int(v) >= 0 && int(v) < 4 ? int(v) : 2];
}
inline const char* tunnelFrequencyName(TunnelFrequency v) {
    static const char* k[] = {"RARE", "NORMAL", "OFTEN"};
    return k[int(v) >= 0 && int(v) < 3 ? int(v) : 1];
}
inline double tunnelFrequencyChance(TunnelFrequency v) {
    static const double k[] = {0.05, 0.15, 1.0};
    return k[int(v) >= 0 && int(v) < 3 ? int(v) : 1];
}
inline const char* waterfallLevelName(WaterfallLevel v) {
    static const char* k[] = {"CLIFFS", "FEW", "NORMAL", "MANY"};
    return k[int(v) >= 0 && int(v) < 4 ? int(v) : 2];
}
inline const char* mountainRiverIceName(MountainRiverIce v) {
    static const char* k[] = {"OPEN", "CALM", "FULL"};
    return k[int(v) >= 0 && int(v) < 3 ? int(v) : 1];
}
inline const char* iceFloesName(IceFloes v) {
    static const char* k[] = {"OFF", "RARE", "NORMAL", "OFTEN", "MANY"};
    return k[int(v) >= 0 && int(v) < 5 ? int(v) : 2];
}

inline std::vector<std::string> splitConfigEntries(const std::string& raw) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < raw.size()) {
        while (i < raw.size() && (raw[i] == ',' || raw[i] == ';' || raw[i] == '\n' || raw[i] == '\r' || raw[i] == ' ' || raw[i] == '\t')) ++i;
        size_t start = i;
        while (i < raw.size() && raw[i] != ',' && raw[i] != ';' && raw[i] != '\n' && raw[i] != '\r') ++i;
        size_t end = i;
        while (end > start && (raw[end - 1] == ' ' || raw[end - 1] == '\t')) --end;
        if (end > start) out.push_back(raw.substr(start, end - start));
    }
    return out;
}

struct DynamicWatersConfig {
    bool enabled = false;

    // [Generation]
    NormalRiverMode normalRiverMode = NormalRiverMode::DYNAMIC;
    DensityLevel normalRiverDensity = DensityLevel::HIGH;
    double normalRiverCanyonHeight = 105.0;
    bool normalRiverTunnels = false;
    int normalTunnelHeight = 16;
    int normalTunnelLedge = 0;
    bool normalTunnelPillars = true;
    bool normalTunnelFormations = true;
    int deltaInlandReach = 256;
    RiverSplits riverSplits = RiverSplits::ENOUGH;
    FallenLogs riverFallenLogs = FallenLogs::NORMAL;

    bool enableWatersheds = true;
    DensityLevel mountainRiverDensity = DensityLevel::HIGH;
    bool mountainRiverTunnels = false;
    TunnelFrequency mountainTunnelFrequency = TunnelFrequency::NORMAL;
    int mountainTunnelLength = 256;
    int mountainTunnelDepth = 32;
    int mountainTunnelHeight = 8;
    int mountainTunnelLedge = 3;
    WaterfallLevel mountainRiverWaterfalls = WaterfallLevel::NORMAL;
    MountainRiverIce mountainRiverIce = MountainRiverIce::CALM;
    IceFloes riverIceFloes = IceFloes::NORMAL;
    bool mountainRiverBiome = true;
    bool enableMountainLakes = true;
    std::string biomeBlacklistText;
    bool riverBoulders = true;
    bool riverbankPalettes = true;
    bool waterPlants = true;

    // ["River Flow Speeds"]
    double playerSpeed = 0.2;
    double animalSpeed = 0.1;
    double waterMobSpeed = 0.0;
    double itemSpeed = 0.2;
    double defaultSpeed = 0.1;
    double sableShipSpeed = 2.0;
    double rainFlowBoost = 0.5;
    double thunderstormFlowBoost = 0.7;
    bool vanillaRiverFlow = true;
    std::string customEntitySpeedsText = "minecraft:warden=1.0";

    // [Visuals] / [Sounds] / [General]
    bool enableRiverParticles = true;
    int riverParticleDensity = 50;
    bool enableRiverVisualFlow = true;
    bool customMainMenuPanorama = true;
    bool showMainMenuButton = true;
    bool enableRiverSound = true;
    bool showWelcomeMessage = true;
    bool disableTectonicUndergroundRivers = true;

    bool operator==(const DynamicWatersConfig&) const = default;
    bool dynamicNormalRivers() const { return enabled && normalRiverMode == NormalRiverMode::DYNAMIC; }
    bool usesVanillaGpuRivers() const { return !enabled || normalRiverMode == NormalRiverMode::VANILLA; }

    // Hash for tile/network building (MainRivers & BasinTile caches).
    uint64_t networkHash() const {
        uint64_t h = 14695981039346656037ull;
        auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix(uint64_t(enabled));
        mix(uint64_t(normalRiverMode));
        mix(uint64_t(normalRiverDensity));
        union { double d; uint64_t u; } du{normalRiverCanyonHeight};
        mix(du.u);
        mix(uint64_t(normalRiverTunnels));
        mix(uint64_t(deltaInlandReach));
        mix(uint64_t(riverSplits));
        mix(uint64_t(enableWatersheds));
        mix(uint64_t(mountainRiverDensity));
        mix(uint64_t(mountainRiverTunnels));
        mix(uint64_t(mountainTunnelFrequency));
        mix(uint64_t(mountainTunnelLength));
        mix(uint64_t(mountainTunnelDepth));
        mix(uint64_t(mountainRiverWaterfalls));
        mix(uint64_t(enableMountainLakes));
        for (const auto& entry : splitConfigEntries(biomeBlacklistText)) {
            for (unsigned char c : entry) mix(c);
            mix(0xff);
        }
        return h;
    }

    // Hash for full terrain/biome/surface application.
    uint64_t generationHash() const {
        uint64_t h = networkHash();
        auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix(uint64_t(normalTunnelHeight));
        mix(uint64_t(normalTunnelLedge));
        mix(uint64_t(normalTunnelPillars));
        mix(uint64_t(normalTunnelFormations));
        mix(uint64_t(mountainTunnelHeight));
        mix(uint64_t(mountainTunnelLedge));
        mix(uint64_t(mountainRiverIce));
        mix(uint64_t(mountainRiverBiome));
        mix(uint64_t(riverFallenLogs));
        mix(uint64_t(riverBoulders));
        mix(uint64_t(riverbankPalettes));
        mix(uint64_t(waterPlants));
        return h;
    }

    std::string toToml() const {
        std::ostringstream o;
        o << std::fixed;
        auto fmtDouble = [](double v) {
            std::ostringstream s;
            s << std::setprecision(6) << v;
            std::string str = s.str();
            if (str.find('.') == std::string::npos && str.find('e') == std::string::npos && str.find('E') == std::string::npos)
                str += ".0";
            return str;
        };
        auto fmtStr = [](const std::string& s) {
            std::string r = "\"";
            for (char c : s) {
                if (c == '"' || c == '\\') r.push_back('\\');
                r.push_back(c);
            }
            r.push_back('"');
            return r;
        };
        auto fmtList = [&](const std::string& raw) {
            auto items = splitConfigEntries(raw);
            if (items.empty()) return std::string("[]");
            std::string r = "[";
            for (size_t i = 0; i < items.size(); ++i) {
                if (i) r += ", ";
                r += fmtStr(items[i]);
            }
            r += "]";
            return r;
        };

        o << "# Dynamic Waters 1.21.1 Common Configuration (dynamicwaters-common.toml)\n\n";
        o << "[\"River Flow Speeds\"]\n";
        o << "\tplayerSpeed = " << fmtDouble(playerSpeed) << "\n";
        o << "\tanimalSpeed = " << fmtDouble(animalSpeed) << "\n";
        o << "\twaterMobSpeed = " << fmtDouble(waterMobSpeed) << "\n";
        o << "\titemSpeed = " << fmtDouble(itemSpeed) << "\n";
        o << "\tdefaultSpeed = " << fmtDouble(defaultSpeed) << "\n";
        o << "\tsableShipSpeed = " << fmtDouble(sableShipSpeed) << "\n";
        o << "\trainFlowBoost = " << fmtDouble(rainFlowBoost) << "\n";
        o << "\tthunderstormFlowBoost = " << fmtDouble(thunderstormFlowBoost) << "\n";
        o << "\tvanillaRiverFlow = " << (vanillaRiverFlow ? "true" : "false") << "\n";
        o << "\tcustomEntitySpeeds = " << fmtList(customEntitySpeedsText) << "\n\n";

        o << "[Visuals]\n";
        o << "\tenableRiverParticles = " << (enableRiverParticles ? "true" : "false") << "\n";
        o << "\triverParticleDensity = " << riverParticleDensity << "\n";
        o << "\tenableRiverVisualFlow = " << (enableRiverVisualFlow ? "true" : "false") << "\n";
        o << "\tcustomMainMenuPanorama = " << (customMainMenuPanorama ? "true" : "false") << "\n";
        o << "\tshowMainMenuButton = " << (showMainMenuButton ? "true" : "false") << "\n\n";

        o << "[Generation]\n";
        o << "\tnormalRiverMode = " << fmtStr(normalRiverModeName(normalRiverMode)) << "\n";
        o << "\tnormalRiverDensity = " << fmtStr(densityLevelName(normalRiverDensity)) << "\n";
        o << "\tnormalRiverCanyonHeight = " << fmtDouble(normalRiverCanyonHeight) << "\n";
        o << "\tnormalRiverTunnels = " << (normalRiverTunnels ? "true" : "false") << "\n";
        o << "\tnormalTunnelHeight = " << normalTunnelHeight << "\n";
        o << "\tnormalTunnelLedge = " << normalTunnelLedge << "\n";
        o << "\tnormalTunnelPillars = " << (normalTunnelPillars ? "true" : "false") << "\n";
        o << "\tnormalTunnelFormations = " << (normalTunnelFormations ? "true" : "false") << "\n";
        o << "\tdeltaInlandReach = " << deltaInlandReach << "\n";
        o << "\triverSplits = " << fmtStr(riverSplitsName(riverSplits)) << "\n";
        o << "\triverFallenLogs = " << fmtStr(fallenLogsName(riverFallenLogs)) << "\n";
        o << "\tenableWatersheds = " << (enableWatersheds ? "true" : "false") << "\n";
        o << "\tmountainRiverDensity = " << fmtStr(densityLevelName(mountainRiverDensity)) << "\n";
        o << "\tmountainRiverTunnels = " << (mountainRiverTunnels ? "true" : "false") << "\n";
        o << "\tmountainTunnelFrequency = " << fmtStr(tunnelFrequencyName(mountainTunnelFrequency)) << "\n";
        o << "\tmountainTunnelLength = " << mountainTunnelLength << "\n";
        o << "\tmountainTunnelDepth = " << mountainTunnelDepth << "\n";
        o << "\tmountainTunnelHeight = " << mountainTunnelHeight << "\n";
        o << "\tmountainTunnelLedge = " << mountainTunnelLedge << "\n";
        o << "\tmountainRiverWaterfalls = " << fmtStr(waterfallLevelName(mountainRiverWaterfalls)) << "\n";
        o << "\tmountainRiverIce = " << fmtStr(mountainRiverIceName(mountainRiverIce)) << "\n";
        o << "\triverIceFloes = " << fmtStr(iceFloesName(riverIceFloes)) << "\n";
        o << "\tmountainRiverBiome = " << (mountainRiverBiome ? "true" : "false") << "\n";
        o << "\tenableMountainLakes = " << (enableMountainLakes ? "true" : "false") << "\n";
        o << "\tbiomeBlacklist = " << fmtList(biomeBlacklistText) << "\n";
        o << "\triverBoulders = " << (riverBoulders ? "true" : "false") << "\n";
        o << "\triverbankPalettes = " << (riverbankPalettes ? "true" : "false") << "\n";
        o << "\twaterPlants = " << (waterPlants ? "true" : "false") << "\n\n";

        o << "[Sounds]\n";
        o << "\tenableRiverSound = " << (enableRiverSound ? "true" : "false") << "\n\n";

        o << "[General]\n";
        o << "\tshowWelcomeMessage = " << (showWelcomeMessage ? "true" : "false") << "\n";
        o << "\tdisableTectonicUndergroundRivers = " << (disableTectonicUndergroundRivers ? "true" : "false") << "\n";
        return o.str();
    }

    nlohmann::json toJson() const {
        return {
            {"enabled", enabled},
            {"normalRiverMode", int(normalRiverMode)},
            {"normalRiverDensity", int(normalRiverDensity)},
            {"normalRiverCanyonHeight", normalRiverCanyonHeight},
            {"normalRiverTunnels", normalRiverTunnels},
            {"normalTunnelHeight", normalTunnelHeight},
            {"normalTunnelLedge", normalTunnelLedge},
            {"normalTunnelPillars", normalTunnelPillars},
            {"normalTunnelFormations", normalTunnelFormations},
            {"deltaInlandReach", deltaInlandReach},
            {"riverSplits", int(riverSplits)},
            {"riverFallenLogs", int(riverFallenLogs)},
            {"enableWatersheds", enableWatersheds},
            {"mountainRiverDensity", int(mountainRiverDensity)},
            {"mountainRiverTunnels", mountainRiverTunnels},
            {"mountainTunnelFrequency", int(mountainTunnelFrequency)},
            {"mountainTunnelLength", mountainTunnelLength},
            {"mountainTunnelDepth", mountainTunnelDepth},
            {"mountainTunnelHeight", mountainTunnelHeight},
            {"mountainTunnelLedge", mountainTunnelLedge},
            {"mountainRiverWaterfalls", int(mountainRiverWaterfalls)},
            {"mountainRiverIce", int(mountainRiverIce)},
            {"riverIceFloes", int(riverIceFloes)},
            {"mountainRiverBiome", mountainRiverBiome},
            {"enableMountainLakes", enableMountainLakes},
            {"biomeBlacklistText", biomeBlacklistText},
            {"riverBoulders", riverBoulders},
            {"riverbankPalettes", riverbankPalettes},
            {"waterPlants", waterPlants},
            {"playerSpeed", playerSpeed},
            {"animalSpeed", animalSpeed},
            {"waterMobSpeed", waterMobSpeed},
            {"itemSpeed", itemSpeed},
            {"defaultSpeed", defaultSpeed},
            {"sableShipSpeed", sableShipSpeed},
            {"rainFlowBoost", rainFlowBoost},
            {"thunderstormFlowBoost", thunderstormFlowBoost},
            {"vanillaRiverFlow", vanillaRiverFlow},
            {"customEntitySpeedsText", customEntitySpeedsText},
            {"enableRiverParticles", enableRiverParticles},
            {"riverParticleDensity", riverParticleDensity},
            {"enableRiverVisualFlow", enableRiverVisualFlow},
            {"customMainMenuPanorama", customMainMenuPanorama},
            {"showMainMenuButton", showMainMenuButton},
            {"enableRiverSound", enableRiverSound},
            {"showWelcomeMessage", showWelcomeMessage},
            {"disableTectonicUndergroundRivers", disableTectonicUndergroundRivers}
        };
    }

    static DynamicWatersConfig fromJson(const nlohmann::json& j) {
        DynamicWatersConfig c;
        if (!j.is_object()) return c;
        auto b = [&](const char* k, bool& v) { if (j.contains(k) && j[k].is_boolean()) v = j[k].get<bool>(); };
        auto i = [&](const char* k, int& v, int lo, int hi) {
            if (j.contains(k) && j[k].is_number_integer()) v = std::clamp(j[k].get<int>(), lo, hi);
        };
        auto d = [&](const char* k, double& v, double lo, double hi) {
            if (j.contains(k) && j[k].is_number()) v = std::clamp(j[k].get<double>(), lo, hi);
        };
        auto e = [&](const char* k, auto& v, int hi) {
            if (j.contains(k) && j[k].is_number_integer()) v = static_cast<std::decay_t<decltype(v)>>(std::clamp(j[k].get<int>(), 0, hi));
        };
        auto s = [&](const char* k, std::string& v) { if (j.contains(k) && j[k].is_string()) v = j[k].get<std::string>(); };

        b("enabled", c.enabled);
        e("normalRiverMode", c.normalRiverMode, 2);
        e("normalRiverDensity", c.normalRiverDensity, 6);
        d("normalRiverCanyonHeight", c.normalRiverCanyonHeight, 60.0, 2000.0);
        b("normalRiverTunnels", c.normalRiverTunnels);
        i("normalTunnelHeight", c.normalTunnelHeight, 8, 40);
        i("normalTunnelLedge", c.normalTunnelLedge, 0, 12);
        b("normalTunnelPillars", c.normalTunnelPillars);
        b("normalTunnelFormations", c.normalTunnelFormations);
        i("deltaInlandReach", c.deltaInlandReach, 0, 512);
        e("riverSplits", c.riverSplits, 4);
        e("riverFallenLogs", c.riverFallenLogs, 3);
        b("enableWatersheds", c.enableWatersheds);
        e("mountainRiverDensity", c.mountainRiverDensity, 6);
        b("mountainRiverTunnels", c.mountainRiverTunnels);
        e("mountainTunnelFrequency", c.mountainTunnelFrequency, 2);
        i("mountainTunnelLength", c.mountainTunnelLength, 64, 640);
        i("mountainTunnelDepth", c.mountainTunnelDepth, 16, 64);
        i("mountainTunnelHeight", c.mountainTunnelHeight, 5, 24);
        i("mountainTunnelLedge", c.mountainTunnelLedge, 1, 8);
        e("mountainRiverWaterfalls", c.mountainRiverWaterfalls, 3);
        e("mountainRiverIce", c.mountainRiverIce, 2);
        e("riverIceFloes", c.riverIceFloes, 4);
        b("mountainRiverBiome", c.mountainRiverBiome);
        b("enableMountainLakes", c.enableMountainLakes);
        s("biomeBlacklistText", c.biomeBlacklistText);
        b("riverBoulders", c.riverBoulders);
        b("riverbankPalettes", c.riverbankPalettes);
        b("waterPlants", c.waterPlants);

        d("playerSpeed", c.playerSpeed, 0.0, 10.0);
        d("animalSpeed", c.animalSpeed, 0.0, 10.0);
        d("waterMobSpeed", c.waterMobSpeed, 0.0, 10.0);
        d("itemSpeed", c.itemSpeed, 0.0, 10.0);
        d("defaultSpeed", c.defaultSpeed, 0.0, 10.0);
        d("sableShipSpeed", c.sableShipSpeed, 0.0, 10.0);
        d("rainFlowBoost", c.rainFlowBoost, 0.0, 10.0);
        d("thunderstormFlowBoost", c.thunderstormFlowBoost, 0.0, 10.0);
        b("vanillaRiverFlow", c.vanillaRiverFlow);
        s("customEntitySpeedsText", c.customEntitySpeedsText);

        b("enableRiverParticles", c.enableRiverParticles);
        i("riverParticleDensity", c.riverParticleDensity, 1, 100);
        b("enableRiverVisualFlow", c.enableRiverVisualFlow);
        b("customMainMenuPanorama", c.customMainMenuPanorama);
        b("showMainMenuButton", c.showMainMenuButton);
        b("enableRiverSound", c.enableRiverSound);
        b("showWelcomeMessage", c.showWelcomeMessage);
        b("disableTectonicUndergroundRivers", c.disableTectonicUndergroundRivers);
        return c;
    }
};

} // namespace dw
