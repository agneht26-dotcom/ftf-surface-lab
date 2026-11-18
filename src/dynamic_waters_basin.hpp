#pragma once
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Adapted from Dynamic Waters 13.1.1 (AdamNew, MPL-2.0), ported to C++ for FTF Surface Lab (2026-11).
// Includes mountain region/river scheduling adapted from Dynamic Waters Async Rivers
// (https://github.com/erutobusiness/dynamicwaters-asyncrivers, Copyright (c) 2026 erutobusiness, MIT License).
// See LICENSES/DynamicWaters-MPL-2.0.txt and LICENSES/DynamicWaters-THIRD-PARTY-NOTICES.txt.
//
// Dynamic Waters 13.1.1 Mountain Watersheds, Basin Routing, Lakes, Channels & Shared Lowland Forms:
// Exact port of BasinTile, WatershedPath, WatershedLake, ChannelForm, CoarseHeightField cache,
// and shared gorge / lowland route / river-radius math from HydrologyManager.
#include "dynamic_waters_config.hpp"
#include "dynamic_waters_noise.hpp"
#include "dynamic_waters_rivers.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma push_macro("near")
#pragma push_macro("far")
#pragma push_macro("min")
#pragma push_macro("max")
#undef near
#undef far
#undef min
#undef max

