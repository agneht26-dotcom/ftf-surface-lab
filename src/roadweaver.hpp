#pragma once
// MIT License - Copyright (c) shiroha233 (RoadWeaver 2.3.1-1.21.1)
// Surface preview adaptation for FTF Surface Lab (2026-11); see LICENSES/RoadWeaver-MIT.txt
// (notices/RoadWeaver-MIT.txt in release packages) for the full licence text.

#include "structures.hpp"
#include "structure_render.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace roadweaver {

enum class NetworkAlgo { KNN = 0, Delaunay = 1, RNG = 2, MST = 3 };
enum class PathAlgo { AStar = 0, BiAStar = 1, DirectBezier = 2 };
enum class RoadStyle { BiomeAdaptive = 0, StoneBrick = 1, CobbleGravel = 2, DirtPath = 3 };
enum class DisplayMode { OverlayIcons = 0, BlockRep = 1, Both = 2 };

struct RoadWeaverConfig {
    bool enabled = false;
    int displayMode = 0;         // 0=Overlay/Icons, 1=Block representation, 2=Both
    int networkAlgorithm = 2;    // 0=KNN, 1=Delaunay, 2=RNG, 3=MST
    int pathfindingMode = 0;     // 0=A*, 1=Bidirectional A*, 2=Direct/Bezier
    int roadStyle = 0;           // 0=Biome-adaptive, 1=Stone Brick, 2=Cobble & Gravel, 3=Dirt Path
    int searchRadius = 2560;     // blocks
    int maxConnections = 3;      // max road links per settlement
    int roadStepSize = 24;       // sampling resolution in blocks
    int roadWidth = 3;           // blocks (2..7)
    int roadClearHeight = 4;     // blocks above road surface
    float slopeWeight = 2.5f;    // terrain steepness penalty
    float waterAvoidanceWeight = 4.0f;
    bool bezierSmoothing = true;
    bool flattenTerrain = true;
    float weatheringChance = 0.25f;
    bool generateBridges = true;
    int bridgeDeckHeight = 3;    // blocks above sea level
    int bridgePierInterval = 3;  // nodes between bridge piers
    bool bridgeRailings = true;
    bool generateTunnels = true;
    int tunnelMinRidgeDepth = 10;
    bool roadsideLamps = true;
    int lampInterval = 52;
    bool roadsideSignposts = true;
    bool roadsideDecorations = true;
    int decorationInterval = 145;
    bool connectVillagesOnly = true;
    uint32_t targetStructureMask = 0xFFFFFFFFu; // per-structure bitmask when connectVillagesOnly is false

