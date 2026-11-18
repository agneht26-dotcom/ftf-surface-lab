// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// Adapted from Dynamic Waters 13.1.1 (AdamNew, MPL-2.0), ported to C++ for FTF Surface Lab (2026-11);
// see LICENSES/DynamicWaters-MPL-2.0.txt (notices/DynamicWaters-MPL-2.0.txt in release packages).
#include "dynamic_waters_hydrology.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace dw {

static constexpr double DW_PI = 3.14159265358979323846;

namespace {

int32_t javaStringHash(const std::string& s) {
    uint32_t h = 0;
    for (unsigned char c : s) {
        h = h * 31u + uint32_t(c);
    }
    return int32_t(h);
}

std::string stripNamespace(const std::string& s) {
    auto pos = s.find(':');
    return pos == std::string::npos ? s : s.substr(pos + 1);
}

std::string trimCopy(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

uint32_t parseTagMaskBit(const std::string& rawTag) {
    std::string t = rawTag;
    if (!t.empty() && t[0] == '#') t = t.substr(1);
    std::string lower;
    lower.reserve(t.size());
    for (unsigned char c : t) lower.push_back(char(std::tolower(c)));

    if (lower == "c:is_forest" || lower == "minecraft:is_forest") return TAG_FOREST;
    if (lower == "c:is_taiga" || lower == "minecraft:is_taiga") return TAG_TAIGA;
    if (lower == "c:is_snowy") return TAG_SNOWY;
    if (lower == "c:is_icy") return TAG_ICY;
    if (lower == "c:is_swamp") return TAG_SWAMP;
    if (lower == "c:is_beach" || lower == "minecraft:is_beach") return TAG_BEACH;
    if (lower == "c:is_ocean" || lower == "minecraft:is_ocean") return TAG_OCEAN;
    if (lower == "c:is_river" || lower == "minecraft:is_river") return TAG_RIVER;
    if (lower == "c:is_badlands" || lower == "minecraft:is_badlands") return TAG_BADLANDS;
    if (lower == "c:is_desert") return TAG_DESERT;
    if (lower == "c:is_jungle" || lower == "minecraft:is_jungle") return TAG_JUNGLE;
    if (lower == "c:is_savanna" || lower == "minecraft:is_savanna") return TAG_SAVANNA;
    if (lower == "c:is_plains") return TAG_PLAINS;
    if (lower == "c:is_mushroom") return TAG_MUSHROOM;
    if (lower == "c:is_stony_shores") return TAG_STONY_SHORES;
    if (lower == "c:is_dry/overworld") return TAG_DRY_OVERWORLD;
    if (lower == "c:is_hot/overworld") return TAG_HOT_OVERWORLD;
    if (lower == "c:is_lush") return TAG_LUSH;
    if (lower == "c:is_underground") return TAG_UNDERGROUND;
    if (lower == "c:is_mountain" || lower == "minecraft:is_mountain" || lower == "c:is_mountain/slope" || lower == "c:is_mountain/peak")
        return TAG_MOUNTAIN;
    return 0u;
}

std::vector<std::string> jsonStrings(const nlohmann::json& j, const char* key) {
    std::vector<std::string> out;
    if (!j.is_object() || !j.contains(key)) return out;
    const auto& e = j[key];
    if (e.is_array()) {
        for (const auto& item : e) {
            if (item.is_string()) out.push_back(item.get<std::string>());
        }
    } else if (e.is_string()) {
        out.push_back(e.get<std::string>());
    }
    return out;
}

BiomeMatch parseBiomeMatch(const nlohmann::json& root) {
    BiomeMatch bm;
    if (!root.is_object() || !root.contains("where") || !root["where"].is_object()) return bm;
    const auto& w = root["where"];
    for (const auto& s : jsonStrings(w, "biomes")) {
        bm.biomes.push_back(trimCopy(s));
    }
    for (const auto& s : jsonStrings(w, "tags")) {
        uint32_t bit = parseTagMaskBit(trimCopy(s));
        if (bit) bm.tags.push_back(bit);
    }
    for (const auto& s : jsonStrings(w, "not")) {
        std::string ts = trimCopy(s);
        if (!ts.empty() && ts[0] == '#') {
            uint32_t bit = parseTagMaskBit(ts);
            if (bit) bm.notTags.push_back(bit);
        } else if (!ts.empty()) {
            bm.notBiomes.push_back(ts);
        }
    }
    if (w.contains("climate") && w["climate"].is_object()) {
        const auto& c = w["climate"];
        bm.hasClimate = true;
        if (c.contains("temperature") && c["temperature"].is_array() && c["temperature"].size() >= 2) {
            bm.minTemp = c["temperature"][0].get<float>();
            bm.maxTemp = c["temperature"][1].get<float>();
        }
        if (c.contains("downfall") && c["downfall"].is_array() && c["downfall"].size() >= 2) {
            bm.minDownfall = c["downfall"][0].get<float>();
            bm.maxDownfall = c["downfall"][1].get<float>();
        }
    }
    return bm;
}

template <typename T, typename GetWhere>
const T* chooseByBiome(const std::vector<T>& all, GetWhere&& getWhere, const BiomeMeta& biome) {
    for (const auto& s : all) {
        const BiomeMatch& m = getWhere(s);
        if (m.namesBiome(biome) && !m.excluded(biome)) return &s;
    }
    for (const auto& s : all) {
        const BiomeMatch& m = getWhere(s);
        if (m.hasAnyTag(biome) && m.inClimate(biome) && !m.excluded(biome)) return &s;
    }
    const T* best = nullptr;
    for (const auto& s : all) {
        const BiomeMatch& m = getWhere(s);
        if (m.byClimateOnly() && m.inClimate(biome) && !m.excluded(biome)) {
            if (!best || m.climateWindow() < getWhere(*best).climateWindow()) best = &s;
        }
    }
    if (best) return best;
    for (const auto& s : all) {
        const BiomeMatch& m = getWhere(s);
        if (m.isDefault() && !m.excluded(biome)) return &s;
    }
    return nullptr;
}

std::string resolveAssetPath(const std::string& assetsDir, const char* sub, const char* file) {
    std::error_code ec;
    std::vector<std::filesystem::path> bases = {
        std::filesystem::path(assetsDir),
        std::filesystem::current_path() / "assets" / "dynamicwaters",
        std::filesystem::path("assets/dynamicwaters"),
    };
    for (const auto& b : bases) {
        auto p = b / sub / file;
        if (std::filesystem::exists(p, ec)) return p.string();
    }
    return "";
}

} // namespace

DwBlockRef parseDwBlock(const std::string& rawId) {
    std::string s = trimCopy(rawId);
    auto bracket = s.find('[');
    std::string base = bracket == std::string::npos ? s : s.substr(0, bracket);
    std::string low;
    low.reserve(base.size());
    for (unsigned char c : base) low.push_back(char(std::tolower(c)));
    if (low == "keep" || low == "dynamicwaters:keep") return {DwBlock::Keep, "dynamicwaters:keep"};
    if (low.find(':') == std::string::npos) low = "minecraft:" + low;

    static const std::unordered_map<std::string, DwBlock> kTable = {
        {"minecraft:air", DwBlock::Air},
        {"minecraft:water", DwBlock::Water},
        {"minecraft:ice", DwBlock::Ice},
        {"minecraft:packed_ice", DwBlock::PackedIce},
        {"minecraft:stone", DwBlock::Stone},
        {"minecraft:deepslate", DwBlock::Deepslate},
        {"minecraft:cobblestone", DwBlock::Cobblestone},
        {"minecraft:mossy_cobblestone", DwBlock::MossyCobblestone},
        {"minecraft:andesite", DwBlock::Andesite},
        {"minecraft:diorite", DwBlock::Diorite},
        {"minecraft:granite", DwBlock::Granite},
        {"minecraft:tuff", DwBlock::Tuff},
        {"minecraft:calcite", DwBlock::Calcite},
        {"minecraft:dripstone_block", DwBlock::DripstoneBlock},
        {"minecraft:gravel", DwBlock::Gravel},
        {"minecraft:sand", DwBlock::Sand},
        {"minecraft:red_sand", DwBlock::RedSand},
        {"minecraft:sandstone", DwBlock::Sandstone},
        {"minecraft:smooth_sandstone", DwBlock::SmoothSandstone},
        {"minecraft:red_sandstone", DwBlock::RedSandstone},
        {"minecraft:clay", DwBlock::Clay},
        {"minecraft:mud", DwBlock::Mud},
        {"minecraft:muddy_mangrove_roots", DwBlock::MuddyMangroveRoots},
        {"minecraft:dirt", DwBlock::Dirt},
        {"minecraft:coarse_dirt", DwBlock::CoarseDirt},
        {"minecraft:rooted_dirt", DwBlock::RootedDirt},
        {"minecraft:podzol", DwBlock::Podzol},
        {"minecraft:grass_block", DwBlock::GrassBlock},
        {"minecraft:mycelium", DwBlock::Mycelium},
        {"minecraft:moss_block", DwBlock::MossBlock},
        {"minecraft:moss_carpet", DwBlock::MossCarpet},
        {"minecraft:snow_block", DwBlock::SnowBlock},
        {"minecraft:terracotta", DwBlock::Terracotta},
        {"minecraft:orange_terracotta", DwBlock::OrangeTerracotta},
        {"minecraft:red_terracotta", DwBlock::RedTerracotta},
        {"minecraft:yellow_terracotta", DwBlock::YellowTerracotta},
        {"minecraft:brown_terracotta", DwBlock::BrownTerracotta},
        {"minecraft:white_terracotta", DwBlock::WhiteTerracotta},
        {"minecraft:light_gray_terracotta", DwBlock::LightGrayTerracotta},
        {"minecraft:kelp", DwBlock::Kelp},
        {"minecraft:kelp_plant", DwBlock::KelpPlant},
        {"minecraft:seagrass", DwBlock::Seagrass},
        {"minecraft:tall_seagrass", DwBlock::TallSeagrass},
        {"minecraft:sea_pickle", DwBlock::SeaPickle},
        {"minecraft:lily_pad", DwBlock::LilyPad},
        {"minecraft:sugar_cane", DwBlock::SugarCane},
        {"minecraft:oak_log", DwBlock::OakLog},
        {"minecraft:spruce_log", DwBlock::SpruceLog},
    };
    auto it = kTable.find(low);
    return {it != kTable.end() ? it->second : DwBlock::Custom, low};
}

const char* dwBlockName(DwBlock b) {
    switch (b) {
        case DwBlock::Keep: return "dynamicwaters:keep";
        case DwBlock::Air: return "minecraft:air";
        case DwBlock::Water: return "minecraft:water";
        case DwBlock::FlowingWater: return "minecraft:water";
        case DwBlock::Ice: return "minecraft:ice";
        case DwBlock::PackedIce: return "minecraft:packed_ice";
        case DwBlock::Stone: return "minecraft:stone";
        case DwBlock::Deepslate: return "minecraft:deepslate";
        case DwBlock::Cobblestone: return "minecraft:cobblestone";
        case DwBlock::MossyCobblestone: return "minecraft:mossy_cobblestone";
        case DwBlock::Andesite: return "minecraft:andesite";
        case DwBlock::Diorite: return "minecraft:diorite";
        case DwBlock::Granite: return "minecraft:granite";
        case DwBlock::Tuff: return "minecraft:tuff";
        case DwBlock::Calcite: return "minecraft:calcite";
        case DwBlock::DripstoneBlock: return "minecraft:dripstone_block";
        case DwBlock::Gravel: return "minecraft:gravel";
        case DwBlock::Sand: return "minecraft:sand";
        case DwBlock::RedSand: return "minecraft:red_sand";
        case DwBlock::Sandstone: return "minecraft:sandstone";
        case DwBlock::SmoothSandstone: return "minecraft:smooth_sandstone";
        case DwBlock::RedSandstone: return "minecraft:red_sandstone";
        case DwBlock::Clay: return "minecraft:clay";
        case DwBlock::Mud: return "minecraft:mud";
        case DwBlock::MuddyMangroveRoots: return "minecraft:muddy_mangrove_roots";
        case DwBlock::Dirt: return "minecraft:dirt";
        case DwBlock::CoarseDirt: return "minecraft:coarse_dirt";
        case DwBlock::RootedDirt: return "minecraft:rooted_dirt";
        case DwBlock::Podzol: return "minecraft:podzol";
        case DwBlock::GrassBlock: return "minecraft:grass_block";
        case DwBlock::Mycelium: return "minecraft:mycelium";
        case DwBlock::MossBlock: return "minecraft:moss_block";
        case DwBlock::MossCarpet: return "minecraft:moss_carpet";
        case DwBlock::SnowBlock: return "minecraft:snow_block";
        case DwBlock::Terracotta: return "minecraft:terracotta";
        case DwBlock::OrangeTerracotta: return "minecraft:orange_terracotta";
        case DwBlock::RedTerracotta: return "minecraft:red_terracotta";
        case DwBlock::YellowTerracotta: return "minecraft:yellow_terracotta";
        case DwBlock::BrownTerracotta: return "minecraft:brown_terracotta";
        case DwBlock::WhiteTerracotta: return "minecraft:white_terracotta";
        case DwBlock::LightGrayTerracotta: return "minecraft:light_gray_terracotta";
        case DwBlock::Kelp: return "minecraft:kelp";
        case DwBlock::KelpPlant: return "minecraft:kelp_plant";
        case DwBlock::Seagrass: return "minecraft:seagrass";
        case DwBlock::TallSeagrass: return "minecraft:tall_seagrass";
        case DwBlock::SeaPickle: return "minecraft:sea_pickle";
        case DwBlock::LilyPad: return "minecraft:lily_pad";
        case DwBlock::SugarCane: return "minecraft:sugar_cane";
        case DwBlock::OakLog: return "minecraft:oak_log";
        case DwBlock::SpruceLog: return "minecraft:spruce_log";
        default: return "minecraft:stone";
    }
}

uint32_t dwBlockRgb24(DwBlock b) {
    switch (b) {
        case DwBlock::Ice: return 0x9AD2FFu;
        case DwBlock::PackedIce: return 0x8DB4F7u;
        case DwBlock::Stone: return 0x7D7D7Du;
        case DwBlock::Deepslate: return 0x515157u;
        case DwBlock::Cobblestone: return 0x7A7A7Au;
        case DwBlock::MossyCobblestone: return 0x67795Eu;
        case DwBlock::Andesite: return 0x88888Au;
        case DwBlock::Diorite: return 0xC6C6C6u;
        case DwBlock::Granite: return 0x9A6B59u;
        case DwBlock::Tuff: return 0x6C6E67u;
        case DwBlock::Calcite: return 0xE2E2DAu;
        case DwBlock::DripstoneBlock: return 0x856E61u;
        case DwBlock::Gravel: return 0x827F7Eu;
        case DwBlock::Sand: return 0xDCD3A1u;
        case DwBlock::RedSand: return 0xC06825u;
        case DwBlock::Sandstone: return 0xDACFA0u;
        case DwBlock::SmoothSandstone: return 0xDECFA0u;
        case DwBlock::RedSandstone: return 0xB86220u;
        case DwBlock::Clay: return 0xA0A6B3u;
        case DwBlock::Mud: return 0x3C393Du;
        case DwBlock::MuddyMangroveRoots: return 0x4A3E36u;
        case DwBlock::Dirt: return 0x866043u;
        case DwBlock::CoarseDirt: return 0x77553Bu;
        case DwBlock::RootedDirt: return 0x8F6748u;
        case DwBlock::Podzol: return 0x5B3F18u;
        case DwBlock::GrassBlock: return 0x6E9D44u;
        case DwBlock::Mycelium: return 0x6F6265u;
        case DwBlock::MossBlock: return 0x5C7830u;
        case DwBlock::MossCarpet: return 0x648335u;
        case DwBlock::SnowBlock: return 0xF0F7F7u;
        case DwBlock::Terracotta: return 0x985D43u;
        case DwBlock::OrangeTerracotta: return 0xA15325u;
        case DwBlock::RedTerracotta: return 0x8F3D2Eu;
        case DwBlock::YellowTerracotta: return 0xBA8523u;
        case DwBlock::BrownTerracotta: return 0x4D3324u;
        case DwBlock::WhiteTerracotta: return 0xD2B2A1u;
        case DwBlock::LightGrayTerracotta: return 0x876A61u;
        case DwBlock::Kelp:
        case DwBlock::KelpPlant: return 0x4C792Fu;
        case DwBlock::Seagrass:
        case DwBlock::TallSeagrass: return 0x4E8A38u;
        case DwBlock::SeaPickle: return 0x697D37u;
        case DwBlock::LilyPad: return 0x3D7A32u;
        case DwBlock::SugarCane: return 0x7EB553u;
        case DwBlock::OakLog: return 0x6B5232u;
        case DwBlock::SpruceLog: return 0x3E2915u;
        case DwBlock::Water:
        case DwBlock::FlowingWater: return 0x3F76E4u;
        default: return 0x7D7D7Du;
    }
}

// ---------- BiomeMatch ----------
bool BiomeMatch::namesBiome(const BiomeMeta& meta) const {
    if (meta.name.empty()) return false;
    std::string metaWithNs = meta.name.find(':') == std::string::npos ? ("minecraft:" + meta.name) : meta.name;
    for (const auto& b : biomes) {
        if (b == metaWithNs || stripNamespace(b) == stripNamespace(metaWithNs)) return true;
    }
    return false;
}

bool BiomeMatch::hasAnyTag(const BiomeMeta& meta) const {
    for (uint32_t t : tags) {
        if ((meta.tags & t) != 0) return true;
    }
    return false;
}

bool BiomeMatch::inClimate(const BiomeMeta& meta) const {
    if (!hasClimate) return true;
    return meta.temperature >= minTemp && meta.temperature <= maxTemp
        && meta.downfall >= minDownfall && meta.downfall <= maxDownfall;
}

bool BiomeMatch::excluded(const BiomeMeta& meta) const {
    if (!meta.name.empty()) {
        std::string metaWithNs = meta.name.find(':') == std::string::npos ? ("minecraft:" + meta.name) : meta.name;
        for (const auto& b : notBiomes) {
            if (b == metaWithNs || stripNamespace(b) == stripNamespace(metaWithNs)) return true;
        }
    }
    for (uint32_t t : notTags) {
        if ((meta.tags & t) != 0) return true;
    }
    return false;
}

bool BiomeMatch::matches(const BiomeMeta& meta) const {
    return !excluded(meta) && (namesBiome(meta) || (hasAnyTag(meta) && inClimate(meta)) || (byClimateOnly() && inClimate(meta)) || isDefault());
}

DwBlockRef RiverbankPalette::Mix::pick(int32_t x, int32_t z, int32_t size, int32_t salt) const {
    if (blocks.empty() || totalWeight <= 0) return {DwBlock::Keep, ""};
    int idx = Patches::spot(weights.data(), int(weights.size()), totalWeight, x, z, size, salt);
    if (idx < 0 || idx >= int(blocks.size())) return {DwBlock::Keep, ""};
    return blocks[idx];
}

// ---------- Ravine ----------
static double ravineSway(double along, double length) {
    double in = mthClamp(along / 8.0, 0.0, 1.0);
    double out = mthClamp((length - along) / 16.0, 0.0, 1.0);
    return in * in * (3.0 - 2.0 * in) * out * out * (3.0 - 2.0 * out);
}

static double ravineCurveVal(double amp, double phase, double along, double length) {
    if (amp <= 0.0) return 0.0;
    return amp * ravineSway(along, length) * std::sin(2.0 * DW_PI * along / 44.0 + phase);
}

double Ravine::curve(double along) const {
    return ravineCurveVal(curveAmp, curvePhase, along, length);
}

double Ravine::centre(const SimplexNoise& bedNoise, double along) const {
    return curve(along) + 2.0 * ravineSway(along, length) * bedNoise.getValue(
        (px + ux * along) * 0.06 + 71.0, (pz + uz * along) * 0.06 - 29.0);
}

double Ravine::across(const SimplexNoise& bedNoise, double x, double z, double ox, double oz, double along) const {
    double bend = 2.0 * ravineSway(along, length) * bedNoise.getValue(
        (px + ux * along) * 0.06 + 71.0, (pz + uz * along) * 0.06 - 29.0);
    return std::max(0.0, std::abs(lateral(ox, oz, along) - bend)
        + 1.2 * bedNoise.getValue(x * 0.08 - 13.0, z * 0.08 + 57.0));
}

int Ravine::levelAt(double t) const {
    if (from.empty() || level.empty()) return 0;
    int i = int(from.size()) - 1;
    while (i > 0 && t < from[i]) i--;
    return level[i];
}

int Ravine::poolLevel(const SimplexNoise& bedNoise, double x, double z, double along) const {
    return levelAt(along + 0.5 * bedNoise.getValue(x * 0.27 + 31.0, z * 0.27 - 83.0));
}

int Ravine::waterAt(const SimplexNoise& bedNoise, double x, double z) const {
    double ox = x - px, oz = z - pz;
    double along = ox * ux + oz * uz;
    if (along < -2.0 || along > length + 8.0 || across(bedNoise, x, z, ox, oz, along) >= width) {
        return INT32_MIN;
    }
    return poolLevel(bedNoise, x, z, along);
}

double Ravine::sideLine(double t) const {
    int j = int(from.size()) - 1;
    while (j > 0 && t < from[j]) j--;
    double line = (j == 0) ? double(top) : double(level[j]);
    for (int i = 0; i < j; ++i) {
        line = std::max(line, double(i == 0 ? top : level[i]) - 2.5 * (t - from[i + 1]));
    }
    return line;
}

// ---------- HydrologyEngine ----------
namespace {
std::shared_ptr<const std::vector<RiverGraphEdge>> owningRegionEdges(
        MainRiversEngine& mr, const RiverGraphEdge& edge, double approxX, double approxZ) {
    int brx = floorDiv(int(std::floor(approxX)), MainRiversEngine::REGION_SIZE);
    int brz = floorDiv(int(std::floor(approxZ)), MainRiversEngine::REGION_SIZE);
    auto r = mr.getRegion(brx, brz);
    if (r && !r->empty() && &edge >= r->data() && &edge < r->data() + r->size()) return r;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue;
            auto nr = mr.getRegion(brx + dx, brz + dz);
            if (nr && !nr->empty() && &edge >= nr->data() && &edge < nr->data() + nr->size()) return nr;
        }
    }
    return r;
}
} // namespace

void HydrologyEngine::reset(int64_t worldSeed, const DynamicWatersConfig& cfg, HydrologyCallbacks cb) {
    seed_ = worldSeed;
    cfg_ = cfg;
    cb_ = std::move(cb);
    noises_.init(worldSeed);

    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        ravineOwnedChunks_.clear();
        for (int i = 0; i < (ravineCount_ + 255) >> 8 && i < int(RAVINE_MAX_CHUNKS); ++i) {
            ravineChunks_[size_t(i)].store(nullptr, std::memory_order_relaxed);
        }
        ravineCount_ = 0;
        headFeedCache_.clear();
        poolBodyCache_.clear();
        poolClearCache_.clear();
        hollowSurfaceCache_.clear();
        resetEpoch_.fetch_add(1, std::memory_order_release);
    }

    mainRivers_.reset(worldSeed, cfg_, TerrainQueryCallbacks{
        cb_.seaLevel,
        cb_.isOceanBiomeAt ? cb_.isOceanBiomeAt : cb_.isMouthBiomeAt,
        cb_.sampleCoarseCell
    });

    basin_.reset(worldSeed, cfg_, &noises_, &mainRivers_, BasinCallbacks{
        cb_.seaLevel,
        cb_.sampleCoarseCell,
        cb_.sampleSurfaceAt,
        cb_.isMouthBiomeAt
    });

    loadAssets(cb_.assetsDir);
    initialized_ = true;
}

