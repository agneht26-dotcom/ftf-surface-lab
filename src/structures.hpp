#pragma once
#include "mc_random.hpp"
#include "biome_census.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mcstruct {

enum class SpreadType { Linear, Triangular };
enum class FreqMethod { Default, Legacy1, Legacy2, Legacy3 };
enum class PlacementType { RandomSpread, ConcentricRings };

struct StructureVariant {
    std::string id;
    std::unordered_set<uint32_t> biomes;
};

struct StructureSetDef {
    std::string id;
    std::string shortId;
    std::string name;
    uint32_t colorRgb = 0xFFFFFFu; // 0xRRGGBB
    PlacementType placement = PlacementType::RandomSpread;
    int spacing = 32;
    int separation = 8;
    int salt = 0;
    SpreadType spreadType = SpreadType::Linear;
    FreqMethod freqMethod = FreqMethod::Default;
    float frequency = 1.0f;
    int locateOffsetX = 0;
    int locateOffsetZ = 0;
    int exclusionSetIndex = -1;
    int exclusionChunkCount = 0;
    // ConcentricRings (strongholds)
    int ringDistance = 32;
    int ringSpread = 3;
    int ringCount = 128;
    std::unordered_set<uint32_t> preferredBiomes;
    std::vector<StructureVariant> variants;
    bool enabled = true;
};

struct StructurePin {
    int setIndex = 0;
    int chunkX = 0;
    int chunkZ = 0;
    float worldX = 0.0f;
    float worldZ = 0.0f;
    uint32_t biomeId = 0;
    std::string variantName;
};

inline int floorDiv(int a, int b) {
    int q = a / b;
    int r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) --q;
    return q;
}

inline uint32_t parseHexColor(const std::string& s) {
    if (s.size() == 7 && s[0] == '#') {
        return static_cast<uint32_t>(std::stoul(s.substr(1), nullptr, 16));
    }
    return 0xFFFFFFu;
}