    uint64_t hash() const {
        uint64_t h = enabled ? 1ull : 0ull;
        auto mix = [&](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
        mix(uint64_t(networkAlgorithm));
        mix(uint64_t(pathfindingMode));
        mix(uint64_t(roadStyle));
        mix(uint64_t(searchRadius));
        mix(uint64_t(maxConnections));
        mix(uint64_t(roadStepSize));
        mix(uint64_t(roadWidth));
        mix(uint64_t(roadClearHeight));
        mix(uint64_t(std::lround(slopeWeight * 100.0f)));
        mix(uint64_t(std::lround(waterAvoidanceWeight * 100.0f)));
        mix(uint64_t(std::lround(weatheringChance * 100.0f)));
        mix(bezierSmoothing ? 11 : 12);
        mix(flattenTerrain ? 31 : 32);
        mix(generateBridges ? 13 : 14);
        mix(uint64_t(bridgeDeckHeight));
        mix(uint64_t(bridgePierInterval));
        mix(bridgeRailings ? 33 : 34);
        mix(generateTunnels ? 15 : 16);
        mix(uint64_t(tunnelMinRidgeDepth));
        mix(roadsideLamps ? 17 : 18);
        mix(uint64_t(lampInterval));
        mix(roadsideSignposts ? 19 : 20);
        mix(roadsideDecorations ? 21 : 22);
        mix(uint64_t(decorationInterval));
        mix(connectVillagesOnly ? 23 : 24);
        mix(uint64_t(targetStructureMask));
        return h;
    }

    nlohmann::json toStateJson() const {
        return {
            {"enabled", enabled},
            {"display_mode", displayMode},
            {"network_algorithm", networkAlgorithm},
            {"pathfinding_mode", pathfindingMode},
            {"road_style", roadStyle},
            {"search_radius", searchRadius},
            {"max_connections", maxConnections},
            {"road_step_size", roadStepSize},
            {"road_width", roadWidth},
            {"road_clear_height", roadClearHeight},
            {"slope_weight", slopeWeight},
            {"water_avoidance_weight", waterAvoidanceWeight},
            {"bezier_smoothing", bezierSmoothing},
            {"flatten_terrain", flattenTerrain},
            {"weathering_chance", weatheringChance},
            {"generate_bridges", generateBridges},
            {"bridge_deck_height", bridgeDeckHeight},
            {"bridge_pier_interval", bridgePierInterval},
            {"bridge_railings", bridgeRailings},
            {"generate_tunnels", generateTunnels},
            {"tunnel_min_ridge_depth", tunnelMinRidgeDepth},
            {"roadside_lamps", roadsideLamps},
            {"lamp_interval", lampInterval},
            {"roadside_signposts", roadsideSignposts},
            {"roadside_decorations", roadsideDecorations},
            {"decoration_interval", decorationInterval},
            {"connect_villages_only", connectVillagesOnly},
            {"target_structure_mask", targetStructureMask}
        };
    }

    static RoadWeaverConfig fromStateJson(const nlohmann::json& j) {
        RoadWeaverConfig c;
        c.enabled = j.value("enabled", c.enabled);
        c.displayMode = std::clamp(j.value("display_mode", c.displayMode), 0, 2);
        c.networkAlgorithm = std::clamp(j.value("network_algorithm", c.networkAlgorithm), 0, 3);
        c.pathfindingMode = std::clamp(j.value("pathfinding_mode", c.pathfindingMode), 0, 2);
        c.roadStyle = std::clamp(j.value("road_style", c.roadStyle), 0, 3);
        c.searchRadius = std::clamp(j.value("search_radius", c.searchRadius), 512, 16384);
        c.maxConnections = std::clamp(j.value("max_connections", c.maxConnections), 1, 6);
        c.roadStepSize = std::clamp(j.value("road_step_size", c.roadStepSize), 12, 64);
        c.roadWidth = std::clamp(j.value("road_width", c.roadWidth), 2, 7);
        c.roadClearHeight = std::clamp(j.value("road_clear_height", c.roadClearHeight), 3, 6);
        c.slopeWeight = std::clamp(j.value("slope_weight", c.slopeWeight), 0.5f, 8.0f);
        c.waterAvoidanceWeight = std::clamp(j.value("water_avoidance_weight", c.waterAvoidanceWeight), 0.5f, 10.0f);
        c.bezierSmoothing = j.value("bezier_smoothing", c.bezierSmoothing);
        c.flattenTerrain = j.value("flatten_terrain", c.flattenTerrain);
        c.weatheringChance = std::clamp(j.value("weathering_chance", c.weatheringChance), 0.0f, 1.0f);
        c.generateBridges = j.value("generate_bridges", c.generateBridges);
        c.bridgeDeckHeight = std::clamp(j.value("bridge_deck_height", c.bridgeDeckHeight), 1, 6);
        c.bridgePierInterval = std::clamp(j.value("bridge_pier_interval", c.bridgePierInterval), 2, 8);
        c.bridgeRailings = j.value("bridge_railings", c.bridgeRailings);
        c.generateTunnels = j.value("generate_tunnels", c.generateTunnels);
        c.tunnelMinRidgeDepth = std::clamp(j.value("tunnel_min_ridge_depth", c.tunnelMinRidgeDepth), 6, 24);
        c.roadsideLamps = j.value("roadside_lamps", c.roadsideLamps);
        c.lampInterval = std::clamp(j.value("lamp_interval", c.lampInterval), 24, 120);
        c.roadsideSignposts = j.value("roadside_signposts", c.roadsideSignposts);
        c.roadsideDecorations = j.value("roadside_decorations", c.roadsideDecorations);
        c.decorationInterval = std::clamp(j.value("decoration_interval", c.decorationInterval), 64, 300);
        c.connectVillagesOnly = j.value("connect_villages_only", c.connectVillagesOnly);
        c.targetStructureMask = j.value("target_structure_mask", c.targetStructureMask);
        return c;
    }
};

enum class SegmentKind : uint8_t { Road = 0, Bridge = 1, Tunnel = 2 };
enum class RoadsideKind : uint8_t { Lamp = 0, Signpost = 1, CampOrInn = 2 };

struct RoadNode {
    float x = 0.0f;
    float y = 64.0f;
    float z = 0.0f;
    SegmentKind kind = SegmentKind::Road;
};

struct RoadPath {
    int fromPin = -1;
    int toPin = -1;
    std::vector<RoadNode> nodes;
};

struct RoadsideProp {
    float x = 0.0f;
    float y = 64.0f;
    float z = 0.0f;
    float dirX = 1.0f;
    float dirZ = 0.0f;
    RoadsideKind kind = RoadsideKind::Lamp;
};

struct RoadNetwork {
    std::vector<RoadPath> roads;
    std::vector<RoadsideProp> props;
    size_t bridgeSegments = 0;
    size_t tunnelSegments = 0;
};

inline nlohmann::json toConfigJson(const RoadWeaverConfig& c) {
    static const char* netNames[] = {"KNN", "DELAUNAY", "RNG", "MST"};
    static const char* pathNames[] = {"ASTAR", "BIDIRECTIONAL_ASTAR", "DIRECT_BEZIER"};
    static const char* styleNames[] = {"BIOME_ADAPTIVE", "STONE_BRICK", "COBBLE_GRAVEL", "DIRT_PATH"};
    return nlohmann::json{
        {"mod", "roadweaver"},
        {"minecraft_version", "1.21.1"},
        {"enabled", c.enabled},
        {"general", {
            {"connect_villages_only", c.connectVillagesOnly},
            {"target_structure_mask", c.targetStructureMask},
            {"structure_search_radius", c.searchRadius},
            {"max_connections_per_settlement", c.maxConnections},
            {"network_planning_algorithm", netNames[std::clamp(c.networkAlgorithm, 0, 3)]},
        }},
        {"pathfinding", {
            {"algorithm", pathNames[std::clamp(c.pathfindingMode, 0, 2)]},
            {"sampling_step_blocks", c.roadStepSize},
            {"road_width", c.roadWidth},
            {"road_clear_height", c.roadClearHeight},
            {"terrain_slope_weight", c.slopeWeight},
            {"water_avoidance_weight", c.waterAvoidanceWeight},
            {"bezier_smoothing", c.bezierSmoothing},
            {"flatten_terrain", c.flattenTerrain},
        }},
        {"structures_and_obstacles", {
            {"generate_bridges", c.generateBridges},
            {"bridge_deck_height", c.bridgeDeckHeight},
            {"bridge_pier_interval", c.bridgePierInterval},
            {"bridge_railings", c.bridgeRailings},
            {"generate_tunnels", c.generateTunnels},
            {"tunnel_min_ridge_depth", c.tunnelMinRidgeDepth},
            {"roadside_lamps", c.roadsideLamps},
            {"lamp_interval_blocks", c.lampInterval},
            {"roadside_signposts", c.roadsideSignposts},
            {"roadside_decorations", c.roadsideDecorations},
            {"decoration_interval_blocks", c.decorationInterval},
        }},
        {"materials", {
            {"road_preset_style", styleNames[std::clamp(c.roadStyle, 0, 3)]},
            {"weathering_chance", c.weatheringChance},
        }},
    };
}

namespace detail {

inline float distXZ(glm::vec2 a, glm::vec2 b) {
    return glm::length(a - b);
}

inline std::vector<std::pair<int, int>> planEdges(const std::vector<glm::vec2>& pts, int algo, float maxRadius, int maxConnections) {
    int n = int(pts.size());
    std::vector<std::pair<int, int>> edges;
    if (n < 2) return edges;
    float r2 = maxRadius * maxRadius;
    int maxCap = std::clamp(maxConnections, 1, 12);
    std::vector<int> deg(n, 0);

    auto addUnique = [&](int a, int b, bool respectCap = true) -> bool {
        if (a == b) return false;
        if (respectCap && (deg[a] >= maxCap || deg[b] >= maxCap)) return false;
        if (a > b) std::swap(a, b);
        for (const auto& e : edges) if (e.first == a && e.second == b) return false;
        edges.emplace_back(a, b);
        ++deg[a];
        ++deg[b];
        return true;
    };

    if (algo == int(NetworkAlgo::MST)) {
        struct Cand { float d2; int a, b; };
        std::vector<Cand> cands;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) {
                glm::vec2 d = pts[i] - pts[j];
                float d2 = glm::dot(d, d);
                if (d2 <= r2) cands.push_back({d2, i, j});
            }
        std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.d2 < y.d2; });
        std::vector<int> parent(n);
        for (int i = 0; i < n; ++i) parent[i] = i;
        auto find = [&](int x) {
            while (parent[x] != x) x = parent[x] = parent[parent[x]];
            return x;
        };
        for (const auto& c : cands) {
            int ra = find(c.a), rb = find(c.b);
            if (ra != rb) {
                if (maxCap == 1 && (deg[c.a] >= 1 || deg[c.b] >= 1)) continue;
                parent[ra] = rb;
                addUnique(c.a, c.b, false);
            }
        }
        return edges;
    }

    if (algo == int(NetworkAlgo::RNG)) {
        struct Cand { float d2; int a, b; };
        std::vector<Cand> rngCands;
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                glm::vec2 dv = pts[i] - pts[j];
                float d2 = glm::dot(dv, dv);
                if (d2 > r2) continue;
                float dij = std::sqrt(d2);
                bool blocked = false;
                for (int k = 0; k < n; ++k) {
                    if (k == i || k == j) continue;
                    if (std::max(distXZ(pts[i], pts[k]), distXZ(pts[j], pts[k])) < dij) {
                        blocked = true;
                        break;
                    }
                }
                if (!blocked) rngCands.push_back({d2, i, j});
            }
        }
        std::sort(rngCands.begin(), rngCands.end(), [](const Cand& x, const Cand& y) { return x.d2 < y.d2; });
        for (const auto& c : rngCands) addUnique(c.a, c.b, true);
        return edges;
    }

    int kNeighbors = (algo == int(NetworkAlgo::Delaunay)) ? std::max(maxCap, 4) : maxCap;
    struct Cand { float d2; int a, b; };
    std::vector<Cand> cands;
    for (int i = 0; i < n; ++i) {
        std::vector<std::pair<float, int>> nbrs;
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            glm::vec2 d = pts[i] - pts[j];
            float d2 = glm::dot(d, d);
            if (d2 <= r2) nbrs.emplace_back(d2, j);
        }
        std::sort(nbrs.begin(), nbrs.end());
        for (int k = 0; k < std::min(kNeighbors, int(nbrs.size())); ++k) {
            cands.push_back({nbrs[k].first, i, nbrs[k].second});
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.d2 < y.d2; });
    for (const auto& c : cands) addUnique(c.a, c.b, true);
    return edges;
}