void HydrologyEngine::setCallbacks(HydrologyCallbacks cb) {
    cb_ = std::move(cb);
    mainRivers_.setCallbacks(TerrainQueryCallbacks{
        cb_.seaLevel,
        cb_.isOceanBiomeAt ? cb_.isOceanBiomeAt : cb_.isMouthBiomeAt,
        cb_.sampleCoarseCell
    });
    basin_.setCallbacks(BasinCallbacks{
        cb_.seaLevel,
        cb_.sampleCoarseCell,
        cb_.sampleSurfaceAt,
        cb_.isMouthBiomeAt
    });
}

void HydrologyEngine::prewarm(int minBlockX, int minBlockZ, int maxBlockX, int maxBlockZ) {
    if (!initialized_ || !cfg_.enabled) return;
    mainRivers_.prewarm(minBlockX, minBlockZ, maxBlockX, maxBlockZ);
    basin_.prewarm(minBlockX, minBlockZ, maxBlockX, maxBlockZ);
}

void HydrologyEngine::loadAssets(const std::string& assetsDir) {
    static const char* kRiverbankBuiltIn[] = {
        "default.json",
        "tundra.json", "wetland.json", "mangrove.json", "dark_forest.json", "red_canyons.json", "hot_desert.json",
        "rainforest.json", "dry_grassland.json", "alpine_meadow.json", "highlands.json", "conifer_forest.json",
        "broadleaf_forest.json", "grassland.json", "mushroom_isle.json", "rocky_coast.json", "sea_coast.json",
        "river_valley.json", "lush_cave.json", "dripstone_cave.json",
        "warm_dry.json", "cold_climate.json", "humid_climate.json",
    };
    static const char* kPlaces[] = { "rapids", "silt", "shore", "slope", "bars", "flats" };
    static const char* kPlantBuiltIn[] = { "default.json", "warm_waters.json" };
    static const char* kPlantKinds[] = { "lowland", "mountain", "lake" };

    palettes_.clear();
    for (const char* fn : kRiverbankBuiltIn) {
        std::string path = resolveAssetPath(assetsDir, "riverbanks", fn);
        if (path.empty()) continue;
        std::ifstream f(path, std::ios::binary);
        if (!f.good()) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        try {
            nlohmann::json o = nlohmann::json::parse(text);
            if (!o.is_object()) continue;
            RiverbankPalette pal;
            pal.source = fn;
            pal.where = parseBiomeMatch(o);
            pal.enabled = o.value("enabled", true);
            pal.patchSize = std::max(1, std::min(16, o.value("patch_size", 5)));
            pal.shoreHeight = std::max(0, std::min(6, o.value("shore_height", 1)));
            for (int p = 0; p < 6; ++p) {
                auto& mix = pal.places[p];
                for (const auto& rawEntry : jsonStrings(o, kPlaces[p])) {
                    std::string id = trimCopy(rawEntry);
                    int weight = 1;
                    auto star = id.rfind('*');
                    if (star != std::string::npos && star > 0) {
                        try { weight = std::max(1, std::stoi(trimCopy(id.substr(star + 1)))); } catch (...) {}
                        id = trimCopy(id.substr(0, star));
                    }
                    DwBlockRef blk = parseDwBlock(id);
                    if (!blk.isKeep()) mix.hasNonKeep = true;
                    mix.blocks.push_back(std::move(blk));
                    mix.weights.push_back(weight);
                    mix.totalWeight += weight;
                }
            }
            palettes_.push_back(std::move(pal));
        } catch (...) {}
    }

    plantSets_.clear();
    for (const char* fn : kPlantBuiltIn) {
        std::string path = resolveAssetPath(assetsDir, "water_plants", fn);
        if (path.empty()) continue;
        std::ifstream f(path, std::ios::binary);
        if (!f.good()) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        try {
            nlohmann::json o = nlohmann::json::parse(text);
            if (!o.is_object()) continue;
            WaterPlantSet wset;
            wset.source = fn;
            wset.where = parseBiomeMatch(o);
            wset.enabled = o.value("enabled", true);
            for (int k = 0; k < 3; ++k) {
                const char* kindName = kPlantKinds[k];
                if (!o.contains(kindName) || !o[kindName].is_array()) continue;
                for (const auto& pe : o[kindName]) {
                    nlohmann::json po;
                    if (pe.is_string()) {
                        po = nlohmann::json::object();
                        po["block"] = pe.get<std::string>();
                    } else if (pe.is_object()) {
                        po = pe;
                    } else continue;
                    std::string raw = po.contains("block") && po["block"].is_string() ? trimCopy(po["block"].get<std::string>()) : "";
                    if (raw.empty()) continue;
                    WaterPlant wp;
                    wp.id = raw;
                    wp.ownState = raw.find('[') != std::string::npos;
                    wp.block = parseDwBlock(raw);
                    if (wp.block.isAir()) continue;
                    if (wp.block.id == DwBlock::LilyPad) wp.at = WaterPlant::At::Surface;
                    else if (wp.block.id == DwBlock::SugarCane) wp.at = WaterPlant::At::Shore;
                    else wp.at = WaterPlant::At::Bed;
                    if (po.contains("at") && po["at"].is_string()) {
                        std::string ats = trimCopy(po["at"].get<std::string>());
                        if (ats == "bed") wp.at = WaterPlant::At::Bed;
                        else if (ats == "surface") wp.at = WaterPlant::At::Surface;
                        else if (ats == "shore") wp.at = WaterPlant::At::Shore;
                    }
                    int d0 = 1, d1 = 64;
                    if (po.contains("depth")) {
                        const auto& d = po["depth"];
                        if (d.is_array() && !d.empty()) {
                            d0 = d[0].get<int>();
                            d1 = d[d.size() > 1 ? 1 : 0].get<int>();
                        } else if (d.is_number()) {
                            d0 = d1 = d.get<int>();
                        }
                    }
                    int h0 = 1, h1 = 1;
                    if (po.contains("height")) {
                        const auto& h = po["height"];
                        if (h.is_array() && !h.empty()) {
                            h0 = h[0].get<int>();
                            h1 = h[h.size() > 1 ? 1 : 0].get<int>();
                        } else if (h.is_number()) {
                            h0 = h1 = h.get<int>();
                        }
                    }
                    wp.minDepth = std::max(1, std::min(d0, d1));
                    wp.maxDepth = std::max(1, std::max(d0, d1));
                    wp.minHeight = std::max(1, std::min(h0, h1));
                    wp.maxHeight = std::min(16, std::max(1, std::max(h0, h1)));
                    wp.chance = mthClamp(po.value("chance", 0.05), 0.0, 1.0);
                    wp.calm = po.value("calm", false);
                    wp.patchSize = std::max(0, std::min(32, po.value("patch_size", 0)));
                    wp.patchShare = mthClamp(po.value("patch_share", 0.3), 0.0, 1.0);
                    wp.alone = po.value("alone", false);
                    wp.salt = po.contains("salt") ? po["salt"].get<int32_t>() : (javaStringHash(raw) * int32_t(0x9E3779B1u)) ^ (javaStringHash(kindName) * int32_t(0x85EBCA77u));
                    wset.kinds[k].push_back(std::move(wp));
                }
            }
            plantSets_.push_back(std::move(wset));
        } catch (...) {}
    }
}

const RiverbankPalette* HydrologyEngine::paletteForBiome(const BiomeMeta& meta) const {
    struct Slot {
        const HydrologyEngine* owner = nullptr;
        uint64_t epoch = 0;
        uint32_t id = UINT32_MAX;
        std::string name;
        const RiverbankPalette* result = nullptr;
        bool valid = false;
    };
    struct Tls {
        std::array<Slot, 16> slots{};
        uint32_t next = 0;
    };
    thread_local Tls tls;
    const uint64_t ep = resetEpoch_.load(std::memory_order_acquire);
    for (const auto& s : tls.slots) {
        if (s.valid && s.owner == this && s.epoch == ep && s.id == meta.id && s.name == meta.name) {
            return s.result;
        }
    }
    const RiverbankPalette* res = chooseByBiome(palettes_, [](const RiverbankPalette& p) -> const BiomeMatch& { return p.where; }, meta);
    Slot& dst = tls.slots[tls.next & 15u];
    ++tls.next;
    dst.owner = this;
    dst.epoch = ep;
    dst.id = meta.id;
    dst.name = meta.name;
    dst.result = res;
    dst.valid = true;
    return res;
}

const WaterPlantSet* HydrologyEngine::plantSetForBiome(const BiomeMeta& meta) const {
    struct Slot {
        const HydrologyEngine* owner = nullptr;
        uint64_t epoch = 0;
        uint32_t id = UINT32_MAX;
        std::string name;
        const WaterPlantSet* result = nullptr;
        bool valid = false;
    };
    struct Tls {
        std::array<Slot, 16> slots{};
        uint32_t next = 0;
    };
    thread_local Tls tls;
    const uint64_t ep = resetEpoch_.load(std::memory_order_acquire);
    for (const auto& s : tls.slots) {
        if (s.valid && s.owner == this && s.epoch == ep && s.id == meta.id && s.name == meta.name) {
            return s.result;
        }
    }
    const WaterPlantSet* res = chooseByBiome(plantSets_, [](const WaterPlantSet& s) -> const BiomeMatch& { return s.where; }, meta);
    Slot& dst = tls.slots[tls.next & 15u];
    ++tls.next;
    dst.owner = this;
    dst.epoch = ep;
    dst.id = meta.id;
    dst.name = meta.name;
    dst.result = res;
    dst.valid = true;
    return res;
}

WatershedContext HydrologyEngine::evaluateWatershedContext(double x, double z, bool cacheOnly) const {
    (void)cacheOnly;
    WatershedContext ctx;
    if (!cfg_.enabled || !cfg_.enableWatersheds) return ctx;
    struct TlsCtx {
        const HydrologyEngine* owner = nullptr;
        uint64_t rEp = 0, bEp = 0;
        double x = 0.0, z = 0.0;
        WatershedContext ctx{};
        bool valid = false;
    };
    thread_local TlsCtx last;
    const uint64_t rEp = resetEpoch_.load(std::memory_order_acquire);
    const uint64_t bEp = basin_.currentEpoch();
    if (last.valid && last.owner == this && last.rEp == rEp && last.bEp == bEp && last.x == x && last.z == z) {
        return last.ctx;
    }
    const int bx = mthFloor(x), bz = mthFloor(z);
    const int mrx = floorDiv(bx, hydrology_const::WATERSHED_REGION_SIZE);
    const int mrz = floorDiv(bz, hydrology_const::WATERSHED_REGION_SIZE);
    const auto& nb = const_cast<BasinTileEngine&>(basin_).getNeighborhood3x3(mrx, mrz);
    if (!nb.anyPaths || x < nb.pathsMinX || x > nb.pathsMaxX || z < nb.pathsMinZ || z > nb.pathsMaxZ) {
        last = {this, rEp, bEp, x, z, ctx, true};
        return ctx;
    }
    double bestRank = 1e300;
    constexpr double PAST_MOUTH_YIELD = 4.0;

    for (int dx = -WATERSHED_SCAN_RADIUS; dx <= WATERSHED_SCAN_RADIUS; ++dx) {
        for (int dz = -WATERSHED_SCAN_RADIUS; dz <= WATERSHED_SCAN_RADIUS; ++dz) {
            const int nrx = mrx + dx, nrz = mrz + dz;
            const RegionSpatialGrid* grid = (dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1)
                ? nb.grids[(dx + 1) * 3 + (dz + 1)]
                : nullptr;
            if (!grid || grid->paths.empty() || x < grid->pathsMinX || x > grid->pathsMaxX || z < grid->pathsMinZ || z > grid->pathsMaxZ) continue;
            for (int pidx : grid->indicesAt(bx, bz, nrx, nrz)) {
                const WatershedPath& mPath = grid->paths[pidx];
                auto ndata = mPath.getClosestNodeData(x, z);
                bool pastMouth = ndata.beyondEnd && ndata.nodeIndex == 0
                    && (x - ndata.x) * ndata.flowDirX + (z - ndata.z) * ndata.flowDirZ > 0.0;
                double rank = ndata.distance + (pastMouth ? PAST_MOUTH_YIELD : 0.0);
                if (rank < bestRank) {
                    bestRank = rank;
                    ctx.distance = ndata.distance;
                    ctx.bestPath = &mPath;
                    ctx.width = ndata.width;
                    ctx.bestY = ndata.y;
                    ctx.bestNodeIndex = ndata.nodeIndex;
                    ctx.flowDirX = ndata.flowDirX;
                    ctx.flowDirZ = ndata.flowDirZ;
                }
            }
        }
    }
    last = {this, rEp, bEp, x, z, ctx, true};
    return ctx;
}

const WatershedLake* HydrologyEngine::findLake(double x, double z, int columnY, bool cacheOnly, bool dugShores) const {
    (void)cacheOnly;
    if (!cfg_.enabled || !cfg_.enableWatersheds || !cfg_.enableMountainLakes) return nullptr;
    struct TlsLake {
        const HydrologyEngine* owner = nullptr;
        uint64_t rEp = 0, bEp = 0;
        double x = 0.0, z = 0.0;
        int columnY = 0;
        bool dugShores = false, valid = false;
        const WatershedLake* res = nullptr;
    };
    thread_local TlsLake last;
    const uint64_t rEp = resetEpoch_.load(std::memory_order_acquire);
    const uint64_t bEp = basin_.currentEpoch();
    if (last.valid && last.owner == this && last.rEp == rEp && last.bEp == bEp
        && last.x == x && last.z == z && last.columnY == columnY && last.dugShores == dugShores) {
        return last.res;
    }
    const int bx = mthFloor(x), bz = mthFloor(z);
    const int mrx = floorDiv(bx, hydrology_const::WATERSHED_REGION_SIZE);
    const int mrz = floorDiv(bz, hydrology_const::WATERSHED_REGION_SIZE);
    const auto& nb = const_cast<BasinTileEngine&>(basin_).getNeighborhood3x3(mrx, mrz);
    if (!nb.anyLakes || x < nb.lakesMinX || x >= nb.lakesMaxX || z < nb.lakesMinZ || z >= nb.lakesMaxZ) {
        last = {this, rEp, bEp, x, z, columnY, dugShores, true, nullptr};
        return nullptr;
    }
    const bool known = (columnY != INT32_MIN);
    const WatershedLake* insideAny = nullptr;
    const WatershedLake* nearby = nullptr;
    const WatershedLake* dugShore = nullptr;

    for (int i = 0; i < 9; ++i) {
        const RegionSpatialGrid* grid = nb.grids[i];
        if (!grid || grid->lakes.empty() || x < grid->lakesMinX || x >= grid->lakesMaxX || z < grid->lakesMinZ || z >= grid->lakesMaxZ) continue;
        for (const auto& lake : grid->lakes) {
            bool holds = !known || lake.holdsColumn(columnY);
            if (lake.inside(x, z)) {
                if (holds) { last = {this, rEp, bEp, x, z, columnY, dugShores, true, &lake}; return &lake; }
                if (!insideAny) insideAny = &lake;
            } else if (!nearby && lake.covers(x, z) && holds) {
                nearby = &lake;
            } else if (dugShores && !dugShore && known && columnY > lake.waterY
                       && lake.dig > WatershedLake::SHAVE && lake.covers(x, z)) {
                dugShore = &lake;
            }
        }
    }
    const WatershedLake* res = nearby ? nearby : (insideAny ? insideAny : dugShore);
    last = {this, rEp, bEp, x, z, columnY, dugShores, true, res};
    return res;
}

const WatershedLake* HydrologyEngine::lakeBeside(double x, double z, int columnY, bool /*cacheOnly*/) const {
    if (!cfg_.enabled || !cfg_.enableWatersheds || !cfg_.enableMountainLakes) return nullptr;
    const int mrx = floorDiv(mthFloor(x), hydrology_const::WATERSHED_REGION_SIZE);
    const int mrz = floorDiv(mthFloor(z), hydrology_const::WATERSHED_REGION_SIZE);
    const auto& nb = const_cast<BasinTileEngine&>(basin_).getNeighborhood3x3(mrx, mrz);
    if (!nb.anyLakes || x < nb.lakesMinX - 16.0 || x >= nb.lakesMaxX + 16.0 || z < nb.lakesMinZ - 16.0 || z >= nb.lakesMaxZ + 16.0) return nullptr;
    for (int i = 0; i < 9; ++i) {
        const RegionSpatialGrid* grid = nb.grids[i];
        if (!grid || grid->lakes.empty() || x < grid->lakesMinX - 16.0 || x >= grid->lakesMaxX + 16.0 || z < grid->lakesMinZ - 16.0 || z >= grid->lakesMaxZ + 16.0) continue;
        for (const auto& lake : grid->lakes) {
            if (columnY < lake.waterY && lake.near(x, z, 3)) return &lake;
        }
    }
    return nullptr;
}

bool HydrologyEngine::lakeDrainsHere(const WatershedLake& lake, double x, double z) const {
    const int ci = mthFloor(x) >> CoarseHeightCache::CELL_SHIFT;
    const int cj = mthFloor(z) >> CoarseHeightCache::CELL_SHIFT;
    for (int dj = 0; dj <= 1; ++dj) {
        for (int di = 0; di <= 1; ++di) {
            if (lake.isLakeCell(ci + di, cj + dj)) continue;
            if (const_cast<BasinTileEngine&>(basin_).heights().sampleCell(ci + di, cj + dj) <= lake.waterY)
                return true;
        }
    }
    return false;
}

bool HydrologyEngine::lakeWaterAt(const WatershedLake& lake, double x, double z, int ground) const {
    if (lake.bedColumn(ground) && lake.inside(x, z)) return true;
    if (lake.holdsColumn(ground) && lake.touchesInside(x, z)) return true;
    return ground < lake.waterY && lake.holdsColumn(ground) && lake.shoreBand(x, z)
        && !lake.touchesInside(x, z) && !lakeDrainsHere(lake, x, z);
}

int HydrologyEngine::lakeDepth(double shoreDistance) {
    double s = 1.0 - std::min(1.0, shoreDistance / WatershedLake::SHORE_REACH);
    return int(dwRound(LAKE_DEPTH - (LAKE_DEPTH - 1) * s * s));
}

bool HydrologyEngine::lakeHollowBesideWater(const WatershedLake& lake, int x, int z) const {
    static constexpr int DX4[4] = { 1, -1, 0, 0 };
    static constexpr int DZ4[4] = { 0, 0, 1, -1 };
    for (int d = 0; d < 4; ++d) {
        int nx = x + DX4[d], nz = z + DZ4[d];
        int gn = const_cast<BasinTileEngine&>(basin_).heights().surfaceAt(nx, nz) - 1;
        if (gn < lake.waterY && lakeWaterAt(lake, nx, nz, gn)) return true;
    }
    return false;
}

int HydrologyEngine::lipDepth(const WatershedLake& lake, bool covered, LakeHollowKind hollow, double x, double z, int ground) const {
    int lip = covered ? LAKE_SHORE_LIP : -1;
    if (hollow == LakeHollowKind::Open && ground < lake.waterY - lip && ground >= lake.waterY - 8
        && lakeHollowBesideWater(lake, mthFloor(x), mthFloor(z))) {
        return 8;
    }
    return lip;
}

HydrologyEngine::LakeHollowKind HydrologyEngine::lakeHollowKind(const WatershedLake& lake, int x, int z, int ground, int lipDepthVal) const {
    static constexpr int DX4[4] = { 1, -1, 0, 0 };
    static constexpr int DZ4[4] = { 0, 0, 1, -1 };
    constexpr int LAKE_HOLLOW_COLUMNS = 128;
    constexpr int LAKE_HOLLOW_REACH = 3;

    auto pack = [](int px, int pz) -> int64_t {
        return (int64_t(px) << 32) | uint32_t(pz);
    };
    auto getSurface = [&](int sx, int sz) -> int {
        return const_cast<BasinTileEngine&>(basin_).heights().surfaceAt(sx, sz) - 1;
    };

    const int waterY = lake.waterY;
    std::vector<int64_t> members;
    std::unordered_set<int64_t> seen;
    int64_t start = pack(x, z);
    members.push_back(start);
    seen.insert(start);
    bool touches = false, open = !lake.near(x, z, LAKE_HOLLOW_REACH);
    bool beyondLip = (ground < waterY - lipDepthVal || !lake.covers(x, z));

    for (size_t k = 0; k < members.size() && !open; ++k) {
        int64_t m = members[k];
        int mx = int(m >> 32), mz = int(int32_t(uint32_t(m)));
        for (int d = 0; d < 4; ++d) {
            int nx = mx + DX4[d], nz = mz + DZ4[d];
            int64_t nk = pack(nx, nz);
            if (!seen.insert(nk).second) continue;
            int gn = getSurface(nx, nz);
            if (gn >= waterY) continue;
            if (lakeWaterAt(lake, nx, nz, gn)) {
                touches = true;
                continue;
            }
            if (int(members.size()) >= LAKE_HOLLOW_COLUMNS || !lake.near(nx, nz, LAKE_HOLLOW_REACH)) {
                open = true;
                break;
            }
            if (gn < waterY - lipDepthVal || !lake.covers(nx, nz)) beyondLip = true;
            members.push_back(nk);
        }
    }
    return open ? LakeHollowKind::Open : ((touches && beyondLip) ? LakeHollowKind::Flooded : LakeHollowKind::None);
}

// ---------- HeadFeed / WaterRadius / RiverSplits ----------
static constexpr double HEAD_FEED_TIP = 3.0;
static constexpr double HEAD_FEED_TIP_MIN = 10.0;
static constexpr double HEAD_FEED_WIDEN = 60.0;
static constexpr double HEAD_FEED_MARGIN = 1.15;

HydrologyEngine::HeadFeed HydrologyEngine::headFeed(const RiverGraphEdge& edge, bool cacheOnly) const {
    const double vx = edge.source.x, vz = edge.source.y;
    const int64_t key = (int64_t(dwRound(vx)) << 32) ^ int64_t(uint32_t(int32_t(dwRound(vz))));
    struct TlsSlot {
        const HydrologyEngine* owner = nullptr;
        uint64_t epoch = 0;
        int64_t key = 0;
        HeadFeed val{};
        bool valid = false;
    };
    struct Tls {
        std::array<TlsSlot, 64> slots{};
    };
    thread_local Tls tls;
    const uint64_t ep = resetEpoch_.load(std::memory_order_acquire);
    const size_t hslot = (size_t(key) ^ (size_t(key) >> 17)) & 63u;
    TlsSlot& s = tls.slots[hslot];
    if (s.valid && s.owner == this && s.epoch == ep && s.key == key) {
        return s.val;
    }
    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        auto it = headFeedCache_.find(key);
        if (it != headFeedCache_.end()) {
            s = {this, ep, key, it->second, true};
            return it->second;
        }
    }
    if (cacheOnly && !cfg_.enableWatersheds) return HeadFeed{};
    auto regPtr = owningRegionEdges(const_cast<MainRiversEngine&>(mainRivers_), edge, vx, vz);
    static const std::vector<RiverGraphEdge> kEmptyEdges;
    const auto& regEdges = regPtr ? *regPtr : kEmptyEdges;
    const double reach = hydrology_const::HEAD_TAPER + normalRiverWaterRadius(edge.drainWidth(regEdges)) + 2.0;
    const int r0x = floorDiv(int(std::floor(vx - reach)), hydrology_const::WATERSHED_REGION_SIZE);
    const int r1x = floorDiv(int(std::ceil(vx + reach)), hydrology_const::WATERSHED_REGION_SIZE);
    const int r0z = floorDiv(int(std::floor(vz - reach)), hydrology_const::WATERSHED_REGION_SIZE);
    const int r1z = floorDiv(int(std::ceil(vz + reach)), hydrology_const::WATERSHED_REGION_SIZE);

    double bestD = 1e300, width = 0.0;
    if (cfg_.enableWatersheds) {

        for (int rz = r0z; rz <= r1z; ++rz) {
            for (int rx = r0x; rx <= r1x; ++rx) {
                auto grid = const_cast<BasinTileEngine&>(basin_).getRegion(rx, rz);
                if (!grid) continue;
                for (const auto& p : grid->paths) {
                    if (!p.hitMainRiver || p.nodeCount < 1) continue;
                    double mx = p.segmentsX[0], mz = p.segmentsZ[0];
                    double d = std::hypot(mx - vx, mz - vz);
                    if (d >= hydrology_const::HEAD_TAPER) continue;
                    double stampR = hydrology_const::waterRadiusAt(edge, mx, mz, regEdges) + 2.0;
                    if (edge.fractal.intersectDistance(mx, mz) > stampR * stampR) continue;
                    if (d < bestD) bestD = d;
                    if (p.segmentWidths[0] > width) width = p.segmentWidths[0];
                }
            }
        }
    }
    HeadFeed feed{bestD, width, bestD < 1e299};
    std::lock_guard<std::mutex> lk(cacheMutex_);
    headFeedCache_[key] = feed;
    return feed;
}