inline std::vector<StructureSetDef> loadStructureSets(
    const std::filesystem::path& structuresJsonPath,
    const nlohmann::json& biomeLegend)
{
    std::unordered_map<std::string, std::vector<uint32_t>> biomeNameToIds;
    if (biomeLegend.contains("biomes") && biomeLegend["biomes"].is_array()) {
        for (const auto& b : biomeLegend["biomes"]) {
            if (b.contains("name") && b.contains("id")) {
                auto& list = biomeNameToIds[b["name"].get<std::string>()];
                list.push_back(b["id"].get<uint32_t>());
                if (b.contains("canonical_id")) list.push_back(b["canonical_id"].get<uint32_t>());
            }
        }
    }

    std::ifstream in(structuresJsonPath);
    if (!in) return {};
    nlohmann::json root;
    in >> root;
    std::vector<StructureSetDef> defs;
    if (!root.contains("structure_sets") || !root["structure_sets"].is_array()) return defs;

    std::unordered_map<std::string, int> idToIndex;
    std::vector<std::pair<std::string, int>> pendingExclusions;

    for (const auto& entry : root["structure_sets"]) {
        StructureSetDef def;
        def.id = entry.value("id", "");
        def.shortId = entry.value("short_id", "");
        def.name = entry.value("name", def.shortId);
        def.colorRgb = parseHexColor(entry.value("color", "#FFFFFF"));
        def.enabled = (def.shortId == "villages");

        const auto& pl = entry["placement"];
        std::string ptype = pl.value("type", "minecraft:random_spread");
        if (ptype == "minecraft:concentric_rings") {
            def.placement = PlacementType::ConcentricRings;
            def.ringDistance = pl.value("distance", 32);
            def.ringSpread = pl.value("spread", 3);
            def.ringCount = pl.value("count", 128);
            def.salt = pl.value("salt", 0);
            if (pl.contains("preferred_biomes_list") && pl["preferred_biomes_list"].is_array()) {
                for (const auto& bn : pl["preferred_biomes_list"]) {
                    auto it = biomeNameToIds.find(bn.get<std::string>());
                    if (it != biomeNameToIds.end()) for (uint32_t bid : it->second) def.preferredBiomes.insert(bid);
                }
            }
        } else {
            def.placement = PlacementType::RandomSpread;
            def.spacing = pl.value("spacing", 32);
            def.separation = pl.value("separation", 8);
            def.salt = pl.value("salt", 0);
            std::string st = pl.value("spread_type", "linear");
            def.spreadType = (st == "triangular") ? SpreadType::Triangular : SpreadType::Linear;
            std::string fm = pl.value("frequency_reduction_method", "default");
            if (fm == "legacy_type_1") def.freqMethod = FreqMethod::Legacy1;
            else if (fm == "legacy_type_2") def.freqMethod = FreqMethod::Legacy2;
            else if (fm == "legacy_type_3") def.freqMethod = FreqMethod::Legacy3;
            else def.freqMethod = FreqMethod::Default;
            def.frequency = pl.value("frequency", 1.0f);
            if (pl.contains("locate_offset") && pl["locate_offset"].is_array() && pl["locate_offset"].size() >= 3) {
                def.locateOffsetX = pl["locate_offset"][0].get<int>();
                def.locateOffsetZ = pl["locate_offset"][2].get<int>();
            }
            if (pl.contains("exclusion_zone") && pl["exclusion_zone"].is_object()) {
                def.exclusionChunkCount = pl["exclusion_zone"].value("chunk_count", 0);
                pendingExclusions.emplace_back(pl["exclusion_zone"].value("other_set", ""), int(defs.size()));
            }
        }

        if (entry.contains("structures") && entry["structures"].is_array()) {
            for (const auto& st : entry["structures"]) {
                StructureVariant var;
                var.id = st.value("id", "");
                if (st.contains("biomes") && st["biomes"].is_array()) {
                    for (const auto& bn : st["biomes"]) {
                        auto it = biomeNameToIds.find(bn.get<std::string>());
                        if (it != biomeNameToIds.end()) for (uint32_t bid : it->second) var.biomes.insert(bid);
                    }
                }
                // Also allow Regions Unexplored biomes that share equivalent climate/terrain types.
                if (biomeLegend.contains("biomes") && biomeLegend["biomes"].is_array()) {
                    auto hasAny = [](const std::string& n, std::initializer_list<const char*> keys) {
                        for (const char* k : keys) if (n.find(k) != std::string::npos) return true;
                        return false;
                    };
                    for (const auto& b : biomeLegend["biomes"]) {
                        if (!b.contains("name") || !b.contains("id")) continue;
                        std::string bname = b["name"].get<std::string>();
                        if (bname.rfind("minecraft:", 0) == 0) continue;
                        if (bname.find("cave/") != std::string::npos || bname.find("_caves") != std::string::npos || bname.find("ocean") != std::string::npos) continue;
                        uint32_t bid = b["id"].get<uint32_t>();
                        uint32_t cid = b.contains("canonical_id") ? b["canonical_id"].get<uint32_t>() : bid;
                        bool match = false;
                        if (var.id == "minecraft:village_plains" || def.shortId == "pillager_outposts") {
                            match = hasAny(bname, {"plains", "meadow", "prairie", "grassland", "field", "pasture", "lavender", "clover", "shrubland", "steppe", "orchard", "bloom", "valley", "highland", "lowland", "clearing", "glade", "greens", "patch", "garden", "woodland", "deciduous", "maple", "aspen", "birch", "willow", "wisteria", "magnolia", "sakura", "golden_wilds", "floral", "heath", "moor", "shield", "alpha_islands", "skylands_spring", "skylands_summer", "skylands_autumn", "mauve", "temperate_forest", "subtropical"});
                        } else if (var.id == "minecraft:village_desert" || def.shortId == "desert_pyramids") {
                            match = hasAny(bname, {"desert", "dune", "oasis", "outback", "arid", "sands", "dryland", "dry_bushland", "scrubland", "xeric"});
                        } else if (var.id == "minecraft:village_savanna") {
                            match = hasAny(bname, {"savanna", "baobab", "chaparral", "brushland", "mediterranean", "eucalyptus", "tropical_woods", "sparse_tropical"});
                        } else if (var.id == "minecraft:village_snowy" || def.shortId == "igloos") {
                            match = hasAny(bname, {"snowy", "tundra", "glacial", "frozen", "frost", "ice_spikes", "wintry", "snowblossom", "icy_heights", "sleeted", "siberian", "skylands_winter", "alpha_islands_winter"});
                        } else if (var.id == "minecraft:village_taiga") {
                            match = hasAny(bname, {"taiga", "pine", "spruce", "redwood", "boreal", "conifer", "fir", "cedar", "sugi", "larch", "moonlight_grove", "temperate_grove", "cloud_forest"});
                        } else if (def.shortId == "jungle_temples") {
                            match = hasAny(bname, {"jungle", "rainforest", "tropics", "bamboo"});
                        } else if (def.shortId == "swamp_huts") {
                            match = hasAny(bname, {"swamp", "marsh", "bog", "fen", "bayou", "wetland", "mire"});
                        }
                        if (match) {
                            var.biomes.insert(bid);
                            var.biomes.insert(cid);
                        }
                    }
                }
                def.variants.push_back(std::move(var));
            }
        }

        idToIndex[def.id] = int(defs.size());
        defs.push_back(std::move(def));
    }

    for (const auto& [otherId, idx] : pendingExclusions) {
        auto it = idToIndex.find(otherId);
        if (it != idToIndex.end()) defs[idx].exclusionSetIndex = it->second;
    }
    return defs;
}

