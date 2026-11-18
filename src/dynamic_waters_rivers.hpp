#pragma once
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Adapted from Dynamic Waters 13.1.1 (AdamNew, MPL-2.0): MainRivers, RiverShaper, and
// TerraFirmaCraft-derived river graph classes (RiverContext, RiverFlow, RiverGraphEdge,
// RiverInstance, RiverMathUtils, RiverNetworkBuilder, RiverPartition, RiverPathFractal, FastConcurrentCache).
// Original TFC components Copyright (c) TerraFirmaCraft team, EUPL v1.2
// (https://github.com/TerraFirmaCraft/TerraFirmaCraft); modified for Dynamic Waters by AdamNew (2026-07-13)
// and ported to C++ for FTF Surface Lab (2026-11). Licensed under MPL-2.0 / compatible EUPL v1.2;
// see LICENSES/DynamicWaters-MPL-2.0.txt, DynamicWaters-EUPL-1.2.txt, and DynamicWaters-THIRD-PARTY-NOTICES.txt.
//
// Dynamic Waters 13.1.1 Lowland River Network Engine:
// Exact port of MainRivers, RiverNetworkBuilder, RiverShaper, RiverInstance,
// RiverPathFractal, RiverGraphEdge, RiverPartition, RiverFlow, RiverMathUtils.
#include "dynamic_waters_config.hpp"
#include "dynamic_waters_noise.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dw {

constexpr double NORMAL_RIVER_WIDTH_SCALE = 1.2;
constexpr double NORMAL_RIVER_WIDTH_EXTRA = 2.0;

inline double normalRiverWaterRadius(double networkWidth) {
    return networkWidth * NORMAL_RIVER_WIDTH_SCALE + NORMAL_RIVER_WIDTH_EXTRA;
}

inline double projectAlongLine(double vx, double vy, double wx, double wy, double px, double py) {
    double dx = vx - wx, dy = vy - wy;
    double l2 = dx * dx + dy * dy;
    if (l2 == 0.0) return 0.0;
    return mthClamp(((px - vx) * (wx - vx) + (py - vy) * (wy - vy)) / l2, 0.0, 1.0);
}

inline double distancePointToLineSq(double vx, double vy, double wx, double wy, double px, double py) {
    double t = projectAlongLine(vx, vy, wx, wy, px, py);
    double x0 = vx + t * (wx - vx);
    double y0 = vy + t * (wy - vy);
    double dx = x0 - px, dy = y0 - py;
    return dx * dx + dy * dy;
}

inline double normInf(double x, double y) { return std::max(std::abs(x), std::abs(y)); }

enum class RiverFlowDir : int {
    EEE = 0, NEE, N_E, NNE, NNN, NNW, N_W, NWW,
    WWW, SWW, S_W, SSW, SSS, SSE, S_E, SEE, NONE = 16
};

inline void riverFlowVec(RiverFlowDir dir, double& outX, double& outZ) {
    if (dir == RiverFlowDir::NONE || int(dir) < 0 || int(dir) >= 16) {
        outX = 0.0;
        outZ = 0.0;
        return;
    }
    static const float* sinLut = []() -> const float* {
        static float tbl[65536];
        constexpr double twoPiOver65536 = 3.14159265358979323846 * 2.0 / 65536.0;
        for (int i = 0; i < 65536; ++i) {
            tbl[i] = float(std::sin(double(i) * twoPiOver65536));
        }
        return tbl;
    }();
    float angle = float(int(dir)) * (1.0f / 8.0f) * 3.1415927f;
    float s = sinLut[int(angle * 10430.378f) & 65535];
    float c = sinLut[int(angle * 10430.378f + 16384.0f) & 65535];
    outX = double(c);
    outZ = double(-s);
}

inline RiverFlowDir flowFromAngle(double angle) {
    int ord = int(std::llround(8.0 * (angle / 3.14159265358979323846))); // Mth.PI float? Note Mth.PI is (float)Math.PI in Java!
    // Wait: RiverFlow.fromAngle uses 8 * (angle / Mth.PI) where Mth.PI = 3.1415927F!
    // We preserve exact float division in calculateFlow below.
    if (ord < 0) ord += 16;
    return static_cast<RiverFlowDir>((ord >= 0 && ord < 16) ? ord : 16);
}

struct RiverVertex {
    double x = 0.0;
    double y = 0.0;
    double angle = 0.0;
    double length = 0.0;
    int distance = 0;
    int uid = -1; // identity within RiverInstance::Builder
};

struct RiverPathFractal {
    std::vector<double> segments;
    double norm = 0.0;
    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;

    static RiverPathFractal createFractal(mc::Xoroshiro& random, int bisections, double sx, double sy, double dx, double dy);
    static RiverPathFractal ofPoints(std::vector<double> pts);

    void computeBounds();
    double boxDistanceSq(double x, double y) const;
    double intersectDistance(double x, double y) const;
    double nearestPoint(double x, double y, double& outX, double& outZ) const;
    double nearestAlong(double x, double y, double out[6]) const;
    RiverFlowDir calculateFlow(double x, double y) const;
    void calculateContinuousFlow(double x, double y, double& vx, double& vz) const;
};

struct RiverGraphEdge {
    static constexpr int MIN_WIDTH = 9;
    static constexpr int MAX_WIDTH = 20;
    static constexpr int WIDTH_STEP = (MAX_WIDTH - MIN_WIDTH + 3) / 4; // 3
    static constexpr int MAX_AFFECTING_BLOCK_DISTANCE = 1 + 270 + 60;  // 1 + ceil(1.5*180) + 60 = 331