double HydrologyEngine::fedWaterRadius(const RiverGraphEdge& edge, double x, double z, double dist, const HeadFeed& feed) const {
    auto regPtr = owningRegionEdges(const_cast<MainRiversEngine&>(mainRivers_), edge, x, z);
    static const std::vector<RiverGraphEdge> kEmpty;
    const auto& edges = regPtr ? *regPtr : kEmpty;
    double maxRadius = hydrology_const::waterRadiusAt(edge, x, z, edges);
    double fedFrom = feed.distToSource - feed.width;
    double widen = std::max(HEAD_FEED_WIDEN, hydrology_const::HEAD_TAPER - fedFrom);
    if (dist >= fedFrom + widen) return maxRadius;
    double least = std::min(maxRadius, feed.width * HEAD_FEED_MARGIN);
    if (dist >= fedFrom) return least + (maxRadius - least) * std::pow((dist - fedFrom) / widen, hydrology_const::HEAD_TIP_POWER);
    double tipLen = std::max(HEAD_FEED_TIP_MIN, HEAD_FEED_TIP * least);
    return least * std::pow(mthClamp((dist - (fedFrom - tipLen)) / tipLen, 0.0, 1.0), hydrology_const::HEAD_TIP_POWER);
}

double HydrologyEngine::gorgeWaterRadius(const RiverGraphEdge& edge, double x, double z, bool cacheOnly) const {
    auto regPtr = owningRegionEdges(const_cast<MainRiversEngine&>(mainRivers_), edge, x, z);
    static const std::vector<RiverGraphEdge> kEmpty;
    const auto& edges = regPtr ? *regPtr : kEmpty;
    double r = hydrology_const::routeWaterRadius(edge, x, z, edges, cfg_);
    if (edge.sourceEdge || edge.delta) return r;
    double dist = std::hypot(x - edge.source.x, z - edge.source.y);
    if (dist >= hydrology_const::HEAD_TAPER + HEAD_FEED_WIDEN) return r;
    HeadFeed feed = headFeed(edge, cacheOnly);
    return !feed.valid ? r : fedWaterRadius(edge, x, z, dist, feed);
}

double HydrologyEngine::flowWaterRadius(const RiverGraphEdge& edge, double x, double z) const {
    if (cfg_.normalRiverTunnels) {
        auto regPtr = owningRegionEdges(const_cast<MainRiversEngine&>(mainRivers_), edge, x, z);
        static const std::vector<RiverGraphEdge> kEmpty;
        return hydrology_const::routeWaterRadius(edge, x, z, regPtr ? *regPtr : kEmpty, cfg_);
    }
    return gorgeWaterRadius(edge, x, z, false);
}

static constexpr double RIVER_SPLIT_MIN_LENGTH = 70.0;
static constexpr double RIVER_SPLIT_END = 0.08;
static constexpr double RIVER_SPLIT_SHOAL = 0.55;
static constexpr double RIVER_SPLIT_BAR = 5.0;
static constexpr double RIVER_SPLIT_RISE = 0.4;
static constexpr int RIVER_SPLIT_TOP = 3;
static constexpr double RIVER_SPLIT_ONE = 0.6;
static constexpr double RIVER_SPLIT_MANY = 0.46;
static constexpr double RIVER_SPLIT_OFF_ONE = 0.1;
static constexpr double RIVER_SPLIT_OFF_MANY = 0.34;
static constexpr double RIVER_SPLIT_END_MIN = 14.0;
static constexpr double RIVER_SPLIT_MANY_FROM = 16.0;
static constexpr double RIVER_SPLIT_SMALL = 0.13;
static constexpr double RIVER_SPLIT_SMALL_MORE = 0.12;
static constexpr double RIVER_SPLIT_OFF_SMALL = 0.1;
static constexpr double RIVER_SPLIT_OFF_SMALL_MORE = 0.45;
static constexpr double RIVER_SPLIT_LAND_IN = 2.0;
static constexpr double RIVER_SPLIT_FLOW_REACH = 8.0;
static constexpr double RIVER_SPLIT_NONE = -1.0E9;

int64_t HydrologyEngine::riverSplitHash(const RiverGraphEdge& edge) const {
    uint64_t h = uint64_t(seed_) ^ 0x5DA7E1B1ull;
    h = uint64_t(hashCommonMix(int64_t(h + uint64_t(dwRound(edge.source.x)))));
    h = uint64_t(hashCommonMix(int64_t(h + uint64_t(dwRound(edge.source.y)))));
    h = uint64_t(hashCommonMix(int64_t(h + uint64_t(dwRound(edge.drain.x)))));
    return hashCommonMix(int64_t(h + uint64_t(dwRound(edge.drain.y))));
}

bool HydrologyEngine::riverSplits(const RiverGraphEdge& edge) const {
    double chance = riverSplitsChance(cfg_.riverSplits);
    if (chance <= 0.0 || !edge.sourceEdge || edge.delta) return false;
    if (std::hypot(edge.drain.x - edge.source.x, edge.drain.y - edge.source.y) < RIVER_SPLIT_MIN_LENGTH) return false;
    return double(uint64_t(riverSplitHash(edge)) >> 11) * 0x1.0p-53 < chance;
}

double HydrologyEngine::riverSplitDepth(const RiverGraphEdge& edge, double x, double z, double radius) const {
    double at[6]{};
    edge.fractal.nearestAlong(x, z, at);
    if (at[4] <= 0.0 || at[4] >= at[5]) return RIVER_SPLIT_NONE;
    double lateral = (z - at[1]) * at[2] - (x - at[0]) * at[3];
    double along = at[4] / at[5];
    uint64_t h = uint64_t(riverSplitHash(edge));
    double end = std::max(RIVER_SPLIT_END, RIVER_SPLIT_END_MIN / at[5]);
    double span = 1.0 - 2.0 * end;

    int kind = int(((h >> 8) & 0xFFull) * 10 / 256);
    bool scatter = kind >= 7;
    int islands = scatter ? 4 + int((h >> 17) & 3ull)
        : ((kind < 4 || radius < RIVER_SPLIT_MANY_FROM) ? 1 : 2 + int((h >> 16) & 1ull));
    double side = (h & 1ull) == 0ull ? 1.0 : -1.0;

    double ragged = 1.0 + 0.25 * noises_.riverBedNoise.getValue(x / 14.0 + 41.0, z / 14.0 - 17.0);
    double wander = 0.08 * radius * noises_.riverBedNoise.getValue(x / 40.0 - 71.0, z / 40.0 + 23.0);
    double best = RIVER_SPLIT_NONE;
    for (int i = 0; i < islands; ++i) {
        uint64_t k = uint64_t(hashCommonMix(int64_t(h + uint64_t(i) + 1ull)));
        double a = double(k >> 44) * 0x1.0p-20;
        double b = double((k >> 24) & 0xFFFFFull) * 0x1.0p-20;
        double c = double((k >> 4) & 0xFFFFFull) * 0x1.0p-20;
        double turn = side * ((i % 2 == 0) ? 1.0 : -1.0);
        double middle, half, width, off;
        if (scatter) {
            middle = end + span * (i + 0.5 + 0.7 * (a - 0.5)) / islands;
            half = span / islands * (0.4 + 0.5 * b);
            width = radius * (RIVER_SPLIT_SMALL + RIVER_SPLIT_SMALL_MORE * c);
            off = turn * radius * (RIVER_SPLIT_OFF_SMALL + RIVER_SPLIT_OFF_SMALL_MORE * a);
        } else if (islands == 1) {
            middle = end + span / 2.0;
            half = span / 2.0;
            width = RIVER_SPLIT_ONE * radius;
            off = turn * RIVER_SPLIT_OFF_ONE * radius;
        } else {
            middle = end + span * (i + 0.5) / islands;
            half = span / (2.0 * islands) * 1.5;
            width = RIVER_SPLIT_MANY * radius;
            off = turn * RIVER_SPLIT_OFF_MANY * radius;
        }
        double u = (along - middle) / half;
        double across = std::abs(lateral - off - wander);
        double depth;
        if (std::abs(u) >= 1.0) {
            depth = -std::hypot(across, (std::abs(u) - 1.0) * half * at[5]);
        } else {
            double round = 1.0 - u * u;
            depth = width * ragged * (u < 0.0 ? std::sqrt(round) : std::pow(round, 1.4)) - across - 1.0;
        }
        best = std::max(best, depth);
    }
    return best;
}

bool HydrologyEngine::onRiverSplit(double x, double z, double in) const {
    if (!cfg_.enabled || !cfg_.dynamicNormalRivers()) return false;
    thread_local std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>> nearby;
    nearby.clear();
    const_cast<MainRiversEngine&>(mainRivers_).collectNearbyEdges(x, z, nearby);
    for (const auto& pr : nearby) {
        const auto& regEdges = *pr.first;
        const RiverGraphEdge& edge = regEdges[pr.second];
        if (riverSplits(edge) && riverSplitDepth(edge, x, z, hydrology_const::waterRadiusAt(edge, x, z, regEdges)) >= in)
            return true;
    }
    return false;
}

bool HydrologyEngine::riverSplitLand(double x, double z) const { return onRiverSplit(x, z, RIVER_SPLIT_LAND_IN); }
bool HydrologyEngine::riverIsland(double x, double z) const { return onRiverSplit(x, z, 0.0); }

void HydrologyEngine::aroundRiverSplit(double x, double z, double& fx, double& fz) const {
    double len2 = fx * fx + fz * fz;
    if (len2 < 1.0E-6 || riverSplitsChance(cfg_.riverSplits) <= 0.0) return;
    thread_local std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>> nearby;
    nearby.clear();
    const_cast<MainRiversEngine&>(mainRivers_).collectNearbyEdges(x, z, nearby);
    for (const auto& pr : nearby) {
        const auto& regEdges = *pr.first;
        const RiverGraphEdge& edge = regEdges[pr.second];
        if (!riverSplits(edge)) continue;
        double r = hydrology_const::waterRadiusAt(edge, x, z, regEdges);
        double depth = riverSplitDepth(edge, x, z, r);
        if (depth <= -RIVER_SPLIT_FLOW_REACH) continue;
        double gx = riverSplitDepth(edge, x + 1.0, z, r) - riverSplitDepth(edge, x - 1.0, z, r);
        double gz = riverSplitDepth(edge, x, z + 1.0, r) - riverSplitDepth(edge, x, z - 1.0, r);
        double gl = std::hypot(gx, gz);
        if (gl < 1.0E-6) continue;
        double nx = gx / gl, nz = gz / gl;
        double into = fx * nx + fz * nz;
        if (into <= 0.0) continue;
        double nearVal = mthClamp(1.0 + depth / RIVER_SPLIT_FLOW_REACH, 0.0, 1.0);
        double nfx = fx - nearVal * into * nx, nfz = fz - nearVal * into * nz;
        double fl = std::hypot(nfx, nfz);
        if (fl < 1.0E-6) continue;
        double speed = std::hypot(fx, fz);
        fx = nfx / fl * speed;
        fz = nfz / fl * speed;
        return;
    }
}

// ---------- Ravines & Gorges ----------
bool HydrologyEngine::entersGorge(const WatershedPath& path, int seaLevel) const {
    if (path.hitMainRiver) return true;
    if (int8_t ge = path.gorgeEntry.load(std::memory_order_acquire); ge != 0) return ge == 2;
    bool in = false;
    const WatershedPath* p = &path;
    for (int hop = 0; hop < 4 && p && p->validNodeCount > 0 && !p->segmentsY.empty() && p->segmentsY[0] <= seaLevel + 1; ++hop) {
        p = downstreamOf(*p);
        if (!p) break;
        if (p->hitMainRiver) { in = true; break; }
    }
    std::lock_guard<std::mutex> lk(cacheMutex_);
    if (path.gorgeEntry.load(std::memory_order_relaxed) == 0) path.gorgeEntry.store(int8_t(in ? 2 : 1), std::memory_order_release);
    return path.gorgeEntry.load(std::memory_order_relaxed) == 2;
}

const WatershedPath* HydrologyEngine::downstreamOf(const WatershedPath& p) const {
    if (p.validNodeCount < 1 || p.segmentsX.empty() || p.segmentsZ.empty()) return nullptr;
    const double mx = p.segmentsX[0], mz = p.segmentsZ[0];
    const int colX = mthFloor(mx), colZ = mthFloor(mz);
    const int homeRx = floorDiv(colX, hydrology_const::WATERSHED_REGION_SIZE);
    const int homeRz = floorDiv(colZ, hydrology_const::WATERSHED_REGION_SIZE);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            auto grid = const_cast<BasinTileEngine&>(basin_).getRegion(homeRx + dx, homeRz + dz);
            if (!grid) continue;
            for (int qi : grid->indicesAt(colX, colZ, homeRx + dx, homeRz + dz)) {
                const WatershedPath& q = grid->paths[qi];
                if (&q == &p) continue;
                for (int i = 1; i < q.validNodeCount; ++i) {
                    double ex = q.segmentsX[i] - mx, ez = q.segmentsZ[i] - mz;
                    if (ex * ex + ez * ez < 2.25) return &q;
                }
            }
        }
    }
    return nullptr;
}

const WatershedPath* HydrologyEngine::joins(const WatershedPath& p) const {
    if (p.validNodeCount < 1 || p.segmentsX.empty() || p.segmentsZ.empty() || p.segmentsY.empty() || p.segmentWidths.empty()) {
        return nullptr;
    }
    if (const WatershedPath* down = downstreamOf(p)) return down;
    const double mx = p.segmentsX[0], mz = p.segmentsZ[0];
    const int colX = mthFloor(mx), colZ = mthFloor(mz);
    const int homeRx = floorDiv(colX, hydrology_const::WATERSHED_REGION_SIZE);
    const int homeRz = floorDiv(colZ, hydrology_const::WATERSHED_REGION_SIZE);
    const WatershedPath* best = nullptr;
    const WatershedPath* after = nullptr;
    double bestOut = 3.0, afterGap = 1e300;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            auto grid = const_cast<BasinTileEngine&>(basin_).getRegion(homeRx + dx, homeRz + dz);
            if (!grid) continue;
            for (int qi : grid->indicesAt(colX, colZ, homeRx + dx, homeRz + dz)) {
                const WatershedPath& q = grid->paths[qi];
                if (&q == &p || q.validNodeCount < 1) continue;
                // drainsInto(q, p)
                bool drainsInto = false;
                double qx = q.segmentsX[0], qz = q.segmentsZ[0];
                for (int i = 1; i < p.validNodeCount; ++i) {
                    double ex = p.segmentsX[i] - qx, ez = p.segmentsZ[i] - qz;
                    if (ex * ex + ez * ez < 2.25) { drainsInto = true; break; }
                }
                if (drainsInto) continue;

                double together = 0.5 * (p.segmentWidths[0] + q.segmentWidths[0]) + 6.0;
                double ex = q.segmentsX[0] - mx, ez = q.segmentsZ[0] - mz;
                if (ex * ex + ez * ez < together * together) continue;
                int s = q.validNodeCount - 1;
                double gap = std::hypot(q.segmentsX[s] - mx, q.segmentsZ[s] - mz);
                if (gap < together && q.segmentsY[s] <= p.segmentsY[0] && gap < afterGap) {
                    afterGap = gap;
                    after = &q;
                }
                auto d = q.getClosestNodeData(mx, mz);
                if (d.distance > 1e290) continue;
                double out = d.distance - 0.5 * d.width;
                if (out < bestOut) {
                    bestOut = out;
                    best = &q;
                }
            }
        }
    }
    return best ? best : after;
}

bool HydrologyEngine::mouthInDelta(const WatershedPath& p) const {
    if (p.validNodeCount < 1 || p.segmentsX.empty() || p.segmentsZ.empty()) return false;
    const double mx = p.segmentsX[0], mz = p.segmentsZ[0];
    std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>> nearby;
    const_cast<MainRiversEngine&>(mainRivers_).collectNearbyEdges(mx, mz, nearby);

    const RiverGraphEdge* nearest = nullptr;
    double best = 16.0;
    double on[2]{};
    for (const auto& pr : nearby) {
        const auto& edges = *pr.first;
        const RiverGraphEdge* e = &edges[pr.second];
        double out = std::sqrt(e->fractal.nearestPoint(mx, mz, on[0], on[1])) - hydrology_const::waterRadiusAt(*e, on[0], on[1], edges);
        if (out < best) {
            best = out;
            nearest = e;
        }
    }
    return nearest != nullptr && nearest->delta;
}

bool HydrologyEngine::lakeFeedsDelta(const WatershedPath& p, std::vector<const WatershedLake*>& through) const {
    if (p.validNodeCount < 1 || p.segmentsX.empty() || p.segmentsZ.empty()) return false;
    const WatershedLake* lake = findLake(p.segmentsX[0], p.segmentsZ[0], INT32_MIN, false, false);
    if (!lake) return false;
    for (const WatershedLake* l : through) {
        if (l->waterY == lake->waterY && l->minCellX == lake->minCellX && l->minCellZ == lake->minCellZ) return false;
    }
    if (through.size() >= 32) return false;
    through.push_back(lake);
    constexpr int reach = CoarseHeightCache::CELL_SIZE;
    int rx0 = floorDiv(mthFloor(lake->minX) - reach, hydrology_const::WATERSHED_REGION_SIZE);
    int rx1 = floorDiv(mthFloor(lake->maxX) + reach, hydrology_const::WATERSHED_REGION_SIZE);
    int rz0 = floorDiv(mthFloor(lake->minZ) - reach, hydrology_const::WATERSHED_REGION_SIZE);
    int rz1 = floorDiv(mthFloor(lake->maxZ) + reach, hydrology_const::WATERSHED_REGION_SIZE);
    for (int rx = rx0; rx <= rx1; ++rx) {
        for (int rz = rz0; rz <= rz1; ++rz) {
            auto grid = const_cast<BasinTileEngine&>(basin_).getRegion(rx, rz);
            if (!grid) continue;
            for (const auto& q : grid->paths) {
                if (&q == &p || q.validNodeCount < 2) continue;
                int s = q.validNodeCount - 1;
                if (q.segmentsY[s] > lake->waterY || !lake->covers(q.segmentsX[s], q.segmentsZ[s])) continue;
                if (walksToDelta(q, through)) return true;
            }
        }
    }
    return false;
}

bool HydrologyEngine::walksToDelta(const WatershedPath& path, std::vector<const WatershedLake*>& through) const {
    const WatershedPath* p = &path;
    for (int guard = 0; guard < 64; ++guard) {
        if (int8_t pdf = p->deltaFed.load(std::memory_order_acquire); pdf != 0) return pdf == 2;
        if (p->hitMainRiver) return mouthInDelta(*p);
        const WatershedPath* next = joins(*p);
        if (!next) return lakeFeedsDelta(*p, through);
        p = next;
    }
    return false;
}

bool HydrologyEngine::feedsDelta(const WatershedPath& path) const {
    if (int8_t df = path.deltaFed.load(std::memory_order_acquire); df != 0) return df == 2;
    std::vector<const WatershedLake*> through;
    bool fed = walksToDelta(path, through);
    std::lock_guard<std::mutex> lk(cacheMutex_);
    if (path.deltaFed.load(std::memory_order_relaxed) == 0) path.deltaFed.store(int8_t(fed ? 2 : 1), std::memory_order_release);
    return path.deltaFed.load(std::memory_order_relaxed) == 2;
}

double HydrologyEngine::toRiverWater(const RiverGraphEdge& river, double x, double z, double on[2]) const {
    double d = std::sqrt(river.fractal.nearestPoint(x, z, on[0], on[1]));
    double r = gorgeWaterRadius(river, on[0], on[1], false);
    if (r >= 2.0) return d - r;
    const auto& s = river.fractal.segments;
    double best = 1e300;
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        double ri = gorgeWaterRadius(river, s[i], s[i + 1], false);
        if (ri < 2.0) continue;
        double di = std::hypot(s[i] - x, s[i + 1] - z) - ri;
        if (di < best) {
            best = di;
            on[0] = s[i];
            on[1] = s[i + 1];
        }
    }
    return best;
}

const RiverGraphEdge* HydrologyEngine::nearestRiver(double x, double z, double on[2]) const {
    std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>> nearby;
    const_cast<MainRiversEngine&>(mainRivers_).collectNearbyEdges(x, z, nearby);
    const RiverGraphEdge* best = nullptr;
    double out = 1e300;
    double p[2]{};
    for (const auto& pr : nearby) {
        const RiverGraphEdge* e = &(*pr.first)[pr.second];
        double d = toRiverWater(*e, x, z, p);
        if (d < out) {
            out = d;
            best = e;
            on[0] = p[0];
            on[1] = p[1];
        }
    }
    return best;
}

double HydrologyEngine::ravineWall(const RiverGraphEdge& river, double x, double z, int seaLevel, double on[2]) const {
    double d = std::sqrt(river.fractal.nearestPoint(x, z, on[0], on[1]));
    double r = gorgeWaterRadius(river, on[0], on[1], false);
    double out = std::max(0.0, d + hydrology_const::getRiverJitter(x, z) * (r / 10.0)) - r;
    int ground = const_cast<BasinTileEngine&>(basin_).heights().surfaceAt(mthFloor(x), mthFloor(z)) - 1;
    double rg = const_cast<BasinTileEngine&>(basin_).heights().coarseGroundAt(on[0], on[1]);
    return hydrology_const::lowlandRouteGround(seed_, noises_.riverBedNoise, x, z, ground, out, r, rg, seaLevel, river.delta);
}

int HydrologyEngine::feedersTop(double px, double pz, double reach, int top) const {
    int colX = mthFloor(px), colZ = mthFloor(pz);
    int homeRx = floorDiv(colX, hydrology_const::WATERSHED_REGION_SIZE);
    int homeRz = floorDiv(colZ, hydrology_const::WATERSHED_REGION_SIZE);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            auto grid = const_cast<BasinTileEngine&>(basin_).getRegion(homeRx + dx, homeRz + dz);
            if (!grid) continue;
            for (int qi : grid->indicesAt(colX, colZ, homeRx + dx, homeRz + dz)) {
                const WatershedPath& q = grid->paths[qi];
                if (q.hitMainRiver || q.validNodeCount < 1) continue;
                double ex = q.segmentsX[0] - px, ez = q.segmentsZ[0] - pz;
                if (ex * ex + ez * ez > reach * reach) continue;
                for (int j = 0; j < std::min(q.validNodeCount, 4); ++j)
                    top = std::max(top, q.segmentsY[j]);
            }
        }
    }
    return top;
}