inline int64_t largeFeatureWithSaltSeed(int64_t levelSeed, int rx, int rz, int salt) {
    return int64_t(rx) * 341873128712LL + int64_t(rz) * 132897987541LL + levelSeed + int64_t(salt);
}

inline std::pair<int, int> randomSpreadCandidate(const StructureSetDef& set, int64_t levelSeed, int rx, int rz) {
    mc::Legacy rng(largeFeatureWithSaltSeed(levelSeed, rx, rz, set.salt));
    int bound = set.spacing - set.separation;
    int ox = 0, oz = 0;
    if (bound > 0) {
        if (set.spreadType == SpreadType::Triangular) {
            ox = (rng.nextInt(bound) + rng.nextInt(bound)) / 2;
            oz = (rng.nextInt(bound) + rng.nextInt(bound)) / 2;
        } else {
            ox = rng.nextInt(bound);
            oz = rng.nextInt(bound);
        }
    }
    return {rx * set.spacing + ox, rz * set.spacing + oz};
}

inline bool passesFrequency(const StructureSetDef& set, int64_t levelSeed, int cx, int cz) {
    if (set.frequency >= 1.0f) return true;
    switch (set.freqMethod) {
        case FreqMethod::Default: {
            mc::Legacy rng(largeFeatureWithSaltSeed(levelSeed, set.salt, cx, cz));
            return rng.nextFloat() < set.frequency;
        }
        case FreqMethod::Legacy1: {
            int rx = cx >> 4;
            int rz = cz >> 4;
            mc::Legacy rng(int64_t(rx ^ (rz << 4)) ^ levelSeed);
            rng.nextInt();
            int inv = int(1.0f / set.frequency);
            return inv > 0 && rng.nextInt(inv) == 0;
        }
        case FreqMethod::Legacy2: {
            mc::Legacy rng(largeFeatureWithSaltSeed(levelSeed, cx, cz, 10387320));
            return rng.nextFloat() < set.frequency;
        }
        case FreqMethod::Legacy3: {
            mc::Legacy rng(levelSeed);
            int64_t a = rng.nextLong();
            int64_t b = rng.nextLong();
            mc::Legacy chunkRng(int64_t(cx) * a ^ int64_t(cz) * b ^ levelSeed);
            return chunkRng.nextDouble() < double(set.frequency);
        }
    }
    return true;
}

inline std::vector<std::pair<int, int>> computeStrongholdRingPositions(const StructureSetDef& set, int64_t levelSeed) {
    std::vector<std::pair<int, int>> out;
    if (set.ringCount <= 0) return out;
    out.reserve(set.ringCount);
    mc::Legacy rng(levelSeed);
    constexpr double TWO_PI = 6.283185307179586476925286766559;
    double angle = rng.nextDouble() * TWO_PI;
    int inRing = 0;
    int ring = 0;
    int targetPerRing = set.ringSpread;
    int distParam = set.ringDistance;
    for (int i = 0; i < set.ringCount; ++i) {
        double dist = double(4 * distParam + distParam * ring * 6) + (rng.nextDouble() - 0.5) * (double(distParam) * 2.5);
        int cx = int(std::llround(std::cos(angle) * dist));
        int cz = int(std::llround(std::sin(angle) * dist));
        rng.nextLong(); // fork() consumes one nextLong in LegacyRandomSource
        out.emplace_back(cx, cz);
        angle += TWO_PI / double(targetPerRing);
        if (++inRing == targetPerRing) {
            inRing = 0;
            ++ring;
            targetPerRing += 2 * targetPerRing / (ring + 1);
            targetPerRing = std::min(targetPerRing, set.ringCount - i);
            angle += rng.nextDouble() * TWO_PI;
        }
    }
    return out;
}