template <class SampleGroundFn>
inline std::vector<glm::vec2> findPathAStar(glm::vec2 start, glm::vec2 goal, float step, float slopeWeight, float waterWeight, float seaLevel, bool allowBridges, bool bidirectional, SampleGroundFn&& sampleGround) {
    float totalDist = glm::length(goal - start);
    if (totalDist < step * 1.5f) return {start, goal};

    int gx0 = int(std::round(start.x / step)), gz0 = int(std::round(start.y / step));
    int gx1 = int(std::round(goal.x / step)), gz1 = int(std::round(goal.y / step));

    auto keyOf = [](int x, int z) -> uint64_t {
        return (uint64_t(uint32_t(x)) << 32) | uint64_t(uint32_t(z));
    };
    auto heightAt = [&](int gx, int gz) {
        float h = seaLevel;
        sampleGround(glm::vec2(gx * step, gz * step), h);
        return h;
    };

    struct OpenNode {
        float f;
        int x, z;
        bool operator>(const OpenNode& o) const { return f > o.f; }
    };

    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> pq;
    std::unordered_map<uint64_t, float> gScore;
    std::unordered_map<uint64_t, uint64_t> cameFrom;

    uint64_t startKey = keyOf(gx0, gz0);
    uint64_t goalKey = keyOf(gx1, gz1);
    gScore[startKey] = 0.0f;
    pq.push({glm::length(goal - start), gx0, gz0});

    static const int dx8[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dz8[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const float diagCost[8] = {1, 1, 1, 1, 1.4142f, 1.4142f, 1.4142f, 1.4142f};

    int maxExpansions = std::clamp(int((totalDist / std::max(1.0f, step)) * 16.0f), 1024, 6000);
    int expansions = 0;
    uint64_t bestKey = startKey;
    float bestHeur = 1e9f;
    std::unordered_set<uint64_t> closed;

    while (!pq.empty() && expansions < maxExpansions) {
        auto cur = pq.top();
        pq.pop();
        uint64_t ck = keyOf(cur.x, cur.z);
        if (!closed.insert(ck).second) continue;
        ++expansions;
        if (ck == goalKey) { bestKey = ck; break; }

        float hCur = heightAt(cur.x, cur.z);
        float curG = gScore[ck];
        for (int dir = 0; dir < 8; ++dir) {
            int nx = cur.x + dx8[dir];
            int nz = cur.z + dz8[dir];
            uint64_t nk = keyOf(nx, nz);
            if (closed.count(nk)) continue;
            float hNext = heightAt(nx, nz);
            float dh = std::abs(hNext - hCur);
            float waterPenalty = (hNext < seaLevel - 0.5f) ? ((allowBridges ? 0.35f : 2.5f) * waterWeight * step) : 0.0f;
            float moveCost = step * diagCost[dir] + dh * slopeWeight + waterPenalty;
            float tentG = curG + moveCost;
            auto it = gScore.find(nk);
            if (it == gScore.end() || tentG < it->second) {
                gScore[nk] = tentG;
                cameFrom[nk] = ck;
                float heur = std::hypot(float(gx1 - nx), float(gz1 - nz)) * step * (bidirectional ? 1.20f : 1.0f);
                if (heur < bestHeur) { bestHeur = heur; bestKey = nk; }
                pq.push({tentG + heur, nx, nz});
            }
        }
    }

    std::vector<glm::vec2> rev;
    uint64_t k = (gScore.count(goalKey)) ? goalKey : bestKey;
    int guard = 0;
    while (k != startKey && guard++ < 8192) {
        int kx = int(int32_t(uint32_t(k >> 32)));
        int kz = int(int32_t(uint32_t(k & 0xffffffffu)));
        rev.emplace_back(kx * step, kz * step);
        auto it = cameFrom.find(k);
        if (it == cameFrom.end()) break;
        k = it->second;
    }
    std::vector<glm::vec2> out;
    out.push_back(start);
    for (auto it = rev.rbegin(); it != rev.rend(); ++it) out.push_back(*it);
    if (out.empty() || glm::length(out.back() - goal) > step * 0.5f) out.push_back(goal);
    return out;
}

inline std::vector<glm::vec2> smoothBezier(const std::vector<glm::vec2>& pts) {
    if (pts.size() < 3) return pts;
    std::vector<glm::vec2> out;
    out.push_back(pts.front());
    for (size_t i = 1; i + 1 < pts.size(); ++i) {
        glm::vec2 prev = pts[i - 1], cur = pts[i], next = pts[i + 1];
        glm::vec2 p0 = glm::mix(prev, cur, 0.72f);
        glm::vec2 p2 = glm::mix(cur, next, 0.28f);
        out.push_back(0.25f * p0 + 0.5f * cur + 0.25f * p2);
    }
    out.push_back(pts.back());
    return out;
}

} // namespace detail

template <class SampleGroundFn>
inline RoadNetwork buildRoadNetwork(
    const RoadWeaverConfig& cfg,
    const std::vector<mcstruct::StructurePin>& pins,
    const std::vector<mcstruct::StructureSetDef>& sets,
    float seaLevel,
    float worldExtent,
    glm::vec2 focusXZ,
    SampleGroundFn&& sampleGround)
{
    RoadNetwork net;
    if (!cfg.enabled || pins.empty()) return net;

    std::vector<std::pair<float, int>> candidates;
    for (int i = 0; i < int(pins.size()); ++i) {
        const auto& p = pins[i];
        if (p.setIndex < 0 || p.setIndex >= int(sets.size())) continue;
        const auto& s = sets[p.setIndex];
        if (cfg.connectVillagesOnly && s.shortId != "villages") continue;
        if (!cfg.connectVillagesOnly && p.setIndex < 32 && ((cfg.targetStructureMask & (1u << p.setIndex)) == 0)) continue;
        if (s.shortId == "mineshafts" || s.shortId == "strongholds" || s.shortId == "ancient_cities" ||
            s.shortId == "trial_chambers" || s.shortId == "buried_treasures" || s.shortId == "ocean_monuments" ||
            s.shortId == "ocean_ruins" || s.shortId == "shipwrecks") continue;
        float d2 = glm::dot(glm::vec2(p.worldX, p.worldZ) - focusXZ, glm::vec2(p.worldX, p.worldZ) - focusXZ);
        candidates.emplace_back(d2, i);
    }
    if (candidates.size() < 2) return net;
    std::sort(candidates.begin(), candidates.end());
    if (candidates.size() > 120) candidates.resize(120);

    std::vector<int> targetPinIdx;
    std::vector<glm::vec2> pts;
    targetPinIdx.reserve(candidates.size());
    pts.reserve(candidates.size());
    for (const auto& [d2, idx] : candidates) {
        targetPinIdx.push_back(idx);
        pts.emplace_back(pins[idx].worldX, pins[idx].worldZ);
    }

    auto edges = detail::planEdges(pts, cfg.networkAlgorithm, float(cfg.searchRadius), cfg.maxConnections);
    float step = std::clamp(float(cfg.roadStepSize), 12.0f, 64.0f);

    for (const auto& edge : edges) {
        glm::vec2 a = pts[edge.first], b = pts[edge.second];
        std::vector<glm::vec2> poly;
        if (cfg.pathfindingMode == int(PathAlgo::DirectBezier)) {
            int segs = std::max(4, int(glm::length(b - a) / step));
            for (int k = 0; k <= segs; ++k) poly.push_back(glm::mix(a, b, float(k) / float(segs)));
        } else {
            poly = detail::findPathAStar(a, b, step, cfg.slopeWeight, cfg.waterAvoidanceWeight, seaLevel, cfg.generateBridges, cfg.pathfindingMode == int(PathAlgo::BiAStar), sampleGround);
        }
        if (cfg.bezierSmoothing) poly = detail::smoothBezier(poly);

        RoadPath rp;
        rp.fromPin = targetPinIdx[edge.first];
        rp.toPin = targetPinIdx[edge.second];
        rp.nodes.reserve(poly.size());

        for (size_t k = 0; k < poly.size(); ++k) {
            float gy = seaLevel;
            sampleGround(poly[k], gy);
            SegmentKind kind = SegmentKind::Road;
            if (cfg.generateBridges && gy < seaLevel - 0.35f) {
                kind = SegmentKind::Bridge;
                gy = seaLevel + float(cfg.bridgeDeckHeight);
                ++net.bridgeSegments;
            } else if (cfg.generateTunnels && k > 0 && k + 1 < poly.size()) {
                float hPrev = seaLevel, hNext = seaLevel;
                sampleGround(poly[k - 1], hPrev);
                sampleGround(poly[k + 1], hNext);
                float avg = 0.5f * (hPrev + hNext);
                if (gy - avg > float(cfg.tunnelMinRidgeDepth)) {
                    kind = SegmentKind::Tunnel;
                    gy = avg + 1.0f;
                    ++net.tunnelSegments;
                }
            }
            rp.nodes.push_back({poly[k].x, gy, poly[k].y, kind});
        }

        // Smooth bridge approach ramps so bridges connect cleanly to shore terrain
        for (size_t k = 1; k + 1 < rp.nodes.size(); ++k) {
            if (rp.nodes[k].kind == SegmentKind::Road && (rp.nodes[k - 1].kind == SegmentKind::Bridge || rp.nodes[k + 1].kind == SegmentKind::Bridge)) {
                rp.nodes[k].y = std::max(rp.nodes[k].y, seaLevel + float(cfg.bridgeDeckHeight) * 0.55f);
            }
        }

        // Roadside structures: signposts at start/end, lamps & decorations along flat land
        if (rp.nodes.size() >= 4) {
            auto dirAt = [&](size_t idx) -> glm::vec2 {
                size_t i0 = idx > 0 ? idx - 1 : 0;
                size_t i1 = std::min(rp.nodes.size() - 1, idx + 1);
                glm::vec2 d(rp.nodes[i1].x - rp.nodes[i0].x, rp.nodes[i1].z - rp.nodes[i0].z);
                float len = glm::length(d);
                return len > 1e-4f ? d / len : glm::vec2(1, 0);
            };
            if (cfg.roadsideSignposts) {
                for (size_t si : {size_t(1), rp.nodes.size() - 2}) {
                    if (rp.nodes[si].kind == SegmentKind::Road) {
                        glm::vec2 d = dirAt(si);
                        glm::vec2 side(-d.y, d.x);
                        float off = float(cfg.roadWidth) + 1.5f;
                        net.props.push_back({rp.nodes[si].x + side.x * off, rp.nodes[si].y, rp.nodes[si].z + side.y * off, d.x, d.y, RoadsideKind::Signpost});
                    }
                }
            }
            float accDist = 0.0f;
            float nextLamp = float(cfg.lampInterval) * 0.8f;
            float nextDeco = float(cfg.decorationInterval) * 0.8f;
            for (size_t k = 1; k + 1 < rp.nodes.size(); ++k) {
                const auto& prev = rp.nodes[k - 1];
                const auto& cur = rp.nodes[k];
                accDist += std::hypot(cur.x - prev.x, cur.z - prev.z);
                if (cur.kind != SegmentKind::Road) continue;
                glm::vec2 d = dirAt(k);
                glm::vec2 side(-d.y, d.x);
                float off = float(cfg.roadWidth) + 1.4f;
                if (cfg.roadsideLamps && accDist >= nextLamp) {
                    float sgn = (int(k) & 1) ? 1.0f : -1.0f;
                    net.props.push_back({cur.x + side.x * off * sgn, cur.y, cur.z + side.y * off * sgn, d.x, d.y, RoadsideKind::Lamp});
                    nextLamp = accDist + float(cfg.lampInterval);
                }
                if (cfg.roadsideDecorations && accDist >= nextDeco) {
                    float sgn = (int(k) & 1) ? -1.0f : 1.0f;
                    net.props.push_back({cur.x + side.x * (off + 2.5f) * sgn, cur.y, cur.z + side.y * (off + 2.5f) * sgn, d.x, d.y, RoadsideKind::CampOrInn});
                    nextDeco = accDist + float(cfg.decorationInterval);
                }
            }
        }
        net.roads.push_back(std::move(rp));
    }
    return net;
}

template <class ProjectFn>
inline void drawRoadWeaver(ImDrawList* dl, ProjectFn&& proj, ImVec2 vpMin, ImVec2 vpMax, float maxDist, glm::vec3 camWorld, const RoadWeaverConfig& cfg, const RoadNetwork& net, float seaLevel = 63.0f) {
    if (!cfg.enabled || net.roads.empty()) return;

    bool drawOverlay = (cfg.displayMode == int(DisplayMode::OverlayIcons) || cfg.displayMode == int(DisplayMode::Both));
    bool drawBlocks = (cfg.displayMode == int(DisplayMode::BlockRep) || cfg.displayMode == int(DisplayMode::Both));

    auto inViewport = [&](ImVec2 p0, ImVec2 p1) -> bool {
        float minX = std::min(p0.x, p1.x), maxX = std::max(p0.x, p1.x);
        float minY = std::min(p0.y, p1.y), maxY = std::max(p0.y, p1.y);
        return maxX >= vpMin.x - 24.0f && minX <= vpMax.x + 24.0f
            && maxY >= vpMin.y - 24.0f && minY <= vpMax.y + 24.0f;
    };

    auto hash2 = [](int x, int z) -> uint32_t {
        uint32_t h = uint32_t(x) * 374761393u + uint32_t(z) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    };

    ImU32 roadEdge = IM_COL32(28, 24, 20, 42);
    ImU32 bridgeRail = IM_COL32(198, 152, 98, 245);
    ImU32 tunnelCol = IM_COL32(98, 104, 118, 220);

    for (const auto& road : net.roads) {
        if (dl->VtxBuffer.Size > 48000) break;
        if (road.nodes.size() < 2) continue;
        size_t rn = road.nodes.size();
        float minSampleDist = std::numeric_limits<float>::max();
        for (size_t s = 0; s <= 4; ++s) {
            const auto& nd = road.nodes[(rn - 1) * s / 4];
            minSampleDist = std::min(minSampleDist, glm::length(camWorld - glm::vec3(nd.x, nd.y, nd.z)));
        }
        if (minSampleDist > maxDist + 2500.0f) continue;
        size_t i = 0;
        while (i + 1 < road.nodes.size()) {
            const auto& a = road.nodes[i];
            float segDist = glm::length(camWorld - glm::vec3(a.x, a.y, a.z));
            size_t stepNodes = (segDist > 7500.0f) ? 4 : (segDist > 4200.0f) ? 2 : 1;
            size_t nextIdx = std::min(i + stepNodes, road.nodes.size() - 1);
            const auto& b = road.nodes[nextIdx];
            if (segDist > maxDist) {
                i = nextIdx;
                continue;
            }
            SegmentKind kind = (a.kind == SegmentKind::Bridge || b.kind == SegmentKind::Bridge) ? SegmentKind::Bridge
                             : (a.kind == SegmentKind::Tunnel || b.kind == SegmentKind::Tunnel) ? SegmentKind::Tunnel
                                                                                                : SegmentKind::Road;
            float yOff = (kind == SegmentKind::Bridge) ? 0.35f : 0.06f;
            glm::vec3 wa(a.x, a.y + yOff, a.z);
            glm::vec3 wb(b.x, b.y + yOff, b.z);
            ImVec2 pa, pb;
            bool okA = proj(wa, pa);
            bool okB = proj(wb, pb);
            if (!okA && !okB) {
                i = nextIdx;
                continue;
            }
            if (okA && okB && !inViewport(pa, pb)) {
                i = nextIdx;
                continue;
            }

            uint32_t hFull = hash2(int(std::floor(a.x * 0.25f)), int(std::floor(a.z * 0.25f)));
            uint32_t hv = hFull & 3u;
            bool weathered = (float(hFull & 255u) / 255.0f) < cfg.weatheringChance;
            // Subtle slope-based terrain shading so road blocks match the terrain lighting instead of glowing like a flat overlay
            float horiz = std::max(1.0f, std::hypot(b.x - a.x, b.z - a.z));
            float slopeShade = std::clamp(0.76f + 0.16f * ((b.y - a.y) / horiz), 0.52f, 0.94f);
            auto shadeCol = [&](uint32_t rgb, float mul = 1.0f) -> ImU32 {
                return mcstruct::shadeRgb(rgb, slopeShade * mul, 232);
            };
            ImU32 roadTop = 0;
            if (cfg.roadStyle == 1) { // Stone Brick
                static const uint32_t pal[4] = {0x767A7E, 0x6A6E72, 0x64785E, 0x7E8286};
                roadTop = shadeCol(weathered ? pal[2] : pal[hv]);
            } else if (cfg.roadStyle == 2) { // Cobble & Gravel
                static const uint32_t pal[4] = {0x747678, 0x808488, 0x7A7470, 0x6C6E72};
                roadTop = shadeCol(weathered ? pal[2] : pal[hv]);
            } else if (cfg.roadStyle == 3) { // Dirt Path
                static const uint32_t pal[4] = {0x8E7246, 0x715436, 0x7E6446, 0x866A42};
                roadTop = shadeCol(weathered ? pal[1] : pal[hv]);
            } else { // Biome-adaptive (Natural)
                if (a.y > 230.0f) {
                    static const uint32_t pal[4] = {0x8E9CA8, 0x68707A, 0x9CA8B4, 0x7E8892};
                    roadTop = shadeCol(weathered ? pal[1] : pal[hv]);
                } else if (a.y < seaLevel + 10.0f) {
                    static const uint32_t pal[4] = {0xCEBA8A, 0xC2AC7A, 0xB48E5E, 0xC6B282};
                    roadTop = shadeCol(weathered ? pal[2] : pal[hv]);
                } else {
                    static const uint32_t pal[4] = {0x8E7246, 0x6E5438, 0x76787A, 0x7E7C76};
                    roadTop = shadeCol(weathered ? pal[2] : pal[hv]);
                }
            }

            if (drawBlocks) {
                glm::vec2 dir(b.x - a.x, b.z - a.z);
                float len = glm::length(dir);
                if (len > 1e-4f) {
                    glm::vec2 side(-dir.y / len, dir.x / len);
                    float hw = float(cfg.roadWidth) * 0.55f;
                    glm::vec3 a0(a.x - side.x * hw, a.y + yOff, a.z - side.y * hw);
                    glm::vec3 a1(a.x + side.x * hw, a.y + yOff, a.z + side.y * hw);
                    glm::vec3 b1(b.x + side.x * hw, b.y + yOff, b.z + side.y * hw);
                    glm::vec3 b0(b.x - side.x * hw, b.y + yOff, b.z - side.y * hw);
                    ImVec2 pa0, pa1, pb1, pb0;
                    if (proj(a0, pa0) && proj(a1, pa1) && proj(b1, pb1) && proj(b0, pb0)) {
                        if (kind == SegmentKind::Bridge) {
                            ImU32 deckCol = (hv & 1u) ? shadeCol(0x9E7444, 1.05f) : shadeCol(0x845E34, 1.05f);
                            dl->AddQuadFilled(pa0, pa1, pb1, pb0, deckCol);
                            if (segDist < 1600.0f) dl->AddQuad(pa0, pa1, pb1, pb0, roadEdge, 1.0f);
                            if (cfg.bridgeRailings) {
                                ImVec2 ra0, rb0, ra1, rb1;
                                if (proj(a0 + glm::vec3(0, 1.3f, 0), ra0) && proj(b0 + glm::vec3(0, 1.3f, 0), rb0))
                                    dl->AddLine(ra0, rb0, bridgeRail, 2.2f);
                                if (proj(a1 + glm::vec3(0, 1.3f, 0), ra1) && proj(b1 + glm::vec3(0, 1.3f, 0), rb1))
                                    dl->AddLine(ra1, rb1, bridgeRail, 2.2f);
                                if (segDist < 2400.0f) {
                                    ImU32 postEdge = segDist > 1400.0f ? IM_COL32(0, 0, 0, 0) : roadEdge;
                                    mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, a0, {0.42f, 1.35f, 0.42f}, 0x7A5430, postEdge);
                                    mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, a1, {0.42f, 1.35f, 0.42f}, 0x7A5430, postEdge);
                                }
                            }
                            int pierStep = std::max(1, cfg.bridgePierInterval);
                            if ((int(i) % pierStep == 0) && segDist < 4800.0f) {
                                float pierBase = seaLevel - 6.0f;
                                float pierH = std::max(1.5f, (a.y + yOff) - pierBase);
                                ImU32 pierEdge = segDist > 1800.0f ? IM_COL32(0, 0, 0, 0) : roadEdge;
                                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, {a.x, pierBase, a.z}, {1.8f, pierH, 1.8f}, 0x6E7276, pierEdge);
                                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, {a.x, pierBase, a.z}, {2.4f, 1.2f, 2.4f}, 0x585C60, pierEdge);
                            }
                        } else {
                            ImU32 fill = (kind == SegmentKind::Tunnel) ? tunnelCol : roadTop;
                            dl->AddQuadFilled(pa0, pa1, pb1, pb0, fill);
                        }
                    }
                }
            }

            if (drawOverlay && okA && okB) {
                ImU32 lineCol = (kind == SegmentKind::Bridge) ? IM_COL32(76, 224, 255, 245)
                              : (kind == SegmentKind::Tunnel) ? IM_COL32(196, 139, 255, 235)
                                                              : IM_COL32(245, 184, 65, 245);
                float w = (kind == SegmentKind::Bridge) ? 3.8f : 3.0f;
                if (segDist < 3600.0f) dl->AddLine(pa, pb, IM_COL32(14, 16, 20, 120), w + 1.4f);
                dl->AddLine(pa, pb, lineCol, w);
            }
            i = nextIdx;
        }
    }

    for (const auto& prop : net.props) {
        if (dl->VtxBuffer.Size > 52000) break;
        glm::vec3 pos(prop.x, prop.y, prop.z);
        float dist = glm::length(camWorld - pos);
        if (dist > std::min(maxDist, 4500.0f)) continue;
        ImVec2 sp;
        if (!proj(pos, sp)) continue;
        if (sp.x < vpMin.x - 32.0f || sp.x > vpMax.x + 32.0f || sp.y < vpMin.y - 32.0f || sp.y > vpMax.y + 32.0f) continue;

        if (drawBlocks && dist <= 1600.0f) {
            float s = std::clamp(dist * 0.0018f, 1.0f, 2.2f);
            ImU32 edge = dist > 750.0f ? IM_COL32(0, 0, 0, 0) : IM_COL32(14, 18, 22, 48);
            if (prop.kind == RoadsideKind::Lamp) {
                // Stone base + spruce post + oak cross-arm + hanging lantern
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos, {0.55f * s, 0.45f * s, 0.55f * s}, 0x7A7E82, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(0, 0.45f * s, 0), {0.26f * s, 1.55f * s, 0.26f * s}, 0x624428, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(0, 1.85f * s, 0), {0.95f * s, 0.24f * s, 0.28f * s}, 0x8C6238, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(0.32f * s, 1.42f * s, 0), {0.42f * s, 0.44f * s, 0.42f * s}, 0xFFE678, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(0.32f * s, 1.86f * s, 0), {0.36f * s, 0.14f * s, 0.36f * s}, 0x3A4048, edge);
            } else if (prop.kind == RoadsideKind::Signpost) {
                // Cobble plinth + spruce post + two crossed oak sign boards
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos, {0.52f * s, 0.32f * s, 0.52f * s}, 0x7C8084, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(0, 0.32f * s, 0), {0.24f * s, 1.15f * s, 0.24f * s}, 0x6A482A, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(0, 1.10f * s, 0), {1.10f * s, 0.34f * s, 0.20f * s}, 0xC69A5B, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(0, 0.72f * s, 0), {0.20f * s, 0.32f * s, 1.05f * s}, 0xB4884C, edge);
            } else {
                // Proper roadside waystation / camp: cobble base + oak pillars + plank walls + stepped roof + campfire
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos, {2.1f * s, 0.30f * s, 2.1f * s}, 0x7A7E82, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(0, 0.30f * s, 0), {1.65f * s, 1.05f * s, 1.65f * s}, 0xB48652, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(0, 1.35f * s, 0), {1.95f * s, 0.36f * s, 1.95f * s}, 0x7A5432, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(0, 1.71f * s, 0), {1.35f * s, 0.36f * s, 1.35f * s}, 0x684426, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(0.45f * s, 1.71f * s, 0.45f * s), {0.38f * s, 0.75f * s, 0.38f * s}, 0x6E7276, edge);
                mcstruct::detail3d::drawBlock3D(dl, proj, camWorld, pos + glm::vec3(-1.35f * s, 0.0f, 0.35f * s), {0.55f * s, 0.28f * s, 0.55f * s}, 0xFF8C32, edge);
            }
        }
        if (drawOverlay) {
            if (prop.kind == RoadsideKind::Lamp) {
                dl->AddCircleFilled(sp, 3.3f, IM_COL32(18, 20, 22, 220));
                dl->AddCircleFilled(sp, 2.2f, IM_COL32(255, 224, 96, 250));
            } else if (prop.kind == RoadsideKind::Signpost) {
                dl->AddRectFilled({sp.x - 3.5f, sp.y - 2.5f}, {sp.x + 3.5f, sp.y + 2.5f}, IM_COL32(204, 158, 96, 245), 1.0f);
                dl->AddRect({sp.x - 3.5f, sp.y - 2.5f}, {sp.x + 3.5f, sp.y + 2.5f}, IM_COL32(18, 20, 22, 220), 1.0f);
            } else {
                ImVec2 tri[3] = {{sp.x, sp.y - 4.5f}, {sp.x + 4.5f, sp.y + 3.5f}, {sp.x - 4.5f, sp.y + 3.5f}};
                dl->AddTriangleFilled(tri[0], tri[1], tri[2], IM_COL32(236, 132, 68, 245));
                dl->AddTriangle(tri[0], tri[1], tri[2], IM_COL32(18, 20, 22, 220), 1.3f);
            }
        }
    }
}

} // namespace roadweaver