Ravine HydrologyEngine::buildRavine(const WatershedPath& path, int seaLevel) const {
    Ravine none;
    none.valid = false;
    int k = 0;
    while (k < path.validNodeCount && path.segmentsY[k] <= seaLevel + 1) k++;
    if (k == 0 || k >= path.validNodeCount || path.segmentsY[k] < seaLevel + 1 + hydrology_const::LOWLAND_FALL_MIN)
        return none;
    double on[2]{};
    const RiverGraphEdge* river = nearestRiver(path.segmentsX[k], path.segmentsZ[k], on);
    if (!river) return none;

    int start = k;
    for (int j = k + 1; j < path.validNodeCount && j <= k + 15; ++j) {
        double toWater = toRiverWater(*river, path.segmentsX[j], path.segmentsZ[j], on);
        if (toWater > 60.0) break;
        int a = j - 1, b = std::min(path.validNodeCount - 1, j + 1);
        double tx = path.segmentsX[b] - path.segmentsX[a], tz = path.segmentsZ[b] - path.segmentsZ[a];
        double tl = std::hypot(tx, tz);
        if (tl < 1.0E-6) break;
        double half = 1.3 * path.segmentWidths[j] + 3.0;
        double jx = path.segmentsX[j], jz = path.segmentsZ[j], nx = -tz / tl * half, nz = tx / tl * half;
        double low = std::min(ravineWall(*river, jx, jz, seaLevel, on),
            std::min(ravineWall(*river, jx + nx, jz + nz, seaLevel, on),
                     ravineWall(*river, jx - nx, jz - nz, seaLevel, on)));
        if (low >= path.segmentsY[j] + 4.0) break;
        start = j;
    }
    double px = path.segmentsX[start], pz = path.segmentsZ[start];
    int lip = path.segmentsY[start];

    river = nearestRiver(px, pz, on);
    if (!river) return none;
    double cx = on[0], cz = on[1];
    double dc = std::hypot(cx - px, cz - pz);
    double out = dc - gorgeWaterRadius(*river, cx, cz, false);
    bool pastHead = std::sqrt(river->fractal.nearestPoint(px, pz, on[0], on[1])) < out;
    if (out < 2.0 || out > (pastHead ? 128.0 : 64.0)) return none;
    double ux = (cx - px) / dc, uz = (cz - pz) / dc;

    double curveAmp = 6.0 * std::min(1.0, out / 40.0);
    auto d2bits = [](double v) -> int64_t { int64_t r; std::memcpy(&r, &v, 8); return r; };
    int64_t hbits = hashCommonMix(d2bits(px) * 31 + d2bits(pz));
    double curvePhase = 2.0 * DW_PI * double(uint64_t(hbits) >> 11) * 0x1.0p-53;

    int n = int(std::ceil(out));
    std::vector<double> wall(n + 1);
    double stream = 0.0;
    for (int j = k; j <= start; ++j) stream = std::max(stream, path.segmentWidths[j]);
    double width = std::max(2.5, stream);
    double wx = -uz * (width + 3.0), wz = ux * (width + 3.0);
    for (int i = 0; i <= n; ++i) {
        double bent = ravineCurveVal(curveAmp, curvePhase, double(i), out);
        double qx = px + ux * i + uz * bent, qz = pz + uz * i - ux * bent;
        wall[i] = std::min(ravineWall(*river, qx, qz, seaLevel, on),
            std::min(ravineWall(*river, qx + wx, qz + wz, seaLevel, on),
                     ravineWall(*river, qx - wx, qz - wz, seaLevel, on)));
    }

    int water = seaLevel - 1;
    for (int i = std::max(1, n - 8); i <= n; ++i) wall[i] = std::min(wall[i], double(water));
    std::vector<double> from(n + 2, 0.0);
    std::vector<int> lv(n + 2, 0);
    int pools = 1;
    lv[0] = lip;
    int w = lip;
    for (int i = 1; i <= n && w > water; ++i) {
        if (wall[i] - 4.0 >= w) continue;
        double low = 1e300;
        for (int j = i; j <= std::min(n, i + 5); ++j) low = std::min(low, wall[j] - 4.0);
        int next = std::max(water, std::min(w - 3, mthFloor(low)));
        if (next >= w) continue;
        from[pools] = i - 0.5;
        lv[pools++] = next;
        w = next;
    }
    if (w > water) {
        from[pools] = out - 0.5;
        lv[pools++] = water;
    }
    int top = lip;
    for (int j = start; j < std::min(path.validNodeCount, start + 4); ++j)
        top = std::max(top, path.segmentsY[j]);
    top = feedersTop(px, pz, width + 3.0 * WatershedPath::NODE_SPACING, top);

    from.resize(pools);
    lv.resize(pools);
    Ravine res;
    res.px = px; res.pz = pz; res.ux = ux; res.uz = uz;
    res.length = out; res.width = width;
    res.start = start; res.top = top;
    res.from = std::move(from);
    res.level = std::move(lv);
    res.curveAmp = curveAmp;
    res.curvePhase = curvePhase;
    res.valid = true;
    return res;
}

const Ravine* HydrologyEngine::ravineOf(const WatershedPath& path, int seaLevel) const {
    if (int32_t ri = path.ravineIdx.load(std::memory_order_acquire); ri == -1) return nullptr;
    else if (ri >= 0) return ravineAtLockFree(ri);
    Ravine r = buildRavine(path, seaLevel);
    std::lock_guard<std::mutex> lk(cacheMutex_);
    if (int32_t ri = path.ravineIdx.load(std::memory_order_relaxed); ri == -1) return nullptr;
    else if (ri >= 0) return ravineAtLockFree(ri);
    if (!r.valid) { path.ravineIdx.store(-1, std::memory_order_release); return nullptr; }
    int idx = pushRavineLocked(std::move(r));
    path.ravineIdx.store(idx, std::memory_order_release);
    return idx >= 0 ? ravineAtLockFree(idx) : nullptr;
}

const Ravine* HydrologyEngine::feederRavine(const WatershedPath& path, int seaLevel) const {
    if (int32_t fi = path.feedsIdx.load(std::memory_order_acquire); fi == -1) return nullptr;
    else if (fi >= 0) return ravineAtLockFree(fi);
    const WatershedPath* matchedDown = nullptr;
    const WatershedPath* down = path.hitMainRiver ? nullptr : downstreamOf(path);
    if (down && path.validNodeCount >= 1 && !path.segmentsX.empty() && !path.segmentsZ.empty()) {
        const Ravine* r = ravineOf(*down, seaLevel);
        if (r) {
            double mx = path.segmentsX[0], mz = path.segmentsZ[0];
            for (int j = r->start; j < std::min(down->validNodeCount, r->start + 3); ++j) {
                double ex = down->segmentsX[j] - mx, ez = down->segmentsZ[j] - mz;
                if (ex * ex + ez * ez < 2.25) { matchedDown = down; break; }
            }
        }
    }
    std::lock_guard<std::mutex> lk(cacheMutex_);
    if (int32_t fi = path.feedsIdx.load(std::memory_order_relaxed); fi == -1) return nullptr;
    else if (fi >= 0) return ravineAtLockFree(fi);
    int foundIdx = matchedDown ? matchedDown->ravineIdx.load(std::memory_order_acquire) : -1;
    path.feedsIdx.store(foundIdx, std::memory_order_release);
    if (foundIdx < 0) return nullptr;
    return ravineAtLockFree(foundIdx);
}

const Ravine* HydrologyEngine::ravineAbove(const WatershedPath& path, int seaLevel) const {
    if (int32_t bi = path.belowIdx.load(std::memory_order_acquire); bi == -1) return nullptr;
    else if (bi >= 0) return ravineAtLockFree(bi);
    const WatershedPath* matchedQ = nullptr;
    int head = path.validNodeCount - 1;
    if (head > 0) {
        double hx = path.segmentsX[head], hz = path.segmentsZ[head];
        int colX = mthFloor(hx), colZ = mthFloor(hz);
        int homeRx = floorDiv(colX, hydrology_const::WATERSHED_REGION_SIZE);
        int homeRz = floorDiv(colZ, hydrology_const::WATERSHED_REGION_SIZE);
        for (int dx = -1; dx <= 1 && !matchedQ; ++dx) {
            for (int dz = -1; dz <= 1 && !matchedQ; ++dz) {
                auto grid = const_cast<BasinTileEngine&>(basin_).getRegion(homeRx + dx, homeRz + dz);
                if (!grid) continue;
                for (int qi : grid->indicesAt(colX, colZ, homeRx + dx, homeRz + dz)) {
                    const WatershedPath& q = grid->paths[qi];
                    if (&q == &path || q.validNodeCount < 2) continue;
                    double ex = q.segmentsX[0] - hx, ez = q.segmentsZ[0] - hz;
                    if (ex * ex + ez * ez > 2.25) continue;
                    const Ravine* qr = entersGorge(q, seaLevel) ? ravineOf(q, seaLevel) : nullptr;
                    if (qr) { matchedQ = &q; break; }
                }
            }
        }
    }
    std::lock_guard<std::mutex> lk(cacheMutex_);
    if (int32_t bi = path.belowIdx.load(std::memory_order_relaxed); bi == -1) return nullptr;
    else if (bi >= 0) return ravineAtLockFree(bi);
    int foundIdx = matchedQ ? matchedQ->ravineIdx.load(std::memory_order_acquire) : -1;
    path.belowIdx.store(foundIdx, std::memory_order_release);
    if (foundIdx < 0) return nullptr;
    return ravineAtLockFree(foundIdx);
}

bool HydrologyEngine::joinsPath(const WatershedPath& q, const WatershedPath& path) const {
    double mx = q.segmentsX[0], mz = q.segmentsZ[0];
    if (mx < path.minX - 2.0 || mx > path.maxX + 2.0 || mz < path.minZ - 2.0 || mz > path.maxZ + 2.0) return false;
    for (int i = 1; i < path.validNodeCount; ++i) {
        double ex = path.segmentsX[i] - mx, ez = path.segmentsZ[i] - mz;
        if (ex * ex + ez * ez < 2.25) return true;
    }
    return false;
}

bool HydrologyEngine::drainedByRavines(const WatershedPath& path, int seaLevel, std::vector<const WatershedPath*>& visited) const {
    if (visited.size() >= 16) return false;
    if (path.validNodeCount < 2) return false;
    for (int i = 0; i < path.validNodeCount; ++i) {
        if (path.segmentsY[i] > seaLevel + 1) return false;
    }
    int rx0 = floorDiv(mthFloor(path.minX), hydrology_const::WATERSHED_REGION_SIZE) - 1;
    int rx1 = floorDiv(mthFloor(path.maxX), hydrology_const::WATERSHED_REGION_SIZE) + 1;
    int rz0 = floorDiv(mthFloor(path.minZ), hydrology_const::WATERSHED_REGION_SIZE) - 1;
    int rz1 = floorDiv(mthFloor(path.maxZ), hydrology_const::WATERSHED_REGION_SIZE) + 1;
    bool fed = false;
    for (int rx = rx0; rx <= rx1; ++rx) {
        for (int rz = rz0; rz <= rz1; ++rz) {
            auto grid = const_cast<BasinTileEngine&>(basin_).getRegion(rx, rz);
            if (!grid) continue;
            for (const auto& q : grid->paths) {
                if (&q == &path || q.validNodeCount < 2 || !joinsPath(q, path)) continue;
                if (std::find(visited.begin(), visited.end(), &q) != visited.end()) continue;
                const Ravine* r = entersGorge(q, seaLevel) ? ravineOf(q, seaLevel) : nullptr;
                if (!r && !leftDry(q, seaLevel, visited)) return false;
                fed = true;
            }
        }
    }
    return fed;
}

bool HydrologyEngine::leftDry(const WatershedPath& path, int seaLevel, std::vector<const WatershedPath*>& visited) const {
    if (int8_t ld = path.leftDry.load(std::memory_order_acquire); ld != 0) return ld == 2;
    if (std::find(visited.begin(), visited.end(), &path) != visited.end() || visited.size() >= 16) {
        return false;
    }
    visited.push_back(&path);
    bool dry = drainedByRavines(path, seaLevel, visited);
    visited.pop_back();
    if (visited.empty()) {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        if (path.leftDry.load(std::memory_order_relaxed) == 0) path.leftDry.store(int8_t(dry ? 2 : 1), std::memory_order_release);
        return path.leftDry.load(std::memory_order_relaxed) == 2;
    }
    return dry;
}

bool HydrologyEngine::leftDry(const WatershedPath& path, int seaLevel) const {
    int8_t ld = path.leftDry.load(std::memory_order_acquire);
    if (ld != 0) return ld == 2;
    std::vector<const WatershedPath*> visited;
    visited.reserve(16);
    return leftDry(path, seaLevel, visited);
}

bool HydrologyEngine::atRavineRim(const WatershedContext& ctx, int seaLevel) const {
    if (!ctx.bestPath) return false;
    const Ravine* r = entersGorge(*ctx.bestPath, seaLevel) ? ravineOf(*ctx.bestPath, seaLevel) : nullptr;
    int start = r ? r->start : 0;
    if (!r && !feederRavine(*ctx.bestPath, seaLevel)) return false;
    return ctx.bestNodeIndex < start + int(std::ceil((ctx.width + 4.0) / WatershedPath::NODE_SPACING)) + 1;
}

int HydrologyEngine::rimLevel(const WatershedPath& path, int node, int seaLevel) {
    int k = node;
    while (k < path.nodeCount && path.segmentsY[k] <= seaLevel + 1) k++;
    if (k >= path.nodeCount) return INT32_MIN;
    int lip = path.segmentsY[k];
    return lip >= seaLevel + 1 + int(hydrology_const::LOWLAND_FALL_MIN) ? lip : INT32_MIN;
}

double HydrologyEngine::rimDistance(const WatershedPath& path, int node, double x, double z, int seaLevel, double reach) {
    if (path.segmentsY[node] <= seaLevel + 1) return std::numeric_limits<double>::quiet_NaN();
    int span = int(std::ceil(reach / WatershedPath::NODE_SPACING)) + 1;
    int k = node;
    while (k > 0 && node - k <= span && path.segmentsY[k - 1] > seaLevel + 1) k--;
    if (k == 0 || node - k > span || path.segmentsY[k] < seaLevel + 1 + hydrology_const::LOWLAND_FALL_MIN)
        return std::numeric_limits<double>::quiet_NaN();
    double t = 0.0;
    if (node + 1 < path.validNodeCount) {
        double dx = path.segmentsX[node + 1] - path.segmentsX[node];
        double dz = path.segmentsZ[node + 1] - path.segmentsZ[node];
        double len2 = dx * dx + dz * dz;
        if (len2 > 0.0) t = mthClamp(((x - path.segmentsX[node]) * dx + (z - path.segmentsZ[node]) * dz) / len2, 0.0, 1.0);
    }
    double d = (node - k + t) * WatershedPath::NODE_SPACING;
    return d <= reach ? d : std::numeric_limits<double>::quiet_NaN();
}

double HydrologyEngine::funnelDistance(const WatershedPath& path, int node, int start, double x, double z, double reach) {
    if (node < start) return std::numeric_limits<double>::quiet_NaN();
    double t = 0.0;
    if (node + 1 < path.validNodeCount) {
        double dx = path.segmentsX[node + 1] - path.segmentsX[node];
        double dz = path.segmentsZ[node + 1] - path.segmentsZ[node];
        double len2 = dx * dx + dz * dz;
        if (len2 > 0.0) t = mthClamp(((x - path.segmentsX[node]) * dx + (z - path.segmentsZ[node]) * dz) / len2, 0.0, 1.0);
    }
    double d = (node - start + t) * WatershedPath::NODE_SPACING;
    return d <= reach ? d : std::numeric_limits<double>::quiet_NaN();
}

double HydrologyEngine::channelFullWidth(const WatershedPath::RiverNodeData& data, double x, double z) const {
    double gorge = double(data.terrainY - data.y);
    double narrow = gorge <= GORGE_NARROW_FROM ? 1.0
        : std::max(GORGE_NARROW_MIN, 1.0 - (1.0 - GORGE_NARROW_MIN) * std::min(1.0, (gorge - GORGE_NARROW_FROM) / (GORGE_NARROW_TO - GORGE_NARROW_FROM)));
    double pinch = 0.6 * noises_.riverBedNoise.getValue(x * 0.007, z * 0.007)
                 + 0.4 * noises_.riverBedNoise.getValue(x * 0.0018 + 91.0, z * 0.0018 - 37.0);
    return data.width * narrow * ChannelForm::slopeWidth(data.slope) * ChannelForm::pinchWidth(pinch);
}

int HydrologyEngine::channelBowl(const WatershedPath::RiverNodeData& data, double chanW) {
    int centerDepth = std::max(1, std::min(MAX_BOWL, int(dwRound(STREAM_DEPTH_RATIO * chanW))));
    double lat = mthClamp(data.lateral / chanW, -1.0, 1.0);
    double skew = ChannelForm::bendSkew(data.curvature, chanW);
    return std::min(MAX_BOWL, int(dwRound(centerDepth * ChannelForm::bedDepth(lat, skew))));
}

bool HydrologyEngine::capYields(const WatershedPath& self, const WatershedPath::RiverNodeData& own, double x, double z, int colX, int colZ, int homeRx, int homeRz) const {
    const auto& nb = const_cast<BasinTileEngine&>(basin_).getNeighborhood3x3(homeRx, homeRz);
    if (!nb.anyPaths || x < nb.pathsMinX || x > nb.pathsMaxX || z < nb.pathsMinZ || z > nb.pathsMaxZ) return false;
    for (int dx = -WATERSHED_SCAN_RADIUS; dx <= WATERSHED_SCAN_RADIUS; ++dx) {
        for (int dz = -WATERSHED_SCAN_RADIUS; dz <= WATERSHED_SCAN_RADIUS; ++dz) {
            int mrx = homeRx + dx, mrz = homeRz + dz;
            const RegionSpatialGrid* grid = (dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1)
                ? nb.grids[(dx + 1) * 3 + (dz + 1)]
                : nullptr;
            if (!grid || grid->paths.empty()) continue;
            for (int oi : grid->indicesAt(colX, colZ, mrx, mrz)) {
                const WatershedPath& other = grid->paths[oi];
                if (&other == &self) continue;
                auto d = other.getClosestNodeData(x, z);
                if (d.beyondEnd || d.nodeIndex < CAP_YIELD_UP || d.y < own.y + CAP_YIELD_RISE) continue;
                if (d.distance < channelFullWidth(d, x, z) + CAP_YIELD_EDGE) return true;
            }
        }
    }
    return false;
}

bool HydrologyEngine::capGoesOn(const WatershedPath& self, double x, double z, int colX, int colZ, int homeRx, int homeRz) const {
    if (self.validNodeCount < 2) return false;
    const auto& nb = const_cast<BasinTileEngine&>(basin_).getNeighborhood3x3(homeRx, homeRz);
    if (!nb.anyPaths || x < nb.pathsMinX || x > nb.pathsMaxX || z < nb.pathsMinZ || z > nb.pathsMaxZ) return false;
    int last = self.validNodeCount - 1;
    bool atMouth = std::hypot(x - self.segmentsX[0], z - self.segmentsZ[0]) <= std::hypot(x - self.segmentsX[last], z - self.segmentsZ[last]);
    double ex = atMouth ? self.segmentsX[0] : self.segmentsX[last];
    double ez = atMouth ? self.segmentsZ[0] : self.segmentsZ[last];
    int mouths = 0, heads = 0;
    bool onIt = false;
    thread_local std::vector<const WatershedPath*> seen;
    seen.clear();
    for (int dx = -WATERSHED_SCAN_RADIUS; dx <= WATERSHED_SCAN_RADIUS; ++dx) {
        for (int dz = -WATERSHED_SCAN_RADIUS; dz <= WATERSHED_SCAN_RADIUS; ++dz) {
            int mrx = homeRx + dx, mrz = homeRz + dz;
            const RegionSpatialGrid* grid = (dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1)
                ? nb.grids[(dx + 1) * 3 + (dz + 1)]
                : nullptr;
            if (!grid || grid->paths.empty()) continue;
            for (int oi : grid->indicesAt(colX, colZ, mrx, mrz)) {
                const WatershedPath& other = grid->paths[oi];
                if (&other == &self || other.validNodeCount < 2) continue;
                if (std::find(seen.begin(), seen.end(), &other) != seen.end()) continue;
                seen.push_back(&other);
                int o = other.validNodeCount - 1;
                bool mouth = std::abs(other.segmentsX[0] - ex) < 1.5 && std::abs(other.segmentsZ[0] - ez) < 1.5;
                bool head = std::abs(other.segmentsX[o] - ex) < 1.5 && std::abs(other.segmentsZ[o] - ez) < 1.5;
                if (mouth) mouths++;
                if (head) heads++;
                if (!(atMouth ? head : mouth) || onIt) continue;
                auto d = other.getClosestNodeData(x, z);
                if (d.beyondEnd) continue;
                double w = channelFullWidth(d, x, z);
                onIt = d.distance < w + std::max(BANK_ZONE_MIN, BANK_ZONE_PER_WIDTH * w);
            }
        }
    }
    return onIt && (atMouth ? (heads == 1 && mouths == 0) : (mouths == 1 && heads == 0));
}

bool HydrologyEngine::inRavineWay(const Ravine& r, double x, double z, bool ragged) const {
    double ox = x - r.px, oz = z - r.pz;
    double along = ox * r.ux + oz * r.uz;
    if (along <= 0.0) return false;
    double side = ragged ? r.across(noises_.riverBedNoise, x, z, ox, oz, along) : std::abs(r.lateral(ox, oz, along));
    return side < r.width + 30.0;
}

bool HydrologyEngine::underStep(const WatershedPath& path, const WatershedPath::RiverNodeData& nd, double x, double z) {
    if (nd.flowDirX == 0.0 && nd.flowDirZ == 0.0) return false;
    auto up = path.getClosestNodeDataNear(x - nd.flowDirX, z - nd.flowDirZ, nd.nodeIndex, 2);
    return up.distance < 1e290 && up.y > nd.y;
}

int HydrologyEngine::bankHold(const WatershedPath& path, int node, int ownY, double x, double z, double chanW,
                              const Ravine* ravine, const Ravine* fed, bool overRim, int seaLevel, bool wetOnly) const {
    int hold = INT32_MIN;
    for (int k = 0; k < 4; ++k) {
        double nx = x + (k == 0 ? 1.0 : (k == 1 ? -1.0 : 0.0));
        double nz = z + (k == 2 ? 1.0 : (k == 3 ? -1.0 : 0.0));
        auto nd = path.getClosestNodeDataNear(nx, nz, node, BANK_HOLD_SPAN);
        if (nd.distance >= chanW + 0.5) continue;
        if (wetOnly && channelBowl(nd, chanW) == 0) continue;
        if (ravine && (nd.nodeIndex < ravine->start || inRavineWay(*ravine, nx, nz, true))) continue;
        if (fed && inRavineWay(*fed, nx, nz, false)) continue;
        if (overRim && nd.y <= seaLevel + 1) continue;
        hold = std::max(hold, (nd.y > ownY || underStep(path, nd, nx, nz)) ? nd.y + 1 : nd.y);
    }
    return hold;
}

int HydrologyEngine::sealDepth(double dOut, double halfWidth) const {
    int deep = std::max(SEAL_MIN, int(dwRound(SEAL_PER_WIDTH * halfWidth)));
    double t = std::min(1.0, std::max(0.0, dOut) / hydrology_const::bankMeet(halfWidth));
    return int(dwRound(deep - (deep - 1) * t));
}

int HydrologyEngine::sealJitter(double x, double z) const {
    return int(dwRound(noises_.riverBedNoise.getValue(x * 0.4, z * 0.4) * 3.5));
}

int HydrologyEngine::vaultTop(const WatershedPath& path, const WatershedPath::RiverNodeData& data, double vaultR, double vaultH, int ground) const {
    int level = data.y;
    int last = std::min(path.validNodeCount - 1, data.nodeIndex + TUNNEL_LOOK_NODES);
    for (int k = data.nodeIndex + 1; k <= last; ++k) {
        level = std::max(level, path.segmentsY[k] - int((k - data.nodeIndex - 1) * WatershedPath::NODE_SPACING));
    }
    double t = std::min(1.0, data.distance / vaultR);
    int arch = int(dwRound(TUNNEL_WAIST + (vaultH - TUNNEL_WAIST) * std::sqrt(1.0 - t * t)));
    return std::max(data.y + arch, std::min(level + arch, ground - TUNNEL_CRUST));
}