    RiverVertex source;
    RiverVertex drain;
    RiverPathFractal fractal;
    int width = MIN_WIDTH;
    int cachedDrainWidth = MIN_WIDTH;
    bool sourceEdge = false;
    int drainEdgeIdx = -1; // index inside region's edge list (-1 if mouth)

    bool delta = false;
    bool deltaMain = false;
    double flareRadius = 0.0;
    double flareLength = 0.0;
    double tipLength = 0.0;

    int minPartX = 0, minPartZ = 0, maxPartX = 0, maxPartZ = 0;

    void initBoundsFromCenter(double cx, double cz);
    double widthAt(double exactX, double exactZ, const std::vector<RiverGraphEdge>& regionEdges) const;
    double widthSq(double exactX, double exactZ, const std::vector<RiverGraphEdge>& regionEdges) const;
    int drainWidth(const std::vector<RiverGraphEdge>& regionEdges) const;
    static double deltaWaterRadius(int w) { return 1.5 + double(w); }
    double deltaRadius(double exactX, double exactZ) const;
    double maxWaterRadius(const std::vector<RiverGraphEdge>& regionEdges) const;
};

struct RiverContext {
    bool valid = false;
    const std::vector<RiverGraphEdge>* regionEdges = nullptr;
    int edgeIdx = -1;
    RiverFlowDir flow = RiverFlowDir::NONE;
    double flowVecX = 0.0;
    double flowVecZ = 0.0;
    double distSq = 0.0;
    double widthSq = 0.0;

    const RiverGraphEdge* edge() const {
        return (valid && regionEdges && edgeIdx >= 0) ? &(*regionEdges)[edgeIdx] : nullptr;
    }
    double normDistSq() const { return widthSq > 0.0 ? distSq / widthSq : 1.0e9; }
};

struct TerrainQueryCallbacks {
    int seaLevel = 63;
    std::function<bool(int bx, int bz)> isOceanAt;
    std::function<int(int cellX, int cellZ)> coarseHeightCell; // 8-block cell coords
};

class MainRiversEngine {
public:
    static constexpr int REGION_SIZE = 2048;
    static constexpr int PARTITION_CELL = 256;
    static constexpr int QUERY_RADIUS = 2; // ceil((180*20)/2048)

    void reset(int64_t worldSeed, const DynamicWatersConfig& cfg, TerrainQueryCallbacks cb);
    void setCallbacks(TerrainQueryCallbacks cb);
    void prewarm(int minBlockX, int minBlockZ, int maxBlockX, int maxBlockZ);

    // Ensures all regions in [homeRx-2..homeRx+2] x [homeRz-2..homeRz+2] are built, then returns nearest context.
    RiverContext findNearest(double x, double z);
    // Returns all edges in overlapping partition cells within [blockX-256..blockX+256].
    void collectNearbyEdges(double x, double z, std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>>& out);

    // Exposed for unit testing / oracle comparison: returns region's final shaped edges.
    std::shared_ptr<const std::vector<RiverGraphEdge>> getRegion(int rx, int rz);

    // Internal region builder state (public for helper functions in dynamic_waters_rivers.cpp).
    struct RawRegion {
        int rx = 0, rz = 0;
        int originX = 0, originZ = 0, cols = 0, rows = 0;
        std::vector<uint8_t> land;
        std::vector<int> distanceToOcean;
        std::vector<RiverGraphEdge> edges;

        std::vector<std::vector<double>> courses;
        std::vector<int> widths;
        std::vector<double> middles;
        std::vector<double> boxes;
        std::vector<int> reaches;
        std::vector<double> ends;
        std::vector<int> drains;

        bool isLand(int bx, int bz) const;
        bool isShore(int bx, int bz) const;
        int distToOcean(int bx, int bz) const;
    };

    struct PartitionEntry {
        std::shared_ptr<const std::vector<RiverGraphEdge>> edges;
        int idx = 0;
    };

private:
    static int64_t packRegion(int rx, int rz) {
        return (int64_t(uint32_t(rx))) | (int64_t(uint32_t(rz)) << 32);
    }
    static int blockToCell(int block) { return block >> 8; }

    std::shared_ptr<RawRegion> growRegion(int rx, int rz);
    std::shared_ptr<RawRegion> getOrGrow(int rx, int rz);
    std::vector<RiverGraphEdge> generateRegion(int rx, int rz, const RawRegion& own, const RawRegion* around[8]);
    uint64_t fillNearbyPartitionCells(
        int cx0, int cz0,
        std::array<std::shared_ptr<const std::vector<PartitionEntry>>, 9>& outCells,
        std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>>& outEdges);

    int64_t seed_ = 0;
    DynamicWatersConfig cfg_{};
    TerrainQueryCallbacks cb_{};

    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_set<int64_t> growingRegions_;
    std::unordered_set<int64_t> buildingRegions_;
    std::atomic<uint64_t> resetEpoch_{1};
    std::atomic<uint64_t> epoch_{1};
    std::unordered_map<int64_t, std::shared_ptr<RawRegion>> grown_;
    std::unordered_map<int64_t, std::shared_ptr<const std::vector<RiverGraphEdge>>> regions_;
    std::unordered_map<int64_t, std::shared_ptr<const std::vector<PartitionEntry>>> partition_;
};

} // namespace dw