namespace dw {

inline int64_t dwRound(double v) { return int64_t(std::floor(v + 0.5)); }
inline int32_t dwRoundF(float v) { return int32_t(std::floor(v + 0.5f)); }

inline double dwSmoothstep(double t) {
    double c = mthClamp(t, 0.0, 1.0);
    return c * c * (3.0 - 2.0 * c);
}

inline double dwClampedLerp(double a, double b, double t) {
    if (t <= 0.0) return a;
    if (t >= 1.0) return b;
    return a + t * (b - a);
}

inline double dwClampedMap(double v, double inMin, double inMax, double outMin, double outMax) {
    double t = (inMax != inMin) ? (v - inMin) / (inMax - inMin) : 0.0;
    return dwClampedLerp(outMin, outMax, t);
}

inline double dwWrapDegrees(double deg) {
    double d = std::fmod(deg, 360.0);
    if (d >= 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

inline double dwSignum(double v) {
    return (v > 0.0) ? 1.0 : ((v < 0.0) ? -1.0 : 0.0);
}

// ---------- ChannelForm ----------
struct ChannelForm {
    static constexpr double BEND_SKEW = 0.18;
    static constexpr double BEND_SCOUR = 1.0;
    static constexpr double BAR_SKEW = 0.08;
    static constexpr double BAR_FROM = 0.3;
    static constexpr double SLOPE_WIDTH = 0.15;
    static constexpr double STEEP_SLOPE = 0.12;
    static constexpr double PINCH_WIDTH = 0.09;

    static double bendSkew(double curvature, double halfWidth) {
        double b = std::abs(curvature) * halfWidth;
        if (b <= 0.0 || b >= 1.0) return 0.0;
        return -dwSignum(curvature) * BEND_SKEW * std::sin(3.14159265358979323846 * b);
    }

    static double bedDepth(double side, double skew) {
        double s = mthClamp(side, -1.0, 1.0);
        double u = (s - skew) / (1.0 - skew * s);
        return (1.0 + BEND_SCOUR * std::abs(skew)) * (1.0 - u * u);
    }

    static bool onBar(double side, double skew) {
        return std::abs(skew) >= BAR_SKEW && side * dwSignum(skew) < -BAR_FROM;
    }

    static double slopeWidth(double slope) {
        return dwClampedLerp(1.0 + SLOPE_WIDTH, 1.0 - SLOPE_WIDTH, slope / STEEP_SLOPE);
    }

    static double pinchWidth(double pinch) {
        return 1.0 + PINCH_WIDTH * pinch;
    }

    static double bankRise(double d, double h, double reach, double talus) {
        if (d <= 0.0) return 0.0;
        double t = std::min(1.0, d / reach);
        double rest = std::max(0.0, h - reach * talus);
        return d * talus + (talus == 1.0 ? rest * t * t * t : rest * std::pow(t, 2.0 + std::sqrt(talus)));
    }

    static double leveeFall(double d, double depth, double reach, double talus) {
        if (d <= 0.0) return 0.0;
        double t = std::min(1.0, d / reach);
        return d * talus + std::max(0.0, depth - reach * talus) * t * t;
    }
};

// ---------- Shared Hydrology Constants & Lowland / Gorge Math ----------
namespace hydrology_const {

constexpr int WATERSHED_REGION_SIZE = 256;
constexpr double HEAD_TAPER = 100.0;
constexpr double HEAD_WATER_FROM = 20.0;
constexpr double HEAD_TIP_POWER = 1.6;
constexpr double BANK_MEET_PER_WIDTH = 1.5;
constexpr double BANK_MEET_MIN = 5.0;
constexpr double BANK_TALUS = 1.0;
constexpr double DELTA_PLAIN = 24.0;
constexpr double DELTA_PLAIN_SLOPE = 0.15;
constexpr double GORGE_FROM = 76.0;
constexpr double GORGE_RAMP = 10.0;
constexpr double GORGE_GRADE_SLOT = 5.0;
constexpr double GORGE_GRADE_TERRACED = 2.2;
constexpr double GORGE_BENCH_RISE = 20.0;
constexpr double GORGE_BENCH_MAX = 9.0;
constexpr double GORGE_BENCH_WARP = 6.0;
constexpr int GORGE_BAND_MIN = 3;
constexpr int GORGE_BAND_MAX = 8;
constexpr double GORGE_WARP = 3.0;
constexpr double GORGE_STEEP_RUN = 0.4;
constexpr double GORGE_ROUGH = 1.2;
constexpr double GORGE_STRIP_MAX = 3.0;
constexpr double GORGE_REACH = 48.0;
constexpr double LOWLAND_CORRIDOR_REACH = GORGE_REACH + GORGE_ROUGH + 1.0;
constexpr double LOWLAND_FALL_MIN = 6.0;

inline double bankMeet(double halfWidth) {
    return std::max(BANK_MEET_MIN, BANK_MEET_PER_WIDTH * halfWidth);
}

inline double normalRiverSkinRadius(double networkWidth) {
    double r = normalRiverWaterRadius(networkWidth);
    return r + bankMeet(r) + 0.15 * r + 1.0;
}

inline double waterRadiusAt(const RiverGraphEdge& edge, double x, double z, const std::vector<RiverGraphEdge>& regionEdges) {
    return edge.delta ? edge.deltaRadius(x, z) : normalRiverWaterRadius(edge.widthAt(x, z, regionEdges));
}

inline double routeWaterRadius(const RiverGraphEdge& edge, double x, double z,
                               const std::vector<RiverGraphEdge>& regionEdges, const DynamicWatersConfig& cfg) {
    double r = waterRadiusAt(edge, x, z, regionEdges);
    if (edge.sourceEdge || edge.delta) return r;
    double dist = std::hypot(x - edge.source.x, z - edge.source.y);
    if (dist >= HEAD_TAPER) return r;
    double taper = std::max(0.0, (dist - HEAD_WATER_FROM) / (HEAD_TAPER - HEAD_WATER_FROM));
    return r * (cfg.normalRiverTunnels ? taper : std::pow(taper, HEAD_TIP_POWER));
}

inline bool inHeadTaper(const RiverGraphEdge& edge, double x, double z) {
    return !edge.sourceEdge && !edge.delta && std::hypot(x - edge.source.x, z - edge.source.y) < HEAD_TAPER;
}

inline double getRiverJitter(double x, double z) {
    double jitter = std::sin(x * 0.04 + z * 0.02) * 0.8;
    jitter += std::sin(x * -0.02 + z * 0.04) * 0.6;
    return jitter;
}

inline uint64_t gorgeHash(int64_t worldSeed, int k, uint64_t salt) {
    uint64_t h = (uint64_t(worldSeed) ^ salt) + uint64_t(k + 1) * 0x9E3779B97F4A7C15ull;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    return h ^ (h >> 31);
}

inline double gorgeShare(double riverGround) {
    double t = mthClamp((riverGround - GORGE_FROM) / GORGE_RAMP, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

struct GorgeProfile {
    static constexpr int MAX_BENCHES = 16;
    double floor = 1.0, floorRise = 1.0, grade = 1.0, rough = 0.0;
    int benches = 0;
    std::array<double, MAX_BENCHES> benchH{}, benchW{}, benchT{};
    bool lastSteep = false;

    double setback(double h) const {
        if (h <= 0.0) return 0.0;
        if (h <= floorRise) return floor * h / floorRise;
        double d = floor + (h - floorRise) / grade;
        for (int b = 0; b < benches; ++b) {
            double t = mthClamp((h - benchH[b]) / benchT[b] + 0.5, 0.0, 1.0);
            d += benchW[b] * t * t * (3.0 - 2.0 * t);
        }
        return d;
    }
};

inline int gorgeWallY(int64_t worldSeed, const SimplexNoise& bedNoise, double x, double z,
                      double dOut, int originalY, int waterY, double depth, bool* outSteep = nullptr) {
    thread_local GorgeProfile p;
    p.lastSteep = false;
    double style = mthClamp(0.65 + 0.9 * bedNoise.getValue(x * 0.004 + 1234.5, z * 0.004 - 987.6), 0.0, 1.0);
    p.rough = GORGE_ROUGH * (0.7 * bedNoise.getValue(x * 0.12 + 300.0, z * 0.12 - 300.0)
            + 0.3 * bedNoise.getValue(x * 0.33 - 500.0, z * 0.33 + 500.0));
    double d = dOut + p.rough;
    if (d >= GORGE_REACH) {
        if (outSteep) *outSteep = false;
        return originalY;
    }

    p.floor = std::max(1.0, GORGE_STRIP_MAX
            * mthClamp(-0.1 + style + 0.4 * bedNoise.getValue(x * 0.05 - 40.0, z * 0.05 + 40.0), 0.0, 1.0));
    p.floorRise = 1.0;

    p.benches = 0;
    double benchSum = 0.0, rise = 0.0;
    for (int b = 0; b < GorgeProfile::MAX_BENCHES; ++b) {
        rise += GORGE_BENCH_RISE * (0.7 + 0.6 * (double(gorgeHash(worldSeed, b, 0x2545F4914F6CDD1Dull) >> 11) * 0x1.0p-53));
        double h = rise + GORGE_BENCH_WARP * bedNoise.getValue(x * 0.015 + b * 41.3, z * 0.015 - b * 23.9);
        if (h >= depth) break;
        double w = mthClamp(style * (1.5 + 6.0 * (0.5 + 0.5
                * bedNoise.getValue(x * 0.025 - b * 17.7, z * 0.025 + b * 29.1))), 0.0, GORGE_BENCH_MAX);
        p.benchH[p.benches] = std::max(p.floorRise + 2.0, h);
        p.benchW[p.benches] = w;
        p.benchT[p.benches] = 2.5 + 2.5 * style;
        p.benches++;
        benchSum += w;
    }

    double room = GORGE_REACH - 4.0 - p.floor;
    if (benchSum > 0.5 * room) {
        double k = 0.5 * room / benchSum;
        for (int b = 0; b < p.benches; ++b) p.benchW[b] *= k;
        benchSum *= k;
    }
    p.grade = std::max(mthLerp(style, GORGE_GRADE_SLOT, GORGE_GRADE_TERRACED),
            (depth - p.floorRise) / std::max(4.0, room - benchSum));

    int hMax = originalY - waterY;
    int lo = 0, hi = std::max(0, hMax);
    while (lo < hi) {
        int mid = (lo + hi + 1) >> 1;
        if (p.setback(mid) <= d) lo = mid;
        else hi = mid - 1;
    }
    int h = lo;
    if (h >= hMax) {
        if (outSteep) *outSteep = false;
        return originalY;
    }
    p.lastSteep = (p.setback(h + 1) - p.setback(h)) < GORGE_STEEP_RUN;
    if (outSteep) *outSteep = p.lastSteep;
    return waterY + h;
}

inline double lowlandRouteGround(int64_t worldSeed, const SimplexNoise& bedNoise, double x, double z,
                                 double ground, double dOut, double waterRadius, double riverGround,
                                 int seaLevel, bool delta) {
    double base = seaLevel - 1;
    if (dOut <= 0.0 || ground <= base) return ground;
    double bank = std::min(ground, base + ChannelForm::bankRise(dOut, ground - base,
            delta ? DELTA_PLAIN : bankMeet(waterRadius), delta ? DELTA_PLAIN_SLOPE : BANK_TALUS));
    double gorge = gorgeShare(riverGround);
    if (gorge < 0.5 || delta) return bank;
    if (dOut >= GORGE_REACH + GORGE_ROUGH) return ground;

    double depth = std::max(8.0, (riverGround - base) * 1.1 + 6.0);
    int wallY = gorgeWallY(worldSeed, bedNoise, x, z, dOut, mthFloor(ground), int(base), depth, nullptr);
    return bank + (std::min(double(wallY), ground) - bank) * gorge;
}

} // namespace hydrology_const

// ---------- WatershedLake ----------
struct WatershedLake {
    static constexpr int CELL_SHIFT = 3;
    static constexpr int CELL_SIZE = 1 << CELL_SHIFT; // 8
    static constexpr int FLOOR_SLACK = 4;
    static constexpr int SHAVE = 3;
    static constexpr double SHORE_BAND = 3.5;
    static constexpr int SHORE_REACH = 14;
    static constexpr int WINDOW_MARGIN = SHORE_REACH + 1;
    static constexpr int WINDOW = 16 + 2 * WINDOW_MARGIN; // 46
    static constexpr double WOBBLE = 8.0;
    static constexpr double WOBBLE_WAVE = 20.0;
    static constexpr double SLOPE_SPAN = 4.0;
    static constexpr double GRADE_RUN = 6.0;
    static constexpr double GRADE_LEAD = 2.0;
    static constexpr double GRADE_SPREAD = 10.0;
    static constexpr double GRADE_WAVE = 16.0;

    int waterY = 0;
    int minCellX = 0, minCellZ = 0, cellsW = 0, cellsH = 0;
    std::vector<uint64_t> bits;
    std::vector<int16_t> coarseH;
    double minX = 0.0, maxX = 0.0, minZ = 0.0, maxZ = 0.0;
    int dig = SHAVE;
    mutable int cachedFloorY = INT32_MIN;
    uint64_t uniqueId = 0;

    void init(int waterY_, int minCellX_, int minCellZ_, int cellsW_, int cellsH_,
              std::vector<uint64_t> bits_, std::vector<int16_t> coarseH_, int dig_, uint64_t uid) {
        waterY = waterY_;
        minCellX = minCellX_;
        minCellZ = minCellZ_;
        cellsW = cellsW_;
        cellsH = cellsH_;
        bits = std::move(bits_);
        coarseH = std::move(coarseH_);
        dig = dig_;
        uniqueId = uid;
        cachedFloorY = INT32_MIN;
        minX = double(minCellX - 1) * CELL_SIZE;
        maxX = double(minCellX + cellsW + 1) * CELL_SIZE;
        minZ = double(minCellZ - 1) * CELL_SIZE;
        maxZ = double(minCellZ + cellsH + 1) * CELL_SIZE;
        floorY();
    }

    static std::vector<uint64_t> newBits(int w, int h) { return std::vector<uint64_t>((w * h + 63) >> 6, 0ull); }
    static void setBit(std::vector<uint64_t>& b, int w, int lx, int lz) {
        int i = lz * w + lx;
        b[i >> 6] |= (1ull << (i & 63));
    }

    bool bit(int lx, int lz) const {
        if (lx < 0 || lz < 0 || lx >= cellsW || lz >= cellsH) return false;
        int i = lz * cellsW + lx;
        return (bits[i >> 6] & (1ull << (i & 63))) != 0;
    }

    int floorY() const;
    bool holdsColumn(int columnY) const { return columnY <= waterY && columnY >= floorY() - FLOOR_SLACK; }
    bool bedColumn(int columnY) const { return columnY <= waterY + dig && columnY >= floorY() - FLOOR_SLACK; }

    double floorSurface(double blockX, double blockZ) const;
    double shoreHeight(double blockX, double blockZ) const;
    double shoreGrade(int x, int z) const;
    bool covers(double blockX, double blockZ) const;
    bool near(double blockX, double blockZ, int cells) const;
    bool inside(double blockX, double blockZ) const;
    bool touchesInside(double blockX, double blockZ) const;
    bool shoreBand(double blockX, double blockZ) const;
    bool isLakeCell(int cellX, int cellZ) const { return bit(cellX - minCellX, cellZ - minCellZ); }
    double shoreDistance(int x, int z) const;
    double waterDistance(int x, int z) const;
    int cellCount() const;

private:
    double gradientNoise(double x, double z, int salt) const;
    double latticeSlope(int ix, int iz, int salt, double dx, double dz) const;
};

// ---------- WatershedPath ----------
struct WatershedPath {
    static constexpr double NODE_SPACING = 3.0;
    static constexpr double MAX_INFLUENCE = 70.0;
    static constexpr int SHEER_DROP = 3;
    static constexpr int CLIFF_DROP = 6;
    static constexpr double FALL_FACE_T = 0.8;
    static constexpr double FALL_BANK_BLOCKS = 4.0;
    static constexpr double FALL_BANK_MAX = 10.0;

    struct RiverNodeData {
        double x = 0.0, z = 0.0;
        int y = 0, terrainY = 0;
        double width = 0.0, distance = std::numeric_limits<double>::max();
        double flowDirX = 0.0, flowDirZ = 0.0;
        int nodeIndex = 0;
        double lateral = 0.0, curvature = 0.0, slope = 0.0;
        int bankFloor = INT32_MIN;
        bool beyondEnd = false;

        RiverNodeData atLevel(int ny) const {
            RiverNodeData copy = *this;
            copy.y = ny;
            return copy;
        }
    };

    static bool isFall(const std::vector<int>& ys, int i) {
        if (i < 0 || i + 1 >= int(ys.size())) return false;
        int rise = ys[i + 1] - ys[i];
        return rise >= CLIFF_DROP || (rise >= SHEER_DROP && i > 0 && ys[i] == ys[i - 1]);
    }
    static double fallWing(int low, int high) {
        return mthClamp(double(high - low), FALL_BANK_BLOCKS, FALL_BANK_MAX);
    }
    static int fallRamp(int low, int high, double below) {
        double s = 1.0 - mthClamp(below / fallWing(low, high), 0.0, 1.0);
        return low + int(dwRound(double(high - low) * s * s * (3.0 - 2.0 * s)));
    }

    std::vector<double> segmentsX;
    std::vector<double> segmentsZ;
    std::vector<int> segmentsY;
    std::vector<int> segmentsTerrainY;
    std::vector<double> segmentWidths;
    bool hitMainRiver = false;
    int nodeCount = 0;
    int validNodeCount = 0;
    double minX = 0.0, maxX = 0.0, minZ = 0.0, maxZ = 0.0;
    double queryMargin = 0.0;
    std::vector<double> curvature;
    std::vector<double> slope;
    std::vector<double> nodeFlowDirX;
    std::vector<double> nodeFlowDirZ;
    bool hasAnyFall = false;

    struct AtomicCacheI32 {
        mutable std::atomic<int32_t> val{-2};
        AtomicCacheI32() = default;
        explicit AtomicCacheI32(int32_t v) : val(v) {}
        AtomicCacheI32(const AtomicCacheI32& o) : val(o.val.load(std::memory_order_relaxed)) {}
        AtomicCacheI32& operator=(const AtomicCacheI32& o) {
            val.store(o.val.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return *this;
        }
        AtomicCacheI32& operator=(int32_t v) {
            val.store(v, std::memory_order_relaxed);
            return *this;
        }
        int32_t load(std::memory_order m = std::memory_order_acquire) const { return val.load(m); }
        void store(int32_t v, std::memory_order m = std::memory_order_release) const { val.store(v, m); }
    };
    struct AtomicCacheI8 {
        mutable std::atomic<int8_t> val{0};
        AtomicCacheI8() = default;
        explicit AtomicCacheI8(int8_t v) : val(v) {}
        AtomicCacheI8(const AtomicCacheI8& o) : val(o.val.load(std::memory_order_relaxed)) {}
        AtomicCacheI8& operator=(const AtomicCacheI8& o) {
            val.store(o.val.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return *this;
        }
        AtomicCacheI8& operator=(int8_t v) {
            val.store(v, std::memory_order_relaxed);
            return *this;
        }
        int8_t load(std::memory_order m = std::memory_order_acquire) const { return val.load(m); }
        void store(int8_t v, std::memory_order m = std::memory_order_release) const { val.store(v, m); }
    };
    struct SegChunk {
        double minX = 0.0, maxX = 0.0, minZ = 0.0, maxZ = 0.0;
    };
    std::vector<SegChunk> segChunks;

    // Optional hydrology metadata populated lazily by HydrologyEngine (Module 4D)
    AtomicCacheI32 ravineIdx; // -2 uncomputed, -1 none, >=0 index into HydrologyEngine ravine store
    AtomicCacheI32 feedsIdx;
    AtomicCacheI32 belowIdx;
    AtomicCacheI8 gorgeEntry;
    AtomicCacheI8 leftDry;
    AtomicCacheI8 deltaFed;

    void init(int nodeCount_, int validNodeCount_, bool hitMainRiver_,
              std::vector<double> xs, std::vector<double> zs,
              std::vector<int> ys, std::vector<int> tys, std::vector<double> ws);

    double influenceMinX() const { return minX - queryMargin; }
    double influenceMaxX() const { return maxX + queryMargin; }
    double influenceMinZ() const { return minZ - queryMargin; }
    double influenceMaxZ() const { return maxZ + queryMargin; }

    RiverNodeData getClosestNodeData(double x, double z) const;
    RiverNodeData getClosestNodeDataNear(double x, double z, int node, int span) const;

private:
    RiverNodeData closest(double x, double z, int lo, int hi) const;
};

// ---------- CoarseHeightCache + BasinTileEngine ----------
struct BasinCallbacks {
    int seaLevel = 63;
    std::function<int(int cellX, int cellZ)> sampleCoarseCell; // OCEAN_FLOOR_WG at (cellX*8, cellZ*8)
    std::function<int(int blockX, int blockZ)> sampleSurfaceAt; // OCEAN_FLOOR_WG at (blockX, blockZ)
    std::function<bool(int blockX, int blockZ)> isMouthBiomeAt;
};

class CoarseHeightCache {
public:
    static constexpr int CELL_SHIFT = 3;
    static constexpr int CELL_SIZE = 1 << CELL_SHIFT; // 8
    static constexpr int BLOCK_SHIFT = 4;
    static constexpr int BLOCK_CELLS = 1 << BLOCK_SHIFT; // 16
    static constexpr int BLOCK_MASK = BLOCK_CELLS - 1;
    static constexpr int BLOCK_AREA = BLOCK_CELLS * BLOCK_CELLS; // 256

    void reset(std::function<int(int, int)> sampleCellFn, std::function<int(int, int)> sampleSurfaceFn, int seaLevel);
    void setCallbacks(std::function<int(int, int)> sampleCellFn, std::function<int(int, int)> sampleSurfaceFn, int seaLevel);
    int sampleCell(int cellX, int cellZ);
    int surfaceAt(int blockX, int blockZ);
    void fillWindow(int cx0, int cz0, int w, int h, float* out);
    double coarseGroundAt(double x, double z);

private:
    static int64_t blockKey(int bx, int bz) {
        return (int64_t(uint32_t(bx))) | (int64_t(uint32_t(bz)) << 32);
    }
    const std::array<int16_t, BLOCK_AREA>* getBlockPtr(int bx, int bz);
    std::shared_ptr<const std::array<int16_t, BLOCK_AREA>> getBlock(int bx, int bz);

    int seaLevel_ = 63;
    std::function<int(int, int)> sampleCellFn_;
    std::function<int(int, int)> sampleSurfaceFn_;
    std::mutex mutex_;
    std::atomic<uint64_t> epoch_{1};
    std::unordered_map<int64_t, std::shared_ptr<const std::array<int16_t, BLOCK_AREA>>> blocks_;
};

// Per-256x256 watershed region spatial grid (ports HydrologyManager.RegionGrid / WatershedRegionData)
struct RegionSpatialGrid {
    static constexpr int CELL_SIZE = 32;
    static constexpr int GRID_DIM = 256 / CELL_SIZE; // 8

    std::vector<WatershedPath> paths;
    std::vector<WatershedLake> lakes;
    std::vector<std::vector<int>> cells; // size GRID_DIM*GRID_DIM
    double pathsMinX = 1.0e30, pathsMaxX = -1.0e30, pathsMinZ = 1.0e30, pathsMaxZ = -1.0e30;
    double lakesMinX = 1.0e30, lakesMaxX = -1.0e30, lakesMinZ = 1.0e30, lakesMaxZ = -1.0e30;

    void build(int rx, int rz, std::vector<WatershedPath> inPaths, std::vector<WatershedLake> inLakes);
    const std::vector<int>& indicesAt(int colX, int colZ, int rx, int rz) const;
};

struct BasinTileData {
    static constexpr int TILE_SHIFT = 10;
    static constexpr int TILE_SIZE = 1 << TILE_SHIFT; // 1024
    static constexpr int REGION_SIZE = hydrology_const::WATERSHED_REGION_SIZE; // 256
    static constexpr int REGIONS_PER_SIDE = TILE_SIZE / REGION_SIZE; // 4

    int tileX = 0, tileZ = 0;
    // Always 16 subregions indexed locally (rz_off * 4 + rx_off)
    std::array<std::vector<WatershedPath>, 16> pathsByRegion;
    std::array<std::vector<WatershedLake>, 16> lakesByRegion;
};

class BasinTileEngine {
public:
    static int tileCoord(int regionCoord) { return floorDiv(regionCoord, BasinTileData::REGIONS_PER_SIDE); }
    static int64_t packKey(int x, int z) { return (int64_t(uint32_t(x))) | (int64_t(uint32_t(z)) << 32); }
    static int channelThreshold(DensityLevel level);
    static double plainDryness(const WorldNoises& noises, int bx, int bz);
    static double turnedDryness(double plain);
    static double labDryness(const WorldNoises& noises, int x, int z);

    void reset(int64_t worldSeed, const DynamicWatersConfig& cfg, const WorldNoises* noises,
               MainRiversEngine* mainRivers, BasinCallbacks cb);
    void setCallbacks(BasinCallbacks cb);
    void prewarm(int minBlockX, int minBlockZ, int maxBlockX, int maxBlockZ);

    struct RegionNeighborhood3x3 {
        const BasinTileEngine* owner = nullptr;
        uint64_t epoch = 0;
        int homeRx = INT32_MIN;
        int homeRz = INT32_MIN;
        std::array<std::shared_ptr<const RegionSpatialGrid>, 9> refs{};
        std::array<const RegionSpatialGrid*, 9> grids{};
        bool anyPaths = false;
        bool anyLakes = false;
        double pathsMinX = 1.0e30, pathsMaxX = -1.0e30, pathsMinZ = 1.0e30, pathsMaxZ = -1.0e30;
        double lakesMinX = 1.0e30, lakesMaxX = -1.0e30, lakesMinZ = 1.0e30, lakesMaxZ = -1.0e30;
    };

    std::shared_ptr<const RegionSpatialGrid> getRegion(int rx, int rz);
    const RegionNeighborhood3x3& getNeighborhood3x3(int homeRx, int homeRz);
    CoarseHeightCache& heights() { return heights_; }
    uint64_t currentEpoch() const { return epoch_.load(std::memory_order_acquire); }

    struct SeamExit {
        int fromX = 0, fromZ = 0, cellX = 0, cellZ = 0;
        int acc = 0, wet = 0, flowLen = 0;
        bool channel = false;
        float level = 0.0f;
    };
    struct TileSeams {
        std::vector<SeamExit> exits;
        std::vector<int16_t> exitOf;
        std::array<bool, 4> taken{};
    };

private:
    std::shared_ptr<const TileSeams> getSeams(int tx, int tz);
    void ensureTileBuilt(int tx, int tz);

    int64_t seed_ = 0;
    DynamicWatersConfig cfg_{};
    const WorldNoises* noises_ = nullptr;
    MainRiversEngine* mainRivers_ = nullptr;
    BasinCallbacks cb_{};
    CoarseHeightCache heights_;

    std::mutex cacheMutex_;
    std::condition_variable cacheCv_;
    std::unordered_set<int64_t> buildingSeams_;
    std::unordered_set<int64_t> buildingTiles_;
    std::atomic<uint64_t> epoch_{1};
    std::unordered_map<int64_t, std::shared_ptr<const TileSeams>> seamCache_;
    std::unordered_map<int64_t, std::shared_ptr<const RegionSpatialGrid>> regionCache_;
    uint64_t nextLakeId_ = 1;
};

} // namespace dw

#pragma pop_macro("max")
#pragma pop_macro("min")
#pragma pop_macro("far")
#pragma pop_macro("near")