int HydrologyEngine::gorgeStratum(double x, double z, int y, int waterY) const {
    int top = waterY + 1 + int(dwRound(hydrology_const::GORGE_WARP * noises_.riverBedNoise.getValue(x * 0.009 - 77.0, z * 0.009 + 41.0)));
    if (y <= top) return -1;
    for (int k = 0; k < 96; ++k) {
        uint64_t h = hydrology_const::gorgeHash(seed_, k, 0x5DEECE66Dull);
        int band = hydrology_const::GORGE_BAND_MIN + int(h % uint64_t(hydrology_const::GORGE_BAND_MAX - hydrology_const::GORGE_BAND_MIN + 1));
        top += band;
        if (y <= top) return k;
    }
    return 96;
}

bool HydrologyEngine::inGorgeReach(double x, double z) const {
    if (!cfg_.enabled || !cfg_.dynamicNormalRivers() || cfg_.normalRiverTunnels) return false;
    std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>> nearby;
    const_cast<MainRiversEngine&>(mainRivers_).collectNearbyEdges(x, z, nearby);
    double onRiver[2]{};
    for (const auto& pr : nearby) {
        const auto& regEdges = *pr.first;
        const RiverGraphEdge& path = regEdges[pr.second];
        double reach = path.maxWaterRadius(regEdges) + hydrology_const::GORGE_REACH + 2.0 + 4.0;
        if (path.fractal.boxDistanceSq(x, z) >= reach * reach) continue;
        if (path.fractal.nearestPoint(x, z, onRiver[0], onRiver[1]) >= reach * reach) continue;
        if (const_cast<BasinTileEngine&>(basin_).heights().coarseGroundAt(onRiver[0], onRiver[1]) <= hydrology_const::GORGE_FROM) continue;
        return true;
    }
    return false;
}

bool HydrologyEngine::inRavine(double x, double z) const {
    if (!cfg_.enabled || cfg_.normalRiverTunnels || !cfg_.enableWatersheds) return false;
    int colX = mthFloor(x), colZ = mthFloor(z);
    int homeRx = floorDiv(colX, hydrology_const::WATERSHED_REGION_SIZE);
    int homeRz = floorDiv(colZ, hydrology_const::WATERSHED_REGION_SIZE);
    const auto& nb = const_cast<BasinTileEngine&>(basin_).getNeighborhood3x3(homeRx, homeRz);
    if (!nb.anyPaths) return false;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const RegionSpatialGrid* grid = nb.grids[(dx + 1) * 3 + (dz + 1)];
            if (!grid || grid->paths.empty()) continue;
            for (int pi : grid->indicesAt(colX, colZ, homeRx + dx, homeRz + dz)) {
                const WatershedPath& p = grid->paths[pi];
                const Ravine* r = (p.validNodeCount >= 2 && entersGorge(p, cb_.seaLevel)) ? ravineOf(p, cb_.seaLevel) : nullptr;
                if (!r) continue;
                double ox = x - r->px, oz = z - r->pz;
                double along = ox * r->ux + oz * r->uz;
                if (along < -3.0 || along > r->length + 8.0) continue;
                if (std::abs(r->lateral(ox, oz, along)) < r->width + 30.0) return true;
            }
        }
    }
    return false;
}