inline uint32_t sampleBiomeAtWorld(
    float wx, float wz,
    float originX, float originZ, float extent, uint32_t resolution,
    const std::vector<uint32_t>& rawBiomes,
    const std::vector<uint32_t>& biomeTableWords)
{
    if (resolution < 2 || rawBiomes.size() < size_t(resolution) * resolution) return 0xFFFFFFFFu;
    float u = (wx - originX) / extent + 0.5f;
    float v = (wz - originZ) / extent + 0.5f;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return 0xFFFFFFFFu;
    uint32_t ix = uint32_t(std::clamp(int(std::lround(u * float(resolution - 1))), 0, int(resolution - 1)));
    uint32_t iz = uint32_t(std::clamp(int(std::lround(v * float(resolution - 1))), 0, int(resolution - 1)));
    uint32_t raw = rawBiomes[size_t(iz) * resolution + ix];
    return biomeTableWords.empty() ? raw : canonicalBiomeId(biomeTableWords, raw);
}

inline std::vector<StructurePin> findStructuresInDomain(
    const std::vector<StructureSetDef>& sets,
    int64_t levelSeed,
    float originX, float originZ, float extent,
    uint32_t resolution,
    const std::vector<uint32_t>& rawBiomes,
    const std::vector<uint32_t>& biomeTableWords,
    uint64_t activeSetMask = ~0ull)
{
    std::vector<StructurePin> pins;
    if (sets.empty() || resolution < 2 || rawBiomes.size() < size_t(resolution) * resolution) return pins;

    float minX = originX - extent * 0.5f;
    float maxX = originX + extent * 0.5f;
    float minZ = originZ - extent * 0.5f;
    float maxZ = originZ + extent * 0.5f;
    int minChunkX = floorDiv(int(std::floor(minX)), 16);
    int maxChunkX = floorDiv(int(std::floor(maxX)), 16);
    int minChunkZ = floorDiv(int(std::floor(minZ)), 16);
    int maxChunkZ = floorDiv(int(std::floor(maxZ)), 16);

    auto matchVariant = [&](const StructureSetDef& set, uint32_t biomeId, std::string& outVariant) -> bool {
        if (biomeId == 0xFFFFFFFFu) return false;
        for (const auto& v : set.variants) {
            if (v.biomes.count(biomeId)) {
                outVariant = v.id;
                return true;
            }
        }
        return false;
    };

    float texelSize = extent / float(resolution - 1);
    auto isValidRandomSpreadChunk = [&](int setIdx, int cx, int cz, uint32_t& outBiome, std::string& outVariant) -> bool {
        const auto& set = sets[setIdx];
        if (!passesFrequency(set, levelSeed, cx, cz)) return false;
        float wx = float(cx * 16 + 8);
        float wz = float(cz * 16 + 8);
        outBiome = sampleBiomeAtWorld(wx, wz, originX, originZ, extent, resolution, rawBiomes, biomeTableWords);
        if (matchVariant(set, outBiome, outVariant)) return true;
        // On coarse biome grids (>16 blocks/texel), a single rounded sample can land on a narrow river/edge texel;
        // check the 4 enclosing texel corners around (wx, wz) so valid village/structure regions are not skipped.
        if (texelSize > 16.0f) {
            float u = ((wx - originX) / extent + 0.5f) * float(resolution - 1);
            float v = ((wz - originZ) / extent + 0.5f) * float(resolution - 1);
            if (u >= 0.0f && u <= float(resolution - 1) && v >= 0.0f && v <= float(resolution - 1)) {
                int ix0 = std::clamp(int(std::floor(u)), 0, int(resolution - 1));
                int iz0 = std::clamp(int(std::floor(v)), 0, int(resolution - 1));
                int ix1 = std::min(ix0 + 1, int(resolution - 1));
                int iz1 = std::min(iz0 + 1, int(resolution - 1));
                const int xs[4] = { ix0, ix1, ix0, ix1 };
                const int zs[4] = { iz0, iz0, iz1, iz1 };
                for (int k = 0; k < 4; ++k) {
                    uint32_t raw = rawBiomes[size_t(zs[k]) * resolution + size_t(xs[k])];
                    uint32_t bid = biomeTableWords.empty() ? raw : canonicalBiomeId(biomeTableWords, raw);
                    if (matchVariant(set, bid, outVariant)) {
                        outBiome = bid;
                        return true;
                    }
                }
            }
        }
        return false;
    };

    for (int setIdx = 0; setIdx < int(sets.size()); ++setIdx) {
        const auto& set = sets[setIdx];
        bool scanSet = (activeSetMask == ~0ull) ? set.enabled : (setIdx < 64 && ((activeSetMask & (1ull << setIdx)) != 0));
        if (!scanSet) continue;

        if (set.placement == PlacementType::ConcentricRings) {
            auto ringChunks = computeStrongholdRingPositions(set, levelSeed);
            for (const auto& [cx, cz] : ringChunks) {
                float wx = float(cx * 16 + 8);
                float wz = float(cz * 16 + 8);
                if (wx < minX || wx > maxX || wz < minZ || wz > maxZ) continue;
                uint32_t b = sampleBiomeAtWorld(wx, wz, originX, originZ, extent, resolution, rawBiomes, biomeTableWords);
                std::string varName;
                if (matchVariant(set, b, varName) || !set.preferredBiomes.empty()) {
                    // Strongholds snap to preferred_biomes within 112 blocks in-game, or stay at ring center.
                    pins.push_back({setIdx, cx, cz, float(cx * 16 + 4), float(cz * 16 + 4), b, set.name});
                }
            }
            continue;
        }

        int minRx = floorDiv(minChunkX, set.spacing);
        int maxRx = floorDiv(maxChunkX, set.spacing);
        int minRz = floorDiv(minChunkZ, set.spacing);
        int maxRz = floorDiv(maxChunkZ, set.spacing);

        for (int rz = minRz; rz <= maxRz; ++rz) {
            for (int rx = minRx; rx <= maxRx; ++rx) {
                auto [cx, cz] = randomSpreadCandidate(set, levelSeed, rx, rz);
                if (cx < minChunkX || cx > maxChunkX || cz < minChunkZ || cz > maxChunkZ) continue;
                uint32_t biomeId = 0;
                std::string variantId;
                if (!isValidRandomSpreadChunk(setIdx, cx, cz, biomeId, variantId)) continue;

                // Exclusion zone check (e.g., pillager_outposts vs villages within 10 chunks)
                if (set.exclusionSetIndex >= 0 && set.exclusionChunkCount > 0) {
                    const auto& other = sets[set.exclusionSetIndex];
                    int r = set.exclusionChunkCount;
                    int oMinRx = floorDiv(cx - r, other.spacing);
                    int oMaxRx = floorDiv(cx + r, other.spacing);
                    int oMinRz = floorDiv(cz - r, other.spacing);
                    int oMaxRz = floorDiv(cz + r, other.spacing);
                    bool excluded = false;
                    for (int orz = oMinRz; orz <= oMaxRz && !excluded; ++orz) {
                        for (int orx = oMinRx; orx <= oMaxRx && !excluded; ++orx) {
                            auto [ocx, ocz] = randomSpreadCandidate(other, levelSeed, orx, orz);
                            if (std::abs(ocx - cx) <= r && std::abs(ocz - cz) <= r) {
                                uint32_t ob = 0;
                                std::string ov;
                                if (isValidRandomSpreadChunk(set.exclusionSetIndex, ocx, ocz, ob, ov)) {
                                    excluded = true;
                                }
                            }
                        }
                    }
                    if (excluded) continue;
                }

                float wx = float(cx * 16 + set.locateOffsetX);
                float wz = float(cz * 16 + set.locateOffsetZ);
                if (wx < minX || wx > maxX || wz < minZ || wz > maxZ) continue;
                pins.push_back({setIdx, cx, cz, wx, wz, biomeId, variantId});
            }
        }
    }
    return pins;
}

} // namespace mcstruct