// ---------- calculateRiverCarving / carve ----------
RiverCarving HydrologyEngine::carve(double x, double z, int originalY, int seaLevel, bool whole) const {
    RiverCarving result;
    result.targetY = originalY;
    result.waterLevel = seaLevel - 1;
    result.sealFloorY = originalY;
    result.cavernRoofY = -999;
    result.modifies = false;
    result.wallTopY = -999;
    if (!cfg_.enabled) return result;

    const int openSkyY = std::max(320, originalY + 64);
    int mountainTargetY = openSkyY;
    int mountainWaterLevel = INT32_MAX;
    int mountainWetLevel = INT32_MAX;
    int mountainAnyLevel = INT32_MIN;
    double riverEdgeGap = 1e300;
    int riverEdgeY = INT32_MIN;
    int riverRim = INT32_MIN;
    int mountainLakeBedY = INT32_MAX;
    int vaultPaths = 0;

    double jitterVal = 0.0;
    bool hasJitter = false;
    auto getJitter = [&]() -> double {
        if (!hasJitter) { jitterVal = hydrology_const::getRiverJitter(x, z); hasJitter = true; }
        return jitterVal;
    };
    double floorNoiseVal = 0.0;
    bool hasFloorNoise = false;
    auto getFloorNoise = [&]() -> double {
        if (!hasFloorNoise) { floorNoiseVal = riverBedNoise(noises_, x, z); hasFloorNoise = true; }
        return floorNoiseVal;
    };

    const WatershedLake* foundLake = whole ? findLake(x, z, originalY, false, true) : nullptr;
    const bool dugShore = (foundLake != nullptr && originalY > foundLake->waterY && !foundLake->inside(x, z));
    const WatershedLake* columnLake = dugShore ? nullptr : foundLake;

    WatershedContext mCtxVal{};
    bool hasMCtx = !whole;
    auto getMCtx = [&]() -> const WatershedContext& {
        if (!hasMCtx) { mCtxVal = evaluateWatershedContext(x, z, false); hasMCtx = true; }
        return mCtxVal;
    };

    if (cfg_.dynamicNormalRivers()) {
        const bool gorgeMode = !cfg_.normalRiverTunnels;
        double onRiver[2]{};
        thread_local std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>> nearbyRivers;
        nearbyRivers.clear();
        const_cast<MainRiversEngine&>(mainRivers_).collectNearbyEdges(x, z, nearbyRivers);

        double ragged = std::numeric_limits<double>::quiet_NaN();
        int splitTop = INT32_MAX, splitBed = INT32_MIN;

        for (const auto& pr : nearbyRivers) {
            const auto& regEdges = *pr.first;
            const RiverGraphEdge& path = regEdges[pr.second];
            double maxRadius = hydrology_const::waterRadiusAt(path, x, z, regEdges);
            if (path.delta) {
                if (std::isnan(ragged)) {
                    double dn = 0.65 * noises_.riverBedNoise.getValue(x / 32.0 + 311.0, z / 32.0 - 173.0)
                              + 0.35 * noises_.riverBedNoise.getValue(x / (0.4 * 32.0) - 59.0, z / (0.4 * 32.0) + 97.0);
                    ragged = 1.0 + 0.2 * mthClamp(dn * 1.6, -1.0, 1.0);
                }
                maxRadius *= ragged;
            }
            double furthest = gorgeMode ? maxRadius + hydrology_const::GORGE_REACH + hydrology_const::GORGE_ROUGH + 6.0 : maxRadius * 2.0 + 1.0;
            if (path.delta) furthest = std::max(furthest, maxRadius + hydrology_const::DELTA_PLAIN + 1.0);
            if (path.fractal.boxDistanceSq(x, z) >= furthest * furthest) continue;

            double rawDistance = std::sqrt(gorgeMode ? path.fractal.nearestPoint(x, z, onRiver[0], onRiver[1])
                                                     : path.fractal.intersectDistance(x, z));
            double canyonRadius = maxRadius;
            double waterRadius = maxRadius;
            double pastTip = 0.0;
            double headSlot = 1.0;

            if (!path.sourceEdge) {
                double distToSource = std::hypot(x - path.source.x, z - path.source.y);
                if (distToSource < hydrology_const::HEAD_TAPER + (gorgeMode ? HEAD_FEED_WIDEN : 0.0)) {
                    HeadFeed feed = whole ? headFeed(path, false) : HeadFeed{};
                    double fedFrom = feed.distToSource - feed.width;
                    bool fed = gorgeMode && feed.valid;
                    double widen = fed ? std::max(HEAD_FEED_WIDEN, hydrology_const::HEAD_TAPER - fedFrom) : 0.0;
                    if (distToSource < (fed ? fedFrom + widen : hydrology_const::HEAD_TAPER)) {
                        canyonRadius = maxRadius * mthClamp(distToSource / hydrology_const::HEAD_TAPER, 0.01, 1.0);
                        double taper = mthClamp((distToSource - hydrology_const::HEAD_WATER_FROM) / (hydrology_const::HEAD_TAPER - hydrology_const::HEAD_WATER_FROM), 0.0, 1.0);
                        waterRadius = maxRadius * (gorgeMode ? std::pow(taper, hydrology_const::HEAD_TIP_POWER) : taper);
                        double tipAt = hydrology_const::HEAD_WATER_FROM;
                        double least = std::min(maxRadius, feed.width * HEAD_FEED_MARGIN);
                        if (fed) {
                            double tipLen = std::max(HEAD_FEED_TIP_MIN, HEAD_FEED_TIP * least);
                            tipAt = fedFrom - tipLen;
                            waterRadius = (distToSource >= fedFrom)
                                ? least + (maxRadius - least) * std::pow((distToSource - fedFrom) / widen, hydrology_const::HEAD_TIP_POWER)
                                : least * std::pow(mthClamp((distToSource - tipAt) / tipLen, 0.0, 1.0), hydrology_const::HEAD_TIP_POWER);
                        } else if (distToSource >= fedFrom) {
                            waterRadius = std::max(waterRadius, least);
                            canyonRadius = std::max(canyonRadius, least);
                        }
                        if (gorgeMode) {
                            canyonRadius = waterRadius;
                            pastTip = std::max(0.0, tipAt - distToSource);
                            headSlot = 0.45 + (1.0 - 0.45) * waterRadius / maxRadius;
                        }
                    }
                }
            }

            double gorge = 0.0;
            double gorgeDepth = 0.0;
            double gorgeReach = waterRadius + hydrology_const::GORGE_REACH + hydrology_const::GORGE_ROUGH + 3.0;
            if (gorgeMode && rawDistance < gorgeReach && !path.delta) {
                double riverGround = cb_.sampleCoarseCell ? const_cast<BasinTileEngine&>(basin_).heights().coarseGroundAt(onRiver[0], onRiver[1]) : double(originalY);
                double t = mthClamp((riverGround - hydrology_const::GORGE_FROM) / hydrology_const::GORGE_RAMP, 0.0, 1.0);
                gorge = t * t * (3.0 - 2.0 * t);
                gorgeDepth = std::max(8.0, (riverGround - (seaLevel - 1)) * 1.1 + 6.0);
            }

            bool inReach = rawDistance < (path.delta ? maxRadius + hydrology_const::DELTA_PLAIN : maxRadius * 2.0);
            if (!inReach && !(gorge > 0.0 && rawDistance < gorgeReach)) continue;

            double distance = std::max(0.0, rawDistance + getJitter() * (canyonRadius / 10.0));
            double baselineY = double(seaLevel - 1);
            double heightDiffForWidth = originalY - baselineY;
            double widthFactor = std::min(1.0, std::max(0.0, (heightDiffForWidth - 3.0) / 10.0));
            double tunnelFactor = gorgeMode ? 0.0 : std::min(1.0, std::max(0.0, (originalY - cfg_.normalRiverCanyonHeight) / 8.0));

            double split = (gorgeMode && rawDistance < waterRadius && riverSplits(path))
                ? riverSplitDepth(path, x, z, maxRadius) : RIVER_SPLIT_NONE;

            if (!gorgeMode && getMCtx().bestPath != nullptr) {
                const auto& mCtx = getMCtx();
                double distToWatershed = mCtx.distance - mCtx.width;
                if (distToWatershed < 25.0 && distToWatershed > -10.0) {
                    double forceTunnel = (distToWatershed < 15.0) ? 1.0 : (1.0 - (distToWatershed - 15.0) / 10.0);
                    double mRiverHeightAboveSea = std::max(0.0, mCtx.bestY - baselineY);
                    double heightFade = std::min(1.0, mRiverHeightAboveSea / 8.0);
                    double nodeFade = std::min(1.0, mCtx.bestNodeIndex / 15.0);
                    forceTunnel *= std::max(heightFade, nodeFade);
                    tunnelFactor = std::max(tunnelFactor, forceTunnel);
                }
            }

            double bankNoise = noises_.riverBedNoise.getValue(x * 0.05, z * 0.05) * 3.0;
            double reqBankWidth;
            double hDiff = std::max(0.0, originalY - baselineY);
            double oldBaseWidth = 16.0 * (1.0 - widthFactor) + 4.0 * widthFactor;
            if (originalY < 80) {
                double reqNormSq = hDiff / 5.6;
                double reqNorm = std::sqrt(std::max(1.0, reqNormSq));
                double valleyBankWidth = (reqNorm - 1.0) * canyonRadius;
                reqBankWidth = valleyBankWidth * (1.0 - tunnelFactor) + oldBaseWidth * tunnelFactor;
            } else {
                reqBankWidth = oldBaseWidth;
            }
            double baseWidth = std::min(70.0, reqBankWidth + 5.0);
            double bankMultiplier = 1.0 - std::min(1.0, tunnelFactor * 1.5);
            double targetBankWidth = (baseWidth + bankNoise) * bankMultiplier;
            int tunnelLedge = tunnelFactor > 0.0 ? cfg_.normalTunnelLedge : 0;
            targetBankWidth = std::max(targetBankWidth, double(tunnelLedge) * tunnelFactor);

            if (getMCtx().bestPath != nullptr) {
                const auto& mCtx = getMCtx();
                double distToMouth = mCtx.distance;
                double mRiverHeightAboveSea = std::max(0.0, mCtx.bestY - baselineY);
                if (distToMouth < 25.0 && mRiverHeightAboveSea < 8.0 && mCtx.bestNodeIndex < 12) {
                    double distFade = std::max(0.0, 1.0 - distToMouth / 25.0);
                    double heightFadeMouth = std::max(0.0, 1.0 - mRiverHeightAboveSea / 8.0);
                    double mouthNodeFade = std::max(0.0, 1.0 - mCtx.bestNodeIndex / 12.0);
                    targetBankWidth = std::max(targetBankWidth, 12.0 * distFade * heightFadeMouth * mouthNodeFade);
                }
            }
            if (targetBankWidth < 0.0) targetBankWidth = 0.0;
            if (path.delta) targetBankWidth = std::max(targetBankWidth, hydrology_const::DELTA_PLAIN);

            double edgeOut = distance - canyonRadius + 0.45 * pastTip;
            bool inCanyon = inReach && (edgeOut < targetBankWidth || edgeOut < 0.0);
            if (inCanyon || (gorge > 0.0 && distance >= waterRadius)) {
                double totalWidth = canyonRadius + targetBankWidth;
                double totalNormDist = (totalWidth > 0.0) ? std::min(1.0, distance / totalWidth) : 1.0;
                double tunnelArch = std::cos(totalNormDist * DW_PI / 2.0);
                double t2 = totalNormDist * totalNormDist;
                double canyonArch = 1.0 - (t2 * t2);
                double arch = canyonArch * (1.0 - tunnelFactor) + tunnelArch * tunnelFactor;
                double canyonNoise = std::max(0.0, noises_.riverBedNoise.getValue(x * 0.02, z * 0.02));
                double openH = std::max(20.0, heightDiffForWidth + 15.0);
                double tunnelH = (tunnelFactor > 0.0) ? (double(cfg_.normalTunnelHeight) + 4.0 * canyonNoise) : 0.0;
                double H = openH * (1.0 - tunnelFactor) + tunnelH * tunnelFactor;

                if (distance < result.distanceFromCenter) {
                    result.distanceFromCenter = distance;
                    result.channelWidth = canyonRadius;
                    result.waterRadius = waterRadius;
                }
                int absoluteRoofY = mthFloor(baselineY + H * arch);
                if (gorge > 0.0) {
                    absoluteRoofY = inCanyon ? int(dwRound(absoluteRoofY + (double(openSkyY) - absoluteRoofY) * gorge)) : openSkyY;
                }

                if (split > 0.0 && originalY >= seaLevel - 1) {
                    int top = std::min(originalY, seaLevel - 1 + std::min(RIVER_SPLIT_TOP, int(split * RIVER_SPLIT_RISE)));
                    splitTop = std::min(splitTop, top);
                    result.modifies = true;
                    result.cavernRoofY = std::max(result.cavernRoofY, absoluteRoofY);
                    result.skinLevel = std::max(result.skinLevel, seaLevel - 1);
                    result.sealFloorY = std::min(result.sealFloorY, top - 3);
                    if (split < RIVER_SPLIT_BAR) result.pointBar = true;
                    if (top < originalY - 1) result.dressCut = true;
                } else if (distance < waterRadius) {
                    result.isMainRiverWater = true;
                    if (path.delta) result.deltaWater = true;
                    result.modifies = true;
                    if (tunnelFactor >= 0.99 && cfg_.normalTunnelPillars) {
                        int32_t hash = int32_t(x * 31337 + z * 42069);
                        hash ^= (hash >> 16);
                        if (std::abs(hash % 1000) < 2) result.isPillar = true;
                    }
                    double tBed = std::min(1.0, (waterRadius > 0.0) ? (distance / waterRadius) : 1.0);
                    double clampedFloorNoise = getFloorNoise() * std::max(0.0, 1.0 - tBed * 2.0);
                    double bowl = std::min(NORMAL_RIVER_DEPTH, 0.9 * waterRadius) * (1.0 - tBed * tBed);
                    double riverHeight = (seaLevel - 1) - bowl + clampedFloorNoise;
                    int localTargetY = mthFloor(std::min(riverHeight, double(originalY)));
                    splitBed = std::max(splitBed, mthFloor(seaLevel - 2 + std::min(0.0, split) * RIVER_SPLIT_SHOAL));
                    if (absoluteRoofY < originalY - 2 && tunnelFactor > 0.8 && cfg_.normalTunnelFormations) {
                        double sNoise = noises_.speleothemNoise.getValue(x * 0.4, z * 0.4);
                        if (sNoise > 0.65) localTargetY += int((sNoise - 0.65) * 12.0);
                        if (sNoise < -0.65) absoluteRoofY -= int((std::abs(sNoise) - 0.65) * 12.0);
                    }
                    result.targetY = std::min(result.targetY, localTargetY);
                    result.cavernRoofY = std::max(result.cavernRoofY, absoluteRoofY);
                    result.skinLevel = std::max(result.skinLevel, seaLevel - 1);
                } else if ((inCanyon && targetBankWidth > 0.0) || gorge > 0.0) {
                    int slopeY = originalY;
                    if (inCanyon && targetBankWidth > 0.0) {
                        double dOut = std::max(0.0, edgeOut);
                        double bankReach = path.delta ? hydrology_const::DELTA_PLAIN : hydrology_const::bankMeet(canyonRadius);
                        double talus = path.delta ? hydrology_const::DELTA_PLAIN_SLOPE : hydrology_const::BANK_TALUS;
                        slopeY = mthFloor(baselineY + ChannelForm::bankRise(dOut, originalY - baselineY, bankReach, talus));
                        if (tunnelLedge > 0 && tunnelFactor > 0.8) {
                            double s = std::min(1.0, dOut / targetBankWidth);
                            slopeY = std::min(slopeY, seaLevel + int(dwRound(TUNNEL_WAIST * (1.0 - std::cos(s * DW_PI / 2.0)))));
                        }
                        slopeY = std::min(slopeY, originalY);
                        if (path.delta && slopeY <= baselineY + 2.0) {
                            result.deltaFlat = true;
                            result.deltaShore = std::min(result.deltaShore, dOut);
                        }
                    }
                    if (gorge > 0.0) {
                        double wallOut = (distance - waterRadius + 0.45 * pastTip) / headSlot;
                        bool steep = false;
                        int wallY = hydrology_const::gorgeWallY(seed_, noises_.riverBedNoise, x, z, wallOut, originalY, seaLevel - 1, gorgeDepth, &steep);
                        slopeY = int(dwRound(slopeY + (wallY - slopeY) * gorge));
                        int hold = INT32_MIN;
                        const auto& mCtx = getMCtx();
                        if (mCtx.bestPath && mCtx.distance < mCtx.width + 4.0 && !atRavineRim(mCtx, seaLevel)) {
                            hold = mCtx.bestY + 1;
                        }
                        if (columnLake && laysLake(*columnLake, seaLevel) && columnLake->covers(x, z)) {
                            hold = std::max(hold, columnLake->waterY + 1);
                        }
                        slopeY = std::min(originalY, std::max(slopeY, hold));
                        if (gorge >= 0.5) {
                            if (slopeY < originalY) {
                                if (!result.gorgeWall || slopeY < result.targetY) result.gorgeSteep = steep;
                                result.gorgeWall = true;
                                result.gorgeSeal = true;
                            } else if (hydrology_const::gorgeWallY(seed_, noises_.riverBedNoise, x, z, wallOut - 2.0, originalY, seaLevel - 1, gorgeDepth) < originalY) {
                                result.gorgeSeal = true;
                            }
                        }
                    }
                    if (inCanyon || slopeY < originalY) {
                        result.modifies = true;
                        result.targetY = std::min(result.targetY, slopeY);
                        result.cavernRoofY = std::max(result.cavernRoofY, absoluteRoofY);
                        result.skinLevel = std::max(result.skinLevel, seaLevel - 1);
                        if (slopeY < originalY - 1 && !(tunnelLedge > 0 && tunnelFactor > 0.8)) result.dressCut = true;
                        result.sealFloorY = std::min(result.sealFloorY, originalY - sealDepth(distance - maxRadius, maxRadius) + sealJitter(x, z));
                        if (gorge > 0.0) result.sealFloorY = std::min(result.sealFloorY, slopeY - 3);
                    }
                }
            }
        }
        if (splitTop != INT32_MAX) {
            result.isMainRiverWater = false;
            result.riverSplit = true;
            result.targetY = splitTop;
        } else if (splitBed > result.targetY && result.isMainRiverWater) {
            result.targetY = std::min(splitBed, originalY);
            if (splitBed >= seaLevel - 3) result.pointBar = true;
        }
    }

    if (!whole) return result;

    const bool columnInLake = columnLake && columnLake->bedColumn(originalY) && columnLake->inside(x, z);
    const bool columnInShore = columnLake && !columnInLake && originalY < columnLake->waterY
        && columnLake->holdsColumn(originalY) && columnLake->shoreBand(x, z)
        && !columnLake->touchesInside(x, z) && !lakeDrainsHere(*columnLake, x, z);
    const bool lakeWater = columnInLake || (columnLake && columnLake->holdsColumn(originalY) && columnLake->touchesInside(x, z)) || columnInShore;

    const WatershedLake* hollowLake = nullptr;
    LakeHollowKind hollow = LakeHollowKind::None;
    if (!lakeWater && cb_.sampleSurfaceAt) {
        hollowLake = columnLake ? columnLake : lakeBeside(x, z, originalY, false);
        if (hollowLake && laysLake(*hollowLake, seaLevel) && originalY < hollowLake->waterY) {
            hollow = lakeHollowKind(*hollowLake, mthFloor(x), mthFloor(z), originalY, LAKE_SHORE_LIP);
        }
    }
    const bool columnIsLakeWater = lakeWater || (hollow == LakeHollowKind::Flooded);

    bool isMountainWater = false;
    const int groundBefore = std::min(originalY, result.targetY);
    int vaultLevel = INT32_MIN;
    int ravineSide = INT32_MAX, ravineHold = INT32_MIN;
    int heldFor = INT32_MIN;
    const WatershedPath* heldBy = nullptr;
    bool heldNearMouth = false;
    int lowNearY = INT32_MAX, lowNearY2 = INT32_MAX;
    const WatershedPath* lowNearPath = nullptr;

    double pinchVal = 0.0;
    bool hasPinchVal = false;
    auto getFullW = [&](const WatershedPath::RiverNodeData& nd) -> double {
        double gorge = double(nd.terrainY - nd.y);
        double narrow = gorge <= GORGE_NARROW_FROM ? 1.0
            : std::max(GORGE_NARROW_MIN, 1.0 - (1.0 - GORGE_NARROW_MIN) * std::min(1.0, (gorge - GORGE_NARROW_FROM) / (GORGE_NARROW_TO - GORGE_NARROW_FROM)));
        if (!hasPinchVal) {
            pinchVal = 0.6 * noises_.riverBedNoise.getValue(x * 0.007, z * 0.007)
                     + 0.4 * noises_.riverBedNoise.getValue(x * 0.0018 + 91.0, z * 0.0018 - 37.0);
            hasPinchVal = true;
        }
        return nd.width * narrow * ChannelForm::slopeWidth(nd.slope) * ChannelForm::pinchWidth(pinchVal);
    };
    double wallNoiseVal = 0.0;
    bool hasWallNoise = false;
    auto getWallNoise = [&]() -> double {
        if (!hasWallNoise) { wallNoiseVal = noises_.riverBedNoise.getValue(x * 0.15, z * 0.15) * 0.25; hasWallNoise = true; }
        return wallNoiseVal;
    };
    int sealJitterVal = 0;
    bool hasSealJitter = false;
    auto getSealJitter = [&]() -> int {
        if (!hasSealJitter) { sealJitterVal = sealJitter(x, z); hasSealJitter = true; }
        return sealJitterVal;
    };
    double vaultSwellCached = 0.0, wallRoughCached = 0.0, roofRoughCached = 0.0;
    bool hasTunnelNoises = false;
    auto ensureTunnelNoises = [&]() {
        if (!hasTunnelNoises) {
            vaultSwellCached = noises_.riverBedNoise.getValue(x * 0.02, z * 0.02) * CAVE_SWELL;
            wallRoughCached = noises_.riverBedNoise.getValue(x * 0.18, z * 0.18) * CAVE_ROUGH;
            roofRoughCached = noises_.riverBedNoise.getValue((x + 512.0) * 0.14, (z - 512.0) * 0.14) * CAVE_ROUGH;
            hasTunnelNoises = true;
        }
    };

    if (cfg_.enableWatersheds && !result.riverSplit) {
        const int colX = mthFloor(x), colZ = mthFloor(z);
        const int homeRx = floorDiv(colX, hydrology_const::WATERSHED_REGION_SIZE);
        const int homeRz = floorDiv(colZ, hydrology_const::WATERSHED_REGION_SIZE);
        const auto& nb = const_cast<BasinTileEngine&>(basin_).getNeighborhood3x3(homeRx, homeRz);
        if (nb.anyPaths && x >= nb.pathsMinX && x <= nb.pathsMaxX && z >= nb.pathsMinZ && z <= nb.pathsMaxZ)
        for (int dx = -WATERSHED_SCAN_RADIUS; dx <= WATERSHED_SCAN_RADIUS; ++dx) {
            for (int dz = -WATERSHED_SCAN_RADIUS; dz <= WATERSHED_SCAN_RADIUS; ++dz) {
                int mrx = homeRx + dx, mrz = homeRz + dz;
                const RegionSpatialGrid* grid = (dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1)
                    ? nb.grids[(dx + 1) * 3 + (dz + 1)]
                    : nullptr;
                if (!grid || grid->paths.empty() || x < grid->pathsMinX || x > grid->pathsMaxX || z < grid->pathsMinZ || z > grid->pathsMaxZ) continue;
                for (int pi : grid->indicesAt(colX, colZ, mrx, mrz)) {
                    const WatershedPath& mountainPath = grid->paths[pi];
                    auto data = mountainPath.getClosestNodeData(x, z);
                    if (data.distance > 1e290) continue;
                    if (feedsDelta(mountainPath)) continue;
                    if (!cfg_.normalRiverTunnels && leftDry(mountainPath, seaLevel)) continue;

                    bool overRim = !cfg_.normalRiverTunnels && !result.isMainRiverWater && entersGorge(mountainPath, seaLevel);
                    const Ravine* ravine = overRim ? ravineOf(mountainPath, seaLevel) : nullptr;
                    const Ravine* fed = (!ravine && !cfg_.normalRiverTunnels && !result.isMainRiverWater) ? feederRavine(mountainPath, seaLevel) : nullptr;
                    if (fed) {
                        double ox = x - fed->px, oz = z - fed->pz;
                        double fedAlong = ox * fed->ux + oz * fed->uz;
                        if (fedAlong > 0.0 && std::abs(fed->lateral(ox, oz, fedAlong)) < fed->width + 30.0) continue;
                    }
                    if (ravine) {
                        double ox = x - ravine->px, oz = z - ravine->pz;
                        double along = ox * ravine->ux + oz * ravine->uz;
                        double across = ravine->across(noises_.riverBedNoise, x, z, ox, oz, along);
                        bool behind = along < 0.0 && along > -(ravine->width + 2.0)
                            && data.nodeIndex < ravine->start && data.distance < ravine->width + 2.0;
                        if ((along >= 0.0 || behind) && along <= ravine->length + 8.0 && across < ravine->width + 30.0) {
                            int w = ravine->poolLevel(noises_.riverBedNoise, x, z, along);
                            bool inPool = across < ravine->width;
                            int beside = INT32_MIN;
                            if (!inPool && across < ravine->width + 2.0) {
                                int east = ravine->waterAt(noises_.riverBedNoise, x + 1.0, z);
                                int west = ravine->waterAt(noises_.riverBedNoise, x - 1.0, z);
                                int south = ravine->waterAt(noises_.riverBedNoise, x, z + 1.0);
                                int north = ravine->waterAt(noises_.riverBedNoise, x, z - 1.0);
                                beside = std::max(std::max(east, west), std::max(south, north));
                                if ((east != INT32_MIN && west != INT32_MIN) || (south != INT32_MIN && north != INT32_MIN)) {
                                    inPool = true;
                                    w = beside;
                                }
                            }
                            bool cut = false;
                            double u = std::min(1.0, across / ravine->width);
                            int bowl = mthClamp(int(dwRound(STREAM_DEPTH_RATIO * ravine->width * (1.0 - u * u))), 1, MAX_BOWL);
                            if (inPool && w - bowl <= groundBefore) {
                                isMountainWater = true;
                                result.isWatershedWall = false;
                                mountainWaterLevel = std::min(mountainWaterLevel, w);
                                mountainWetLevel = std::min(mountainWetLevel, w);
                                mountainTargetY = std::min(mountainTargetY, w - bowl);
                                if (across < result.distanceFromCenter) {
                                    result.distanceFromCenter = across;
                                    result.channelWidth = ravine->width;
                                    result.channelSlope = 1.0;
                                }
                                cut = true;
                            } else if (!inPool) {
                                double noise = noises_.riverBedNoise.getValue(x * 0.15, z * 0.15) * 1.5;
                                double out = across - ravine->width;
                                int line = mthFloor(ravine->sideLine(along)) + 1 + std::max(0, mthFloor(out * 1.0 + noise));
                                int bank = ravine->levelAt(along - 2.0 - 1.0 * out) + 1 + std::max(0, mthFloor(out * 3.0 + noise));
                                ravineSide = std::min(ravineSide, std::min(line, bank));
                                if (beside > seaLevel - 1) ravineHold = std::max(ravineHold, beside + 1);
                            }
                            if (cut) {
                                result.modifies = true;
                                result.isWatershed = true;
                                result.isMountainRiver = true;
                                result.cavernRoofY = openSkyY;
                                mountainAnyLevel = std::max(mountainAnyLevel, w);
                            }
                        }
                        int roundStart = ravine->start + int(std::ceil((ravine->width + 2.0) / WatershedPath::NODE_SPACING));
                        if (data.nodeIndex < ravine->start || (data.nodeIndex <= roundStart && along > 0.0 && across < ravine->width + 30.0))
                            continue;
                    }
                    bool atRim = false;
                    if (overRim && !ravine && data.y <= seaLevel + 1) {
                        int lip = rimLevel(mountainPath, data.nodeIndex, seaLevel);
                        if (lip != INT32_MIN) {
                            if (std::min(originalY, result.targetY) < lip) continue;
                            data = data.atLevel(lip);
                            atRim = true;
                        }
                        const Ravine* above = atRim ? nullptr : ravineAbove(mountainPath, seaLevel);
                        if (above) {
                            double ox = x - above->px, oz = z - above->pz;
                            double along = ox * above->ux + oz * above->uz;
                            if (along > -3.0 && along < above->length + 8.0 && std::abs(above->lateral(ox, oz, along)) < above->width + 30.0)
                                continue;
                        }
                    }

                    double gorge = double(data.terrainY - data.y);
                    double fullW = getFullW(data);
                    double outletW = ravine ? std::min(fullW, ravine->width) : std::min(fullW, std::max(2.5, 0.5 * fullW));
                    double chanW = atRim ? outletW : fullW;
                    if (overRim && !atRim && outletW < fullW) {
                        double funnel = std::max(8.0, 2.0 * fullW);
                        double d = ravine ? funnelDistance(mountainPath, data.nodeIndex, ravine->start, x, z, funnel)
                                          : rimDistance(mountainPath, data.nodeIndex, x, z, seaLevel, funnel);
                        if (!std::isnan(d)) {
                            double t = std::max(0.0, d - (ravine ? outletW + 1.0 : 0.0)) / funnel;
                            chanW = mthLerp(t * t * (3.0 - 2.0 * t), outletW, fullW);
                        }
                    } else if (fed && fed->width < fullW) {
                        double funnel = std::max(8.0, 2.0 * fullW);
                        double d = funnelDistance(mountainPath, data.nodeIndex, 0, x, z, funnel);
                        if (!std::isnan(d)) {
                            double t = std::max(0.0, d - (fed->width + 1.0)) / funnel;
                            chanW = mthLerp(t * t * (3.0 - 2.0 * t), fed->width, fullW);
                        }
                    }

                    bool tunnel = cfg_.mountainRiverTunnels && gorge > TUNNEL_FROM_DEPTH;
                    double vaultR = chanW + 1.0;
                    double vaultH = TUNNEL_WAIST + 2.0;
                    if (tunnel && (data.distance < chanW || data.distance < chanW + std::max(1.0, double(cfg_.mountainTunnelLedge) + 2.0 * (CAVE_SWELL + CAVE_ROUGH) + PORTAL_FLARE))) {
                        ensureTunnelNoises();
                        double vaultH0 = std::max(double(cfg_.mountainTunnelHeight), 1.2 * chanW) + vaultSwellCached + roofRoughCached;
                        double flare = mthClamp(1.0 - (groundBefore - (data.y + vaultH0)) / PORTAL_CRUST, 0.0, 1.0);
                        vaultR = chanW + std::max(1.0, double(cfg_.mountainTunnelLedge) + vaultSwellCached + wallRoughCached + PORTAL_FLARE * flare);
                        vaultH = std::max(TUNNEL_WAIST + 2.0, vaultH0 + PORTAL_FLARE * flare);
                    }

                    bool atLakeLevel = columnLake && columnLake->covers(x, z) && data.y >= columnLake->waterY - 2 && data.y <= columnLake->waterY + 1;
                    if (result.isMainRiverWater && data.distance >= chanW) {
                        if (result.cavernRoofY < originalY - 2 || result.targetY <= seaLevel - 1) continue;
                    }

                    double distToOutlet = data.nodeIndex * WatershedPath::NODE_SPACING;
                    bool isNearOutlet = distToOutlet < 24.0;
                    double reach = std::max(BANK_ZONE_MIN, BANK_ZONE_PER_WIDTH * chanW);

                    if (data.beyondEnd && (data.distance >= chanW || channelBowl(data, chanW) == 0)
                        && data.distance < chanW + std::max(reach, 2.0)
                        && (capYields(mountainPath, data, x, z, colX, colZ, homeRx, homeRz)
                            || capGoesOn(mountainPath, x, z, colX, colZ, homeRx, homeRz))) {
                        continue;
                    }

                    if (data.distance < chanW + 2.0) riverRim = std::max(riverRim, data.y);
                    if (data.distance < chanW + reach) {
                        mountainAnyLevel = std::max(mountainAnyLevel, data.y);
                        if (data.distance - chanW < riverEdgeGap) {
                            riverEdgeGap = data.distance - chanW;
                            riverEdgeY = data.y;
                        }
                        result.modifies = true;
                        result.isWatershed = true;
                        result.isMountainRiver = true;

                        if (data.distance < chanW) {
                            isMountainWater = true;
                            result.isWatershedWall = false;
                            double lat = mthClamp(data.lateral / chanW, -1.0, 1.0);
                            double skew = ChannelForm::bendSkew(data.curvature, chanW);
                            int bowl = channelBowl(data, chanW);
                            if (!columnInLake && !atLakeLevel && ChannelForm::onBar(lat, skew)) result.pointBar = true;

                            int localTargetY = columnInLake
                                ? std::min(originalY - 1, std::min(columnLake->waterY, data.y) - bowl)
                                : (data.y - bowl);
                            if (!columnInLake && originalY >= seaLevel - 1) {
                                localTargetY = std::max(localTargetY, (seaLevel - 1) - bowl);
                            }
                            int targetWaterLevel = data.y;
                            if (isNearOutlet && !result.isMainRiverWater && !atRim && !ravine) {
                                int outletY = mountainPath.segmentsY[0];
                                if (outletY < targetWaterLevel) {
                                    double blend = distToOutlet / 24.0;
                                    targetWaterLevel = mthFloor(outletY + blend * (targetWaterLevel - outletY));
                                    if (!columnInLake) localTargetY = targetWaterLevel - bowl;
                                }
                            }
                            if (result.isMainRiverWater) targetWaterLevel = std::min(targetWaterLevel, seaLevel - 1);
                            if (data.bankFloor != INT32_MIN && localTargetY >= targetWaterLevel && !columnInLake && !atLakeLevel && !tunnel) {
                                localTargetY = targetWaterLevel - 1;
                            }
                            if (localTargetY >= targetWaterLevel && !columnInLake && !atLakeLevel && !tunnel && !atRim && !ravine && !result.isMainRiverWater) {
                                int rimHold = bankHold(mountainPath, data.nodeIndex, data.y, x, z, chanW, nullptr, fed, overRim, seaLevel, true);
                                if (rimHold > localTargetY && originalY < rimHold && rimHold - originalY <= BANK_HOLD_OVER) {
                                    localTargetY = rimHold;
                                }
                            }
                            mountainWaterLevel = std::min(mountainWaterLevel, targetWaterLevel);
                            if (localTargetY < targetWaterLevel) mountainWetLevel = std::min(mountainWetLevel, targetWaterLevel);
                            if (atLakeLevel) {
                                mountainLakeBedY = std::min(mountainLakeBedY, std::min(columnLake->waterY, targetWaterLevel) - bowl);
                            }
                            mountainTargetY = std::min(mountainTargetY, localTargetY);
                            if (tunnel) {
                                result.cavernRoofY = std::max(result.cavernRoofY, vaultTop(mountainPath, data, vaultR, vaultH, groundBefore));
                                vaultPaths++;
                            } else {
                                result.cavernRoofY = openSkyY;
                            }
                            if (data.distance < result.distanceFromCenter) {
                                result.distanceFromCenter = data.distance;
                                result.channelWidth = chanW;
                                result.channelSlope = data.slope;
                            }
                        } else if (tunnel) {
                            if (data.distance >= vaultR) continue;
                            int roofY = vaultTop(mountainPath, data, vaultR, vaultH, groundBefore);
                            double s = (data.distance - chanW) / (vaultR - chanW);
                            int floorY = data.y + int(dwRound(TUNNEL_WAIST * (1.0 - std::cos(s * DW_PI / 2.0))));
                            if (floorY >= roofY || floorY >= groundBefore) continue;
                            if (result.targetY >= originalY || floorY < result.targetY) result.targetY = floorY;
                            result.cavernRoofY = std::max(result.cavernRoofY, roofY);
                            vaultLevel = (vaultLevel == INT32_MIN) ? data.y : std::min(vaultLevel, data.y);
                            vaultPaths++;
                        } else {
                            if (columnIsLakeWater || atLakeLevel) continue;
                            double dOut = data.distance - chanW;
                            double extraReach = 6.0;
                            double outerReach = reach + extraReach;
                            if (dOut <= outerReach) {
                                if (data.y <= seaLevel && originalY <= seaLevel + 1) continue;
                                if (originalY <= seaLevel - 1) continue;
                                bool needsBerm = originalY < data.y + 1;
                                if (isNearOutlet && data.y <= seaLevel) needsBerm = false;
                                if (columnLake && data.y <= columnLake->waterY + 1) {
                                    double distToHead = (mountainPath.nodeCount - 1 - data.nodeIndex) * WatershedPath::NODE_SPACING;
                                    if (isNearOutlet || distToHead < 24.0) needsBerm = false;
                                }
                                int hold = (dOut < BANK_HOLD_REACH && !atRim && (needsBerm || originalY >= data.y + 1))
                                    ? bankHold(mountainPath, data.nodeIndex, data.y, x, z, chanW, ravine, fed, overRim, seaLevel, false)
                                    : INT32_MIN;
                                if (hold > heldFor) { heldFor = hold; heldBy = &mountainPath; heldNearMouth = isNearOutlet; }
                                if (dOut < BANK_HOLD_NEAR) {
                                    if (data.y < lowNearY) {
                                        if (lowNearPath != &mountainPath) lowNearY2 = lowNearY;
                                        lowNearY = data.y; lowNearPath = &mountainPath;
                                    } else if (data.y < lowNearY2 && &mountainPath != lowNearPath) {
                                        lowNearY2 = data.y;
                                    }
                                }
                                double bankWidth = hydrology_const::bankMeet(chanW);
                                double tOut = std::min(1.0, dOut / bankWidth);
                                double wallNoise = getWallNoise();
                                int finalTargetY = originalY;
                                if (needsBerm) {
                                    double dEff = dOut - 1.0;
                                    int below = data.y - originalY;
                                    int fall = mthClamp(mthFloor(ChannelForm::leveeFall(dEff, double(below), bankWidth - 1.0, 0.5) + wallNoise), 0, std::max(0, below));
                                    int bermTop = data.y + 1 - fall;
                                    if (dOut < 1.0) bermTop = data.y;
                                    else if (dEff <= std::max(2.0, chanW * 2.0 / 3.0)) bermTop = std::max(bermTop, data.y + 1);
                                    else if (dEff <= 3.0) bermTop = std::max(bermTop, data.y);
                                    if (data.bankFloor != INT32_MIN) {
                                        bermTop = std::max(bermTop, data.bankFloor - mthFloor(std::max(0.0, dEff) * 0.5));
                                    }
                                    if (dOut > bankWidth) {
                                        double tOuter = std::min(1.0, (dOut - bankWidth) / extraReach);
                                        double outerBlend = tOuter * tOuter * (3.0 - 2.0 * tOuter);
                                        bermTop = mthFloor(bermTop + outerBlend * (originalY - bermTop));
                                    }
                                    finalTargetY = bermTop;
                                } else {
                                    int bankTop = mthFloor(data.y + ChannelForm::bankRise(dOut, originalY - data.y, bankWidth, hydrology_const::BANK_TALUS) + wallNoise);
                                    if (originalY <= seaLevel + 1 && bankTop < seaLevel + 1) bankTop = std::max(bankTop, seaLevel + 1);
                                    int maxCut = int(dwRound(MAX_BANK_CUT * (1.0 - tOut)));
                                    int steepest = mthFloor(data.y + STEEP_BANK_TALUS * dOut + wallNoise);
                                    bankTop = std::max(bankTop, std::min(originalY - maxCut, steepest));
                                    if (columnLake && data.y < columnLake->waterY && columnLake->covers(x, z)) {
                                        bankTop = std::max(bankTop, columnLake->waterY + 1);
                                    }
                                    bankTop = std::max(data.y, std::min(bankTop, std::max(data.y, originalY)));
                                    if (data.bankFloor != INT32_MIN) {
                                        bankTop = std::max(bankTop, std::min(originalY, data.bankFloor - mthFloor(std::max(0.0, dOut - 1.0) * 0.5)));
                                    }
                                    if (dOut > bankWidth) {
                                        double tOuter = std::min(1.0, (dOut - bankWidth) / extraReach);
                                        double outerBlend = tOuter * tOuter * (3.0 - 2.0 * tOuter);
                                        bankTop = mthFloor(bankTop + outerBlend * (originalY - bankTop));
                                    }
                                    finalTargetY = bankTop;
                                }
                                if (hold > finalTargetY) {
                                    finalTargetY = hold;
                                    if (hold > originalY) needsBerm = true;
                                }
                                if (needsBerm) {
                                    if (result.targetY >= originalY) result.targetY = std::max(result.targetY, finalTargetY);
                                } else {
                                    if (result.targetY >= originalY || finalTargetY < result.targetY) result.targetY = finalTargetY;
                                    if (finalTargetY < originalY - 1) result.dressCut = true;
                                }
                                if (finalTargetY > originalY || originalY > data.y) {
                                    result.isWatershedWall = true;
                                    result.wallTopY = std::max(result.wallTopY, finalTargetY);
                                }
                                result.cavernRoofY = openSkyY;
                            }
                            result.sealFloorY = std::min(result.sealFloorY, originalY - sealDepth(data.distance - chanW, chanW) + getSealJitter());
                        }
                    }
                }
            }
        }
    }

    if (ravineSide != INT32_MAX) {
        int top = std::max(std::max(ravineSide, ravineHold), riverRim + 1);
        if (top < groundBefore) {
            if (result.targetY >= originalY || top < result.targetY) result.targetY = top;
            result.modifies = true;
            result.cavernRoofY = openSkyY;
            result.gorgeWall = true;
            result.gorgeSteep = false;
            result.gorgeSeal = true;
            result.dressCut = true;
        } else if (ravineHold > groundBefore && ravineHold - groundBefore <= 3 && !isMountainWater) {
            result.targetY = std::max(result.targetY, ravineHold);
            result.modifies = true;
            result.cavernRoofY = openSkyY;
            result.isWatershed = true;
            result.isWatershedWall = true;
            result.wallTopY = std::max(result.wallTopY, ravineHold);
            result.skinLevel = std::max(result.skinLevel, ravineHold - 2);
        }
    }

    int lowerBeside = (lowNearPath != heldBy) ? lowNearY : lowNearY2;
    if (heldFor > result.targetY && !isMountainWater && !result.isMainRiverWater
        && heldFor - originalY <= BANK_HOLD_OVER && (lowerBeside >= heldFor - 1 || !heldNearMouth)) {
        result.targetY = heldFor;
        result.modifies = true;
        result.isWatershed = true;
        result.isWatershedWall = true;
        result.wallTopY = std::max(result.wallTopY, heldFor);
        result.skinLevel = std::max(result.skinLevel, mountainAnyLevel);
        result.cavernRoofY = openSkyY;
    }

    if (vaultPaths >= 2 && result.cavernRoofY > -999 && result.cavernRoofY < openSkyY) {
        result.cavernRoofY += JUNCTION_CHAMBER;
    }

    if (isMountainWater) {
        result.isWatershedWall = false;
        result.waterLevel = std::max(result.waterLevel, mountainWetLevel != INT32_MAX ? mountainWetLevel : mountainWaterLevel);
        bool isOceanFloor = originalY < seaLevel - 2 && mountainTargetY <= seaLevel;
        if (result.isMainRiverWater && result.targetY < mountainTargetY) {
            // main river dominates depth
        } else if (isOceanFloor) {
            result.isMountainRiver = false;
            if (!result.isMainRiverWater) { result.modifies = false; result.isWatershed = false; }
            result.targetY = std::min(result.targetY, originalY);
            result.waterLevel = std::max(result.waterLevel, seaLevel - 1);
        } else {
            result.targetY = mountainTargetY;
        }
    } else if (vaultLevel > INT32_MIN) {
        result.waterLevel = std::max(result.waterLevel, vaultLevel);
    }

    if (isMountainWater) result.skinLevel = result.waterLevel;
    else if (mountainAnyLevel > INT32_MIN) result.skinLevel = mountainAnyLevel;

    const WatershedLake* lakeHere = columnLake ? columnLake : (hollow != LakeHollowKind::None ? hollowLake : (dugShore ? foundLake : nullptr));
    if (lakeHere && laysLake(*lakeHere, seaLevel)) {
        int lakeWaterY = lakeHere->waterY;
        if (columnIsLakeWater) {
            bool riverAbove = mountainAnyLevel > lakeWaterY + 1;
            if (!result.isWatershedWall || result.wallTopY <= lakeWaterY || !riverAbove) {
                result.modifies = true;
                result.isWatershed = true;
                result.isLake = true;
                result.isWatershedWall = false;
                result.cavernRoofY = openSkyY;
                bool channelAbove = isMountainWater && mountainWaterLevel > lakeWaterY + 1;
                result.waterLevel = channelAbove ? std::max(result.waterLevel, lakeWaterY) : lakeWaterY;
                result.skinLevel = channelAbove ? std::max(result.skinLevel, lakeWaterY) : lakeWaterY;
                if (columnInShore || (hollow == LakeHollowKind::Flooded && originalY >= lakeHere->floorY() - WatershedLake::FLOOR_SLACK)) {
                    result.targetY = std::min(result.targetY, originalY);
                } else {
                    int bed = lakeWaterY - lakeDepth(lakeHere->shoreDistance(mthFloor(x), mthFloor(z)));
                    result.targetY = result.isMainRiverWater ? std::min(result.targetY, bed) : bed;
                }
                if (mountainLakeBedY < result.targetY) result.targetY = mountainLakeBedY;
            }
        } else if ((!isMountainWater || result.targetY >= lakeWaterY) && riverEdgeY == lakeWaterY
                   && riverEdgeGap < LAKE_LIP_RIVER_GAP && lakeHere->covers(x, z)
                   && std::min(originalY, result.modifies ? result.targetY : originalY) <= lakeWaterY
                   && originalY >= lakeWaterY - LAKE_SHORE_LIP) {
            result.modifies = true;
            result.isWatershed = true;
            result.isLake = true;
            result.isWatershedWall = false;
            result.targetY = std::min(result.targetY, std::min(originalY, lakeWaterY - 1));
            result.waterLevel = lakeWaterY;
            result.skinLevel = lakeWaterY;
            result.cavernRoofY = openSkyY;
        } else if (originalY <= lakeWaterY
                   && originalY >= lakeWaterY - lipDepth(*lakeHere, columnLake != nullptr, hollow, x, z, originalY)
                   && !isMountainWater
                   && (!result.modifies || result.targetY >= originalY)) {
            result.modifies = true;
            result.isWatershed = true;
            result.isWatershedWall = true;
            result.targetY = std::max(result.targetY, lakeWaterY);
            result.wallTopY = std::max(result.wallTopY, lakeWaterY);
            result.skinLevel = std::max(result.skinLevel, lakeWaterY);
            result.cavernRoofY = openSkyY;
        } else if (originalY > lakeWaterY && !isMountainWater
                   && (!result.modifies || result.targetY >= originalY)
                   && lakeHere->covers(x, z)) {
            double grade = lakeHere->shoreGrade(mthFloor(x), mthFloor(z));
            int shoreTop = lakeWaterY + int(dwRound((originalY - lakeWaterY) * grade));
            shoreTop = std::max(shoreTop, originalY - LAKE_SHORE_CUT);
            if (shoreTop < originalY) {
                result.modifies = true;
                result.isWatershed = true;
                result.isWatershedWall = true;
                result.targetY = shoreTop;
                result.wallTopY = std::max(result.wallTopY, shoreTop);
                result.skinLevel = std::max(result.skinLevel, lakeWaterY);
                result.cavernRoofY = openSkyY;
            }
        }
    }
    return result;
}

RiverCarving HydrologyEngine::calculateRiverCarving(double x, double z, int originalY, int seaLevel) const {
    return carve(x, z, originalY, seaLevel, true);
}

RiverCarving HydrologyEngine::lowlandCut(double x, double z, int ground, int seaLevel) const {
    return carve(x, z, ground, seaLevel, false);
}

// ---------- Flow & Water Predicates ----------
void HydrologyEngine::ravineFlowVec(double x, int y, double z, double& fx, double& fz, bool& found) const {
    found = false;
    if (cfg_.normalRiverTunnels || !cfg_.enableWatersheds) return;
    struct TlsRavFlow {
        const HydrologyEngine* owner = nullptr;
        uint64_t rEp = 0, bEp = 0;
        double x = 0.0, z = 0.0;
        int y = 0;
        double fx = 0.0, fz = 0.0;
        bool found = false, valid = false;
    };
    thread_local TlsRavFlow last;
    const uint64_t rEp = resetEpoch_.load(std::memory_order_acquire);
    const uint64_t bEp = basin_.currentEpoch();
    if (last.valid && last.owner == this && last.rEp == rEp && last.bEp == bEp && last.x == x && last.y == y && last.z == z) {
        fx = last.fx; fz = last.fz; found = last.found;
        return;
    }
    int colX = mthFloor(x), colZ = mthFloor(z);
    int homeRx = floorDiv(colX, hydrology_const::WATERSHED_REGION_SIZE);
    int homeRz = floorDiv(colZ, hydrology_const::WATERSHED_REGION_SIZE);
    const auto& nb = const_cast<BasinTileEngine&>(basin_).getNeighborhood3x3(homeRx, homeRz);
    if (!nb.anyPaths) {
        last = {this, rEp, bEp, x, z, y, fx, fz, false, true};
        return;
    }
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const RegionSpatialGrid* grid = nb.grids[(dx + 1) * 3 + (dz + 1)];
            if (!grid || grid->paths.empty()) continue;
            for (int pi : grid->indicesAt(colX, colZ, homeRx + dx, homeRz + dz)) {
                const WatershedPath& p = grid->paths[pi];
                const Ravine* r = (p.validNodeCount >= 2 && entersGorge(p, cb_.seaLevel)) ? ravineOf(p, cb_.seaLevel) : nullptr;
                if (!r || y > r->top + 1 || y < cb_.seaLevel - 10) continue;
                double ox = x - r->px, oz = z - r->pz;
                double along = ox * r->ux + oz * r->uz;
                if (along < 0.0 || along > r->length) continue;
                if (r->across(noises_.riverBedNoise, x, z, ox, oz, along) >= r->width + 1.0) continue;
                double bend = r->centre(noises_.riverBedNoise, along + 0.5) - r->centre(noises_.riverBedNoise, along - 0.5);
                double vx = r->ux + r->uz * bend, vz = r->uz - r->ux * bend;
                double fl = std::hypot(vx, vz);
                if (fl > 1e-6) {
                    fx = vx / fl; fz = vz / fl; found = true;
                    last = {this, rEp, bEp, x, z, y, fx, fz, true, true};
                    return;
                }
            }
        }
    }
    last = {this, rEp, bEp, x, z, y, fx, fz, false, true};
}

void HydrologyEngine::lowlandFlow(double x, int y, double z, double& fx, double& fz, bool checkOceanBiome) const {
    fx = 0.0; fz = 0.0;
    if (y < cb_.seaLevel - 6 || y > cb_.seaLevel + 1) return;
    if (checkOceanBiome && cb_.isOceanBiomeAt && cb_.isOceanBiomeAt(mthFloor(x), mthFloor(z))) return;
    RiverContext ctx = const_cast<MainRiversEngine&>(mainRivers_).findNearest(x, z);
    const RiverGraphEdge* ctxEdge = ctx.edge();
    if (!ctxEdge) return;
    if (ctxEdge->delta && !ctxEdge->deltaMain) return;
    double rawDistance = std::sqrt(ctx.distSq);
    double jitter = hydrology_const::getRiverJitter(x, z) * (std::sqrt(ctx.widthSq) / 10.0);
    double distance = std::max(0.0, rawDistance + jitter);
    if (distance >= flowWaterRadius(*ctxEdge, x, z) + 1.0) return;

    thread_local std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>> regionPaths;
    regionPaths.clear();
    const_cast<MainRiversEngine&>(mainRivers_).collectNearbyEdges(x, z, regionPaths);
    double minRatioFlow = 1e300;
    for (const auto& pr : regionPaths) {
        const RiverGraphEdge* p = &(*pr.first)[pr.second];
        if (p->delta && !p->deltaMain) continue;
        double bound = p->width * 2.0;
        if (p->fractal.boxDistanceSq(x, z) >= bound * bound) continue;
        double dSq = p->fractal.intersectDistance(x, z);
        if (dSq >= bound * bound) continue;
        double ratio = std::sqrt(dSq) / p->width;
        if (ratio < minRatioFlow) minRatioFlow = ratio;
    }
    double ratioThreshold = minRatioFlow * 1.3;
    double sinSum = 0.0, cosSum = 0.0, totalWeight = 0.0;
    for (const auto& pr : regionPaths) {
        const RiverGraphEdge* p = &(*pr.first)[pr.second];
        if (p->delta && !p->deltaMain) continue;
        double bound = p->width * 2.0;
        if (p->fractal.boxDistanceSq(x, z) >= bound * bound) continue;
        double dSq = p->fractal.intersectDistance(x, z);
        if (dSq >= bound * bound) continue;
        double d = std::sqrt(dSq);
        if (d / p->width > ratioThreshold) continue;
        double wDist = 1.0 - std::min(1.0, d / p->width);
        double w = wDist * wDist;
        const auto& segs = p->fractal.segments;
        double drainX = segs[segs.size() - 2], drainZ = segs[segs.size() - 1];
        double srcX = segs[0], srcZ = segs[1];
        double totalLenSq = (drainX - srcX) * (drainX - srcX) + (drainZ - srcZ) * (drainZ - srcZ);
        double toDrainSq = (drainX - x) * (drainX - x) + (drainZ - z) * (drainZ - z);
        double drainProximity = (totalLenSq > 0.0) ? std::sqrt(toDrainSq / totalLenSq) : 1.0;
        w *= std::min(1.0, drainProximity / 0.4);
        if (w > 0.0) {
            double cfx = 0.0, cfz = 0.0;
            p->fractal.calculateContinuousFlow(x, z, cfx, cfz);
            double angle = std::atan2(cfz, cfx);
            cosSum += std::cos(angle) * w;
            sinSum += std::sin(angle) * w;
            totalWeight += w;
        }
    }
    if (totalWeight > 0.0) {
        double blended = std::atan2(sinSum, cosSum);
        fx = std::cos(blended);
        fz = std::sin(blended);
    } else {
        ctxEdge->fractal.calculateContinuousFlow(x, z, fx, fz);
    }
}

void HydrologyEngine::getRiverFlow(double x, int y, double z, double& outFx, double& outFz, bool useLandOceanCheck) const {
    outFx = 0.0; outFz = 0.0;
    if (!cfg_.enabled) return;
    double cx = x + 0.5, cz = z + 0.5;
    if (findLake(cx, cz, INT32_MIN, false, false) != nullptr) return;

    double mfx = 0.0, mfz = 0.0;
    bool foundM = false, pastLowlandMouth = false;
    ravineFlowVec(cx, y, cz, mfx, mfz, foundM);
    if (!foundM) {
        WatershedContext mCtx = evaluateWatershedContext(cx, cz, false);
        if (mCtx.bestPath && mCtx.distance < mCtx.width * 1.5) {
            auto data = mCtx.bestPath->getClosestNodeData(cx, cz);
            int top = std::max(data.y + 20, cb_.seaLevel);
            if (data.y <= cb_.seaLevel + 1) {
                int lip = rimLevel(*mCtx.bestPath, data.nodeIndex, cb_.seaLevel);
                if (lip != INT32_MIN) top = std::max(top, lip + 1);
            }
            if (y >= data.y - 20 && y <= top) {
                mfx = data.flowDirX; mfz = data.flowDirZ; foundM = true;
                pastLowlandMouth = mCtx.bestPath->hitMainRiver && data.beyondEnd && data.nodeIndex == 0
                    && (cx - data.x) * data.flowDirX + (cz - data.z) * data.flowDirZ > 0.0;
            }
        }
    }
    if (foundM) {
        if (y <= cb_.seaLevel + 1) {
            RiverContext parent = const_cast<MainRiversEngine&>(mainRivers_).findNearest(x, z);
            const RiverGraphEdge* parentEdge = parent.edge();
            if (parentEdge) {
                double into = flowWaterRadius(*parentEdge, x, z) - std::sqrt(parent.distSq);
                if (into > 0.0) {
                    double lfx = 0.0, lfz = 0.0;
                    lowlandFlow(x, y, z, lfx, lfz, useLandOceanCheck);
                    aroundRiverSplit(x, z, lfx, lfz);
                    if (into >= 6.0) { outFx = lfx; outFz = lfz; return; }
                    if (lfx * lfx + lfz * lfz < 1.0E-6) { outFx = mfx; outFz = mfz; return; }
                    double t = into / 6.0;
                    double s = t * t * (3.0 - 2.0 * t);
                    if (mfx * mfx + mfz * mfz < 1.0E-6) { outFx = lfx; outFz = lfz; return; }
                    double a = std::atan2(mfz, mfx);
                    double diff = std::atan2(lfz, lfx) - a;
                    while (diff < -DW_PI) diff += 2.0 * DW_PI;
                    while (diff >= DW_PI) diff -= 2.0 * DW_PI;
                    outFx = std::cos(a + diff * s); outFz = std::sin(a + diff * s);
                    return;
                }
                if (pastLowlandMouth) {
                    double on[2]{};
                    parentEdge->fractal.nearestPoint(x, z, on[0], on[1]);
                    double dfx = 0.0, dfz = 0.0;
                    parentEdge->fractal.calculateContinuousFlow(on[0], on[1], dfx, dfz);
                    double dx = on[0] + dfx * 12.0 - x, dz = on[1] + dfz * 12.0 - z;
                    double len = std::hypot(dx, dz);
                    if (len >= 1.0E-6) { outFx = dx / len; outFz = dz / len; return; }
                }
            }
        }
        outFx = mfx; outFz = mfz;
        return;
    }
    lowlandFlow(x, y, z, outFx, outFz, useLandOceanCheck);
    aroundRiverSplit(x, z, outFx, outFz);
}

bool HydrologyEngine::isMountainRiverWater(int x, int y, int z) const {
    double cx = x + 0.5, cz = z + 0.5;
    if (findLake(cx, cz, INT32_MIN, true, false) != nullptr) return false;
    double fx = 0.0, fz = 0.0; bool found = false;
    ravineFlowVec(cx, y, cz, fx, fz, found);
    if (found) return true;
    WatershedContext ctx = evaluateWatershedContext(cx, cz, true);
    if (!ctx.bestPath || ctx.distance >= ctx.width * 1.5) return false;
    auto data = ctx.bestPath->getClosestNodeData(cx, cz);
    return y >= data.y - 20 && y <= std::max(data.y + 20, cb_.seaLevel);
}

bool HydrologyEngine::inMountainRiverMiddle(double x, int y, double z) const {
    if (findLake(x, z, INT32_MIN, true, false) != nullptr) return false;
    double fx = 0.0, fz = 0.0; bool found = false;
    ravineFlowVec(x, y, z, fx, fz, found);
    if (found) return true;
    WatershedContext ctx = evaluateWatershedContext(x, z, false);
    if (!ctx.bestPath || ctx.distance >= std::max(ctx.width * 0.75, 2.9)) return false;
    auto data = ctx.bestPath->getClosestNodeData(x, z);
    return y >= data.y - 20 && y <= std::max(data.y + 20, cb_.seaLevel);
}

bool HydrologyEngine::nearMountainFall(int x, int y, int z) const {
    double cx = x + 0.5, cz = z + 0.5;
    double fx = 0.0, fz = 0.0; bool found = false;
    ravineFlowVec(cx, y, cz, fx, fz, found);
    if (found) return true;
    WatershedContext ctx = evaluateWatershedContext(cx, cz, true);
    const WatershedPath* p = ctx.bestPath;
    if (!p || ctx.bestNodeIndex < 0) return false;
    double reach = ctx.width + 6.0;
    if (ctx.distance >= reach || y < ctx.bestY - 20 || y > ctx.bestY + 2) return false;
    int lo = ctx.bestY, hi = ctx.bestY;
    double s = 0.0;
    for (int i = ctx.bestNodeIndex; i > 0 && s <= reach; --i) {
        s += std::hypot(p->segmentsX[i] - p->segmentsX[i - 1], p->segmentsZ[i] - p->segmentsZ[i - 1]);
        lo = std::min(lo, p->segmentsY[i - 1]);
        hi = std::max(hi, p->segmentsY[i - 1]);
    }
    s = 0.0;
    for (int i = ctx.bestNodeIndex; i + 1 < p->validNodeCount && s <= reach; ++i) {
        s += std::hypot(p->segmentsX[i + 1] - p->segmentsX[i], p->segmentsZ[i + 1] - p->segmentsZ[i]);
        lo = std::min(lo, p->segmentsY[i + 1]);
        hi = std::max(hi, p->segmentsY[i + 1]);
    }
    return hi > lo;
}

bool HydrologyEngine::inLowlandWater(double x, double z) const {
    if (!cfg_.enabled || !cfg_.dynamicNormalRivers()) return false;
    thread_local std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>> nearby;
    nearby.clear();
    const_cast<MainRiversEngine&>(mainRivers_).collectNearbyEdges(x, z, nearby);
    double on[2]{};
    for (const auto& pr : nearby) {
        const RiverGraphEdge& edge = (*pr.first)[pr.second];
        double d = std::sqrt(edge.fractal.nearestPoint(x, z, on[0], on[1]));
        if (d < flowWaterRadius(edge, on[0], on[1])) return true;
    }
    return false;
}

// ---------- Biome Overrides ----------
bool HydrologyEngine::getLowlandRiverBiomeOverride(int wx, int wy, int wz, bool& outFrozen) const {
    outFrozen = false;
    if (!cfg_.enabled || !cfg_.dynamicNormalRivers()) return false;
    const double worldY = (wy >> 2) * 4.0 + 2.0;
    if (worldY > 80.0) return false;
    const int qx = wx >> 2, qz = wz >> 2;

    struct QuartSlot {
        const HydrologyEngine* owner = nullptr;
        uint64_t epoch = 0;
        int qx = 0, qz = 0;
        bool valid = false;
        bool overrideRiver = false;
        bool frozen = false;
    };
    struct QuartTls {
        std::array<QuartSlot, 64> slots{};
        uint32_t next = 0;
    };
    thread_local QuartTls qTls;
    const uint64_t ep = resetEpoch_.load(std::memory_order_acquire);
    const uint32_t hashSlot = (uint32_t(qx * 73856093) ^ uint32_t(qz * 19349663)) & 63u;
    QuartSlot& cached = qTls.slots[hashSlot];
    if (cached.valid && cached.owner == this && cached.epoch == ep && cached.qx == qx && cached.qz == qz) {
        outFrozen = cached.frozen;
        return cached.overrideRiver;
    }

    const double worldX = qx * 4.0 + 2.0, worldZ = qz * 4.0 + 2.0;
    RiverContext info = const_cast<MainRiversEngine&>(mainRivers_).findNearest(worldX, worldZ);
    bool isOv = info.valid && info.normDistSq() < 0.25 && !riverSplitLand(worldX, worldZ);
    bool frz = false;
    if (isOv && cb_.biomeAt) {
        constexpr int R = 3;
        constexpr int cell = CoarseHeightCache::CELL_SIZE;
        int cold = 0;
        for (int k = 0; k < 5; ++k) {
            int nqx = qx + (k == 1 ? R : (k == 2 ? -R : 0));
            int nqz = qz + (k == 3 ? R : (k == 4 ? -R : 0));
            int ground = const_cast<BasinTileEngine&>(basin_).heights().sampleCell(
                floorDiv(nqx * 4 + 2, cell), floorDiv(nqz * 4 + 2, cell));
            BiomeMeta m = cb_.biomeAt(nqx * 4 + 2, ground + 2, nqz * 4 + 2);
            if (m.temperature < 0.15f && ++cold >= 2) {
                frz = true;
                break;
            }
        }
    }
    cached.owner = this;
    cached.epoch = ep;
    cached.qx = qx;
    cached.qz = qz;
    cached.overrideRiver = isOv;
    cached.frozen = frz;
    cached.valid = true;
    outFrozen = frz;
    return isOv;
}

bool HydrologyEngine::getMountainRiverBiomeOverride(int wx, int waterTopY, int wz, const BiomeMeta& hereMeta, bool& outFrozen) const {
    outFrozen = false;
    if (!cfg_.enabled || !cfg_.mountainRiverBiome || !cfg_.enableWatersheds || !cfg_.dynamicNormalRivers()) return false;
    if ((hereMeta.tags & (TAG_RIVER | TAG_OCEAN)) != 0) return false;
    if (!inMountainRiverMiddle(wx + 0.5, waterTopY, wz + 0.5)) return false;
    outFrozen = noises_.coldEnoughToSnow(wx, waterTopY, wz, hereMeta.temperature, (hereMeta.tags & TAG_FROZEN_MOD) != 0);
    return true;
}

// ---------- Pool Bodies & Clear (for inland depressions below sea level) ----------
bool HydrologyEngine::keepsStandingWater(const BiomeMeta& b) {
    if ((b.tags & (TAG_OCEAN | TAG_RIVER | TAG_BEACH | TAG_SWAMP)) != 0) return true;
    const std::string& p = b.name;
    return p.find("swamp") != std::string::npos || p.find("marsh") != std::string::npos || p.find("bog") != std::string::npos
        || p.find("fen") != std::string::npos || p.find("mangrove") != std::string::npos || p.find("wetland") != std::string::npos
        || p.find("lake") != std::string::npos || p.find("beach") != std::string::npos || p.find("shore") != std::string::npos
        || p.find("ocean") != std::string::npos || p.find("river") != std::string::npos;
}

bool HydrologyEngine::poolPoint(int cx, int cz, int seaLevel) const {
    return const_cast<BasinTileEngine&>(basin_).heights().sampleCell(cx, cz) - 1 < seaLevel - 1;
}

bool HydrologyEngine::poolClear(int cx, int cz) const {
    int64_t key = BasinTileEngine::packKey(cx, cz);
    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        auto it = poolClearCache_.find(key);
        if (it != poolClearCache_.end()) return it->second;
    }
    const int x = cx * CoarseHeightCache::CELL_SIZE, z = cz * CoarseHeightCache::CELL_SIZE;
    bool clear = true;
    if (cb_.isMouthBiomeAt) {
        for (int dz = -POOL_SEA_CLEAR; dz <= POOL_SEA_CLEAR && clear; dz += POOL_SEA_CLEAR / 2) {
            for (int dx = -POOL_SEA_CLEAR; dx <= POOL_SEA_CLEAR; dx += POOL_SEA_CLEAR / 2) {
                if (cb_.isMouthBiomeAt(x + dx, z + dz)) { clear = false; break; }
            }
        }
    }
    std::lock_guard<std::mutex> lk(cacheMutex_);
    if (poolClearCache_.size() >= 16384) poolClearCache_.clear();
    poolClearCache_[key] = clear;
    return clear;
}

bool HydrologyEngine::poolBody(int cx, int cz, int seaLevel) const {
    int64_t key = BasinTileEngine::packKey(cx, cz);
    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        auto it = poolBodyCache_.find(key);
        if (it != poolBodyCache_.end()) return it->second;
    }
    std::unordered_set<int64_t> body;
    std::vector<int64_t> open;
    body.insert(key);
    open.push_back(key);
    bool fill = true;
    for (size_t head = 0; head < open.size() && fill; ++head) {
        int64_t pt = open[head];
        int px = int(int32_t(uint32_t(pt & 0xffffffffu)));
        int pz = int(int32_t(uint32_t((pt >> 32) & 0xffffffffu)));
        int ground = const_cast<BasinTileEngine&>(basin_).heights().sampleCell(px, pz);
        BiomeMeta pb = cb_.biomeAt ? cb_.biomeAt(px * CoarseHeightCache::CELL_SIZE, seaLevel, pz * CoarseHeightCache::CELL_SIZE) : BiomeMeta{};
        if (seaLevel - ground > POOL_FILL_DEPTH || keepsStandingWater(pb)) {
            fill = false;
            break;
        }
        for (int dz = -1; dz <= 1 && fill; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx | dz) == 0 || !poolPoint(px + dx, pz + dz, seaLevel)) continue;
                int64_t next = BasinTileEngine::packKey(px + dx, pz + dz);
                if (!body.insert(next).second) continue;
                if (int(body.size()) > POOL_MAX_CELLS) { fill = false; break; }
                open.push_back(next);
            }
        }
    }
    std::lock_guard<std::mutex> lk(cacheMutex_);
    if (poolBodyCache_.size() >= 65536) poolBodyCache_.clear();
    for (int64_t p : body) poolBodyCache_[p] = fill;
    return fill;
}

bool HydrologyEngine::poolFilled(int x, int z, int top, const BiomeMeta& biome) const {
    if (!cfg_.enabled || !cfg_.enableWatersheds) return false;
    const int seaLevel = cb_.seaLevel;
    const int cell = CoarseHeightCache::CELL_SIZE;
    const int cx = floorDiv(x + cell / 2, cell), cz = floorDiv(z + cell / 2, cell);
    if (poolPoint(cx, cz, seaLevel)) return poolBody(cx, cz, seaLevel);
    bool nearVal = false;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx | dz) == 0 || !poolPoint(cx + dx, cz + dz, seaLevel)) continue;
            if (!poolBody(cx + dx, cz + dz, seaLevel)) return false;
            nearVal = true;
        }
    }
    if (nearVal) return true;
    return (seaLevel - 1 - top <= POOL_FILL_DEPTH) && !keepsStandingWater(biome) && poolClear(cx, cz);
}

// ---------- GorgeDress / ShoreSkin / Boulders / WaterPlants / Freezing ----------
DwBlockRef HydrologyEngine::dressGorgeBlock(int wx, int wz, int topY, int waterY, bool steep, int groundY, const BiomeMeta& biome) const {
    static const DwBlock kBadlandsBands[] = {
        DwBlock::Terracotta, DwBlock::Terracotta, DwBlock::Terracotta,
        DwBlock::OrangeTerracotta, DwBlock::OrangeTerracotta,
        DwBlock::RedTerracotta, DwBlock::YellowTerracotta, DwBlock::BrownTerracotta,
        DwBlock::WhiteTerracotta, DwBlock::LightGrayTerracotta
    };
    const std::string bs = stripNamespace(biome.name);
    if (bs == "stony_peaks" || bs == "jagged_peaks" || bs == "frozen_peaks") {
        return {DwBlock::Keep, ""};
    }
    bool badlands = (biome.tags & TAG_BADLANDS) != 0;
    bool desert = !badlands && (bs == "desert" || (biome.tags & TAG_DESERT) != 0);
    if (badlands || desert) {
        if (desert ? !steep : (topY <= waterY + 2)) {
            return desert ? DwBlockRef{DwBlock::Sand, "minecraft:sand"} : DwBlockRef{DwBlock::RedSand, "minecraft:red_sand"};
        }
        int s = gorgeStratum(wx, wz, topY, waterY);
        uint64_t h = hydrology_const::gorgeHash(seed_, s, 0x6A09E667F3BCC909ull);
        if (badlands) {
            DwBlock b = kBadlandsBands[h % 10];
            return {b, dwBlockName(b)};
        }
        DwBlock b = (h % 3ull == 0ull) ? DwBlock::SmoothSandstone : DwBlock::Sandstone;
        return {b, dwBlockName(b)};
    }
    if (bs == "mushroom_fields") return {DwBlock::Mycelium, "minecraft:mycelium"};
    if (bs == "snowy_slopes" || bs == "grove") {
        if (steep) return {DwBlock::Keep, ""};
        return {DwBlock::SnowBlock, "minecraft:snow_block"};
    }
    DwBlockRef natural = cb_.naturalTopBlockAt ? cb_.naturalTopBlockAt(wx, groundY, wz) : DwBlockRef{DwBlock::GrassBlock, "minecraft:grass_block"};
    if (natural.isKeep() || natural.isAir() || natural.isWater() || natural.id == DwBlock::Dirt || natural.id == DwBlock::GrassBlock || natural.id == DwBlock::Podzol || natural.id == DwBlock::CoarseDirt || natural.id == DwBlock::RootedDirt) {
        return {DwBlock::GrassBlock, "minecraft:grass_block"};
    }
    if (natural.isRock()) return {DwBlock::Keep, ""};
    return natural;
}

DwBlockRef HydrologyEngine::applyShoreSkinBlock(int wx, int wz, int sy, int originalY, const RiverCarving& c, bool wet, bool stepNear, int exposure, const BiomeMeta& landBiome) const {
    if (!cfg_.riverbankPalettes) return {DwBlock::Keep, ""};
    int ws = (c.modifies && c.skinLevel != INT32_MIN) ? c.skinLevel : (!c.modifies ? c.waterLevel : INT32_MIN);
    if (ws == INT32_MIN) return {DwBlock::Keep, ""};
    bool cut = sy < originalY;
    bool built = c.isWatershedWall && sy > originalY;
    bool flat = c.deltaFlat && !wet && sy <= ws + 3;
    if (!wet && !cut && !built && !flat) return {DwBlock::Keep, ""};
    if (flat) {
        double n = Patches::smooth(wx, wz, 12, 0x6F);
        if (!(c.deltaShore <= 3.5 * (0.4 + 1.2 * n) || n > 0.8)) return {DwBlock::Keep, ""};
    }

    const RiverbankPalette* style = paletteForBiome(landBiome);
    if (!style || !style->enabled) return {DwBlock::Keep, ""};

    auto pickPlace = [&](int place) -> DwBlockRef {
        const RiverbankPalette::Mix* mix = style->place(place);
        if (!mix) return {DwBlock::Keep, ""};
        int spotSize = style->patchSize;
        if (place == RiverbankPalette::RAPIDS) spotSize = std::max(2, (style->patchSize * 2 + 2) / 3);
        else if (place == RiverbankPalette::SILT) spotSize = style->patchSize + style->patchSize / 2;
        else if (place == RiverbankPalette::FLATS) spotSize = 2 * style->patchSize;
        int salt = 0x51A7 + place * 0x9E3;
        return mix->pick(wx, wz, spotSize, salt);
    };

    DwBlockRef skin{DwBlock::Keep, ""};
    if (flat) {
        skin = pickPlace(RiverbankPalette::FLATS);
    } else if (wet) {
        if (ws - sy <= 1 && !c.pointBar) skin = pickPlace(RiverbankPalette::SHORE);
        if (skin.isKeep()) {
            double run = 0.3;
            if (stepNear) run = 1.0;
            else if (c.isLake) {
                int depth = ws - sy;
                run = depth <= 1 ? 0.35 : 0.08;
            } else if (c.isMountainRiver) {
                double t = c.channelWidth > 0.0 ? std::min(1.0, c.distanceFromCenter / c.channelWidth) : 1.0;
                double raw = 0.12 + 0.38 * (1.0 - t * t);
                double slack = (c.channelWidth >= 10.0) ? raw * 0.7 : raw;
                int depth = ws - sy;
                double srun = depth >= 4 ? 0.55 : 0.8;
                run = slack + (srun - slack) * std::min(1.0, c.channelSlope / 0.08);
            } else if (c.isMainRiverWater) {
                if (c.deltaWater) run = 0.1;
                else {
                    double t = c.waterRadius > 0.0 ? std::min(1.0, c.distanceFromCenter / c.waterRadius) : 1.0;
                    double raw = 0.12 + 0.38 * (1.0 - t * t);
                    run = (c.waterRadius >= 10.0) ? raw * 0.7 : raw;
                }
            }
            run *= (c.pointBar ? 0.5 : 1.0);
            double sortDraw = Patches::blob(wx, wz, style->patchSize + style->patchSize / 2, 0x50F7);
            skin = pickPlace(sortDraw < run ? RiverbankPalette::RAPIDS : RiverbankPalette::SILT);
        }
    } else {
        int h = sy - ws;
        if (c.pointBar && h >= -1) skin = pickPlace(RiverbankPalette::BARS);
        else if (h >= -1) {
            int waver = int(dwRound((Patches::smooth(wx, wz, 7, 0x5E0) - 0.5) * 2.6));
            skin = pickPlace(h <= style->shoreHeight + waver ? RiverbankPalette::SHORE : RiverbankPalette::SLOPE);
        }
    }
    (void)exposure;
    return skin;
}

void HydrologyEngine::applyBouldersColumn(int wx, int wz, int bedY, int waterLevel, const RiverCarving& c, const BiomeMeta& landBiome, bool fallNear, int& outCrownY, DwBlockRef& outBlock) const {
    outCrownY = INT32_MIN;
    if (!cfg_.riverBoulders || !c.modifies || c.isWatershedWall || c.isLake || fallNear) return;
    if (waterLevel <= bedY) return;
    bool lowland = c.isMainRiverWater && !c.deltaWater;
    double fast = 0.0;
    if (!lowland && c.isMountainRiver && c.channelWidth >= 2.5 && c.distanceFromCenter <= c.channelWidth - 1.0) {
        fast = mthClamp((c.channelSlope - 0.04) / (0.12 - 0.04), 0.0, 1.0);
    }
    if (!lowland && fast <= 0.0) return;
    if (waterLevel - bedY > (lowland ? 4 : 3)) return;

    constexpr int SALT = 0xB01D;
    auto rise = [&](int sx, int sz) -> int {
        double u = Patches::hash01(sx, SALT + 2, sz);
        return u < 0.6 ? 1 : (u < 0.9 ? 0 : -1);
    };

    int top = INT32_MIN;
    if (lowland) {
        int reach = int(std::ceil(3.4 * 1.12));
        for (int dz = -reach; dz <= reach; ++dz) {
            for (int dx = -reach; dx <= reach; ++dx) {
                int sx = wx - dx, sz = wz - dz;
                if (Patches::hash01(sx, SALT + 10, sz) >= 0.001) continue;
                double r = mthLerp(Patches::hash01(sx, SALT + 11, sz), 2.4, 3.4);
                double edge = r * (1.0 + 0.12 * std::sin(3.0 * std::atan2(double(dz), double(dx)) + Patches::hash01(sx, SALT + 12, sz) * DW_PI * 2.0));
                double d = std::sqrt(double(dx * dx + dz * dz)) / edge;
                if (d > 1.0) continue;
                double sink = 0.9 * (1.0 + Patches::hash01(sx, SALT + 13, sz));
                int ball = int(dwRound(waterLevel - sink + r * std::sqrt(1.0 - d * d)));
                top = std::max(top, std::min(ball, bedY + 7));
            }
        }
    } else {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                int sx = wx - dx, sz = wz - dz;
                if (Patches::hash01(sx, SALT, sz) >= 0.001 * fast) continue;
                double shape = Patches::hash01(sx, SALT + 1, sz);
                int rVal = rise(sx, sz);
                bool own = (dx == 0 && dz == 0);
                int here = INT32_MIN, tall = 3;
                if (shape < 0.55) {
                    if (own && waterLevel - bedY <= 2) here = rVal;
                } else if (shape < 0.85) {
                    if (dx >= 0 && dz >= 0) {
                        bool far = (dx == 1 && dz == 1);
                        here = far ? rVal - 1 : rVal;
                        tall = far ? 3 : 4;
                    }
                } else if (own) {
                    here = rVal; tall = 4;
                } else if (dx == 0 || dz == 0) {
                    here = rVal - 1;
                }
                if (here != INT32_MIN) top = std::max(top, std::min(waterLevel + here, bedY + tall));
            }
        }
    }
    if (top <= bedY) return;
    outCrownY = top;

    double u = Patches::hash01(wx + top * 7919, SALT + 3, wz);
    if ((landBiome.tags & TAG_BADLANDS) != 0) {
        outBlock = {DwBlock::Terracotta, "minecraft:terracotta"};
    } else if ((landBiome.tags & TAG_DESERT) != 0) {
        DwBlock b = u < 0.6 ? DwBlock::Sandstone : (u < 0.8 ? DwBlock::Stone : DwBlock::Andesite);
        outBlock = {b, dwBlockName(b)};
    } else if (noises_.coldEnoughToSnow(wx, waterLevel, wz, landBiome.temperature, (landBiome.tags & TAG_FROZEN_MOD) != 0)
               || (landBiome.tags & (TAG_SNOWY | TAG_ICY)) != 0) {
        DwBlock b = u < 0.45 ? DwBlock::Stone : (u < 0.8 ? DwBlock::Andesite : DwBlock::Cobblestone);
        outBlock = {b, dwBlockName(b)};
    } else if ((landBiome.tags & (TAG_JUNGLE | TAG_SWAMP | TAG_LUSH)) != 0) {
        DwBlock b = u < 0.8 ? DwBlock::MossyCobblestone : (u < 0.7 ? DwBlock::Stone : (u < 0.85 ? DwBlock::Andesite : DwBlock::Cobblestone));
        outBlock = {b, dwBlockName(b)};
    } else if ((landBiome.tags & TAG_NO_PRECIP) != 0 || (landBiome.tags & (TAG_SAVANNA | TAG_DRY_OVERWORLD)) != 0) {
        DwBlock b = u < 0.4 ? DwBlock::Stone : (u < 0.75 ? DwBlock::Andesite : DwBlock::Granite);
        outBlock = {b, dwBlockName(b)};
    } else {
        DwBlock b = u < 0.6 ? DwBlock::MossyCobblestone : (u < 0.65 ? DwBlock::Stone : (u < 0.88 ? DwBlock::Andesite : DwBlock::Cobblestone));
        outBlock = {b, dwBlockName(b)};
    }
}

void HydrologyEngine::applyWaterPlantsColumn(int wx, int wz, int bedY, int waterLevel, bool wet, const RiverCarving& c, const BiomeMeta& landBiome, const BiomeMeta& waterBiome, bool stepNear, const DwBlockRef& groundBlock, int& outTopY, DwBlockRef& outBlock) const {
    outTopY = INT32_MIN;
    if (!cfg_.waterPlants) return;
    int kind = -1;
    if (c.isLake) kind = WaterPlantSet::LAKE;
    else if (c.isMountainRiver || c.isWatershed) kind = WaterPlantSet::MOUNTAIN;
    else if (c.isMainRiverWater) kind = WaterPlantSet::LOWLAND;
    if (kind < 0) return;

    const WaterPlantSet* wset = plantSetForBiome(landBiome);
    if (!wset || !wset->enabled) return;

    auto calm = [&]() -> bool {
        if (c.isLake) return true;
        if (c.isMountainRiver) return c.channelSlope <= 0.02 && c.channelWidth > 0.0 && c.distanceFromCenter >= 0.5 * c.channelWidth;
        if (c.isMainRiverWater) return c.deltaWater || (c.waterRadius > 0.0 && c.distanceFromCenter >= 0.7 * c.waterRadius);
        return false;
    };
    auto drawn = [&](const WaterPlant& p, int x, int z) -> bool {
        if (p.patchSize > 0 && Patches::blob(x, z, p.patchSize, p.salt) < 1.0 - p.patchShare) return false;
        double own = Patches::hash01(x, p.salt + 1, z);
        if (own >= p.chance) return false;
        if (p.alone) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx != 0 || dz != 0) && Patches::hash01(x + dx, p.salt + 1, z + dz) < std::min(own, p.chance))
                        return false;
                }
            }
        }
        return true;
    };

    const bool isFrozenTop = noises_.coldEnoughToSnow(wx, waterLevel + 1, wz, waterBiome.temperature, (waterBiome.tags & TAG_FROZEN_MOD) != 0);
    const int depth = wet ? (waterLevel - bedY) : 0;

    if (wet && !c.isWatershedWall && depth >= 1) {
        // Check SURFACE plants first for visible surface overlay (lily_pad), then BED plants
        if (!stepNear && !isFrozenTop) {
            for (const auto& p : wset->kinds[kind]) {
                if (p.at != WaterPlant::At::Surface) continue;
                if (p.calm && !calm()) continue;
                if (depth < p.minDepth || (p.maxDepth < 64 && depth > p.maxDepth)) continue;
                if (!drawn(p, wx, wz)) continue;
                outTopY = waterLevel + 1;
                outBlock = p.block;
                return;
            }
        }
        for (const auto& p : wset->kinds[kind]) {
            if (p.at != WaterPlant::At::Bed) continue;
            if (p.calm && !calm()) continue;
            if (depth < p.minDepth || (p.maxDepth < 64 && depth > p.maxDepth)) continue;
            if (!drawn(p, wx, wz)) continue;
            int h = 1;
            if (p.isKelp()) {
                double u = Patches::hash01(wz, p.salt + 2, wx);
                h = std::max(3, depth - 1 - int(u * u * (depth - 3)));
            } else if (p.isDouble()) {
                if (depth < 2) continue;
                h = 2;
            } else {
                h = std::min(p.minHeight + int(Patches::hash01(wz, p.salt + 2, wx) * (p.maxHeight - p.minHeight + 1)), depth);
            }
            outTopY = bedY + h;
            outBlock = p.block;
            return;
        }
    } else if (!wet && !isFrozenTop && bedY == waterLevel && (groundBlock.isDirtLike() || groundBlock.isSandLike())) {
        // SHORE plants (sugar_cane etc.) — simplified check matching calm bank adjacent to still water
        for (const auto& p : wset->kinds[kind]) {
            if (p.at != WaterPlant::At::Shore) continue;
            if (!drawn(p, wx, wz)) continue;
            int h = p.minHeight + int(Patches::hash01(wz, p.salt + 2, wx) * (p.maxHeight - p.minHeight + 1));
            outTopY = bedY + h;
            outBlock = p.block;
            return;
        }
    }
}

ColumnOutcome HydrologyEngine::evaluateColumn(int wx, int wz, int originalGroundY) const {
    ColumnOutcome out;
    out.finalSolidY = originalGroundY;
    if (!cfg_.enabled) return out;

    const int seaLevel = cb_.seaLevel;
    BiomeMeta colBiome = cb_.biomeAt ? cb_.biomeAt(wx, originalGroundY, wz) : BiomeMeta{};

    // Ocean exclusion matches NoiseChunkGeneratorMixin line 203
    if ((colBiome.tags & TAG_OCEAN) != 0 && originalGroundY < seaLevel - 4) {
        return out;
    }

    int preCarveY = originalGroundY;
    if (preCarveY < seaLevel - 1 && poolFilled(wx, wz, preCarveY, colBiome)) {
        preCarveY = seaLevel - 1;
    }

    RiverCarving c = calculateRiverCarving(double(wx), double(wz), preCarveY, seaLevel);
    out.carving = c;

    int targetY = c.targetY;
    int solidY = preCarveY;
    bool wet = false;

    if (c.modifies) {
        if (targetY > preCarveY + 4 && !c.isWatershed) {
            solidY = preCarveY;
        } else {
            solidY = (!c.isWatershed && targetY > preCarveY) ? preCarveY : targetY;
        }
        if (solidY <= c.waterLevel && !c.isWatershedWall) {
            wet = true;
            out.waterTopY = c.waterLevel;
        }
    }
    out.finalSolidY = solidY;

    // Land & water biomes (lazy landBiome avoids redundant biomeAt lookup on unmodified columns)
    BiomeMeta landBiomeVal{};
    bool hasLandBiome = false;
    auto getLandBiome = [&]() -> const BiomeMeta& {
        if (!hasLandBiome) {
            landBiomeVal = cb_.biomeAt ? cb_.biomeAt(wx, std::max(solidY, c.skinLevel), wz) : colBiome;
            hasLandBiome = true;
        }
        return landBiomeVal;
    };
    BiomeMeta waterBiome = colBiome;
    bool lowFrozen = false;
    if (getLowlandRiverBiomeOverride(wx, std::max(seaLevel - 1, solidY), wz, lowFrozen)) {
        out.overrideRiverBiome = true;
        out.riverBiomeIsFrozen = lowFrozen;
        waterBiome.name = lowFrozen ? "minecraft:frozen_river" : "minecraft:river";
        waterBiome.temperature = lowFrozen ? 0.0f : 0.5f;
        waterBiome.tags |= TAG_RIVER;
    } else if (wet) {
        bool mtnFrozen = false;
        if (getMountainRiverBiomeOverride(wx, out.waterTopY, wz, getLandBiome(), mtnFrozen)) {
            out.overrideRiverBiome = true;
            out.riverBiomeIsFrozen = mtnFrozen;
            waterBiome.name = mtnFrozen ? "minecraft:frozen_river" : "minecraft:river";
            waterBiome.temperature = mtnFrozen ? 0.0f : 0.5f;
            waterBiome.tags |= TAG_RIVER;
        }
    }

    // Check freezing (BiomeFreezeMixin)
    if (wet) {
        bool vanillaShouldFreeze = noises_.coldEnoughToSnow(wx, out.waterTopY, wz, waterBiome.temperature, (waterBiome.tags & TAG_FROZEN_MOD) != 0);
        bool isWideWater = ((waterBiome.tags & (TAG_OCEAN | TAG_BEACH)) != 0)
            || ((waterBiome.tags & TAG_RIVER) != 0 && (!cfg_.dynamicNormalRivers() || inLowlandWater(wx + 0.5, wz + 0.5)));
        if (cfg_.mountainRiverIce == MountainRiverIce::FULL) {
            out.isIceSurface = vanillaShouldFreeze;
        } else if (vanillaShouldFreeze) {
            if (isWideWater) {
                out.isIceSurface = true;
            } else {
                bool keepOpen = (cfg_.mountainRiverIce == MountainRiverIce::OPEN && isMountainRiverWater(wx, out.waterTopY, wz))
                             || nearMountainFall(wx, out.waterTopY, wz);
                out.isIceSurface = !keepOpen;
            }
        }
    }

    // Base surface dressing: gorgeDress -> river bed stone -> ShoreSkin -> Boulders -> WaterPlants
    DwBlockRef surfBlock{DwBlock::Keep, ""};
    if (c.modifies) {
        if (wet && solidY < preCarveY) {
            surfBlock = {DwBlock::Stone, "minecraft:stone"};
        } else if (c.isWatershedWall && solidY < preCarveY && c.waterLevel > solidY + 3) {
            surfBlock = {DwBlock::Gravel, "minecraft:gravel"};
        }
        const BiomeMeta& landBiome = getLandBiome();
        if ((c.gorgeWall || c.dressCut) && solidY >= c.waterLevel && solidY < preCarveY) {
            DwBlockRef dressed = dressGorgeBlock(wx, wz, solidY, c.waterLevel, c.gorgeSteep, preCarveY, landBiome);
            if (!dressed.isKeep()) surfBlock = dressed;
        }
        DwBlockRef skin = applyShoreSkinBlock(wx, wz, solidY, preCarveY, c, wet, false, 2, landBiome);
        if (!skin.isKeep()) surfBlock = skin;
    }
    out.surfaceBlock = surfBlock;

    if (wet) {
        const BiomeMeta& landBiome = getLandBiome();
        DwBlockRef bBlock{DwBlock::Keep, ""};
        int crownY = INT32_MIN;
        applyBouldersColumn(wx, wz, solidY, c.waterLevel, c, landBiome, false, crownY, bBlock);
        if (crownY > solidY) {
            out.aboveWaterTopY = crownY;
            out.aboveWaterBlock = bBlock;
        } else {
            DwBlockRef pBlock{DwBlock::Keep, ""};
            int pTopY = INT32_MIN;
            applyWaterPlantsColumn(wx, wz, solidY, c.waterLevel, wet, c, landBiome, waterBiome, false, surfBlock, pTopY, pBlock);
            if (pTopY > solidY) {
                out.aboveWaterTopY = pTopY;
                out.aboveWaterBlock = pBlock;
            }
        }
        getRiverFlow(wx, out.waterTopY, wz, out.flowX, out.flowZ, true);
    }
    return out;
}

} // namespace dw
