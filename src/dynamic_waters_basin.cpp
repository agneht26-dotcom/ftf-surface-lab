#include "dynamic_waters_basin.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <thread>

namespace dw {

namespace {
constexpr double DW_PI = 3.14159265358979323846;

const std::array<double, 16>& lakeGradX() {
    static const std::array<double, 16> gx = []() {
        std::array<double, 16> a{};
        for (int k = 0; k < 16; ++k) a[k] = std::cos(k * DW_PI / 8.0);
        return a;
    }();
    return gx;
}

const std::array<double, 16>& lakeGradZ() {
    static const std::array<double, 16> gz = []() {
        std::array<double, 16> a{};
        for (int k = 0; k < 16; ++k) a[k] = std::sin(k * DW_PI / 8.0);
        return a;
    }();
    return gz;
}

inline double lakeFade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

struct ShoreWindow {
    uint64_t lakeUid = 0;
    int32_t chunkX = INT32_MIN, chunkZ = INT32_MIN;
    std::array<uint8_t, WatershedLake::WINDOW * WatershedLake::WINDOW> in{};
    std::array<float, 16 * 16> dist{};
};

struct ShoreWindows {
    std::array<ShoreWindow, 3> slots;
    int next = 0;
};

const ShoreWindow& getShoreWindow(const WatershedLake& lake, int cx, int cz) {
    thread_local ShoreWindows ws;
    for (const auto& w : ws.slots) {
        if (w.lakeUid == lake.uniqueId && w.chunkX == cx && w.chunkZ == cz) return w;
    }
    ShoreWindow& w = ws.slots[ws.next];
    ws.next = (ws.next + 1) % int(ws.slots.size());
    int ox = (cx << 4) - WatershedLake::WINDOW_MARGIN;
    int oz = (cz << 4) - WatershedLake::WINDOW_MARGIN;
    for (int j = 0; j < WatershedLake::WINDOW; ++j) {
        for (int i = 0; i < WatershedLake::WINDOW; ++i) {
            w.in[j * WatershedLake::WINDOW + i] = lake.inside(ox + i, oz + j) ? 1 : 0;
        }
    }
    w.dist.fill(std::numeric_limits<float>::quiet_NaN());
    w.lakeUid = lake.uniqueId;
    w.chunkX = cx;
    w.chunkZ = cz;
    return w;
}

float acrossContour(ShoreWindow& w, int lx, int lz) {
    float d = w.dist[lz * 16 + lx];
    if (std::isnan(d)) {
        int ci = lx + WatershedLake::WINDOW_MARGIN;
        int cj = lz + WatershedLake::WINDOW_MARGIN;
        uint8_t own = w.in[cj * WatershedLake::WINDOW + ci];
        int best = INT32_MAX;
        for (int dj = -WatershedLake::WINDOW_MARGIN; dj <= WatershedLake::WINDOW_MARGIN; ++dj) {
            int dj2 = dj * dj;
            if (dj2 >= best) continue;
            int row = (cj + dj) * WatershedLake::WINDOW + ci;
            for (int di = -WatershedLake::WINDOW_MARGIN; di <= WatershedLake::WINDOW_MARGIN; ++di) {
                int d2 = di * di + dj2;
                if (d2 < best && w.in[row + di] != own) best = d2;
            }
        }
        d = (best == INT32_MAX) ? float(WatershedLake::SHORE_REACH)
                                : float(std::min(double(WatershedLake::SHORE_REACH), std::sqrt(double(best)) - 0.5));
        w.dist[lz * 16 + lx] = d;
    }
    return d;
}

} // namespace

// ---------- WatershedLake ----------
int WatershedLake::floorY() const {
    int f = cachedFloorY;
    if (f == INT32_MIN) {
        f = waterY;
        int gw = cellsW + 2;
        for (int lz = 0; lz < cellsH; ++lz) {
            for (int lx = 0; lx < cellsW; ++lx) {
                if (!bit(lx, lz)) continue;
                int h = coarseH[(lz + 1) * gw + (lx + 1)];
                if (h < f) f = h;
            }
        }
        cachedFloorY = f;
    }
    return f;
}

double WatershedLake::floorSurface(double blockX, double blockZ) const {
    int gw = cellsW + 2, gh = cellsH + 2;
    double u = blockX / CELL_SIZE - (minCellX - 1);
    double v = blockZ / CELL_SIZE - (minCellZ - 1);
    int i0 = mthClamp(mthFloor(u), 0, gw - 2);
    int j0 = mthClamp(mthFloor(v), 0, gh - 2);
    double fx = mthClamp(u - i0, 0.0, 1.0);
    double fz = mthClamp(v - j0, 0.0, 1.0);
    int b = j0 * gw + i0;
    double top = coarseH[b] * (1.0 - fx) + coarseH[b + 1] * fx;
    double bot = coarseH[b + gw] * (1.0 - fx) + coarseH[b + gw + 1] * fx;
    return top * (1.0 - fz) + bot * fz;
}

double WatershedLake::latticeSlope(int ix, int iz, int salt, double dx, double dz) const {
    uint64_t h = uint64_t(int64_t(minCellX)) * 0x2545F4914F6CDD1Dull
               ^ uint64_t(int64_t(minCellZ)) * 0x5851F42D4C957F2Dull
               ^ uint64_t(int64_t(ix)) * 0xD1B54A32D192ED03ull
               ^ uint64_t(int64_t(iz)) * 0xABC98388FB8FAC03ull
               ^ uint64_t(int64_t(salt)) * 0x8CB92BA72F3D8DD7ull;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    int k = int((h ^ (h >> 31)) >> 60);
    return lakeGradX()[k] * dx + lakeGradZ()[k] * dz;
}

double WatershedLake::gradientNoise(double x, double z, int salt) const {
    int ix = mthFloor(x), iz = mthFloor(z);
    double fx = x - ix, fz = z - iz;
    double a = latticeSlope(ix, iz, salt, fx, fz);
    double b = latticeSlope(ix + 1, iz, salt, fx - 1.0, fz);
    double c = latticeSlope(ix, iz + 1, salt, fx, fz - 1.0);
    double d = latticeSlope(ix + 1, iz + 1, salt, fx - 1.0, fz - 1.0);
    double u = lakeFade(fx), v = lakeFade(fz);
    double near = a + (b - a) * u;
    double far = c + (d - c) * u;
    return near + (far - near) * v;
}

double WatershedLake::shoreHeight(double blockX, double blockZ) const {
    double gx = floorSurface(blockX + SLOPE_SPAN, blockZ) - floorSurface(blockX - SLOPE_SPAN, blockZ);
    double gz = floorSurface(blockX, blockZ + SLOPE_SPAN) - floorSurface(blockX, blockZ - SLOPE_SPAN);
    double g = std::sqrt(gx * gx + gz * gz);
    if (g < 1.0e-6) return floorSurface(blockX, blockZ);
    double step = WOBBLE * gradientNoise(blockX / WOBBLE_WAVE, blockZ / WOBBLE_WAVE, 0) / g;
    return floorSurface(blockX - step * gx, blockZ - step * gz);
}

double WatershedLake::shoreGrade(int x, int z) const {
    double lead = GRADE_LEAD + GRADE_SPREAD * gradientNoise(double(x) / GRADE_WAVE, double(z) / GRADE_WAVE, 1);
    if (dig > SHAVE) lead = std::min(lead, GRADE_LEAD);
    double t = mthClamp((waterDistance(x, z) + lead) / GRADE_RUN, 0.0, 1.0);
    return 1.0 - std::cos(t * DW_PI / 2.0);
}

bool WatershedLake::covers(double blockX, double blockZ) const {
    if (blockX < minX || blockX >= maxX || blockZ < minZ || blockZ >= maxZ) return false;
    int lx = (mthFloor(blockX) >> CELL_SHIFT) - minCellX;
    int lz = (mthFloor(blockZ) >> CELL_SHIFT) - minCellZ;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (bit(lx + dx, lz + dz)) return true;
        }
    }
    return false;
}

bool WatershedLake::near(double blockX, double blockZ, int cells) const {
    double margin = double(cells - 1) * double(CELL_SIZE);
    if (blockX < minX - margin || blockX >= maxX + margin || blockZ < minZ - margin || blockZ >= maxZ + margin) {
        return false;
    }
    int lx = (mthFloor(blockX) >> CELL_SHIFT) - minCellX;
    int lz = (mthFloor(blockZ) >> CELL_SHIFT) - minCellZ;
    for (int dz = -cells; dz <= cells; ++dz) {
        for (int dx = -cells; dx <= cells; ++dx) {
            if (bit(lx + dx, lz + dz)) return true;
        }
    }
    return false;
}

bool WatershedLake::inside(double blockX, double blockZ) const {
    if (!covers(blockX, blockZ)) return false;
    return shoreHeight(blockX, blockZ) <= waterY + 0.5;
}

bool WatershedLake::touchesInside(double blockX, double blockZ) const {
    return inside(blockX + 1.0, blockZ) || inside(blockX - 1.0, blockZ)
        || inside(blockX, blockZ + 1.0) || inside(blockX, blockZ - 1.0);
}

bool WatershedLake::shoreBand(double blockX, double blockZ) const {
    return covers(blockX, blockZ) && shoreHeight(blockX, blockZ) <= waterY + SHORE_BAND;
}

double WatershedLake::shoreDistance(int x, int z) const {
    ShoreWindow& w = const_cast<ShoreWindow&>(getShoreWindow(*this, floorDiv(x, 16), floorDiv(z, 16)));
    int lx = x & 15, lz = z & 15;
    return w.in[(lz + WINDOW_MARGIN) * WINDOW + lx + WINDOW_MARGIN] ? acrossContour(w, lx, lz) : 0.0;
}

double WatershedLake::waterDistance(int x, int z) const {
    ShoreWindow& w = const_cast<ShoreWindow&>(getShoreWindow(*this, floorDiv(x, 16), floorDiv(z, 16)));
    int lx = x & 15, lz = z & 15;
    return w.in[(lz + WINDOW_MARGIN) * WINDOW + lx + WINDOW_MARGIN] ? 0.0 : acrossContour(w, lx, lz);
}

int WatershedLake::cellCount() const {
    int n = 0;
    for (uint64_t w : bits) {
#if defined(_MSC_VER)
        n += int(__popcnt64(w));
#else
        n += __builtin_popcountll(w);
#endif
    }
    return n;
}

// ---------- WatershedPath ----------
void WatershedPath::init(int nodeCount_, int validNodeCount_, bool hitMainRiver_,
                         std::vector<double> xs, std::vector<double> zs,
                         std::vector<int> ys, std::vector<int> tys, std::vector<double> ws) {
    nodeCount = nodeCount_;
    validNodeCount = validNodeCount_;
    hitMainRiver = hitMainRiver_;
    segmentsX = std::move(xs);
    segmentsZ = std::move(zs);
    segmentsY = std::move(ys);
    segmentsTerrainY = std::move(tys);
    segmentWidths = std::move(ws);
    ravineIdx = feedsIdx = belowIdx = -2;
    gorgeEntry = leftDry = deltaFed = 0;

    double txMin = std::numeric_limits<double>::max(), txMax = -std::numeric_limits<double>::max();
    double tzMin = std::numeric_limits<double>::max(), tzMax = -std::numeric_limits<double>::max();
    double maxWidth = 0.0;
    for (int i = 0; i < nodeCount; ++i) {
        if (segmentsX[i] < txMin) txMin = segmentsX[i];
        if (segmentsX[i] > txMax) txMax = segmentsX[i];
        if (segmentsZ[i] < tzMin) tzMin = segmentsZ[i];
        if (segmentsZ[i] > tzMax) tzMax = segmentsZ[i];
        if (segmentWidths[i] > maxWidth) maxWidth = segmentWidths[i];
    }
    minX = txMin;
    maxX = txMax;
    minZ = tzMin;
    maxZ = tzMax;
    queryMargin = maxWidth + MAX_INFLUENCE;

    curvature.assign(nodeCount, 0.0);
    slope.assign(nodeCount, 0.0);
    for (int j = 0; j < nodeCount; ++j) {
        int up = std::min(nodeCount - 1, j + 2), dn = std::max(0, j - 2);
        if (up == dn) continue;
        double ax = segmentsX[j] - segmentsX[up], az = segmentsZ[j] - segmentsZ[up];
        double bx = segmentsX[dn] - segmentsX[j], bz = segmentsZ[dn] - segmentsZ[j];
        double la = std::sqrt(ax * ax + az * az), lb = std::sqrt(bx * bx + bz * bz);
        if (la > 1.0e-6 && lb > 1.0e-6) {
            curvature[j] = std::atan2(ax * bz - az * bx, ax * bx + az * bz) / (0.5 * (la + lb));
        }
    if (la + lb > 1.0e-6) {
            slope[j] = std::max(0, segmentsY[up] - segmentsY[dn]) / (la + lb);
        }
    }

    hasAnyFall = false;
    for (int i = 0; i + 1 < nodeCount; ++i) {
        if (isFall(segmentsY, i)) { hasAnyFall = true; break; }
    }
    nodeFlowDirX.assign(nodeCount, 0.0);
    nodeFlowDirZ.assign(nodeCount, 0.0);
    for (int i = 0; i < nodeCount; ++i) {
        double fdx = 0.0, fdz = 0.0;
        if (i > 0) {
            fdx = segmentsX[i - 1] - segmentsX[i];
            fdz = segmentsZ[i - 1] - segmentsZ[i];
        } else if (validNodeCount > 1) {
            fdx = segmentsX[0] - segmentsX[1];
            fdz = segmentsZ[0] - segmentsZ[1];
        }
        double flen = std::sqrt(fdx * fdx + fdz * fdz);
        if (flen > 0.01) {
            nodeFlowDirX[i] = fdx / flen;
            nodeFlowDirZ[i] = fdz / flen;
        }
    }

    constexpr int SEG_CHUNK = 8;
    int segCount = std::max(0, validNodeCount - 1);
    int nChunks = (segCount + SEG_CHUNK - 1) / SEG_CHUNK;
    segChunks.resize(nChunks);
    for (int c = 0; c < nChunks; ++c) {
        int s0 = c * SEG_CHUNK;
        int s1 = std::min(segCount, s0 + SEG_CHUNK);
        double cxMin = std::numeric_limits<double>::max(), cxMax = -std::numeric_limits<double>::max();
        double czMin = std::numeric_limits<double>::max(), czMax = -std::numeric_limits<double>::max();
        for (int i = s0; i <= s1; ++i) {
            if (segmentsX[i] < cxMin) cxMin = segmentsX[i];
            if (segmentsX[i] > cxMax) cxMax = segmentsX[i];
            if (segmentsZ[i] < czMin) czMin = segmentsZ[i];
            if (segmentsZ[i] > czMax) czMax = segmentsZ[i];
        }
        segChunks[c] = { cxMin, cxMax, czMin, czMax };
    }
}

WatershedPath::RiverNodeData WatershedPath::getClosestNodeData(double x, double z) const {
    if (x < minX - queryMargin || x > maxX + queryMargin || z < minZ - queryMargin || z > maxZ + queryMargin || validNodeCount <= 0) {
        RiverNodeData d;
        d.x = x; d.z = z;
        return d;
    }
    return closest(x, z, 0, validNodeCount - 1);
}

WatershedPath::RiverNodeData WatershedPath::getClosestNodeDataNear(double x, double z, int node, int span) const {
    if (validNodeCount <= 0) {
        RiverNodeData d;
        d.x = x; d.z = z;
        return d;
    }
    int lo = mthClamp(node - span, 0, validNodeCount - 1);
    int hi = mthClamp(node + span, lo, validNodeCount - 1);
    return closest(x, z, lo, hi);
}

WatershedPath::RiverNodeData WatershedPath::closest(double x, double z, int lo, int hi) const {
    int searchCount = validNodeCount;
    double minDistSq = std::numeric_limits<double>::max();
    int bestIdx = lo;
    double bestT = 0.0, bestRawT = 0.0;
    bool bestIsPoint = false;

    if (hi - lo >= 16 && !segChunks.empty()) {
        constexpr int SEG_CHUNK = 8;
        int cLo = lo / SEG_CHUNK;
        int cHi = (hi - 1) / SEG_CHUNK;
        for (int c = cLo; c <= cHi; ++c) {
            const SegChunk& sc = segChunks[c];
            double bdx = (x < sc.minX) ? (sc.minX - x) : ((x > sc.maxX) ? (x - sc.maxX) : 0.0);
            double bdz = (z < sc.minZ) ? (sc.minZ - z) : ((z > sc.maxZ) ? (z - sc.maxZ) : 0.0);
            if (bdx * bdx + bdz * bdz > minDistSq) continue;
            int i0 = std::max(lo, c * SEG_CHUNK);
            int i1 = std::min(hi, (c + 1) * SEG_CHUNK);
            for (int i = i0; i < i1; ++i) {
                double sx = segmentsX[i], sz = segmentsZ[i];
                double dx = segmentsX[i + 1] - sx, dz = segmentsZ[i + 1] - sz;
                double len2 = dx * dx + dz * dz;
                double t = (len2 == 0.0) ? 0.0 : ((x - sx) * dx + (z - sz) * dz) / len2;
                double rawT = t;
                if (t < 0.0) t = 0.0;
                else if (t > 1.0) t = 1.0;
                double px = x - (sx + t * dx), pz = z - (sz + t * dz);
                double dSq = px * px + pz * pz;
                if (dSq < minDistSq) {
                    minDistSq = dSq;
                    bestIdx = i;
                    bestT = t;
                    bestRawT = rawT;
                }
            }
        }
    } else {
        for (int i = lo; i < hi; ++i) {
            double sx = segmentsX[i], sz = segmentsZ[i];
            double dx = segmentsX[i + 1] - sx, dz = segmentsZ[i + 1] - sz;
            double len2 = dx * dx + dz * dz;
            double t = (len2 == 0.0) ? 0.0 : ((x - sx) * dx + (z - sz) * dz) / len2;
            double rawT = t;
            if (t < 0.0) t = 0.0;
            else if (t > 1.0) t = 1.0;
            double px = x - (sx + t * dx), pz = z - (sz + t * dz);
            double dSq = px * px + pz * pz;
            if (dSq < minDistSq) {
                minDistSq = dSq;
                bestIdx = i;
                bestT = t;
                bestRawT = rawT;
            }
        }
    }
    {
        int last = hi;
        double px = x - segmentsX[last], pz = z - segmentsZ[last];
        double dSq = px * px + pz * pz;
        if (dSq < minDistSq) {
            minDistSq = dSq;
            bestIdx = last;
            bestIsPoint = true;
        }
    }

    double projY, projTerrainY, projWidth, projCurv, projSlope;
    double sideSign = 1.0;
    if (bestIsPoint || bestIdx >= searchCount - 1) {
        projY = segmentsY[bestIdx];
        projTerrainY = segmentsTerrainY[bestIdx];
        projWidth = segmentWidths[bestIdx];
        projCurv = curvature[bestIdx];
        projSlope = slope[bestIdx];
        if (bestIdx > 0) {
            double fx = segmentsX[bestIdx - 1] - segmentsX[bestIdx];
            double fz = segmentsZ[bestIdx - 1] - segmentsZ[bestIdx];
            sideSign = (fx * (z - segmentsZ[bestIdx]) - fz * (x - segmentsX[bestIdx]) >= 0.0) ? 1.0 : -1.0;
        }
    } else {
        int s = bestIdx;
        double fx = segmentsX[s] - segmentsX[s + 1], fz = segmentsZ[s] - segmentsZ[s + 1];
        double px = x - (segmentsX[s] - bestT * fx), pz = z - (segmentsZ[s] - bestT * fz);
        sideSign = (fx * pz - fz * px >= 0.0) ? 1.0 : -1.0;
        projCurv = curvature[s] + bestT * (curvature[s + 1] - curvature[s]);
        projSlope = slope[s] + bestT * (slope[s + 1] - slope[s]);
        int i = bestIdx;
        double t = bestT;
        int yDiff = segmentsY[i + 1] - segmentsY[i];
        if (isFall(segmentsY, i)) {
            if (t > FALL_FACE_T) {
                projY = segmentsY[i + 1];
                projTerrainY = segmentsTerrainY[i + 1];
            } else {
                projY = segmentsY[i];
                projTerrainY = segmentsTerrainY[i];
            }
        } else {
            projY = segmentsY[i] + t * yDiff;
            projTerrainY = segmentsTerrainY[i] + t * (segmentsTerrainY[i + 1] - segmentsTerrainY[i]);
        }
        projWidth = segmentWidths[i] + t * (segmentWidths[i + 1] - segmentWidths[i]);
    }

    double flowDirX = (bestIdx < int(nodeFlowDirX.size())) ? nodeFlowDirX[bestIdx] : 0.0;
    double flowDirZ = (bestIdx < int(nodeFlowDirZ.size())) ? nodeFlowDirZ[bestIdx] : 0.0;

    int bankFloor = INT32_MIN;
    if (hasAnyFall) {
        bool onSegment = !bestIsPoint && bestIdx < searchCount - 1;
        double along = (bestIdx + (onSegment ? bestT : 0.0)) * NODE_SPACING;
        int span = int(std::ceil(FALL_BANK_MAX / NODE_SPACING)) + 1;
        int fEnd = std::min(searchCount - 2, bestIdx + span);
        for (int f = bestIdx; f <= fEnd; ++f) {
            if (!isFall(segmentsY, f)) continue;
            double d = (f + FALL_FACE_T) * NODE_SPACING - along;
            if (d < 0.0) continue;
            if (d <= fallWing(segmentsY[f], segmentsY[f + 1])) {
                bankFloor = fallRamp(segmentsY[f], segmentsY[f + 1], d) + 1;
            }
            break;
        }
    }

    bool beyondEnd = (lo == 0 && bestIdx == 0 && !bestIsPoint && bestRawT < 0.0)
                  || (hi == validNodeCount - 1 && (bestIsPoint || (bestIdx == hi - 1 && bestRawT > 1.0)));
    double dist = std::sqrt(minDistSq);

    RiverNodeData out;
    out.x = segmentsX[bestIdx];
    out.z = segmentsZ[bestIdx];
    out.y = int(dwRound(projY));
    out.terrainY = int(dwRound(projTerrainY));
    out.width = projWidth;
    out.distance = dist;
    out.flowDirX = flowDirX;
    out.flowDirZ = flowDirZ;
    out.nodeIndex = bestIdx;
    out.lateral = dist * sideSign;
    out.curvature = projCurv;
    out.slope = projSlope;
    out.bankFloor = bankFloor;
    out.beyondEnd = beyondEnd;
    return out;
}

// ---------- CoarseHeightCache ----------
namespace {
std::atomic<uint64_t> gCoarseEpoch{1};
}

void CoarseHeightCache::reset(std::function<int(int, int)> sampleCellFn, std::function<int(int, int)> sampleSurfaceFn, int seaLevel) {
    std::lock_guard<std::mutex> lk(mutex_);
    blocks_.clear();
    sampleCellFn_ = std::move(sampleCellFn);
    sampleSurfaceFn_ = std::move(sampleSurfaceFn);
    seaLevel_ = seaLevel;
    epoch_.store(gCoarseEpoch.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_release);
}

void CoarseHeightCache::setCallbacks(std::function<int(int, int)> sampleCellFn, std::function<int(int, int)> sampleSurfaceFn, int seaLevel) {
    std::lock_guard<std::mutex> lk(mutex_);
    sampleCellFn_ = std::move(sampleCellFn);
    sampleSurfaceFn_ = std::move(sampleSurfaceFn);
    seaLevel_ = seaLevel;
}

const std::array<int16_t, CoarseHeightCache::BLOCK_AREA>* CoarseHeightCache::getBlockPtr(int bx, int bz) {
    const uint64_t curEpoch = epoch_.load(std::memory_order_acquire);
    const int64_t key = blockKey(bx, bz);
    struct TlsSlot {
        const CoarseHeightCache* owner = nullptr;
        uint64_t epoch = 0;
        int64_t key = 0;
        std::shared_ptr<const std::array<int16_t, BLOCK_AREA>> block;
    };
    thread_local std::array<TlsSlot, 16> tls{};
    const size_t slotIdx = (uint64_t(uint32_t(bx)) ^ (uint64_t(uint32_t(bz)) * 0x9E3779B97F4A7C15ull)) & 15u;
    auto& slot = tls[slotIdx];
    if (slot.owner == this && slot.epoch == curEpoch && slot.key == key && slot.block) {
        return slot.block.get();
    }

    std::function<int(int, int)> sampleFn;
    int sea = 63;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = blocks_.find(key);
        if (it != blocks_.end()) {
            slot.owner = this;
            slot.epoch = curEpoch;
            slot.key = key;
            slot.block = it->second;
            return slot.block.get();
        }
        sampleFn = sampleCellFn_;
        sea = seaLevel_;
    }

    auto b = std::make_shared<std::array<int16_t, BLOCK_AREA>>();
    int cellX0 = bx << BLOCK_SHIFT;
    int cellZ0 = bz << BLOCK_SHIFT;
    if (!sampleFn) {
        b->fill(int16_t(sea));
    } else {
        for (int j = 0; j < BLOCK_CELLS; ++j) {
            int row = j << BLOCK_SHIFT;
            for (int i = 0; i < BLOCK_CELLS; ++i) {
                int y = sampleFn(cellX0 + i, cellZ0 + j);
                (*b)[row | i] = int16_t(std::max(-32768, std::min(32767, y)));
            }
        }
    }
    std::shared_ptr<const std::array<int16_t, BLOCK_AREA>> res = std::move(b);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        auto [it, inserted] = blocks_.try_emplace(key, res);
        res = it->second;
    }
    slot.owner = this;
    slot.epoch = curEpoch;
    slot.key = key;
    slot.block = std::move(res);
    return slot.block.get();
}

std::shared_ptr<const std::array<int16_t, CoarseHeightCache::BLOCK_AREA>> CoarseHeightCache::getBlock(int bx, int bz) {
    const uint64_t curEpoch = epoch_.load(std::memory_order_acquire);
    const int64_t key = blockKey(bx, bz);
    struct TlsSlot {
        const CoarseHeightCache* owner = nullptr;
        uint64_t epoch = 0;
        int64_t key = 0;
        std::shared_ptr<const std::array<int16_t, BLOCK_AREA>> block;
    };
    thread_local std::array<TlsSlot, 16> tls{};
    const size_t slotIdx = (uint64_t(uint32_t(bx)) ^ (uint64_t(uint32_t(bz)) * 0x9E3779B97F4A7C15ull)) & 15u;
    auto& slot = tls[slotIdx];
    if (slot.owner == this && slot.epoch == curEpoch && slot.key == key && slot.block) {
        return slot.block;
    }
    (void)getBlockPtr(bx, bz);
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = blocks_.find(key);
    if (it != blocks_.end()) {
        slot.owner = this;
        slot.epoch = curEpoch;
        slot.key = key;
        slot.block = it->second;
        return it->second;
    }
    return {};
}

int CoarseHeightCache::sampleCell(int cellX, int cellZ) {
    const auto* b = getBlockPtr(cellX >> BLOCK_SHIFT, cellZ >> BLOCK_SHIFT);
    return (*b)[((cellZ & BLOCK_MASK) << BLOCK_SHIFT) | (cellX & BLOCK_MASK)];
}

int CoarseHeightCache::surfaceAt(int blockX, int blockZ) {
    if (sampleSurfaceFn_) return sampleSurfaceFn_(blockX, blockZ);
    return sampleCell(blockX >> CELL_SHIFT, blockZ >> CELL_SHIFT);
}

void CoarseHeightCache::fillWindow(int cx0, int cz0, int w, int h, float* out) {
    int bx0 = cx0 >> BLOCK_SHIFT, bx1 = (cx0 + w - 1) >> BLOCK_SHIFT;
    int bz0 = cz0 >> BLOCK_SHIFT, bz1 = (cz0 + h - 1) >> BLOCK_SHIFT;
    for (int bz = bz0; bz <= bz1; ++bz) {
        int cellZStart = std::max(cz0, bz << BLOCK_SHIFT);
        int cellZEnd = std::min(cz0 + h, (bz + 1) << BLOCK_SHIFT);
        for (int bx = bx0; bx <= bx1; ++bx) {
            const auto* b = getBlockPtr(bx, bz);
            int cellXStart = std::max(cx0, bx << BLOCK_SHIFT);
            int cellXEnd = std::min(cx0 + w, (bx + 1) << BLOCK_SHIFT);
            for (int cz = cellZStart; cz < cellZEnd; ++cz) {
                int srcRow = (cz & BLOCK_MASK) << BLOCK_SHIFT;
                int dstRow = (cz - cz0) * w;
                for (int cx = cellXStart; cx < cellXEnd; ++cx) {
                    out[dstRow + (cx - cx0)] = float((*b)[srcRow | (cx & BLOCK_MASK)]);
                }
            }
        }
    }
}

double CoarseHeightCache::coarseGroundAt(double x, double z) {
    double fx = x / CELL_SIZE, fz = z / CELL_SIZE;
    int i = mthFloor(fx), j = mthFloor(fz);
    double tx = fx - i, tz = fz - j;
    double h00 = sampleCell(i, j);
    double h10 = sampleCell(i + 1, j);
    double h01 = sampleCell(i, j + 1);
    double h11 = sampleCell(i + 1, j + 1);
    return mthLerp(tz, mthLerp(tx, h00, h10), mthLerp(tx, h01, h11));
}

// ---------- RegionSpatialGrid ----------
void RegionSpatialGrid::build(int rx, int rz, std::vector<WatershedPath> inPaths, std::vector<WatershedLake> inLakes) {
    paths = std::move(inPaths);
    lakes = std::move(inLakes);
    cells.assign(GRID_DIM * GRID_DIM, {});
    pathsMinX = 1.0e30; pathsMaxX = -1.0e30; pathsMinZ = 1.0e30; pathsMaxZ = -1.0e30;
    lakesMinX = 1.0e30; lakesMaxX = -1.0e30; lakesMinZ = 1.0e30; lakesMaxZ = -1.0e30;
    int originX = rx * hydrology_const::WATERSHED_REGION_SIZE;
    int originZ = rz * hydrology_const::WATERSHED_REGION_SIZE;
    for (int idx = 0; idx < int(paths.size()); ++idx) {
        const auto& path = paths[idx];
        pathsMinX = std::min(pathsMinX, path.influenceMinX());
        pathsMaxX = std::max(pathsMaxX, path.influenceMaxX());
        pathsMinZ = std::min(pathsMinZ, path.influenceMinZ());
        pathsMaxZ = std::max(pathsMaxZ, path.influenceMaxZ());
        int minCol = std::max(0, std::min(GRID_DIM - 1, int(std::floor((path.influenceMinX() - originX) / CELL_SIZE))));
        int maxCol = std::max(0, std::min(GRID_DIM - 1, int(std::floor((path.influenceMaxX() - originX) / CELL_SIZE))));
        int minRow = std::max(0, std::min(GRID_DIM - 1, int(std::floor((path.influenceMinZ() - originZ) / CELL_SIZE))));
        int maxRow = std::max(0, std::min(GRID_DIM - 1, int(std::floor((path.influenceMaxZ() - originZ) / CELL_SIZE))));
        for (int c = minCol; c <= maxCol; ++c) {
            for (int r = minRow; r <= maxRow; ++r) {
                cells[c * GRID_DIM + r].push_back(idx);
            }
        }
    }
    for (const auto& lk : lakes) {
        lakesMinX = std::min(lakesMinX, lk.minX);
        lakesMaxX = std::max(lakesMaxX, lk.maxX);
        lakesMinZ = std::min(lakesMinZ, lk.minZ);
        lakesMaxZ = std::max(lakesMaxZ, lk.maxZ);
    }
}

const std::vector<int>& RegionSpatialGrid::indicesAt(int colX, int colZ, int rx, int rz) const {
    static const std::vector<int> empty;
    if (cells.empty()) return empty;
    int originX = rx * hydrology_const::WATERSHED_REGION_SIZE;
    int originZ = rz * hydrology_const::WATERSHED_REGION_SIZE;
    int c = std::max(0, std::min(GRID_DIM - 1, int(std::floor(double(colX - originX) / double(CELL_SIZE)))));
    int r = std::max(0, std::min(GRID_DIM - 1, int(std::floor(double(colZ - originZ) / double(CELL_SIZE)))));
    return cells[c * GRID_DIM + r];
}

// ---------- BasinTileEngine static helpers ----------
int BasinTileEngine::channelThreshold(DensityLevel level) {
    switch (level) {
        case DensityLevel::VERY_LOW: return 320;
        case DensityLevel::LOW: return 160;
        case DensityLevel::MEDIUM: return 80;
        case DensityLevel::HIGH: return 40;
        case DensityLevel::ULTRA: return 24;
        case DensityLevel::EXTREME: return 14;
        case DensityLevel::INSANE: return 8;
        default: return 40;
    }
}

static constexpr double DENSITY_NOISE_SCALE = 1700.0;
static constexpr double DENSITY_HUMIDITY_SPREAD = 0.4;
static constexpr double DENSITY_NOISE_SPREAD = 0.35;
static constexpr double DENSITY_WET_SPAN = 3.0;
static constexpr double DENSITY_TURN = 0.2;

double BasinTileEngine::plainDryness(const WorldNoises& noises, int bx, int bz) {
    double hum = noises.sampleVegetation(bx, bz);
    double local = noises.densityNoise.getValue(double(bx) / DENSITY_NOISE_SCALE, double(bz) / DENSITY_NOISE_SCALE);
    double wet = 0.75 * mthClamp(hum / DENSITY_HUMIDITY_SPREAD, -2.5, 2.5)
               + 0.66 * mthClamp(local / DENSITY_NOISE_SPREAD, -2.5, 2.5);
    return mthClamp(0.5 - wet / DENSITY_WET_SPAN, 0.0, 1.0);
}

double BasinTileEngine::turnedDryness(double plain) {
    double t = mthClamp((plain - 0.5) / DENSITY_TURN + 0.5, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double BasinTileEngine::labDryness(const WorldNoises& noises, int x, int z) {
    return turnedDryness(plainDryness(noises, x, z));
}

// ---------- BasinTile::Builder exact port ----------
namespace {

constexpr int CS = CoarseHeightCache::CELL_SHIFT; // 3
constexpr int CELL = CoarseHeightCache::CELL_SIZE; // 8
constexpr int TILE_SHIFT = BasinTileData::TILE_SHIFT; // 10
constexpr int CORE = BasinTileData::TILE_SIZE >> CS; // 128
constexpr int HALO = 512 >> CS; // 64
constexpr int W = CORE + 2 * HALO; // 256
constexpr int W_SHIFT = 8;
constexpr int W_MASK = W - 1;
constexpr int N = W * W; // 65536
constexpr int REGION_CELLS = BasinTileData::REGION_SIZE >> CS; // 32
constexpr int REGION_SHIFT = 5;
constexpr int HALO_REGIONS = HALO / REGION_CELLS; // 2
constexpr int WINDOW_REGIONS = W / REGION_CELLS; // 8
constexpr int CELL_KEY_BITS = 20;
constexpr uint64_t CELL_KEY_MASK = (1ull << CELL_KEY_BITS) - 1;

constexpr double NODE_SPACING = 3.0;
constexpr double MIN_WIDTH = 2.5;
constexpr double MAX_WIDTH = 20.0;
constexpr double WIDTH_K = 0.564;
constexpr double WIDTH_POWER = 0.42;
inline double riverWidth(double rain) {
    return mthClamp(WIDTH_K * std::pow(std::max(0.0, rain), WIDTH_POWER), MIN_WIDTH, MAX_WIDTH);
}

constexpr float EPS = 1e-3f;
constexpr float MAX_FILL = 2.0f;
constexpr float MOUTH_RAMP_PER_CELL = 8.0f;
constexpr float UNDER_RUN = 0.25f;
constexpr float UNDER_DIVE = 6.0f;
constexpr int UNDER_COVER = 12;
constexpr int UNDER_MIN_CELLS = 4;
constexpr int UNDER_GAP = 12;
constexpr int UNDER_CLEAR = 6;
constexpr int UNDER_SEAM = 6;
constexpr int UNDER_DIVE_CELLS = 32;

constexpr float CAVE_SPIKE = 5.0f;
constexpr float CAVE_DEEP = 20.0f;
constexpr float TROUGH_MARGIN = 2.0f;
constexpr int CAPTURE_RATIO = 4;
constexpr int FUSE_OWN_CELLS = 24;
constexpr double FUSE_WIDE_GAP = 6.0;
constexpr int JOIN_ALONGSIDE_CELLS = 3;
constexpr int CHANNEL_CONVERGENCE = 3;

constexpr double MAIN_JOIN_IN = 0.45;
constexpr double MAIN_JOIN_CONTACT = 2.0;
constexpr double MAIN_JOIN_CONTACT_MARGIN = 4.0;
constexpr int MAIN_MOUTH_RAMP = 1;
constexpr int MAIN_MOUTH_CUT = 3;
constexpr int MAIN_MOUTH_RAPIDS = 4;
constexpr int MAIN_MOUTH_GORGE_CELLS = 3;
constexpr int JOIN_SCAN_CELLS = 16;
constexpr float SEA_JOIN_RISE = 4.0f;
constexpr int SEA_SHORE_CELLS = 2;
constexpr double MAIN_JOIN_WIDTH = 0.75;
constexpr double MAIN_JOIN_TAPER = 64.0;

constexpr float LAKE_MIN_DEPTH = 3.0f;
constexpr float LAKE_MIN_DEPTH_FED = 2.0f;
constexpr int LAKE_MIN_CELLS = 24;
constexpr int LAKE_MIN_CELLS_FED = 20;
constexpr double LAKE_MIN_SHAPE = 8.0;
constexpr int LAKE_ROUND_CELLS = 200;
constexpr int LAKE_ABOVE_SEA = 4;
constexpr double LAKE_DEEP_SHARE = 0.7;
constexpr float POND_MIN_DEPTH = 6.0f;
constexpr float LAKE_MAX_DEPTH = 16.0f;
constexpr float LAKE_SHAFT_RATIO = 0.5f;
constexpr int LAKE_MAX_CELLS = 3600;
constexpr int LAKE_MAX_EXTENT = 60;
constexpr int LAKE_MAX_GROWTH = 4000;
constexpr int LAKE_CHAIN_CELLS = 3;
constexpr int LAKE_CHAIN_STEP = 3;
constexpr int LAKE_UNDERCUT_ROUNDS = 4;
constexpr float LAKE_CLOSED_RISE = 8.0f;
constexpr float LAKE_WIDE_DEPTH = 8.0f;
constexpr int LAKE_RIVER_GAP = 3;
constexpr double FALL_FLOOR_SLOPE = 0.06;
constexpr double RIM_CUT_REACH = 6.0;
constexpr double RIM_CUT_DEPTH = 3.0;
constexpr int WATERFALL_MIN_DROP = 7;
constexpr int CLIFF_SLOT_DEPTH = 12;

constexpr int STEP_MIN_FALL = WatershedPath::SHEER_DROP;
constexpr int STEP_MIN_POOL_NODES = 2;
constexpr int STEP_MAX_POOL_NODES = 8;
constexpr int STEP_MAX_HOLD = 5;
constexpr int STEP_MAX_GORGE = 7;
constexpr int STEP_END_NODES = 3;

constexpr double JUNCTION_HEAD_MARGIN = 3.0;
constexpr int JUNCTION_HEAD_GROUND = 2;
constexpr double ALONGSIDE_GAP = 4.0;
constexpr int ALONGSIDE_DROP = 4;
constexpr float ROUTE_NOISE = 0.05f;
constexpr double ROUTE_WANDER = 1.6;
constexpr double ROUTE_WANDER_SLOPE = 0.4;
constexpr double ROUTE_WANDER_CELLS = 10.0;
constexpr int SPLINE_STRIDE = 3;

constexpr double JUNCTION_TURN[3] = { 1.0, 0.55, 0.22 };
constexpr double JUNCTION_Y = 60.0;
constexpr double JUNCTION_APART = 65.0;
constexpr double JUNCTION_MAX_TURN = 35.0;

constexpr double MEANDER_WAVELENGTH = 6.5;
constexpr double MEANDER_AMPLITUDE = 0.4;
constexpr double MEANDER_STRENGTH_SPAN = 4.3;
constexpr double MEANDER_MAX_SWING = 60.0;
constexpr double MEANDER_WAVE_SPREAD = 0.4;
constexpr double MEANDER_WAVE_CELLS = 12.0;
constexpr double MEANDER_ROOM_RISE = 4.0;
constexpr double MEANDER_ROOM_PER_WIDTH = 0.25;
constexpr double MEANDER_EDGE_SHARE = 1.0;
constexpr double MEANDER_ROOM_DROP = 2.0;
constexpr double MEANDER_BEND_SHARE = 0.6;
constexpr double MEANDER_FLAT_SLOPE = 0.02;
constexpr double MEANDER_STEEP_SLOPE = 0.15;
constexpr double MEANDER_STEEP_SHARE = 0.4;
constexpr double MEANDER_MOUTH_FADE = 32.0;
constexpr double MEANDER_SOURCE_FADE = 24.0;
constexpr int MEANDER_KEEP_CELLS = 3;
constexpr double MEANDER_SEAM_MARGIN = 2.0;
constexpr int MEANDER_KEEP_MAIN_CELLS = 4;
constexpr int MEANDER_MAIN_GAP_CELLS = 2;
constexpr double MEANDER_RIVER_GAP = 4.0;
constexpr int MEANDER_JOIN_CELLS = 6;
constexpr double MEANDER_JOIN_LEVEL = 1.0;
constexpr double MEANDER_GAP_PER_BLOCK = 2.0;
constexpr double MEANDER_GAP_MAX = 8.0;
constexpr double MEANDER_RIVER_APART = 4.0;
constexpr double MEANDER_JOIN_BLEND = 24.0;
constexpr double MEANDER_JOIN_PER_BLOCK = 2.5;
constexpr double MEANDER_SPIKE = 3.0;
constexpr double MEANDER_NECK_SHARE = 1.0;
constexpr double MEANDER_NECK_MIN = 8.0;
constexpr double MEANDER_NECK_ARC = 2.5;
constexpr int MEANDER_NECK_ROUNDS = 8;
constexpr int MEANDER_NECK_EDGE = 8;
constexpr double MEANDER_NECK_SCALE = 0.7;

constexpr double NECK_TOUCH = 1.1;
constexpr double NECK_DETOUR = 1.5;
constexpr int NECK_EDGE = 2;
constexpr double BEND_FOLD = 2.0;
constexpr double BEND_MIN_RADIUS = 8.0;
constexpr double BEND_SPAN = 6.0;
constexpr double BEND_SPAN_WIDTHS = 1.0;
constexpr int BEND_EASE_ROUNDS = 24;
constexpr double SOURCE_TAPER = 36.0;
constexpr double SOURCE_TIP_POWER = 1.6;

constexpr double DENSITY_WET = 3.6;
constexpr double DENSITY_DRY = 30.0;
constexpr double MAIN_FEED_SHARE = 0.5;
constexpr double MAIN_FEED_CONVERGENCE = 2.5;
constexpr double MAIN_FEED_FROM = 3.0;
constexpr double MAIN_FEED_FULL = 7.0;
constexpr double MAIN_FEED_HOLD = 18.0;
constexpr double MAIN_FEED_REACH = 36.0;
constexpr double DENSITY_LAKE_WET = 1.0;
constexpr double DENSITY_LAKE_DRY = 10.0;
constexpr int DENSITY_GRID = 4;
constexpr int RAIN_UNITS = 64;

constexpr int MAX_REACH_CELLS = 96;
constexpr int LAKE_SILL_NODES = 8;
constexpr int MOUTH_EXTRA_CELLS = 2;
constexpr int LAKE_LEAD_CELLS = 2;
constexpr int LAKE_BORDER_MARGIN = 4;
constexpr int MOUTH_WALK_BLOCKS = 24;
constexpr int MOUTH_OVERSHOOT_BLOCKS = 8;
constexpr int MOUTH_AIM_CELLS = 6;
constexpr int CHANNEL_INCISION = 2;

constexpr int SEAM_BOUNCE_PASSES = 4;
constexpr float SEAM_FLAT_RELIEF = 1.0f;
constexpr int SEAM_BAND = 16;
constexpr int SEAM_SEGMENT = 16;
constexpr int SEAM_POOL_SHARED = HALO - 16;
constexpr int SEAM_POOL_BAND = 2;
constexpr float SEAM_POOL_FILL = 0.5f;
constexpr int SEAM_CYCLE_BAND = 40;
constexpr int SEAM_CYCLE_REACH = 48;
constexpr float SEAM_CYCLE_CLIMB = 2.0f;
constexpr float SEAM_CYCLE_WANDER = 1.0f;
constexpr int SEAM_CYCLE_INSET = 3;
constexpr int SEAM_CORNER_HOPS = 16;
constexpr float SEAM_CYCLE_RISE = 1.0f;
constexpr float SEAM_CYCLE_HEIGHT = 8.0f;
constexpr int PASS_THROUGH_CELLS = 5;
constexpr int PASS_CLIMB_CELLS = 10;

constexpr int DEAD_END_LAKE_SEARCH = 48;
constexpr double DEAD_END_LAKE_FULL = 0.6;
constexpr double DEAD_END_LAKE_WOBBLE = 0.45;
constexpr double DEAD_END_LAKE_WAVE = 6.0;
constexpr double DEAD_END_LAKE_GROUND = 1.0;
constexpr double DEAD_END_LAKE_SPAN = 0.7;
constexpr int DEAD_END_LAKE_MIN = 5;
constexpr int DEAD_END_LAKE_MAX = 14;
constexpr int DEAD_END_LAKE_LAST = 3;
constexpr int DEAD_END_LAKE_EDGE = 0;
constexpr int DEAD_END_LAKE_GAP = 2;
constexpr double DEAD_END_LAKE_REACH = 1.6;
constexpr int DEAD_END_LAKE_RIM_WOBBLE = 3;
constexpr int DEAD_END_LAKE_JOIN = 8;
constexpr int DEAD_END_LAKE_JOIN_NEAR = 3;
constexpr int DEAD_END_LAKE_DIG = 7;

constexpr int SEAM_LAKE_RUN_GAP = 8;
constexpr double SEAM_LAKE_SPAN = 0.5;
constexpr int SEAM_LAKE_MIN = 8;
constexpr int SEAM_LAKE_MAX = 22;
constexpr int SEAM_LAKE_RUN_MAX = 64;

constexpr int DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
constexpr int DZ[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
constexpr float DIST[8] = { 1.0f, 1.41421356f, 1.0f, 1.41421356f, 1.0f, 1.41421356f, 1.0f, 1.41421356f };
constexpr double DIR_ANGLE[8] = {
    0.0, 0.25 * DW_PI, 0.5 * DW_PI, 0.75 * DW_PI, DW_PI, -0.75 * DW_PI, -0.5 * DW_PI, -0.25 * DW_PI
};
constexpr int SEAM_NEIGHBOURS[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

inline int64_t heapKeyFor(float value, int c) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    uint32_t mask = (bits & 0x80000000u) ? 0xFFFFFFFFu : 0x80000000u;
    bits ^= mask;
    return int64_t((uint64_t(bits) << CELL_KEY_BITS) | uint64_t(uint32_t(c) & uint32_t(CELL_KEY_MASK)));
}

inline void catmullRom(double x0, double z0, double x1, double z1, double x2, double z2, double x3, double z3,
                       double t, double out[2]) {
    constexpr double t0 = 0.0;
    double t1 = t0 + std::max(1.0e-4, std::sqrt(std::hypot(x1 - x0, z1 - z0)));
    double t2 = t1 + std::max(1.0e-4, std::sqrt(std::hypot(x2 - x1, z2 - z1)));
    double t3 = t2 + std::max(1.0e-4, std::sqrt(std::hypot(x3 - x2, z3 - z2)));
    double u = t1 + t * (t2 - t1);
    double a1x = ((t1 - u) * x0 + (u - t0) * x1) / (t1 - t0), a1z = ((t1 - u) * z0 + (u - t0) * z1) / (t1 - t0);
    double a2x = ((t2 - u) * x1 + (u - t1) * x2) / (t2 - t1), a2z = ((t2 - u) * z1 + (u - t1) * z2) / (t2 - t1);
    double a3x = ((t3 - u) * x2 + (u - t2) * x3) / (t3 - t2), a3z = ((t3 - u) * z2 + (u - t2) * z3) / (t3 - t2);
    double b1x = ((t2 - u) * a1x + (u - t0) * a2x) / (t2 - t0), b1z = ((t2 - u) * a1z + (u - t0) * a2z) / (t2 - t0);
    double b2x = ((t3 - u) * a2x + (u - t1) * a3x) / (t3 - t1), b2z = ((t3 - u) * a2z + (u - t1) * a3z) / (t3 - t1);
    out[0] = ((t2 - u) * b1x + (u - t1) * b2x) / (t2 - t1);
    out[1] = ((t2 - u) * b1z + (u - t1) * b2z) / (t2 - t1);
}

std::vector<double> splinePts(const std::vector<double>& pts, int stride) {
    int n = int(pts.size()) / 3;
    if (n < 3) return pts;
    std::vector<int> idx;
    idx.reserve(n);
    for (int k = 0; k < n - 2; k += stride) idx.push_back(k);
    idx.push_back(n - 2);
    idx.push_back(n - 1);
    double q[2];
    std::vector<double> out;
    out.reserve(n * 24);
    int c = int(idx.size());
    for (int i = 0; i + 1 < c; ++i) {
        int a = idx[i], b = idx[i + 1];
        double x1 = pts[a * 3], z1 = pts[a * 3 + 1], x2 = pts[b * 3], z2 = pts[b * 3 + 1];
        double x0 = i > 0 ? pts[idx[i - 1] * 3] : 2.0 * x1 - x2;
        double z0 = i > 0 ? pts[idx[i - 1] * 3 + 1] : 2.0 * z1 - z2;
        double x3 = i + 2 < c ? pts[idx[i + 2] * 3] : 2.0 * x2 - x1;
        double z3 = i + 2 < c ? pts[idx[i + 2] * 3 + 1] : 2.0 * z2 - z1;
        int steps = std::max(1, int(std::ceil(std::hypot(x2 - x1, z2 - z1) / 1.5)));
        for (int j = 0; j < steps; ++j) {
            double t = double(j) / double(steps);
            catmullRom(x0, z0, x1, z1, x2, z2, x3, z3, t, q);
            out.push_back(q[0]);
            out.push_back(q[1]);
            out.push_back(pts[a * 3 + 2] + t * (pts[b * 3 + 2] - pts[a * 3 + 2]));
        }
    }
    out.push_back(pts[(n - 1) * 3]);
    out.push_back(pts[(n - 1) * 3 + 1]);
    out.push_back(pts[(n - 1) * 3 + 2]);
    return out;
}

std::vector<double> resamplePts(const std::vector<double>& pts, double spacing) {
    int n = int(pts.size()) / 3;
    if (n < 2) return pts;
    std::vector<double> cum(n, 0.0);
    for (int k = 1; k < n; ++k) {
        double dx = pts[k * 3] - pts[(k - 1) * 3], dz = pts[k * 3 + 1] - pts[(k - 1) * 3 + 1];
        cum[k] = cum[k - 1] + std::sqrt(dx * dx + dz * dz);
    }
    double total = cum[n - 1];
    int count = std::max(2, int(dwRound(total / spacing)) + 1);
    double step = total / double(count - 1);
    std::vector<double> out(count * 3);
    int seg = 0;
    for (int k = 0; k < count; ++k) {
        double s = std::min(total, k * step);
        while (seg < n - 2 && cum[seg + 1] < s) ++seg;
        double segLen = cum[seg + 1] - cum[seg];
        double t = segLen > 1e-9 ? (s - cum[seg]) / segLen : 0.0;
        for (int c = 0; c < 3; ++c) {
            out[k * 3 + c] = pts[seg * 3 + c] + t * (pts[seg * 3 + 3 + c] - pts[seg * 3 + c]);
        }
    }
    return out;
}

double circumradius(const std::vector<double>& p, int a, int b, int c) {
    double ax = p[a * 3], az = p[a * 3 + 1], bx = p[b * 3], bz = p[b * 3 + 1], cx = p[c * 3], cz = p[c * 3 + 1];
    double cross = (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
    if (std::abs(cross) < 1.0e-9) return std::numeric_limits<double>::infinity();
    return std::hypot(bx - ax, bz - az) * std::hypot(cx - bx, cz - bz) * std::hypot(ax - cx, az - cz) / (2.0 * std::abs(cross));
}

void untangle(const std::vector<double>& nodes, std::vector<double>& sx, std::vector<double>& sz,
              const std::vector<double>& radius, int n) {
    if (n < 2 * MEANDER_NECK_EDGE + 2) return;
    std::vector<double> arc(n, 0.0), near(n);
    for (int round = 0; round < MEANDER_NECK_ROUNDS; ++round) {
        for (int k = 1; k < n; ++k) {
            arc[k] = arc[k - 1] + std::hypot(nodes[k * 3] + sx[k] - nodes[(k - 1) * 3] - sx[k - 1],
                                             nodes[k * 3 + 1] + sz[k] - nodes[(k - 1) * 3 + 1] - sz[k - 1]);
        }
        std::fill(near.begin(), near.end(), std::numeric_limits<double>::max());
        bool any = false;
        for (int i = 0; i < n; ++i) {
            double xi = nodes[i * 3] + sx[i], zi = nodes[i * 3 + 1] + sz[i];
            for (int j = i + 1; j < n; ++j) {
                double reach = radius[i] + radius[j];
                double keep = reach + std::max(MEANDER_NECK_MIN, MEANDER_NECK_SHARE * reach);
                if (arc[j] - arc[i] < MEANDER_NECK_ARC * keep) continue;
                double dx = nodes[j * 3] + sx[j] - xi, dz = nodes[j * 3 + 1] + sz[j] - zi;
                if (dx * dx + dz * dz >= keep * keep) continue;
                any = true;
                for (int k = i; k <= j; ++k) near[k] = 0.0;
            }
        }
        if (!any) return;
        for (int k = 1; k < n; ++k) near[k] = std::min(near[k], near[k - 1] + 1.0);
        for (int k = n - 2; k >= 0; --k) near[k] = std::min(near[k], near[k + 1] + 1.0);
        for (int k = 0; k < n; ++k) {
            double w = dwSmoothstep(1.0 - near[k] / double(MEANDER_NECK_EDGE))
                     * dwSmoothstep(double(std::min(k, n - 1 - k)) / double(MEANDER_NECK_EDGE));
            if (w <= 0.0) continue;
            double f = 1.0 - (1.0 - MEANDER_NECK_SCALE) * w;
            sx[k] *= f;
            sz[k] *= f;
        }
    }
}

std::vector<double> cutNecks(const std::vector<double>& course, const std::vector<double>& sx,
                             const std::vector<double>& sz, const std::vector<double>& radius, int n) {
    std::vector<double> p(n * 3);
    std::vector<double> r = radius;
    for (int k = 0; k < n; ++k) {
        p[k * 3] = course[k * 3] + sx[k];
        p[k * 3 + 1] = course[k * 3 + 1] + sz[k];
        p[k * 3 + 2] = course[k * 3 + 2];
    }
    int count = n;
    bool changed = false;
    if (count >= 2 * NECK_EDGE + 3) {
        std::vector<double> arc(count, 0.0);
        for (int k = 1; k < count; ++k) {
            arc[k] = arc[k - 1] + std::hypot(p[k * 3] - p[(k - 1) * 3], p[k * 3 + 1] - p[(k - 1) * 3 + 1]);
        }
        std::vector<double> q;
        std::vector<double> qr;
        q.reserve((count + 8) * 3);
        qr.reserve(count + 8);
        for (int i = 0; i < count;) {
            int cut = -1;
            if (i >= NECK_EDGE) {
                double xi = p[i * 3], zi = p[i * 3 + 1];
                for (int j = i + 2; j < count - NECK_EDGE; ++j) {
                    double gap = std::hypot(p[j * 3] - xi, p[j * 3 + 1] - zi);
                    if (gap < NECK_TOUCH * (r[i] + r[j]) && arc[j] - arc[i] > gap + NECK_DETOUR * std::max(r[i], r[j])) {
                        cut = j;
                    }
                }
            }
            int steps = cut < 0 ? 1 : std::max(1, int(dwRound(
                    std::hypot(p[cut * 3] - p[i * 3], p[cut * 3 + 1] - p[i * 3 + 1]) / NODE_SPACING)));
            for (int s = 0; s < steps; ++s) {
                double t = double(s) / double(steps);
                for (int c = 0; c < 3; ++c) {
                    q.push_back(cut < 0 ? p[i * 3 + c] : p[i * 3 + c] + t * (p[cut * 3 + c] - p[i * 3 + c]));
                }
                qr.push_back(cut < 0 ? r[i] : r[i] + t * (r[cut] - r[i]));
            }
            if (cut < 0) ++i;
            else { i = cut; changed = true; }
        }
        if (changed) {
            p = std::move(q);
            r = std::move(qr);
            count = int(r.size());
        }
    }
    for (int round = 0; round < BEND_EASE_ROUNDS && count >= 2 * NECK_EDGE + 3; ++round) {
        std::vector<double> arc(count, 0.0);
        for (int k = 1; k < count; ++k) {
            arc[k] = arc[k - 1] + std::hypot(p[k * 3] - p[(k - 1) * 3], p[k * 3 + 1] - p[(k - 1) * 3 + 1]);
        }
        bool any = false;
        std::vector<double> e = p;
        for (int k = NECK_EDGE + 1; k < count - NECK_EDGE - 1; ++k) {
            double span = std::max(BEND_SPAN, BEND_SPAN_WIDTHS * r[k]);
            int a = k, c = k;
            while (a > 0 && arc[k] - arc[a] < span) --a;
            while (c + 1 < count && arc[c] - arc[k] < span) ++c;
            if (arc[k] - arc[a] < span || arc[c] - arc[k] < span) continue;
            double bend = circumradius(p, a, k, c), want = std::max(BEND_MIN_RADIUS, BEND_FOLD * r[k]);
            if (bend >= want) continue;
            any = true;
            double f = std::min(0.5, 1.0 - bend / want);
            e[k * 3] = p[k * 3] + f * (0.5 * (p[a * 3] + p[c * 3]) - p[k * 3]);
            e[k * 3 + 1] = p[k * 3 + 1] + f * (0.5 * (p[a * 3 + 1] + p[c * 3 + 1]) - p[k * 3 + 1]);
        }
        if (!any) break;
        p = std::move(e);
    }
    return p;
}

void collapseCliffs(std::vector<int>& ys, std::vector<int>& tys, const std::vector<uint8_t>& fixed) {
    int n = int(ys.size());
    int k = 0;
    while (k + 2 < n) {
        if (!WatershedPath::isFall(ys, k) || !WatershedPath::isFall(ys, k + 1)) { ++k; continue; }
        int foot = ys[k];
        int i = k + 1;
        while (i + 1 < n && WatershedPath::isFall(ys, i) && !fixed[i] && tys[i] - foot <= CLIFF_SLOT_DEPTH) {
            ys[i] = foot;
            tys[i] = foot + 1;
            ++i;
        }
        k = i;
    }
}

void junctionHead(std::vector<int>& ys, const std::vector<int>& tys, const std::vector<uint8_t>& fixed, int nodes) {
    int n = int(ys.size()), head = n - 1, base = ys[head];
    if (n < 3) return;
    if (!fixed[head - 1] && tys[head - 1] >= base + JUNCTION_HEAD_GROUND) {
        for (int k = head - 1; k >= std::max(1, head - nodes); --k) {
            if (fixed[k] || tys[k] < base + JUNCTION_HEAD_GROUND) return;
            ys[k] = base;
        }
        return;
    }
    int best = -1;
    for (int k = head - STEP_MIN_POOL_NODES; k >= 1 && head - k <= STEP_MAX_POOL_NODES; --k) {
        bool holds = true;
        for (int i = k + 1; i < head && holds; ++i) {
            holds = !fixed[i] && ys[i] - ys[k] <= STEP_MAX_HOLD && tys[i] - ys[k] <= STEP_MAX_GORGE;
        }
        if (!holds) break;
        if (base - ys[k] >= STEP_MIN_FALL) best = k;
    }
    if (best < 0) return;
    for (int i = best + 1; i < head; ++i) ys[i] = ys[best];
}

int poolHead(const std::vector<int>& ys, const std::vector<int>& tys, const std::vector<uint8_t>& fixed, int k, int top) {
    int base = ys[k];
    for (int j = k + 1; j <= top && j - k <= STEP_MAX_POOL_NODES; ++j) {
        int rise = ys[j] - base;
        bool pooled = (j - k >= STEP_MIN_POOL_NODES);
        if (rise > STEP_MAX_HOLD || tys[j] - base > STEP_MAX_GORGE) {
            return (pooled && rise >= STEP_MIN_FALL) ? j : -1;
        }
        if (pooled && rise >= STEP_MIN_FALL) return j;
        if (fixed[j]) return -1;
    }
    return -1;
}

int stepWindow(WaterfallLevel level) {
    switch (level) {
        case WaterfallLevel::FEW: return 6;
        case WaterfallLevel::NORMAL: return 3;
        default: return 1;
    }
}

int stepGap(int64_t seed, int i, WaterfallLevel level) {
    int minV = 0, jitter = 0;
    switch (level) {
        case WaterfallLevel::FEW: minV = 14; jitter = 10; break;
        case WaterfallLevel::NORMAL: minV = 4; jitter = 4; break;
        default: return 0;
    }
    int64_t h = hashCommonMix(int64_t(uint64_t(seed) + uint64_t(i) * 0x9E3779B97F4A7C15ull));
    int64_t mod = h % int64_t(jitter + 1);
    if (mod < 0) mod += int64_t(jitter + 1);
    return minV + int(mod);
}

void stepPools(std::vector<int>& ys, const std::vector<int>& tys, const std::vector<uint8_t>& fixed,
               int64_t seed, WaterfallLevel level, bool openHead) {
    if (level == WaterfallLevel::CLIFFS) return;
    int n = int(ys.size());
    int top = openHead ? n - 1 : n - STEP_END_NODES;
    int window = stepWindow(level);
    int from = STEP_END_NODES, falls = 0, k = STEP_END_NODES;
    while (k < top) {
        if (WatershedPath::isFall(ys, k)) {
            from = std::max(from, k + 1 + stepGap(seed, falls++, level));
            ++k;
            continue;
        }
        if (k < from) { ++k; continue; }
        int bestK = -1, bestJ = -1, bestStep = -1;
        for (int b = k; b < std::min(top - 1, k + window); ++b) {
            if (b > k && WatershedPath::isFall(ys, b)) break;
            int j = poolHead(ys, tys, fixed, b, top);
            if (j < 0) continue;
            bool lowers = false;
            for (int i = b + 1; i < j && !lowers; ++i) lowers = ys[i] > ys[b];
            if (lowers && ys[j] - ys[j - 1] > bestStep) {
                bestStep = ys[j] - ys[j - 1];
                bestK = b;
                bestJ = j;
            }
        }
        if (bestJ < 0) { ++k; continue; }
        for (int i = bestK + 1; i < bestJ; ++i) ys[i] = ys[bestK];
        from = bestJ + stepGap(seed, falls++, level);
        k = bestJ;
    }
}

struct BasinBuilder {
    using SeamExit = BasinTileEngine::SeamExit;
    using TileSeams = BasinTileEngine::TileSeams;

    BasinTileData tile;
    const DynamicWatersConfig& cfg;
    const WorldNoises& noises;
    MainRiversEngine* mainRivers = nullptr;
    CoarseHeightCache& heights;
    const BasinCallbacks& cb;
    std::function<std::shared_ptr<const TileSeams>(int, int)> getSeamsFn;
    std::function<uint64_t()> allocLakeIdFn;

    int64_t seed = 0;
    int seaLevel = 63;
    int threshold = 40;
    int thresholdSetting = 40;
    bool lakesEnabled = true;
    bool neighbour = false;
    WaterfallLevel waterfalls = WaterfallLevel::NORMAL;

    int underCount = 0;
    std::vector<uint8_t> underCells;
    int cx0 = 0, cz0 = 0;

    std::vector<float> h, wanderWay, wanderPull, filled, breached, dryness, seamLevel;
    std::vector<int8_t> dir;
    std::vector<int32_t> acc, flowLen, order, lakeId, lakeShore, wet, rain, needRain;
    std::vector<float> needConvergence;
    std::vector<uint8_t> sink, mainRiver, nearRiver, cut, pushed, chan, upCount, upLake, sameRegionUp, inflow, takenOver, dropped;
    std::vector<float> ground0;
    std::vector<int32_t> inAcc, inWet, inLen, wetAlone, lakeFloorAt;
    std::vector<int64_t> heap;
    int heapSize = 0, orderCount = 0;

    std::array<std::shared_ptr<const TileSeams>, 4> around{};
    std::shared_ptr<const TileSeams> ownSeams;
    std::unordered_map<int64_t, std::shared_ptr<const TileSeams>> farSeams;
    std::array<std::vector<uint8_t>, 4> withdrawn{};
    std::array<std::vector<int16_t>, 4> wayOut{};
    bool seamsImported = false;

    std::vector<std::vector<int>> lakeCells;
    std::vector<int> lakeWaterY;
    std::vector<uint8_t> lakeClosed;
    bool closedChanged = false;
    std::vector<std::vector<int>> lakeShoreCells;
    std::vector<WatershedLake> packaged;
    std::vector<uint8_t> packagedValid;

    std::vector<float> bendKeep, bendNx, bendNz, bendK, bendPhase, bendFlat;
    std::vector<uint8_t> bendCrossing;
    double swayNormal[2]{ 0.0, 0.0 };

    std::vector<uint8_t> deadEndLakeIdSet; // flags by lake id
    struct SeamLake { std::vector<int> cells; int waterY; };
    std::vector<SeamLake> seamLakes;
    std::vector<int> floodMark;
    int floodStamp = 0;
    int streamRain = 0;

    struct MainRiverPoint {
        double x = 0, z = 0, dirX = 0, dirZ = 0, width = 0, waterRadius = 0;
        bool valid = false;
    };

    BasinBuilder(int tileX, int tileZ, const DynamicWatersConfig& cfg_, const WorldNoises& noises_,
                 MainRiversEngine* mr, CoarseHeightCache& heights_, const BasinCallbacks& cb_,
                 std::function<std::shared_ptr<const TileSeams>(int, int)> getSeamsFn_,
                 std::function<uint64_t()> allocLakeIdFn_,
                 int64_t seed_, int seaLevel_, int thresh_, bool lakes_, bool nb)
        : cfg(cfg_), noises(noises_), mainRivers(mr), heights(heights_), cb(cb_),
          getSeamsFn(std::move(getSeamsFn_)), allocLakeIdFn(std::move(allocLakeIdFn_)),
          seed(seed_), seaLevel(seaLevel_), threshold(std::max(4, thresh_)), thresholdSetting(thresh_),
          lakesEnabled(lakes_), neighbour(nb), waterfalls(cfg_.mountainRiverWaterfalls) {
        tile.tileX = tileX;
        tile.tileZ = tileZ;
        cx0 = (tileX << TILE_SHIFT >> CS) - HALO;
        cz0 = (tileZ << TILE_SHIFT >> CS) - HALO;
        streamRain = int(dwRound(double(threshold) * DENSITY_WET * double(RAIN_UNITS)));

        h.assign(N, 0.0f);
        wanderWay.assign(N, 0.0f);
        wanderPull.assign(N, 0.0f);
        filled.assign(N, 0.0f);
        breached.assign(N, 0.0f);
        dryness.assign(N, 0.0f);
        dir.assign(N, -1);
        acc.assign(N, 0);
        flowLen.assign(N, 0);
        order.assign(N, 0);
        lakeId.assign(N, -1);
        lakeShore.assign(N, -1);
        wet.assign(N, 0);
        rain.assign(N, 0);
        needRain.assign(N, 0);
        needConvergence.assign(N, 0.0f);
        sink.assign(N, 0);
        mainRiver.assign(N, 0);
        nearRiver.assign(N, 0);
        cut.assign(N, 0);
        pushed.assign(N, 0);
        chan.assign(N, 0);
        upCount.assign(N, 0);
        upLake.assign(N, 0);
        sameRegionUp.assign(N, 0);
        inflow.assign(N, 0);
        dropped.assign(N, 0);
        lakeFloorAt.assign(N, INT32_MIN);
        heap.assign(N + 1, 0);
    }

    static bool inCore(int c) {
        int i = c & W_MASK, j = c >> W_SHIFT;
        return i >= HALO && j >= HALO && i < HALO + CORE && j < HALO + CORE;
    }
    static int coreIndex(int c) {
        return ((c >> W_SHIFT) - HALO) * CORE + ((c & W_MASK) - HALO);
    }
    int windowCell(int wx, int wz) const {
        int i = wx - cx0, j = wz - cz0;
        return (i < 0 || j < 0 || i >= W || j >= W) ? -1 : ((j << W_SHIFT) | i);
    }
    static bool inTile(int tx, int tz, int wx, int wz) {
        return floorDiv(wx, CORE) == tx && floorDiv(wz, CORE) == tz;
    }
    int rimDistance(int c) const {
        int i = (c & W_MASK) - HALO, j = (c >> W_SHIFT) - HALO;
        return std::min(std::min(i, CORE - 1 - i), std::min(j, CORE - 1 - j));
    }
    int regionIndex(int c) const {
        return ((c >> W_SHIFT) >> REGION_SHIFT) * WINDOW_REGIONS + ((c & W_MASK) >> REGION_SHIFT);
    }
    static bool isCoreRegion(int r) {
        int ri = r % WINDOW_REGIONS, rj = r / WINDOW_REGIONS;
        return ri >= HALO_REGIONS && ri < HALO_REGIONS + BasinTileData::REGIONS_PER_SIDE
            && rj >= HALO_REGIONS && rj < HALO_REGIONS + BasinTileData::REGIONS_PER_SIDE;
    }
    static int localRegionSlot(int r) {
        int ri = (r % WINDOW_REGIONS) - HALO_REGIONS;
        int rj = (r / WINDOW_REGIONS) - HALO_REGIONS;
        return rj * BasinTileData::REGIONS_PER_SIDE + ri;
    }
    double cellWidth(int c) const { return riverWidth(double(wet[c]) / double(RAIN_UNITS)); }
    bool closedLake(int a) const { return a >= 0 && a < int(lakeClosed.size()) && lakeClosed[a] != 0; }

    int downstream(int c) const {
        int d = dir[c];
        if (d < 0) return -1;
        if (lakeId[c] >= 0 && closedLake(lakeId[c])) return -1;
        return (((c >> W_SHIFT) + DZ[d]) << W_SHIFT) | ((c & W_MASK) + DX[d]);
    }
    int mainUp(int c) const {
        int i = c & W_MASK, j = c >> W_SHIFT;
        int best = -1, bestWet = 0;
        for (int k = 0; k < 8; ++k) {
            int ni = i + DX[k], nj = j + DZ[k];
            if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
            int nc = (nj << W_SHIFT) | ni;
            if (wet[nc] > bestWet && downstream(nc) == c) {
                bestWet = wet[nc];
                best = nc;
            }
        }
        return best;
    }

    bool overCorner(int i, int j, int k) const {
        if (DX[k] == 0 || DZ[k] == 0) return false;
        int mx = floorMod(cx0 + i, CORE), mz = floorMod(cz0 + j, CORE);
        return (DX[k] > 0 ? mx == CORE - 1 : mx == 0) && (DZ[k] > 0 ? mz == CORE - 1 : mz == 0);
    }
    bool overCornerCells(int p, int q) const {
        return floorDiv(cx0 + (p & W_MASK), CORE) != floorDiv(cx0 + (q & W_MASK), CORE)
            && floorDiv(cz0 + (p >> W_SHIFT), CORE) != floorDiv(cz0 + (q >> W_SHIFT), CORE);
    }

    void pushHeap(int64_t key) {
        int i = heapSize++;
        while (i > 0) {
            int p = (i - 1) >> 1;
            if (heap[p] <= key) break;
            heap[i] = heap[p];
            i = p;
        }
        heap[i] = key;
    }
    int64_t popHeap() {
        int64_t top = heap[0];
        int64_t last = heap[--heapSize];
        if (heapSize > 0) {
            int i = 0;
            while (true) {
                int l = 2 * i + 1;
                if (l >= heapSize) break;
                int r = l + 1;
                int m = (r < heapSize && heap[r] < heap[l]) ? r : l;
                if (heap[m] >= last) break;
                heap[i] = heap[m];
                i = m;
            }
            heap[i] = last;
        }
        return top;
    }

    double valueNoise(double x, double z, int salt) const {
        auto lattice = [&](int ix, int iz) -> double {
            uint64_t hsh = uint64_t(hashCommonMix(int64_t(
                uint64_t(seed) ^ uint64_t(int64_t(ix)) * 0x9E3779B97F4A7C15ull
                               ^ uint64_t(int64_t(iz)) * 0xC2B2AE3D27D4EB4Full
                               ^ uint64_t(int64_t(salt + 1)) * 0x632BE59BD9B4E019ull)));
            return double(hsh >> 11) * 0x1.0p-53 * 2.0 - 1.0;
        };
        int ix = mthFloor(x), iz = mthFloor(z);
        double u = dwSmoothstep(x - ix), v = dwSmoothstep(z - iz);
        return mthLerp(v, mthLerp(u, lattice(ix, iz), lattice(ix + 1, iz)),
                          mthLerp(u, lattice(ix, iz + 1), lattice(ix + 1, iz + 1)));
    }
    double lakeNoise(int c) const {
        double x = double(cx0 + (c & W_MASK)) / DEAD_END_LAKE_WAVE;
        double z = double(cz0 + (c >> W_SHIFT)) / DEAD_END_LAKE_WAVE;
        return 0.7 * valueNoise(x, z, 0) + 0.3 * valueNoise(x * 2.3, z * 2.3, 1);
    }

    double groundAt(const std::vector<float>& ground, double x, double z) const {
        double fx = x / CELL - cx0, fz = z / CELL - cz0;
        int i = mthFloor(fx), j = mthFloor(fz);
        if (i < 0 || j < 0 || i >= W - 1 || j >= W - 1) {
            return double(heights.sampleCell(mthFloor(x / CELL), mthFloor(z / CELL)));
        }
        double tx = fx - i, tz = fz - j;
        int c = (j << W_SHIFT) | i;
        return mthLerp(tz, mthLerp(tx, ground[c], ground[c + 1]), mthLerp(tx, ground[c + W], ground[c + W + 1]));
    }

    static bool inTrough(const std::vector<float>& src, int i, int j, float below) {
        for (int k = 0; k < 4; ++k) {
            int ai = i + DX[k], aj = j + DZ[k];
            int bi = i + DX[k + 4], bj = j + DZ[k + 4];
            if (ai < 0 || aj < 0 || ai >= W || aj >= W || bi < 0 || bj < 0 || bi >= W || bj >= W) continue;
            if (src[(aj << W_SHIFT) | ai] < below && src[(bj << W_SHIFT) | bi] < below) return true;
        }
        return false;
    }

    void despike() {
        std::vector<float> src = h;
        float nb[8];
        for (int c = 0; c < N; ++c) {
            int i = c & W_MASK, j = c >> W_SHIFT;
            int n = 0;
            for (int k = 0; k < 8; ++k) {
                int ni = i + DX[k], nj = j + DZ[k];
                if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
                nb[n++] = src[(nj << W_SHIFT) | ni];
            }
            if (n < 5) continue;
            std::sort(nb, nb + n);
            float median = (n & 1) ? nb[n / 2] : 0.5f * (nb[n / 2 - 1] + nb[n / 2]);
            if (src[c] < median - CAVE_SPIKE) {
                if (src[c] > median - CAVE_DEEP && inTrough(src, i, j, median - TROUGH_MARGIN)) continue;
                h[c] = median;
            }
        }
    }

    void routeNoise() {
        for (int c = 0; c < N; ++c) {
            int64_t wx = cx0 + (c & W_MASK), wz = cz0 + (c >> W_SHIFT);
            uint64_t hsh = uint64_t(seed) ^ (uint64_t(wx) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(wz) * 0xC2B2AE3D27D4EB4Full);
            hsh ^= hsh >> 33;
            hsh *= 0xFF51AFD7ED558CCDull;
            hsh ^= hsh >> 33;
            h[c] += (float(double(hsh >> 11) * 0x1.0p-53f) - 0.5f) * 2.0f * ROUTE_NOISE;
        }
    }

    void wanderField() {
        for (int c = 0; c < N; ++c) {
            double x = double(cx0 + (c & W_MASK)) / ROUTE_WANDER_CELLS;
            double z = double(cz0 + (c >> W_SHIFT)) / ROUTE_WANDER_CELLS;
            double vx = valueNoise(x, z, 5) + 0.4 * valueNoise(x * 2.3, z * 2.3, 7);
            double vz = valueNoise(x, z, 6) + 0.4 * valueNoise(x * 2.3, z * 2.3, 8);
            wanderWay[c] = float(std::atan2(vz, vx));
            wanderPull[c] = float(std::min(1.0, 2.0 * std::hypot(vx, vz)));
        }
    }

    void densityField() {
        const int g = DENSITY_GRID;
        int gi0 = floorDiv(cx0, g), gj0 = floorDiv(cz0, g);
        int gw = floorDiv(cx0 + W - 1, g) - gi0 + 2;
        int gh = floorDiv(cz0 + W - 1, g) - gj0 + 2;
        std::vector<double> dry(gw * gh, 0.0);
        for (int b = 0; b < gh; ++b) {
            for (int a = 0; a < gw; ++a) {
                dry[b * gw + a] = BasinTileEngine::plainDryness(noises, ((gi0 + a) * g) << CS, ((gj0 + b) * g) << CS);
            }
        }
        for (int c = 0; c < N; ++c) {
            int wx = cx0 + (c & W_MASK), wz = cz0 + (c >> W_SHIFT);
            int a = floorDiv(wx, g) - gi0, b = floorDiv(wz, g) - gj0;
            double tx = double(floorMod(wx, g)) / double(g);
            double tz = double(floorMod(wz, g)) / double(g);
            int k = b * gw + a;
            double d = BasinTileEngine::turnedDryness(
                mthLerp(tz, mthLerp(tx, dry[k], dry[k + 1]), mthLerp(tx, dry[k + gw], dry[k + gw + 1])));
            dryness[c] = float(d);
            rain[c] = int(dwRound(double(RAIN_UNITS) * std::pow(DENSITY_WET / DENSITY_DRY, d)));
        }
    }

    int lakeMinCells(int c, bool fed) const {
        double k = DENSITY_LAKE_WET * std::pow(DENSITY_LAKE_DRY / DENSITY_LAKE_WET, dryness[c]);
        return int(std::ceil((fed ? LAKE_MIN_CELLS_FED : LAKE_MIN_CELLS) * k));
    }

    void nearRiverMask() {
        std::vector<uint8_t> rows(N, 0);
        for (int j = 0; j < W; ++j) {
            int last = INT32_MIN / 2;
            for (int i = 0; i < W; ++i) {
                if (mainRiver[(j << W_SHIFT) | i]) last = i;
                if (i - last <= LAKE_RIVER_GAP) rows[(j << W_SHIFT) | i] = 1;
            }
            last = INT32_MAX / 2;
            for (int i = W - 1; i >= 0; --i) {
                if (mainRiver[(j << W_SHIFT) | i]) last = i;
                if (last - i <= LAKE_RIVER_GAP) rows[(j << W_SHIFT) | i] = 1;
            }
        }
        for (int i = 0; i < W; ++i) {
            int last = INT32_MIN / 2;
            for (int j = 0; j < W; ++j) {
                if (rows[(j << W_SHIFT) | i]) last = j;
                if (j - last <= LAKE_RIVER_GAP) nearRiver[(j << W_SHIFT) | i] = 1;
            }
            last = INT32_MAX / 2;
            for (int j = W - 1; j >= 0; --j) {
                if (rows[(j << W_SHIFT) | i]) last = j;
                if (last - j <= LAKE_RIVER_GAP) nearRiver[(j << W_SHIFT) | i] = 1;
            }
        }
    }

    void stampSinks() {
        for (int c = 0; c < N; ++c) {
            if (h[c] >= seaLevel) continue;
            int x = (cx0 + (c & W_MASK)) << CS, z = (cz0 + (c >> W_SHIFT)) << CS;
            if (cb.isMouthBiomeAt && cb.isMouthBiomeAt(x, z)) sink[c] = 1;
        }
        if (cfg.normalRiverMode != NormalRiverMode::DYNAMIC || !mainRivers) return;

        double canyonHeight = cfg.normalRiverCanyonHeight;
        bool tunnelIsSink = cfg.mountainRiverTunnels || !cfg.normalRiverTunnels;
        int bx0 = cx0 << CS, bz0 = cz0 << CS;
        int span = W << CS;
        int margin = 512;
        int r0x = floorDiv(bx0 - margin, MainRiversEngine::REGION_SIZE);
        int r1x = floorDiv(bx0 + span + margin, MainRiversEngine::REGION_SIZE);
        int r0z = floorDiv(bz0 - margin, MainRiversEngine::REGION_SIZE);
        int r1z = floorDiv(bz0 + span + margin, MainRiversEngine::REGION_SIZE);
        for (int rz = r0z; rz <= r1z; ++rz) {
            for (int rx = r0x; rx <= r1x; ++rx) {
                mainRivers->getRegion(rx, rz);
            }
        }

        bool corridor = !cfg.normalRiverTunnels;
        double reachOut = corridor ? hydrology_const::LOWLAND_CORRIDOR_REACH : 0.0;
        if (corridor) ground0 = h;
        const int segCell = MainRiversEngine::PARTITION_CELL;
        const int cellsPerSeg = segCell >> CS;
        std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>> edges;
        for (int sz = bz0; sz < bz0 + span; sz += segCell) {
            for (int sx = bx0; sx < bx0 + span; sx += segCell) {
                edges.clear();
                mainRivers->collectNearbyEdges(sx + segCell / 2.0, sz + segCell / 2.0, edges);
                if (edges.empty()) continue;
                int ne = int(edges.size());
                std::vector<double> bb(ne * 4);
                std::vector<double> radSq(ne);
                for (int e = 0; e < ne; ++e) {
                    const auto& regionEdges = *edges[e].first;
                    const RiverGraphEdge& edge = regionEdges[edges[e].second];
                    double mnx = edge.fractal.minX, mxx = edge.fractal.maxX;
                    double mnz = edge.fractal.minY, mxz = edge.fractal.maxY;
                    double r = edge.maxWaterRadius(regionEdges) + 2.0 + reachOut;
                    bb[e * 4] = mnx - r; bb[e * 4 + 1] = mxx + r;
                    bb[e * 4 + 2] = mnz - r; bb[e * 4 + 3] = mxz + r;
                    radSq[e] = r * r;
                }
                int i0 = (sx - bx0) >> CS, j0 = (sz - bz0) >> CS;
                for (int j = j0; j < j0 + cellsPerSeg; ++j) {
                    double z = ((cz0 + j) << CS) + CELL * 0.5;
                    for (int i = i0; i < i0 + cellsPerSeg; ++i) {
                        int c = (j << W_SHIFT) | i;
                        if (h[c] >= canyonHeight && !tunnelIsSink) continue;
                        double x = ((cx0 + i) << CS) + CELL * 0.5;

                        double bestOut = std::numeric_limits<double>::max(), bestR = 0.0, bestX = 0.0, bestZ = 0.0;
                        bool bestDelta = false;
                        double ox = 0.0, oz = 0.0;
                        for (int e = 0; e < ne; ++e) {
                            if (x < bb[e * 4] || x > bb[e * 4 + 1] || z < bb[e * 4 + 2] || z > bb[e * 4 + 3]) continue;
                            const auto& regionEdges = *edges[e].first;
                            const RiverGraphEdge& edge = regionEdges[edges[e].second];
                            double dSq = corridor ? edge.fractal.nearestPoint(x, z, ox, oz)
                                                  : edge.fractal.intersectDistance(x, z);
                            if (dSq > radSq[e]) continue;
                            double waterR = hydrology_const::routeWaterRadius(edge, x, z, regionEdges, cfg);
                            double rc = waterR + 2.0;
                            if (dSq <= rc * rc) {
                                mainRiver[c] = 1;
                                sink[c] = 1;
                                if (corridor) h[c] = std::min(h[c], float(seaLevel));
                                break;
                            }
                            if (sink[c] && !mainRiver[c] && hydrology_const::inHeadTaper(edge, x, z)) {
                                double whole = hydrology_const::waterRadiusAt(edge, x, z, regionEdges) + 2.0;
                                if (dSq <= whole * whole) {
                                    mainRiver[c] = 1;
                                    break;
                                }
                            }
                            if (corridor) {
                                double out = std::sqrt(dSq) - waterR;
                                if (out < bestOut) {
                                    bestOut = out;
                                    bestR = waterR;
                                    bestX = ox;
                                    bestZ = oz;
                                    bestDelta = edge.delta;
                                }
                            }
                        }
                        if (corridor && !sink[c] && bestOut < reachOut) {
                            double riverGround = groundAt(ground0, bestX, bestZ);
                            double ground = hydrology_const::lowlandRouteGround(seed, noises.riverBedNoise,
                                    x, z, h[c], bestOut, bestR, riverGround, seaLevel, bestDelta);
                            double routed = ground;
                            if (hydrology_const::gorgeShare(riverGround) >= 0.5) {
                                cut[c] = hydrology_const::lowlandRouteGround(seed, noises.riverBedNoise,
                                        x, z, h[c], bestOut - RIM_CUT_REACH, bestR, riverGround, seaLevel, bestDelta)
                                    < h[c] - RIM_CUT_DEPTH ? 1 : 0;
                                if (cut[c]) routed = std::min(routed, seaLevel + FALL_FLOOR_SLOPE * bestOut);
                            }
                            if (routed < h[c]) h[c] = float(routed);
                        }
                    }
                }
            }
        }
        nearRiverMask();
    }

    void feedField() {
        constexpr float far = 1.0e9f;
        const float diag = float(std::sqrt(2.0));
        std::vector<float> dist(N, far);
        bool any = false;
        for (int c = 0; c < N; ++c) {
            if (mainRiver[c]) { dist[c] = 0.0f; any = true; }
        }
        if (!any) {
            std::fill(needRain.begin(), needRain.end(), streamRain);
            std::fill(needConvergence.begin(), needConvergence.end(), float(CHANNEL_CONVERGENCE));
            return;
        }
        for (int j = 0; j < W; ++j) {
            for (int i = 0; i < W; ++i) {
                int c = (j << W_SHIFT) | i;
                float d = dist[c];
                if (i > 0) d = std::min(d, dist[c - 1] + 1.0f);
                if (j > 0) {
                    d = std::min(d, dist[c - W] + 1.0f);
                    if (i > 0) d = std::min(d, dist[c - W - 1] + diag);
                    if (i < W - 1) d = std::min(d, dist[c - W + 1] + diag);
                }
                dist[c] = d;
            }
        }
        for (int j = W - 1; j >= 0; --j) {
            for (int i = W - 1; i >= 0; --i) {
                int c = (j << W_SHIFT) | i;
                float d = dist[c];
                if (i < W - 1) d = std::min(d, dist[c + 1] + 1.0f);
                if (j < W - 1) {
                    d = std::min(d, dist[c + W] + 1.0f);
                    if (i < W - 1) d = std::min(d, dist[c + W + 1] + diag);
                    if (i > 0) d = std::min(d, dist[c + W - 1] + diag);
                }
                dist[c] = d;
            }
        }
        for (int c = 0; c < N; ++c) {
            double d = dist[c];
            double feed = dwSmoothstep((d - MAIN_FEED_FROM) / (MAIN_FEED_FULL - MAIN_FEED_FROM))
                        * (1.0 - dwSmoothstep((d - MAIN_FEED_HOLD) / (MAIN_FEED_REACH - MAIN_FEED_HOLD)));
            double dry = 1.0 - feed * (1.0 - double(rain[c]) / double(RAIN_UNITS));
            needRain[c] = int(dwRound(double(streamRain) * (1.0 - (1.0 - MAIN_FEED_SHARE) * feed) * dry));
            needConvergence[c] = float(CHANNEL_CONVERGENCE - (CHANNEL_CONVERGENCE - MAIN_FEED_CONVERGENCE) * feed);
        }
    }

    void flood() {
        std::fill(filled.begin(), filled.end(), std::numeric_limits<float>::infinity());
        std::fill(pushed.begin(), pushed.end(), 0);
        heapSize = 0;
        for (int c = 0; c < N; ++c) {
            int i = c & W_MASK, j = c >> W_SHIFT;
            bool border = (i == 0 || j == 0 || i == W - 1 || j == W - 1);
            bool closedWater = lakeId[c] >= 0 && closedLake(lakeId[c]);
            if (border || sink[c] || closedWater) {
                filled[c] = closedWater ? float(lakeWaterY[lakeId[c]])
                          : (mainRiver[c] ? std::min(h[c], float(seaLevel - 1)) : h[c]);
                pushed[c] = 1;
                pushHeap(heapKeyFor(filled[c], c));
            }
        }
        orderCount = 0;
        while (heapSize > 0) {
            int c = int(uint64_t(popHeap()) & CELL_KEY_MASK);
            order[orderCount++] = c;
            int i = c & W_MASK, j = c >> W_SHIFT;
            float base = filled[c] + EPS;
            for (int k = 0; k < 8; ++k) {
                int ni = i + DX[k], nj = j + DZ[k];
                if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
                int nc = (nj << W_SHIFT) | ni;
                if (pushed[nc] || overCorner(i, j, k)) continue;
                filled[nc] = std::max(h[nc], base);
                pushed[nc] = 1;
                pushHeap(heapKeyFor(filled[nc], nc));
            }
        }
    }

    void computeD8() {
        for (int c = 0; c < N; ++c) {
            if (sink[c]) { dir[c] = -1; continue; }
            int i = c & W_MASK, j = c >> W_SHIFT;
            float fc = filled[c];
            int best = -1;
            float bestSlope = 0.0f;
            for (int k = 0; k < 8; ++k) {
                int ni = i + DX[k], nj = j + DZ[k];
                if (ni < 0 || nj < 0 || ni >= W || nj >= W || overCorner(i, j, k)) continue;
                float sl = (fc - filled[(nj << W_SHIFT) | ni]) / DIST[k];
                if (sl > bestSlope) { bestSlope = sl; best = k; }
            }
            double lean = (best < 0) ? 0.0
                : ROUTE_WANDER * double(wanderPull[c]) * mthClamp(2.0 - double(bestSlope) / ROUTE_WANDER_SLOPE, 0.0, 1.0);
            if (lean > 0.0) {
                double theta = wanderWay[c];
                int steepest = best;
                int tx = floorDiv(cx0 + i, CORE), tz = floorDiv(cz0 + j, CORE);
                double bestScore = 0.0;
                for (int k = 0; k < 8; ++k) {
                    int ni = i + DX[k], nj = j + DZ[k];
                    if (ni < 0 || nj < 0 || ni >= W || nj >= W || overCorner(i, j, k)) continue;
                    float sl = (fc - filled[(nj << W_SHIFT) | ni]) / DIST[k];
                    if (sl <= 0.0f) continue;
                    if (k != steepest && (floorDiv(cx0 + ni, CORE) != tx || floorDiv(cz0 + nj, CORE) != tz)) continue;
                    double score = double(sl) * std::exp(lean * std::cos(DIR_ANGLE[k] - theta));
                    if (score > bestScore) { bestScore = score; best = k; }
                }
            }
            dir[c] = int8_t(best);
        }
    }

    void accumulate() {
        std::fill(acc.begin(), acc.end(), 1);
        std::fill(flowLen.begin(), flowLen.end(), 1);
        wet = rain;
        for (int k = orderCount - 1; k >= 0; --k) {
            int c = order[k];
            int d = downstream(c);
            if (d >= 0) {
                acc[d] += acc[c];
                wet[d] += wet[c];
                if (flowLen[c] + 1 > flowLen[d]) flowLen[d] = flowLen[c] + 1;
            }
        }
    }

    bool startsStream(int c, int lessWet, int lessAcc) const {
        return !cut[c] && wet[c] - lessWet >= needRain[c] && acc[c] - lessAcc >= needConvergence[c] * float(flowLen[c]);
    }

    bool onCourse(int from, int target, int cells) const {
        for (int s = 0, e = from; s <= cells && e >= 0; ++s, e = downstream(e)) {
            if (e == target) return true;
        }
        return false;
    }

    bool standing(int c) const { return !lakeId.empty() && (lakeId[c] >= 0 || lakeShore[c] >= 0); }

    void capture() {
        bool changed = false;
        for (int c = 0; c < N; ++c) {
            if (sink[c] || wet[c] < needRain[c]) continue;
            int d = downstream(c);
            if (d < 0 || acc[d] >= acc[c] * CAPTURE_RATIO) continue;
            int i = c & W_MASK, j = c >> W_SHIFT;
            int best = -1;
            int bestAcc = acc[c] * CAPTURE_RATIO;
            for (int k = 0; k < 8; ++k) {
                int ni = i + DX[k], nj = j + DZ[k];
                if (ni < 0 || nj < 0 || ni >= W || nj >= W || overCorner(i, j, k)) continue;
                int nc = (nj << W_SHIFT) | ni;
                if (nc == d || filled[nc] >= filled[c]) continue;
                if (acc[nc] > bestAcc) { bestAcc = acc[nc]; best = k; }
            }
            if (best >= 0) { dir[c] = int8_t(best); changed = true; }
        }
        if (changed) accumulate();
    }

    void fuse() {
        std::vector<int> end(N);
        for (int k = 0; k < orderCount; ++k) {
            int c = order[k], d = downstream(c);
            end[c] = (d < 0) ? c : end[d];
        }
        std::vector<uint8_t> stream(N, 0), ins(N, 0);
        for (int k = orderCount - 1; k >= 0; --k) {
            int c = order[k];
            if (sink[c]) continue;
            if (!stream[c]) stream[c] = startsStream(c, 0, 0) ? 1 : 0;
            int d = downstream(c);
            if (stream[c] && d >= 0 && !sink[d]) {
                stream[d] = 1;
                ins[d]++;
            }
        }
        bool changed = false;
        for (int k = orderCount - 1; k >= 0; --k) {
            int c = order[k];
            if (!stream[c]) continue;
            int d = downstream(c);
            if (d < 0 || sink[d]) continue;
            int i = c & W_MASK, j = c >> W_SHIFT;
            int fi = (d & W_MASK) - i, fj = (d >> W_SHIFT) - j;
            int best = -1;
            float bestH = filled[c];
            for (int e = 0; e < 8; ++e) {
                int ni = i + DX[e], nj = j + DZ[e];
                if (ni < 0 || nj < 0 || ni >= W || nj >= W || overCorner(i, j, e)) continue;
                int nc = (nj << W_SHIFT) | ni;
                if (nc == d || !stream[nc] || sink[nc] || end[nc] != end[c] || filled[nc] >= bestH) continue;
                if (DX[e] * fi + DZ[e] * fj < 0) continue;
                int nd = downstream(nc);
                if (nd < 0 || ((nd & W_MASK) - ni) * fi + ((nd >> W_SHIFT) - nj) * fj < 0) continue;
                if (onCourse(d, nc, FUSE_OWN_CELLS)) continue;
                best = e; bestH = filled[nc];
            }
            int via = -1;
            if (best < 0) {
                for (int b = -2; b <= 2; ++b) {
                    for (int a = -2; a <= 2; ++a) {
                        if (std::max(std::abs(a), std::abs(b)) != 2) continue;
                        int ni = i + a, nj = j + b;
                        if (ni < 0 || nj < 0 || ni >= W || nj >= W || a * fi + b * fj < 0) continue;
                        int nc = (nj << W_SHIFT) | ni;
                        if (!stream[nc] || sink[nc] || end[nc] != end[c] || filled[nc] >= bestH) continue;
                        int nd = downstream(nc);
                        if (nd < 0 || ((nd & W_MASK) - ni) * fi + ((nd >> W_SHIFT) - nj) * fj < 0) continue;
                        if (onCourse(d, nc, FUSE_OWN_CELLS)) continue;
                        for (int e = 0; e < 8; ++e) {
                            int mi = i + DX[e], mj = j + DZ[e];
                            if (std::abs(ni - mi) > 1 || std::abs(nj - mj) > 1 || DX[e] * fi + DZ[e] * fj < 0) continue;
                            int mc = (mj << W_SHIFT) | mi;
                            if (stream[mc] || sink[mc] || filled[mc] >= filled[c] || filled[mc] <= filled[nc]) continue;
                            if (overCornerCells(c, mc) || overCornerCells(mc, nc)) continue;
                            best = e; via = nc; bestH = filled[nc];
                            break;
                        }
                    }
                }
            }
            if (best < 0) continue;
            int n = ((j + DZ[best]) << W_SHIFT) | (i + DX[best]);
            dir[c] = int8_t(best);
            if (via >= 0) {
                int vi = (via & W_MASK) - (n & W_MASK), vj = (via >> W_SHIFT) - (n >> W_SHIFT);
                for (int e = 0; e < 8; ++e) {
                    if (DX[e] == vi && DZ[e] == vj) dir[n] = int8_t(e);
                }
                stream[n] = 1;
                ins[via]++;
            }
            ins[n]++;
            changed = true;
            for (int e = d; e >= 0 && !sink[e] && stream[e]; e = downstream(e)) {
                if (--ins[e] > 0 || startsStream(e, wet[c], acc[c])) break;
                stream[e] = 0;
            }
        }
        if (changed) accumulate();
    }

    void reorder() {
        std::vector<int> head(N, -1), next(N, -1), out(N, 0);
        int count = 0;
        for (int c = 0; c < N; ++c) {
            int d = downstream(c);
            if (d < 0) out[count++] = c;
            else { next[c] = head[d]; head[d] = c; }
        }
        for (int k = 0; k < count; ++k) {
            for (int u = head[out[k]]; u >= 0; u = next[u]) out[count++] = u;
        }
        if (count != N) return;
        order = std::move(out);
        orderCount = N;
    }

    void accumulateCore() {
        std::vector<int> acc2(N, 0), wet2(N, 0), len2(N, 0);
        for (int c = 0; c < N; ++c) {
            if (!inCore(c)) continue;
            acc2[c] = 1 + inAcc[c];
            wet2[c] = rain[c] + inWet[c];
            len2[c] = std::max(1, inLen[c]);
        }
        for (int k = orderCount - 1; k >= 0; --k) {
            int c = order[k];
            int d = downstream(c);
            if (d < 0 || !inCore(d)) continue;
            if (inCore(c)) {
                acc2[d] += acc2[c];
                wet2[d] += wet2[c];
                if (len2[c] + 1 > len2[d]) len2[d] = len2[c] + 1;
            } else if (!takenOver[c]) {
                acc2[d] += acc[c];
                wet2[d] += wet[c];
                if (flowLen[c] + 1 > len2[d]) len2[d] = flowLen[c] + 1;
            }
        }
        for (int c = 0; c < N; ++c) {
            if (!inCore(c)) continue;
            acc[c] = acc2[c];
            wet[c] = wet2[c];
            flowLen[c] = len2[c];
        }
    }

    void fuseWide(bool lakesBuilt) {
        bool fin = seamsImported;
        std::vector<int> end(N), out(fin ? N : 0, -1);
        for (int k = 0; k < orderCount; ++k) {
            int c = order[k], d = downstream(c);
            end[c] = (d < 0) ? c : end[d];
            if (fin && inCore(c)) out[c] = (d < 0 || sink[d]) ? -1 : (inCore(d) ? out[d] : c);
        }
        std::vector<uint8_t> stream(N, 0), ins(N, 0);
        for (int k = orderCount - 1; k >= 0; --k) {
            int c = order[k];
            if (sink[c]) continue;
            if (!stream[c]) stream[c] = (startsStream(c, 0, 0) || (fin && inflow[c])) ? 1 : 0;
            int d = downstream(c);
            if (stream[c] && d >= 0 && !sink[d]) {
                stream[d] = 1;
                ins[d]++;
            }
        }
        std::vector<float> width(N, 0.0f);
        for (int c = 0; c < N; ++c) if (stream[c]) width[c] = float(riverWidth(double(wet[c]) / double(RAIN_UNITS)));

        std::vector<int> own(N, 0);
        int path[64]{};
        int stamp = 0;
        bool changed = false;
        auto isStanding = [&](int c) { return lakesBuilt && (lakeId[c] >= 0 || lakeShore[c] >= 0); };
        for (int k = orderCount - 1; k >= 0; --k) {
            int c = order[k];
            if (!stream[c] || (fin && !inCore(c)) || isStanding(c)) continue;
            int d = downstream(c);
            if (d < 0 || sink[d]) continue;
            int i = c & W_MASK, j = c >> W_SHIFT;
            int tx = floorDiv(cx0 + i, CORE), tz = floorDiv(cz0 + j, CORE);
            int fi = (d & W_MASK) - i, fj = (d >> W_SHIFT) - j;
            int reach = int(std::ceil((width[c] + MAX_WIDTH + FUSE_WIDE_GAP) / double(CELL)));
            stamp++;
            own[c] = stamp;
            for (int s = 0, e = d; s < FUSE_OWN_CELLS && e >= 0; ++s, e = downstream(e)) own[e] = stamp;
            int target = -1;
            double bestGap = FUSE_WIDE_GAP;
            for (int b = -reach; b <= reach; ++b) {
                int nj = j + b;
                if (nj < 0 || nj >= W) continue;
                for (int a = -reach; a <= reach; ++a) {
                    int ni = i + a;
                    if (ni < 0 || ni >= W) continue;
                    int t = (nj << W_SHIFT) | ni;
                    if (!stream[t] || own[t] == stamp || sink[t] || end[t] != end[c] || filled[t] >= filled[c]
                            || wet[t] < wet[c] || isStanding(t)) continue;
                    double gap = std::hypot(double(a), double(b)) * double(CELL) - double(width[c]) - double(width[t]);
                    if (gap >= bestGap) continue;
                    if (floorDiv(cx0 + ni, CORE) != tx || floorDiv(cz0 + nj, CORE) != tz) continue;
                    if (fin && out[t] != out[c]) continue;
                    int td = downstream(t);
                    if (td < 0 || ((td & W_MASK) - ni) * fi + ((td >> W_SHIFT) - nj) * fj < 0) continue;
                    bestGap = gap;
                    target = t;
                }
            }
            if (target < 0) continue;
            int ti = target & W_MASK, tj = target >> W_SHIFT;
            int len = 0, cur = c;
            path[len++] = c;
            bool reached = false;
            while (len < 64) {
                int ci = cur & W_MASK, cj = cur >> W_SHIFT;
                double from = std::hypot(double(ti - ci), double(tj - cj));
                int next = -1;
                double nearest = from;
                for (int e = 0; e < 8; ++e) {
                    int ni = ci + DX[e], nj = cj + DZ[e];
                    if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
                    int nc = (nj << W_SHIFT) | ni;
                    if (sink[nc] || filled[nc] >= filled[cur] || isStanding(nc)) continue;
                    if (floorDiv(cx0 + ni, CORE) != tx || floorDiv(cz0 + nj, CORE) != tz) continue;
                    double to = std::hypot(double(ti - ni), double(tj - nj));
                    if (to < nearest) { nearest = to; next = nc; }
                }
                if (next < 0 || own[next] == stamp) break;
                path[len++] = next;
                if (stream[next]) {
                    reached = (end[next] == end[c]) && (!fin || out[next] == out[c]);
                    break;
                }
                cur = next;
            }
            if (!reached) continue;
            if (fin) {
                bool loop = false;
                for (int s = 0, e = path[len - 1]; s < N && e >= 0 && !loop; ++s, e = downstream(e)) {
                    for (int q = 0; q < len - 1; ++q) if (path[q] == e) loop = true;
                }
                if (loop) continue;
            }
            for (int s = 0; s < len - 1; ++s) {
                int p = path[s], q = path[s + 1];
                int di = (q & W_MASK) - (p & W_MASK), dj = (q >> W_SHIFT) - (p >> W_SHIFT);
                for (int e = 0; e < 8; ++e) if (DX[e] == di && DZ[e] == dj) dir[p] = int8_t(e);
                if (s > 0) stream[p] = 1;
            }
            ins[path[1]]++;
            for (int s = 2; s < len; ++s) ins[path[s]]++;
            changed = true;
            for (int e = d; e >= 0 && !sink[e] && stream[e]; e = downstream(e)) {
                if (--ins[e] > 0 || startsStream(e, wet[c], acc[c])) break;
                stream[e] = 0;
            }
        }
        if (!changed) return;
        if (fin) { reorder(); accumulateCore(); }
        else accumulate();
    }

    void joinAlongside() {
        std::vector<uint8_t> ins(N, 0);
        for (int k = orderCount - 1; k >= 0; --k) {
            int c = order[k], d = downstream(c);
            if (d >= 0 && !sink[c] && (ins[c] > 0 || startsStream(c, 0, 0))) ins[d]++;
        }
        bool changed = false;
        for (int k = orderCount - 1; k >= 0; --k) {
            int c = order[k];
            if (sink[c] || (ins[c] == 0 && !startsStream(c, 0, 0))) continue;
            int i = c & W_MASK, j = c >> W_SHIFT;
            int first = downstream(c);
            bool confluence = false;
            int e = first;
            for (int s = 1; s <= JOIN_ALONGSIDE_CELLS && e >= 0 && !sink[e]; ++s) {
                if (ins[e] >= 2) confluence = true;
                int next = downstream(e);
                if (next < 0) break;
                if (confluence && std::abs((next & W_MASK) - i) <= 1 && std::abs((next >> W_SHIFT) - j) <= 1 && !overCornerCells(c, next)) {
                    for (int a = 0; a < 8; ++a) {
                        if (i + DX[a] == (next & W_MASK) && j + DZ[a] == (next >> W_SHIFT)) dir[c] = int8_t(a);
                    }
                    ins[first]--;
                    ins[next]++;
                    changed = true;
                    break;
                }
                e = next;
            }
        }
        if (changed) accumulate();
    }

    static int ufRoot(std::vector<int>& parent, int a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    }

    int keptAt(int a, int y) const {
        int own = lakeWaterY[a];
        const auto& cells = lakeCells[a];
        if (y == own) return int(cells.size());
        if (y > own) return 0;
        int kept = 0, deep = 0;
        float deepest = std::numeric_limits<float>::infinity();
        for (int c : cells) {
            if (h[c] <= float(y)) {
                kept++;
                if (h[c] < float(y) - 0.5f) deep++;
                if (h[c] < deepest) deepest = h[c];
            }
        }
        return (kept >= LAKE_MIN_CELLS_FED && float(y + 1) - deepest >= LAKE_MIN_DEPTH_FED
                && double(deep) >= LAKE_DEEP_SHARE * double(kept)) ? kept : 0;
    }

    bool settleLake(int a, int y) {
        int own = lakeWaterY[a];
        const auto& cells = lakeCells[a];
        if (y == own || cells.empty()) return false;
        int kept = keptAt(a, y);
        std::vector<int> next;
        next.reserve(kept);
        for (int c : cells) {
            if (kept > 0 && h[c] <= float(y)) next.push_back(c);
            else lakeId[c] = -1;
        }
        lakeCells[a] = std::move(next);
        for (int c : lakeShoreCells[a]) {
            if (lakeShore[c] == a) lakeShore[c] = -1;
        }
        lakeShoreCells[a].clear();
        if (closedLake(a)) closedChanged = true;
        if (kept > 0) lakeWaterY[a] = y;
        return true;
    }

    void extractLakes() {
        std::vector<int64_t> pits;
        pits.reserve(1024);
        for (int c = 0; c < N; ++c) {
            if (sink[c] || filled[c] - h[c] < LAKE_MIN_DEPTH_FED) continue;
            int i = c & W_MASK, j = c >> W_SHIFT;
            bool minimum = true;
            for (int k = 0; k < 8; ++k) {
                int ni = i + DX[k], nj = j + DZ[k];
                if (ni < 0 || nj < 0 || ni >= W || nj >= W) { minimum = false; break; }
                int nc = (nj << W_SHIFT) | ni;
                if (h[nc] < h[c] || (h[nc] == h[c] && nc < c)) { minimum = false; break; }
            }
            if (minimum) pits.push_back(heapKeyFor(h[c], c));
        }
        std::sort(pits.begin(), pits.end());

        std::vector<int> stamp(N, 0), water(N, 0), region(LAKE_MAX_GROWTH);
        std::vector<int> marked(LAKE_MAX_GROWTH * 8 + 8), markedWas(LAKE_MAX_GROWTH * 8 + 8);
        int growth = 0;
        for (int64_t pk : pits) {
            int pit = int(uint64_t(pk) & CELL_KEY_MASK);
            if (lakeId[pit] != -1 || stamp[pit] != 0) continue;
            growth++;
            heapSize = 0;
            pushHeap(heapKeyFor(h[pit], pit));
            stamp[pit] = growth;
            int marks = 0;
            markedWas[marks] = 0;
            marked[marks++] = pit;
            int size = 0;
            float rim = h[pit];
            bool escaped = false;
            int probeFrom = -1, probeMarks = 0, lastRise = 0;
            int loI = W, hiI = -1, loJ = W, hiJ = -1;
            while (heapSize > 0) {
                int r = int(uint64_t(popHeap()) & CELL_KEY_MASK);
                int i = r & W_MASK, j = r >> W_SHIFT;
                if (probeFrom >= 0 && h[r] >= rim) probeFrom = -1;
                bool border = i < LAKE_BORDER_MARGIN || j < LAKE_BORDER_MARGIN
                           || i >= W - LAKE_BORDER_MARGIN || j >= W - LAKE_BORDER_MARGIN;
                bool wide = std::max(hiI, i) - std::min(loI, i) >= LAKE_MAX_EXTENT
                         || std::max(hiJ, j) - std::min(loJ, j) >= LAKE_MAX_EXTENT;
                if (sink[r] || border || wide || size >= LAKE_MAX_GROWTH) {
                    if (probeFrom >= 0) {
                        for (int k = probeMarks; k < marks; ++k) stamp[marked[k]] = markedWas[k];
                        size = probeFrom;
                    } else if (!sink[r]) {
                        if (border || float(mthFloorF(rim) - 1) - h[pit] < LAKE_WIDE_DEPTH) escaped = true;
                    }
                    break;
                }
                if (probeFrom < 0) {
                    if (h[r] < rim) { probeFrom = size; probeMarks = lastRise; }
                    else rim = h[r];
                }
                region[size++] = r;
                if (i < loI) loI = i; if (i > hiI) hiI = i;
                if (j < loJ) loJ = j; if (j > hiJ) hiJ = j;
                if (probeFrom < 0) lastRise = marks;
                for (int k = 0; k < 8; ++k) {
                    int ni = i + DX[k], nj = j + DZ[k];
                    if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
                    int nc = (nj << W_SHIFT) | ni;
                    if (stamp[nc] == growth) continue;
                    markedWas[marks] = stamp[nc];
                    stamp[nc] = growth;
                    marked[marks++] = nc;
                    pushHeap(heapKeyFor(h[nc], nc));
                }
            }
            int waterY = mthFloorF(rim) - 1;
            float depth = rim - h[pit];
            int count = 0, deep = 0;
            bool fed = false, byRiver = false;
            float deepest = rim;
            int lo_i = W, hi_i = -1, lo_j = W, hi_j = -1;
            for (int k = 0; k < size; ++k) {
                int c = region[k];
                int i = c & W_MASK, j = c >> W_SHIFT;
                if (i < lo_i) lo_i = i; if (i > hi_i) hi_i = i;
                if (j < lo_j) lo_j = j; if (j > hi_j) hi_j = j;
                if (h[c] > float(waterY)) continue;
                count++;
                if (h[c] < float(waterY) - 0.5f) deep++;
                if (h[c] < deepest) deepest = h[c];
                if (wet[c] >= streamRain) fed = true;
                if (nearRiver[c]) byRiver = true;
            }
            bool tooWide = hi_i - lo_i >= LAKE_MAX_EXTENT || hi_j - lo_j >= LAKE_MAX_EXTENT;
            int minCells = lakeMinCells(pit, fed);
            float width = float(std::sqrt(double(count))) * float(CELL);
            bool bottomless = float(waterY) - deepest > std::max(LAKE_MAX_DEPTH, LAKE_SHAFT_RATIO * width);
            float minDepth = fed ? LAKE_MIN_DEPTH_FED : LAKE_MIN_DEPTH;
            bool pond = fed && depth >= POND_MIN_DEPTH;
            if (escaped || tooWide || byRiver || (bottomless && !pond) || (count < minCells && !pond)
                    || count > LAKE_MAX_CELLS || depth < minDepth || waterY < seaLevel - 1 + LAKE_ABOVE_SEA
                    || double(deep) < LAKE_DEEP_SHARE * double(count)) {
                continue;
            }
            if (!pond && count < LAKE_ROUND_CELLS) {
                for (int k = 0; k < size; ++k) if (h[region[k]] <= float(waterY)) water[region[k]] = growth;
                int edges = 0;
                for (int k = 0; k < size; ++k) {
                    int c = region[k];
                    if (h[c] > float(waterY)) continue;
                    int i = c & W_MASK, j = c >> W_SHIFT;
                    for (int e = 0; e < 8; e += 2) {
                        int ni = i + DX[e], nj = j + DZ[e];
                        if (ni < 0 || nj < 0 || ni >= W || nj >= W || water[(nj << W_SHIFT) | ni] != growth) edges++;
                    }
                }
                if (double(edges) < LAKE_MIN_SHAPE * std::sqrt(double(count))) continue;
            }
            int id = int(lakeCells.size());
            std::vector<int> cells;
            cells.reserve(count);
            for (int k = 0; k < size; ++k) {
                int c = region[k];
                if (h[c] > float(waterY)) continue;
                lakeId[c] = id;
                cells.push_back(c);
            }
            lakeCells.push_back(std::move(cells));
            std::vector<int> shore;
            shore.reserve(size - count);
            for (int k = 0; k < size; ++k) {
                int c = region[k];
                if (lakeId[c] >= 0 || lakeShore[c] >= 0) continue;
                lakeShore[c] = id;
                shore.push_back(c);
            }
            lakeShoreCells.push_back(std::move(shore));
            lakeWaterY.push_back(waterY);
        }
    }

    void settleLakeChains() {
        int n = int(lakeCells.size());
        if (n < 2) return;
        std::vector<int> parent(n), members(n);
        std::vector<uint8_t> seen(n);
        for (int round = 0; round < 16; ++round) {
            for (int a = 0; a < n; ++a) parent[a] = a;
            bool anyPair = false;
            for (int a = 0; a < n; ++a) {
                int ya = lakeWaterY[a];
                for (int c : lakeCells[a]) {
                    int i = c & W_MASK, j = c >> W_SHIFT;
                    int i0 = std::max(0, i - LAKE_CHAIN_CELLS), i1 = std::min(W - 1, i + LAKE_CHAIN_CELLS);
                    int j0 = std::max(0, j - LAKE_CHAIN_CELLS), j1 = std::min(W - 1, j + LAKE_CHAIN_CELLS);
                    for (int nj = j0; nj <= j1; ++nj) {
                        for (int ni = i0; ni <= i1; ++ni) {
                            int b = lakeId[(nj << W_SHIFT) | ni];
                            if (b <= a || std::abs(lakeWaterY[b] - ya) > LAKE_CHAIN_STEP) continue;
                            anyPair = true;
                            int ra = ufRoot(parent, a), rb = ufRoot(parent, b);
                            if (ra != rb) parent[std::max(ra, rb)] = std::min(ra, rb);
                        }
                    }
                }
            }
            if (!anyPair) return;
            bool changed = false;
            std::fill(seen.begin(), seen.end(), 0);
            for (int a = 0; a < n; ++a) {
                int g = ufRoot(parent, a);
                if (seen[g]) continue;
                seen[g] = 1;
                int count = 0, lo = INT32_MAX, hi = INT32_MIN;
                for (int b = a; b < n; ++b) {
                    if (ufRoot(parent, b) != g) continue;
                    members[count++] = b;
                    int y = lakeWaterY[b];
                    if (y < lo) lo = y;
                    if (y > hi) hi = y;
                }
                if (lo == hi) continue;
                int bestY = lo;
                int64_t bestKept = -1;
                for (int y = lo; y <= hi; ++y) {
                    bool candidate = false;
                    for (int k = 0; k < count && !candidate; ++k) candidate = (lakeWaterY[members[k]] == y);
                    if (!candidate) continue;
                    int64_t kept = 0;
                    for (int k = 0; k < count; ++k) kept += keptAt(members[k], y);
                    if (kept > bestKept) { bestKept = kept; bestY = y; }
                }
                for (int k = 0; k < count; ++k) {
                    if (settleLake(members[k], bestY)) changed = true;
                }
            }
            if (!changed) return;
        }
    }

    bool closeLakes() {
        int n = int(lakeCells.size());
        if (int(lakeClosed.size()) < n) lakeClosed.resize(n, 0);
        bool any = closedChanged;
        closedChanged = false;
        for (int a = 0; a < n; ++a) {
            const auto& cells = lakeCells[a];
            if (lakeClosed[a] || cells.empty()) continue;
            float spill = std::numeric_limits<float>::infinity();
            for (int c : cells) spill = std::min(spill, filled[c]);
            if (spill - float(lakeWaterY[a]) > LAKE_CLOSED_RISE) {
                lakeClosed[a] = 1;
                any = true;
            }
        }
        return any;
    }

    bool sharedLake(int a) const {
        const auto& cells = lakeCells[a];
        if (cells.empty()) return false;
        int tx = floorDiv(cx0 + (cells[0] & W_MASK), CORE), tz = floorDiv(cz0 + (cells[0] >> W_SHIFT), CORE);
        for (int c : cells) {
            if (floorDiv(cx0 + (c & W_MASK), CORE) != tx || floorDiv(cz0 + (c >> W_SHIFT), CORE) != tz) return true;
        }
        return false;
    }

    int underLevel(int a, float level) const {
        int routed = dwRoundF(level);
        return routed < lakeWaterY[a] ? routed - CHANNEL_INCISION : INT32_MAX;
    }

    bool heldUnder(int c, int level) const {
        if (dwRoundF(breached[c]) >= level) return false;
        std::vector<uint8_t> seen(N, 0);
        std::vector<int> stack;
        stack.push_back(c);
        seen[c] = 1;
        while (!stack.empty()) {
            int e = stack.back(); stack.pop_back();
            if (seamsImported && inflow[e] && dwRoundF(seamLevel[e]) < level) return true;
            int i = e & W_MASK, j = e >> W_SHIFT;
            int ups = 0;
            for (int k = 0; k < 8; ++k) {
                int ni = i + DX[k], nj = j + DZ[k];
                if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
                int nc = (nj << W_SHIFT) | ni;
                if (!chan[nc] || downstream(nc) != e) continue;
                if (++ups >= 2 || regionIndex(nc) != regionIndex(e)) return true;
                if (seen[nc] || dwRoundF(breached[nc]) >= level) continue;
                seen[nc] = 1;
                stack.push_back(nc);
            }
        }
        return false;
    }

    bool lowerUndercutLakes() {
        int n = int(lakeCells.size());
        if (n == 0) return false;
        std::vector<int> under(n, INT32_MAX);
        for (int c = 0; c < N; ++c) {
            int own = (lakeId[c] >= 0) ? lakeId[c] : lakeShore[c];
            if (own >= 0) {
                if (seamsImported && inflow[c]) under[own] = std::min(under[own], underLevel(own, seamLevel[c]));
                continue;
            }
            int d = chan[c] ? downstream(c) : -1;
            int lake = (d < 0) ? -1 : ((lakeId[d] >= 0) ? lakeId[d] : lakeShore[d]);
            if (lake >= 0 && heldUnder(c, lakeWaterY[lake])) {
                under[lake] = std::min(under[lake], underLevel(lake, breached[c]));
            }
        }
        bool changed = false;
        for (int a = 0; a < n; ++a) {
            if (seamsImported && closedLake(a)) continue;
            if (sharedLake(a)) continue;
            if (under[a] < lakeWaterY[a] && settleLake(a, under[a])) changed = true;
        }
        return changed;
    }

    void settle(const std::vector<float>* floor) {
        for (int c = 0; c < N; ++c) {
            breached[c] = (lakeId[c] >= 0) ? float(lakeWaterY[lakeId[c]])
                        : sink[c] ? float(seaLevel - 1)
                        : (lakeShore[c] >= 0 && !closedLake(lakeShore[c])) ? float(lakeWaterY[lakeShore[c]])
                        : (wet[c] < needRain[c] && filled[c] - h[c] >= POND_MIN_DEPTH) ? filled[c]
                        : std::min(filled[c], h[c] + MAX_FILL);
            if (seamsImported && seamLevel[c] < breached[c] && lakeId[c] < 0 && lakeShore[c] < 0 && !sink[c]) {
                breached[c] = seamLevel[c];
            }
            if (floor && breached[c] < (*floor)[c]) breached[c] = (*floor)[c];
        }
        for (int k = orderCount - 1; k >= 0; --k) {
            int c = order[k], d = downstream(c);
            if (d < 0 || lakeId[d] >= 0 || lakeShore[d] >= 0 || sink[d]) continue;
            if (seamsImported && takenOver[c] && inCore(d)) continue;
            if (breached[c] < breached[d]) breached[d] = breached[c];
        }
    }

    std::vector<float> holdExits(bool& outAny) {
        outAny = false;
        std::vector<uint8_t> seen(N, 0);
        std::vector<int> stack(N, 0);
        std::vector<float> held;
        for (const auto& e : ownSeams->exits) {
            int c = windowCell(e.fromX, e.fromZ);
            if (c < 0 || dropped[c] || lakeId[c] >= 0) continue;
            int d = downstream(c);
            if (d < 0 || inCore(d)) continue;
            float floorVal = float(dwRoundF(e.level)) - 0.5f;
            if (breached[c] >= floorVal) continue;
            int top = 0, count = 0;
            stack[top++] = c;
            seen[c] = 1;
            bool hold = true;
            while (count < top) {
                int u = stack[count++];
                if (!inCore(u) || lakeId[u] >= 0 || lakeShore[u] >= 0 || sink[u]
                        || (inflow[u] && seamLevel[u] < floorVal) || floorVal > h[u] + MAX_FILL) {
                    hold = false;
                    break;
                }
                int i = u & W_MASK, j = u >> W_SHIFT;
                for (int k = 0; k < 8; ++k) {
                    int ni = i + DX[k], nj = j + DZ[k];
                    if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
                    int v = (nj << W_SHIFT) | ni;
                    if (seen[v] || breached[v] >= floorVal || downstream(v) != u) continue;
                    if (takenOver[v] && !inCore(v)) continue;
                    seen[v] = 1;
                    stack[top++] = v;
                }
            }
            if (hold) {
                if (!outAny) {
                    held.assign(N, -std::numeric_limits<float>::infinity());
                    outAny = true;
                }
                for (int k = 0; k < top; ++k) held[stack[k]] = floorVal;
            }
            for (int k = 0; k < top; ++k) seen[stack[k]] = 0;
        }
        return held;
    }

    void breach() {
        settle(nullptr);
        if (seamsImported) {
            bool any = false;
            std::vector<float> fl = holdExits(any);
            if (any) settle(&fl);
        }
        bool gorgeWalls = !cfg.normalRiverTunnels;
        if (cfg.mountainRiverTunnels || gorgeWalls) {
            std::vector<uint8_t> ramped(N, 0);
            for (int k = 0; k < orderCount; ++k) {
                int c = order[k], d = downstream(c);
                if (d < 0 || lakeId[c] >= 0 || lakeShore[c] >= 0) continue;
                if (!sink[d] && !ramped[d]) continue;
                if (gorgeWalls && mainRiver[d]) continue;
                float limit = breached[d] + MOUTH_RAMP_PER_CELL;
                if (breached[c] > limit) {
                    breached[c] = limit;
                    ramped[c] = 1;
                }
            }
        }
    }

    void computeChannels() {
        for (int c = 0; c < N; ++c) {
            chan[c] = (!sink[c] && lakeId[c] < 0 && !(seamsImported && takenOver[c])
                    && ((seamsImported && inflow[c])
                        || (!cut[c] && wet[c] >= needRain[c] && float(acc[c]) >= needConvergence[c] * float(flowLen[c]))))
                ? 1 : 0;
        }
        for (int c = 0; c < N; ++c) {
            if (!chan[c]) continue;
            int d = downstream(c);
            while (d >= 0 && !chan[d] && !sink[d] && lakeId[d] < 0) {
                chan[d] = 1;
                if (seamsImported && takenOver[d]) break;
                d = downstream(d);
            }
        }
    }

    void reroute() {
        flood();
        computeD8();
        accumulate();
        capture();
        fuse();
        fuseWide(true);
        joinAlongside();
    }

    void route() {
        heights.fillWindow(cx0, cz0, W, W, h.data());
        despike();
        routeNoise();
        wanderField();
        densityField();

        stampSinks();
        feedField();
        flood();
        computeD8();
        accumulate();
        capture();
        fuse();
        fuseWide(false);
        joinAlongside();

        std::fill(lakeId.begin(), lakeId.end(), -1);
        std::fill(lakeShore.begin(), lakeShore.end(), -1);
        if (lakesEnabled) {
            extractLakes();
            settleLakeChains();
            if (closeLakes()) reroute();
        }
        breach();
        computeChannels();
        for (int round = 0; round < LAKE_UNDERCUT_ROUNDS && lowerUndercutLakes(); ++round) {
            if (closeLakes()) reroute();
            breach();
            computeChannels();
        }
    }

    bool seamFlat(int n) const {
        int dx = SEAM_NEIGHBOURS[n][0], dz = SEAM_NEIGHBOURS[n][1];
        int seam = (dx + dz > 0) ? (HALO + CORE) : HALO;
        for (int s = 0; s < CORE / SEAM_SEGMENT; ++s) {
            int along0 = HALO + s * SEAM_SEGMENT, along1 = along0 + SEAM_SEGMENT;
            int i0 = (dx != 0) ? seam - SEAM_BAND : along0, i1 = (dx != 0) ? seam + SEAM_BAND : along1;
            int j0 = (dz != 0) ? seam - SEAM_BAND : along0, j1 = (dz != 0) ? seam + SEAM_BAND : along1;
            double sum = 0.0;
            int pairs = 0;
            for (int j = j0; j < j1; ++j) {
                for (int i = i0; i < i1; ++i) {
                    int c = (j << W_SHIFT) | i;
                    if (i + 1 < i1) { sum += std::abs(h[c] - h[c + 1]); pairs++; }
                    if (j + 1 < j1) { sum += std::abs(h[c] - h[c + W]); pairs++; }
                }
            }
            if (sum < double(SEAM_FLAT_RELIEF) * double(pairs)) return true;
        }
        return false;
    }

    bool seamPooled(int n) {
        int dx = SEAM_NEIGHBOURS[n][0], dz = SEAM_NEIGHBOURS[n][1];
        int seam = (dx + dz > 0) ? (HALO + CORE) : HALO;
        int i0 = (dx != 0) ? seam - SEAM_POOL_SHARED : 0, j0 = (dz != 0) ? seam - SEAM_POOL_SHARED : 0;
        int sw = (dx != 0) ? 2 * SEAM_POOL_SHARED : W, sh = (dz != 0) ? 2 * SEAM_POOL_SHARED : W;
        int sn = sw * sh;
        std::vector<float> fill(sn, 0.0f);
        std::vector<uint8_t> seen(sn, 0);
        std::vector<int> popped(sn, 0);
        int count = 0;
        heapSize = 0;
        for (int lj = 0; lj < sh; ++lj) {
            for (int li = 0; li < sw; ++li) {
                int l = lj * sw + li, c = ((j0 + lj) << W_SHIFT) | (i0 + li);
                if (li == 0 || lj == 0 || li == sw - 1 || lj == sh - 1 || sink[c]) {
                    fill[l] = mainRiver[c] ? std::min(h[c], float(seaLevel - 1)) : h[c];
                    seen[l] = 1;
                    pushHeap(heapKeyFor(fill[l], l));
                }
            }
        }
        while (heapSize > 0) {
            int l = int(uint64_t(popHeap()) & CELL_KEY_MASK);
            popped[count++] = l;
            int li = l % sw, lj = l / sw;
            float base = fill[l] + EPS;
            for (int k = 0; k < 8; ++k) {
                int ni = li + DX[k], nj = lj + DZ[k];
                if (ni < 0 || nj < 0 || ni >= sw || nj >= sh) continue;
                int nl = nj * sw + ni;
                if (seen[nl]) continue;
                seen[nl] = 1;
                fill[nl] = std::max(h[((j0 + nj) << W_SHIFT) | (i0 + ni)], base);
                pushHeap(heapKeyFor(fill[nl], nl));
            }
        }
        std::vector<uint8_t> across(sn, 0);
        std::vector<int> to(sn, -1);
        for (int s = 0; s < count; ++s) {
            int l = popped[s], li = l % sw, lj = l / sw;
            if (li == 0 || lj == 0 || li == sw - 1 || lj == sh - 1) {
                across[l] = (dx != 0) ? (li == 0 || li == sw - 1 ? 1 : 0) : (lj == 0 || lj == sh - 1 ? 1 : 0);
                continue;
            }
            if (sink[((j0 + lj) << W_SHIFT) | (i0 + li)]) continue;
            int best = -1;
            float bestSlope = 0.0f;
            for (int k = 0; k < 8; ++k) {
                float sl = (fill[l] - fill[(lj + DZ[k]) * sw + li + DX[k]]) / DIST[k];
                if (sl > bestSlope) { bestSlope = sl; best = k; }
            }
            if (best >= 0) {
                to[l] = (lj + DZ[best]) * sw + li + DX[best];
                across[l] = across[to[l]];
            }
        }
        std::vector<int> drains(sn, 0);
        for (int s = count - 1; s >= 0; --s) {
            int l = popped[s];
            drains[l]++;
            if (to[l] >= 0) drains[to[l]] += drains[l];
        }
        for (int lj = 0; lj < sh; ++lj) {
            for (int li = 0; li < sw; ++li) {
                int off = (dx != 0 ? li : lj) - SEAM_POOL_SHARED;
                if (off < -SEAM_POOL_BAND || off >= SEAM_POOL_BAND) continue;
                int l = lj * sw + li;
                if (across[l] && drains[l] >= threshold && fill[l] - h[((j0 + lj) << W_SHIFT) | (i0 + li)] >= SEAM_POOL_FILL) {
                    return true;
                }
            }
        }
        return false;
    }

    std::shared_ptr<TileSeams> seams() {
        auto out = std::make_shared<TileSeams>();
        out->exitOf.assign(CORE * CORE, -1);
        for (int k = 0; k < orderCount; ++k) {
            int c = order[k];
            if (!inCore(c)) continue;
            int d = downstream(c);
            int16_t exitIdx = -1;
            if (d >= 0 && !sink[d]) {
                if (inCore(d)) {
                    exitIdx = out->exitOf[coreIndex(d)];
                } else {
                    exitIdx = int16_t(out->exits.size());
                    SeamExit se;
                    se.fromX = cx0 + (c & W_MASK); se.fromZ = cz0 + (c >> W_SHIFT);
                    se.cellX = cx0 + (d & W_MASK); se.cellZ = cz0 + (d >> W_SHIFT);
                    se.acc = acc[c]; se.wet = wet[c]; se.flowLen = flowLen[c];
                    se.channel = chan[c] != 0; se.level = breached[d];
                    out->exits.push_back(se);
                }
            }
            out->exitOf[coreIndex(c)] = exitIdx;
        }
        for (int n = 0; n < 4; ++n) out->taken[n] = seamFlat(n) || seamPooled(n);
        return out;
    }

    int16_t exitOfIn(int n, int wx, int wz) const {
        int tx = tile.tileX + SEAM_NEIGHBOURS[n][0], tz = tile.tileZ + SEAM_NEIGHBOURS[n][1];
        return around[n]->exitOf[(wz - tz * CORE) * CORE + (wx - tx * CORE)];
    }
    int16_t ownExitAt(int wx, int wz) const {
        return ownSeams->exitOf[coreIndex(((wz - cz0) << W_SHIFT) | (wx - cx0))];
    }
    static int sideTo(int dx, int dz) {
        for (int n = 0; n < 4; ++n) if (SEAM_NEIGHBOURS[n][0] == dx && SEAM_NEIGHBOURS[n][1] == dz) return n;
        return -1;
    }
    int neighbourOf(int wx, int wz) const {
        int tx = floorDiv(wx, CORE), tz = floorDiv(wz, CORE);
        for (int n = 0; n < 4; ++n) if (tile.tileX + SEAM_NEIGHBOURS[n][0] == tx && tile.tileZ + SEAM_NEIGHBOURS[n][1] == tz) return n;
        return -1;
    }
    int seamDistance(int n, int wx, int wz) const {
        int dx = SEAM_NEIGHBOURS[n][0], dz = SEAM_NEIGHBOURS[n][1];
        if (dx != 0) {
            int sx = (dx > 0 ? tile.tileX + 1 : tile.tileX) * CORE;
            return wx >= sx ? wx - sx : sx - 1 - wx;
        }
        int sz = (dz > 0 ? tile.tileZ + 1 : tile.tileZ) * CORE;
        return wz >= sz ? wz - sz : sz - 1 - wz;
    }
    void markTakenOver(int n) {
        int dx = SEAM_NEIGHBOURS[n][0], dz = SEAM_NEIGHBOURS[n][1];
        int i0 = dx > 0 ? HALO + CORE : dx < 0 ? 0 : HALO, i1 = dx > 0 ? W : dx < 0 ? HALO : HALO + CORE;
        int j0 = dz > 0 ? HALO + CORE : dz < 0 ? 0 : HALO, j1 = dz > 0 ? W : dz < 0 ? HALO : HALO + CORE;
        for (int j = j0; j < j1; ++j) {
            for (int i = i0; i < i1; ++i) takenOver[(j << W_SHIFT) | i] = 1;
        }
    }
    std::shared_ptr<const TileSeams> seamsAt(int tx, int tz) {
        if (tx == tile.tileX && tz == tile.tileZ) return ownSeams;
        int n = sideTo(tx - tile.tileX, tz - tile.tileZ);
        if (n >= 0 && around[n]) return around[n];
        int64_t k = BasinTileEngine::packKey(tx, tz);
        auto it = farSeams.find(k);
        if (it != farSeams.end()) return it->second;
        auto s = getSeamsFn(tx, tz);
        farSeams.emplace(k, s);
        return s;
    }
    int exitAt(bool mine, int n, int c) const {
        int wx = cx0 + (c & W_MASK), wz = cz0 + (c >> W_SHIFT);
        return mine ? ownSeams->exitOf[coreIndex(c)] : exitOfIn(n, wx, wz);
    }
    static int64_t worldPlace(const SeamExit& e) {
        return int64_t((uint64_t(uint32_t(e.fromZ)) << 32) | uint64_t(uint32_t(e.fromX)));
    }

    struct Plan {
        float cost = 0.0f;
        std::vector<int> cells;
        std::vector<int8_t> dirs;
        std::vector<int> crossings;
        std::vector<int> leaveBy;
        std::vector<int> cellLeave;
        bool valid = false;
    };

    bool comesRound(int n, bool mine, int out, int water, int64_t place, int e0,
                    const std::vector<uint8_t>& pair, const std::vector<uint8_t>& cyclic,
                    const std::unordered_map<int, int>& laid) {
        int ntx = tile.tileX + SEAM_NEIGHBOURS[n][0], ntz = tile.tileZ + SEAM_NEIGHBOURS[n][1];
        std::shared_ptr<const TileSeams> from = mine ? ownSeams : around[n];
        int ftx = mine ? tile.tileX : ntx, ftz = mine ? tile.tileZ : ntz;
        SeamExit e = from->exits[out];
        int btx = floorDiv(e.cellX, CORE), btz = floorDiv(e.cellZ, CORE);
        int x0 = std::min(std::min(tile.tileX, ntx), btx), x1 = std::max(std::max(tile.tileX, ntx), btx);
        int z0 = std::min(std::min(tile.tileZ, ntz), btz), z1 = std::max(std::max(tile.tileZ, ntz), btz);

        int byTx[SEAM_CORNER_HOPS + 1], byTz[SEAM_CORNER_HOPS + 1], byX[SEAM_CORNER_HOPS + 1];
        SeamExit byExit[SEAM_CORNER_HOPS + 1];
        byTx[0] = ftx; byTz[0] = ftz; byX[0] = out; byExit[0] = e;
        int by = 1;
        for (int hop = 0; hop < SEAM_CORNER_HOPS; ++hop) {
            int ttx = floorDiv(e.cellX, CORE), ttz = floorDiv(e.cellZ, CORE);
            if (ttx < x0 || ttx > x1 || ttz < z0 || ttz > z1) return false;
            int side = sideTo(ttx - ftx, ttz - ftz);
            if (side < 0 || !from->taken[side]) return false;
            auto to = seamsAt(ttx, ttz);
            int x = to->exitOf[(e.cellZ - ttz * CORE) * CORE + (e.cellX - ttx * CORE)];
            if (x < 0) return false;
            bool here = (ttx == tile.tileX && ttz == tile.tileZ), there = (ttx == ntx && ttz == ntz);
            if (mine ? here : there) {
                int c = windowCell(e.cellX, e.cellZ);
                if (c >= 0 && laid.find(c) != laid.end()) return false;
            }
            int m = here ? x : (there ? e0 + x : -1);
            if (m >= 0 && pair[m] && cyclic[m]) return true;
            for (int k = 0; k < by; ++k) {
                if (byTx[k] != ttx || byTz[k] != ttz || byX[k] != x) continue;
                int loopWater = 0;
                int64_t loopPlace = INT64_MAX;
                bool two = true;
                for (int q = k; q < by; ++q) {
                    loopWater = std::max(loopWater, byExit[q].acc);
                    loopPlace = std::min(loopPlace, worldPlace(byExit[q]));
                    if ((byTx[q] != byTx[k] || byTz[q] != byTz[k]) && (byTx[q] != byTx[k + 1] || byTz[q] != byTz[k + 1])) two = false;
                }
                return !two || loopWater < water || (loopWater == water && loopPlace > place);
            }
            from = to; ftx = ttx; ftz = ttz;
            e = to->exits[x];
            byTx[by] = ttx; byTz[by] = ttz; byX[by] = x; byExit[by++] = e;
        }
        return false;
    }

    Plan planCycle(int n, const std::vector<int>& cycle, bool mine, int e0,
                   const std::vector<uint8_t>& pair, const std::vector<uint8_t>& cyclic, float level,
                   std::vector<float>& scratchCost, std::vector<int>& scratchPrev,
                   std::vector<int>& scratchSteps, std::vector<uint8_t>& scratchSeen) {
        const auto& exits = mine ? ownSeams->exits : around[n]->exits;
        int base = mine ? 0 : e0;
        int tx = mine ? tile.tileX : tile.tileX + SEAM_NEIGHBOURS[n][0];
        int tz = mine ? tile.tileZ : tile.tileZ + SEAM_NEIGHBOURS[n][1];
        std::vector<int> crossings;
        for (int m : cycle) if ((m >= e0) != mine) crossings.push_back(m - base);
        int count = int(crossings.size());
        std::sort(crossings.begin(), crossings.end(), [&](int a, int b) {
            const auto& ea = exits[a], & eb = exits[b];
            return ea.fromZ != eb.fromZ ? ea.fromZ < eb.fromZ : ea.fromX < eb.fromX;
        });

        Plan res;
        res.crossings = crossings;
        res.leaveBy.resize(count, -1);

        int water = 0;
        int64_t place = INT64_MAX;
        for (int m : cycle) {
            const auto& crossing = (m < e0) ? ownSeams->exits[m] : around[n]->exits[m - e0];
            water = std::max(water, crossing.acc);
            place = std::min(place, worldPlace(crossing));
        }

        std::unordered_map<int, int> laid;
        float total = 0.0f;
        struct QNode {
            int32_t bits;
            int64_t ord;
            int c;
            bool operator>(const QNode& o) const {
                if (bits != o.bits) return bits > o.bits;
                return ord > o.ord;
            }
        };

        for (int k = 0; k < count; ++k) {
            int x = crossings[k];
            const auto& start = exits[x];
            int s = windowCell(start.fromX, start.fromZ);
            if (s < 0) return res;
            std::fill(scratchSeen.begin(), scratchSeen.end(), 0);
            std::priority_queue<QNode, std::vector<QNode>, std::greater<QNode>> pq;
            int64_t ord = 0;
            scratchCost[s] = 0.0f;
            scratchSteps[s] = 0;
            scratchPrev[s] = -1;
            scratchSeen[s] = 1;
            int32_t zbits = 0;
            pq.push({ zbits, ord++, s });
            int target = -1, targetExit = -1;
            while (!pq.empty()) {
                QNode top = pq.top(); pq.pop();
                int c = top.c;
                int32_t cbits = 0;
                std::memcpy(&cbits, &scratchCost[c], 4);
                if (cbits != top.bits) continue;
                if (c != s) {
                    auto lit = laid.find(c);
                    bool viaWay = (lit != laid.end());
                    int e = viaWay ? lit->second : exitAt(mine, n, c);
                    bool low = (e < 0 || exits[e].level <= level + SEAM_CYCLE_RISE);
                    bool out = viaWay || e < 0 || (low && pair[base + e] && !cyclic[base + e]);
                    if (!out && low && !pair[base + e] && dwRoundF(exits[e].level) <= dwRoundF(level)) {
                        const auto& third = exits[e];
                        if (third.channel) {
                            out = !comesRound(n, mine, e, water, place, e0, pair, cyclic, laid);
                        }
                    }
                    if (out) { target = c; targetExit = e; break; }
                }
                if (scratchSteps[c] >= SEAM_CYCLE_REACH) continue;
                int i = c & W_MASK, j = c >> W_SHIFT;
                for (int d = 0; d < 8; ++d) {
                    int ni = i + DX[d], nj = j + DZ[d];
                    if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
                    int nc = (nj << W_SHIFT) | ni;
                    int wx = cx0 + ni, wz = cz0 + nj;
                    if (!inTile(tx, tz, wx, wz) || seamDistance(n, wx, wz) > SEAM_CYCLE_BAND) continue;
                    if (scratchSteps[c] >= SEAM_CYCLE_INSET && seamDistance(n, wx, wz) < SEAM_CYCLE_INSET) continue;
                    if (h[nc] > level + SEAM_CYCLE_HEIGHT) continue;
                    float step = DIST[d] * (1.0f + SEAM_CYCLE_CLIMB * std::max(0.0f, h[nc] - h[s]))
                               * (1.0f + SEAM_CYCLE_WANDER * wanderPull[nc] * 0.5f * (1.0f - float(std::cos(DIR_ANGLE[d] - double(wanderWay[nc])))));
                    float to = scratchCost[c] + step;
                    if (scratchSeen[nc] && to >= scratchCost[nc]) continue;
                    scratchSeen[nc] = 1;
                    scratchCost[nc] = to;
                    scratchPrev[nc] = c;
                    scratchSteps[nc] = scratchSteps[c] + 1;
                    int32_t tbits = 0;
                    std::memcpy(&tbits, &to, 4);
                    pq.push({ tbits, ord++, nc });
                }
            }
            if (target < 0) return res;
            total += scratchCost[target];
            res.leaveBy[k] = targetExit;
            for (int c = scratchPrev[target], next = target; c >= 0; next = c, c = scratchPrev[c]) {
                int di = (next & W_MASK) - (c & W_MASK), dj = (next >> W_SHIFT) - (c >> W_SHIFT);
                int8_t d = -1;
                for (int q = 0; q < 8; ++q) if (DX[q] == di && DZ[q] == dj) d = int8_t(q);
                res.cells.push_back(c);
                res.dirs.push_back(d);
                res.cellLeave.push_back(targetExit);
                laid[c] = targetExit;
            }
        }
        res.cost = total;
        res.valid = true;
        return res;
    }

    void redirect(int n, int y, int out, const std::vector<SeamExit>& theirs, float level) {
        wayOut[n][y] = int16_t(out);
        if (out < 0) return;
        const auto& e = theirs[y];
        const auto& to = theirs[out];
        if (!inTile(tile.tileX, tile.tileZ, to.cellX, to.cellZ)) return;
        int c = ((to.cellZ - cz0) << W_SHIFT) | (to.cellX - cx0);
        seamLevel[c] = std::min(seamLevel[c], level);
        inAcc[c] += e.acc;
        inWet[c] += e.wet;
        inLen[c] = std::max(inLen[c], e.flowLen + 1);
        if (e.channel) inflow[c] = 1;
    }

    bool seamLakeMay(int c, int wx, int wz, int along, int ntx, int ntz, int waterY) const {
        int i = c & W_MASK, j = c >> W_SHIFT;
        if (i < 2 || j < 2 || i >= W - 2 || j >= W - 2 || h[c] > float(waterY + DEAD_END_LAKE_DIG - 1)) return false;
        if (!inTile(tile.tileX, tile.tileZ, wx, wz) && !inTile(ntx, ntz, wx, wz)) return false;
        int off = floorMod(along, CORE);
        int wobble = std::min(DEAD_END_LAKE_RIM_WOBBLE,
                int((DEAD_END_LAKE_RIM_WOBBLE + 1) * (0.5 + 0.5 * valueNoise(wx / 4.0, wz / 4.0, 2))));
        if (std::min(off, CORE - 1 - off) < DEAD_END_LAKE_GAP + wobble) return false;
        for (int dj = -2; dj <= 2; ++dj) {
            for (int di = -2; di <= 2; ++di) {
                int q = c + dj * W + di;
                if (sink[q] || (std::abs(di) <= 1 && std::abs(dj) <= 1 && (lakeId[q] >= 0 || lakeShore[q] >= 0))) return false;
            }
        }
        return true;
    }

    void planSeamLake(int n, int e0, const std::vector<SeamExit>& theirs, const std::vector<int64_t>& sorted, int from, int to) {
        const auto& own = ownSeams->exits;
        int dx = SEAM_NEIGHBOURS[n][0], dz = SEAM_NEIGHBOURS[n][1];
        bool acrossX = (dx != 0);
        int ntx = tile.tileX + dx, ntz = tile.tileZ + dz;
        int seam = acrossX ? (dx > 0 ? tile.tileX + 1 : tile.tileX) * CORE : (dz > 0 ? tile.tileZ + 1 : tile.tileZ) * CORE;
        int a0 = int(sorted[from] >> 32), a1 = int(sorted[to] >> 32);
        float level = std::numeric_limits<float>::infinity();
        int biggest = -1, biggestAt = a0;
        for (int t = from; t <= to; ++t) {
            int m = int(int32_t(sorted[t] & 0xFFFFFFFFll));
            const auto& e = (m < e0) ? own[m] : theirs[m - e0];
            if (!e.channel) continue;
            level = std::min(level, e.level);
            if (e.acc > biggest) { biggest = e.acc; biggestAt = int(sorted[t] >> 32); }
        }
        if (biggest < 0) return;
        if (a1 - a0 >= SEAM_LAKE_RUN_MAX) {
            a0 = std::max(a0, biggestAt - SEAM_LAKE_RUN_MAX / 2);
            a1 = std::min(a1, a0 + SEAM_LAKE_RUN_MAX - 1);
        }
        int waterY = std::max(dwRoundF(level) - CHANNEL_INCISION, seaLevel - 1);
        int r = mthClamp(int(dwRound(SEAM_LAKE_SPAN * double(a1 - a0 + 1))), SEAM_LAKE_MIN, SEAM_LAKE_MAX);
        int reach = int(std::ceil(DEAD_END_LAKE_REACH * double(r)));

        std::vector<uint8_t> may(N, 0), take(N, 0), reached(N, 0);
        for (int b = a0 - reach; b <= a1 + reach; ++b) {
            double along = (b < a0) ? double(a0 - b) : ((b > a1) ? double(b - a1) : 0.0);
            for (int a = seam - reach; a < seam + reach; ++a) {
                int wx = acrossX ? a : b, wz = acrossX ? b : a;
                int c = windowCell(wx, wz);
                if (c < 0 || !seamLakeMay(c, wx, wz, b, ntx, ntz, waterY)) continue;
                may[c] = 1;
                double ground = 0.0;
                for (int dj = -1; dj <= 1; ++dj) for (int di = -1; di <= 1; ++di) ground += h[c + dj * W + di];
                double far = std::hypot(a >= seam ? double(a - seam) + 0.5 : double(seam - a) - 0.5, along);
                if (far + DEAD_END_LAKE_GROUND * (ground / 9.0 - waterY) + DEAD_END_LAKE_WOBBLE * r * lakeNoise(c) <= double(r)) {
                    take[c] = 1;
                }
            }
        }
        std::vector<int> cells;
        for (int t = from; t <= to; ++t) {
            int m = int(int32_t(sorted[t] & 0xFFFFFFFFll));
            const auto& e = (m < e0) ? own[m] : theirs[m - e0];
            int c = windowCell(e.fromX, e.fromZ);
            if (c < 0 || !take[c] || reached[c]) continue;
            reached[c] = 1;
            cells.push_back(c);
        }
        for (size_t head = 0; head < cells.size(); ++head) {
            int q = cells[head];
            for (int k = 0; k < 8; k += 2) {
                int nq = q + DZ[k] * W + DX[k];
                if (nq < 0 || nq >= N || !take[nq] || reached[nq]) continue;
                reached[nq] = 1;
                cells.push_back(nq);
            }
        }
        if (int(cells.size()) < 2 * SEAM_LAKE_MIN * SEAM_LAKE_MIN) return;
        for (int b = a0 - reach; b <= a1 + reach; ++b) {
            for (int a = seam - reach; a < seam + reach; ++a) {
                int c = windowCell(acrossX ? a : b, acrossX ? b : a);
                if (c < 0 || !may[c] || reached[c]) continue;
                int round = (reached[c + 1] ? 1 : 0) + (reached[c - 1] ? 1 : 0) + (reached[c + W] ? 1 : 0) + (reached[c - W] ? 1 : 0);
                if (round < 3) continue;
                reached[c] = 1;
                cells.push_back(c);
            }
        }
        seamLakes.push_back({ std::move(cells), waterY });
    }

    void planSeamLakes(int n, int e0, const std::vector<SeamExit>& theirs, const std::vector<uint8_t>& dead) {
        const auto& own = ownSeams->exits;
        bool acrossX = (SEAM_NEIGHBOURS[n][0] != 0);
        std::vector<int64_t> sorted;
        for (int m = 0; m < int(dead.size()); ++m) {
            if (!dead[m]) continue;
            const auto& e = (m < e0) ? own[m] : theirs[m - e0];
            int64_t along = acrossX ? e.fromZ : e.fromX;
            sorted.push_back(int64_t((uint64_t(along) << 32) | uint64_t(uint32_t(m))));
        }
        std::sort(sorted.begin(), sorted.end());
        int count = int(sorted.size());
        for (int k = 0; k < count;) {
            int end = k;
            while (end + 1 < count && int32_t(sorted[end + 1] >> 32) - int32_t(sorted[end] >> 32) <= SEAM_LAKE_RUN_GAP) end++;
            planSeamLake(n, e0, theirs, sorted, k, end);
            k = end + 1;
        }
    }

    static bool inCycle(const std::vector<int>& cycle, int x) {
        for (int m : cycle) if (m == x) return true;
        return false;
    }

    void breakSeamCycles() {
        const auto& own = ownSeams->exits;
        bool turned = false;
        std::vector<float> scratchCost, scratchStepsF;
        std::vector<int> scratchPrev, scratchSteps;
        std::vector<uint8_t> scratchSeen;
        bool scratchInit = false;

        for (int n = 0; n < 4; ++n) {
            if (!around[n]) continue;
            const auto& theirs = around[n]->exits;
            int e0 = int(own.size()), total = e0 + int(theirs.size());
            int ntx = tile.tileX + SEAM_NEIGHBOURS[n][0], ntz = tile.tileZ + SEAM_NEIGHBOURS[n][1];
            std::vector<int> succ(total, -1);
            std::vector<uint8_t> pair(total, 0);
            for (int x = 0; x < e0; ++x) {
                const auto& e = own[x];
                if (!inTile(ntx, ntz, e.cellX, e.cellZ)) continue;
                pair[x] = 1;
                int16_t y = exitOfIn(n, e.cellX, e.cellZ);
                if (y >= 0 && inTile(tile.tileX, tile.tileZ, theirs[y].cellX, theirs[y].cellZ)) succ[x] = e0 + y;
            }
            for (int y = 0; y < int(theirs.size()); ++y) {
                const auto& e = theirs[y];
                if (!inTile(tile.tileX, tile.tileZ, e.cellX, e.cellZ)) continue;
                pair[e0 + y] = 1;
                int16_t x = ownExitAt(e.cellX, e.cellZ);
                if (x >= 0 && inTile(ntx, ntz, own[x].cellX, own[x].cellZ)) succ[e0 + y] = x;
            }
            std::vector<int8_t> state(total, 0);
            std::vector<uint8_t> cyclic(total, 0);
            std::vector<int> cycleOf(total, 0), walk(total, 0);
            std::vector<std::vector<int>> cycles;
            for (int s = 0; s < total; ++s) {
                if (!pair[s] || state[s] != 0) continue;
                int len = 0, cur = s;
                while (cur >= 0 && state[cur] == 0) {
                    state[cur] = 1;
                    walk[len++] = cur;
                    cur = succ[cur];
                }
                int into = (cur >= 0 && cyclic[cur]) ? cycleOf[cur] : -1;
                if (cur >= 0 && state[cur] == 1) {
                    int from = 0;
                    while (walk[from] != cur) from++;
                    into = int(cycles.size());
                    cycles.emplace_back(walk.begin() + from, walk.begin() + len);
                }
                for (int k = 0; k < len; ++k) {
                    state[walk[k]] = 2;
                    if (into >= 0) { cyclic[walk[k]] = 1; cycleOf[walk[k]] = into; }
                }
            }
            std::vector<float> cycleLevel(cycles.size(), std::numeric_limits<float>::infinity());
            for (int m = 0; m < total; ++m) {
                if (!cyclic[m]) continue;
                float lv = (m < e0 ? own[m] : theirs[m - e0]).level;
                cycleLevel[cycleOf[m]] = std::min(cycleLevel[cycleOf[m]], lv);
            }
            if (!cycles.empty() && !scratchInit) {
                scratchCost.assign(N, 0.0f);
                scratchPrev.assign(N, -1);
                scratchSteps.assign(N, 0);
                scratchSeen.assign(N, 0);
                scratchInit = true;
            }
            std::vector<uint8_t> dead(total, 0);
            for (int ci = 0; ci < int(cycles.size()); ++ci) {
                const auto& cycle = cycles[ci];
                Plan mine = planCycle(n, cycle, true, e0, pair, cyclic, cycleLevel[ci], scratchCost, scratchPrev, scratchSteps, scratchSeen);
                Plan other = planCycle(n, cycle, false, e0, pair, cyclic, cycleLevel[ci], scratchCost, scratchPrev, scratchSteps, scratchSeen);
                if (!mine.valid && !other.valid) {
                    int ownWater = 0, theirWater = 0;
                    for (int m : cycle) {
                        if (m < e0) ownWater = std::max(ownWater, own[m].acc);
                        else theirWater = std::max(theirWater, theirs[m - e0].acc);
                    }
                    bool mineStand = ownWater > theirWater || (ownWater == theirWater && (tile.tileX < ntx || (tile.tileX == ntx && tile.tileZ < ntz)));
                    for (int m : cycle) {
                        if (m >= e0) {
                            if (mineStand) withdrawn[n][m - e0] = 1;
                        } else if (!mineStand) {
                            const auto& e = own[m];
                            dropped[((e.fromZ - cz0) << W_SHIFT) | (e.fromX - cx0)] = 1;
                        }
                    }
                    for (int m = 0; m < total; ++m) {
                        if (!cyclic[m] || cycleOf[m] != ci) continue;
                        dead[m] = 1;
                        if (inCycle(cycle, m)) continue;
                        if (m >= e0) withdrawn[n][m - e0] = 1;
                        else {
                            const auto& e = own[m];
                            dropped[((e.fromZ - cz0) << W_SHIFT) | (e.fromX - cx0)] = 1;
                        }
                    }
                    continue;
                }
                bool mineCarries = !other.valid || (mine.valid && (mine.cost < other.cost
                        || (mine.cost == other.cost && (tile.tileX < ntx || (tile.tileX == ntx && tile.tileZ < ntz)))));
                if (mineCarries) {
                    for (size_t k = 0; k < mine.cells.size(); ++k) dir[mine.cells[k]] = mine.dirs[k];
                    turned = true;
                    for (int t : mine.cells) {
                        int16_t y = ownSeams->exitOf[coreIndex(t)];
                        if (y < 0 || !pair[y] || inCycle(cycle, y)) continue;
                        const auto& e = own[y];
                        dropped[((e.fromZ - cz0) << W_SHIFT) | (e.fromX - cx0)] = 1;
                    }
                    continue;
                }
                for (size_t k = 0; k < other.crossings.size(); ++k) {
                    int y = other.crossings[k];
                    withdrawn[n][y] = 1;
                    redirect(n, y, other.leaveBy[k], theirs, cycleLevel[ci]);
                }
                for (size_t k = 0; k < other.cells.size(); ++k) {
                    int t = other.cells[k];
                    int16_t y = exitOfIn(n, cx0 + (t & W_MASK), cz0 + (t >> W_SHIFT));
                    if (y < 0 || !pair[e0 + y] || withdrawn[n][y]) continue;
                    withdrawn[n][y] = 1;
                    redirect(n, y, other.cellLeave[k], theirs, cycleLevel[ci]);
                }
            }
            if (lakesEnabled && !neighbour) planSeamLakes(n, e0, theirs, dead);
        }
        if (turned) reorder();
    }

    static bool climbsBeyond(const SeamExit& x, const SeamExit& e) {
        if (!e.channel || dwRoundF(e.level) <= dwRoundF(x.level)) return false;
        if (std::max(std::abs(e.fromX - x.cellX), std::abs(e.fromZ - x.cellZ)) > PASS_CLIMB_CELLS) return false;
        return floorDiv(e.cellX, CORE) != floorDiv(x.fromX, CORE) || floorDiv(e.cellZ, CORE) != floorDiv(x.fromZ, CORE);
    }
    static bool diesBeyond(const SeamExit& x, const SeamExit& e) {
        if (e.channel) return false;
        if (std::max(std::abs(e.fromX - x.cellX), std::abs(e.fromZ - x.cellZ)) > PASS_THROUGH_CELLS) return false;
        return floorDiv(e.cellX, CORE) != floorDiv(x.fromX, CORE) || floorDiv(e.cellZ, CORE) != floorDiv(x.fromZ, CORE);
    }

    void dropPassingThrough() {
        const auto& own = ownSeams->exits;
        for (int n = 0; n < 4; ++n) {
            if (!around[n]) continue;
            int ntx = tile.tileX + SEAM_NEIGHBOURS[n][0], ntz = tile.tileZ + SEAM_NEIGHBOURS[n][1];
            for (const auto& x : own) {
                if (!x.channel || !inTile(ntx, ntz, x.cellX, x.cellZ)) continue;
                int16_t e = exitOfIn(n, x.cellX, x.cellZ);
                if (e < 0) continue;
                const auto& beyond = around[n]->exits[e];
                if (!diesBeyond(x, beyond) && !climbsBeyond(x, beyond)) continue;
                int c = windowCell(x.fromX, x.fromZ);
                if (c < 0 || dropped[c]) continue;
                dropped[c] = 1;
            }
            const auto& theirs = around[n]->exits;
            for (size_t y = 0; y < theirs.size(); ++y) {
                const auto& x = theirs[y];
                if (withdrawn[n][y] || !x.channel || !inTile(tile.tileX, tile.tileZ, x.cellX, x.cellZ)) continue;
                int16_t e = ownExitAt(x.cellX, x.cellZ);
                if (e >= 0 && (diesBeyond(x, own[e]) || climbsBeyond(x, own[e]))) withdrawn[n][y] = 1;
            }
        }
    }

    void importSeams() {
        inAcc.assign(N, 0);
        inWet.assign(N, 0);
        inLen.assign(N, 0);
        seamLevel.assign(N, std::numeric_limits<float>::infinity());
        takenOver.assign(N, 0);
        for (int n = 0; n < 4; ++n) {
            if (!ownSeams->taken[n]) continue;
            around[n] = getSeamsFn(tile.tileX + SEAM_NEIGHBOURS[n][0], tile.tileZ + SEAM_NEIGHBOURS[n][1]);
            withdrawn[n].assign(around[n]->exits.size(), 0);
            wayOut[n].assign(around[n]->exits.size(), -1);
            markTakenOver(n);
        }
        breakSeamCycles();
        dropPassingThrough();
        for (int n = 0; n < 4; ++n) {
            if (!around[n]) continue;
            const auto& exits = around[n]->exits;
            for (size_t x = 0; x < exits.size(); ++x) {
                if (withdrawn[n][x]) continue;
                const auto& e = exits[x];
                int i = e.cellX - cx0, j = e.cellZ - cz0;
                if (i < HALO || j < HALO || i >= HALO + CORE || j >= HALO + CORE) continue;
                int c = (j << W_SHIFT) | i;
                inAcc[c] += e.acc;
                inWet[c] += e.wet;
                inLen[c] = std::max(inLen[c], e.flowLen + 1);
                seamLevel[c] = std::min(seamLevel[c], e.level);
                if (e.channel) inflow[c] = 1;
            }
        }
        wetAlone.assign(CORE * CORE, 0);
        for (int c = 0; c < N; ++c) if (inCore(c)) wetAlone[coreIndex(c)] = wet[c];
        accumulateCore();
        seamsImported = true;
    }

    int leavesBy(int c) const {
        for (int steps = 0; steps < N && c >= 0; ++steps) {
            if (sink[c]) return -1;
            int d = downstream(c);
            if (d < 0) return -1;
            if (!inCore(d)) return c;
            c = d;
        }
        return -1;
    }

    bool bounce() {
        bool added = false;
        for (int j = HALO; j < HALO + CORE; ++j) {
            bool rimRow = (j == HALO || j == HALO + CORE - 1);
            for (int i = HALO; i < HALO + CORE; ++i) {
                if (!rimRow && i != HALO && i != HALO + CORE - 1) continue;
                int c = (j << W_SHIFT) | i;
                if (!chan[c] || dropped[c]) continue;
                int d = downstream(c);
                if (d < 0 || inCore(d) || sink[d] || lakeId[d] >= 0) continue;
                int wx = cx0 + (d & W_MASK), wz = cz0 + (d >> W_SHIFT);
                int n = neighbourOf(wx, wz);
                if (n < 0 || !around[n]) continue;
                int16_t exitIdx = exitOfIn(n, wx, wz);
                if (exitIdx >= 0 && withdrawn[n][exitIdx]) exitIdx = wayOut[n][exitIdx];
                if (exitIdx < 0) continue;
                const auto& e = around[n]->exits[exitIdx];
                int bi = e.cellX - cx0, bj = e.cellZ - cz0;
                if (bi < HALO || bj < HALO || bi >= HALO + CORE || bj >= HALO + CORE) continue;
                int back = (bj << W_SHIFT) | bi;
                if (lakeId[back] >= 0 || sink[back]) continue;
                if (leavesBy(back) == c) continue;
                int16_t told = ownSeams->exitOf[coreIndex(c)];
                float level = std::min(std::min(breached[c], e.level),
                        told >= 0 ? ownSeams->exits[told].level : std::numeric_limits<float>::infinity());
                if (level < seamLevel[back]) { seamLevel[back] = level; added = true; }
                if (inflow[back]) continue;
                inflow[back] = 1;
                inAcc[back] += acc[c];
                inWet[back] += wet[c];
                inLen[back] = std::max(inLen[back], flowLen[c] + 2);
                added = true;
            }
        }
        return added;
    }

    void markDeadEndLake(int id) {
        if (id >= int(deadEndLakeIdSet.size())) deadEndLakeIdSet.resize(id + 1, 0);
        deadEndLakeIdSet[id] = 1;
    }
    bool isDeadEndLake(int id) const { return id >= 0 && id < int(deadEndLakeIdSet.size()) && deadEndLakeIdSet[id] != 0; }

    void makeSeamLake(const SeamLake& s) {
        std::vector<int> cells;
        for (int q : s.cells) if (lakeId[q] < 0) cells.push_back(q);
        if (cells.empty()) return;
        int id = int(lakeCells.size());
        lakeCells.push_back(cells);
        lakeShoreCells.emplace_back();
        lakeWaterY.push_back(s.waterY);
        markDeadEndLake(id);
        if (int(lakeClosed.size()) <= id) lakeClosed.resize(id + 1, 0);
        lakeClosed[id] = 1;
        for (int q : cells) {
            lakeId[q] = id;
            lakeShore[q] = -1;
            chan[q] = 0;
            h[q] = std::min(h[q], float(s.waterY - 1));
            breached[q] = float(s.waterY);
        }
    }

    int riverLevel(int p) const {
        float level = breached[p];
        for (int q = p, steps = 0; steps < N; ++steps) {
            int u = mainUp(q);
            if (u < 0 || !chan[u] || lakeId[u] >= 0) break;
            level = std::min(level, breached[u]);
            if (!inCore(u)) break;
            q = u;
        }
        return std::max(dwRoundF(level) - CHANNEL_INCISION, seaLevel - 1);
    }

    void inflowsIntoLake(const std::vector<int>& chain, int inlet, int id) {
        for (int k = 0; k < inlet; ++k) {
            int c = chain[k];
            if (!inflow[c] || lakeId[c] >= 0) continue;
            int ci = c & W_MASK, cj = c >> W_SHIFT;
            for (int a = 0; a < 8; ++a) {
                int nc = ((cj + DZ[a]) << W_SHIFT) | (ci + DX[a]);
                if (lakeId[nc] != id) continue;
                dir[c] = int8_t(a);
                chan[c] = 1;
                break;
            }
        }
    }

    void makeLake(const std::vector<int>& chain, int inlet, std::vector<int> cells, int waterY) {
        int id = int(lakeCells.size());
        lakeCells.push_back(cells);
        lakeShoreCells.emplace_back();
        lakeWaterY.push_back(waterY);
        markDeadEndLake(id);
        if (int(lakeClosed.size()) <= id) lakeClosed.resize(id + 1, 0);
        lakeClosed[id] = 1;
        for (int q : cells) {
            lakeId[q] = id;
            chan[q] = 0;
            h[q] = std::min(h[q], float(waterY - 1));
            breached[q] = float(waterY);
        }
        for (int k = 0; k < inlet; ++k) chan[chain[k]] = 0;
        int over = inlet > 0 ? downstream(chain[0]) : -1;
        if (over >= 0 && !inCore(over) && chan[over]) {
            bool fed = false;
            int oi = over & W_MASK, oj = over >> W_SHIFT;
            for (int a = 0; a < 8 && !fed; ++a) {
                int ni = oi + DX[a], nj = oj + DZ[a];
                if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
                int u = (nj << W_SHIFT) | ni;
                fed = (chan[u] && downstream(u) == over);
            }
            if (!fed) chan[over] = 0;
        }
        inflowsIntoLake(chain, inlet, id);
    }

    int streamsBelow(const std::vector<int>& chain, int inlet, std::vector<int>& streams, int from) {
        int count = from;
        for (int k = 0; k < inlet; ++k) {
            int d = chain[k];
            if (lakeId[d] >= 0) continue;
            if (inflow[d] && !chan[d] && inCore(d) && count < int(streams.size())) {
                chan[d] = 1;
                streams[count++] = d;
            }
            int di = d & W_MASK, dj = d >> W_SHIFT;
            for (int a = 0; a < 8; ++a) {
                int u = ((dj + DZ[a]) << W_SHIFT) | (di + DX[a]);
                if (chan[u] && inCore(u) && downstream(u) == d && count < int(streams.size())) streams[count++] = u;
            }
        }
        return count;
    }

    bool lakeMayTake(int q, int waterY, int top, const std::vector<int>& reach, const std::vector<uint8_t>& apart,
                     bool wayIn, int joinId, int edge) const {
        if (!inCore(q) || apart[q] || lakeId[q] >= 0 || h[q] > float(waterY + DEAD_END_LAKE_DIG - 1)) return false;
        int qi = q & W_MASK, qj = q >> W_SHIFT;
        for (int k = -1; k < 8; ++k) {
            int c = (k < 0) ? q : (((qj + DZ[k]) << W_SHIFT) | (qi + DX[k]));
            if ((chan[c] && reach[c] > top) || (lakeId[c] >= 0 && lakeId[c] != joinId) || (lakeShore[c] >= 0 && lakeShore[c] != joinId)) {
                return false;
            }
        }
        if (chan[q] && reach[q] >= 0) return rimDistance(q) >= edge;
        if (wayIn || edge < 1) return rimDistance(q) >= 1;
        double x = (cx0 + qi) / 4.0, z = (cz0 + qj) / 4.0;
        int wobble = std::min(DEAD_END_LAKE_RIM_WOBBLE,
                int((DEAD_END_LAKE_RIM_WOBBLE + 1) * (0.5 + 0.5 * valueNoise(x, z, 2))));
        return rimDistance(q) >= DEAD_END_LAKE_GAP + wobble;
    }

    void queueAround(int q, int ci, int cj, int r, double reachOut, int waterY, int top,
                     const std::vector<int>& reach, const std::vector<uint8_t>& apart, int edge) {
        int qi = q & W_MASK, qj = q >> W_SHIFT;
        for (int k = 0; k < 8; k += 2) {
            int ni = qi + DX[k], nj = qj + DZ[k];
            if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
            int nq = (nj << W_SHIFT) | ni;
            if (floodMark[nq] == floodStamp) continue;
            double far = std::hypot(double(ni - ci), double(nj - cj));
            if (far > reachOut || !lakeMayTake(nq, waterY, top, reach, apart, false, -1, edge)) continue;
            floodMark[nq] = floodStamp;
            double ground = 0.0;
            for (int dj = -1; dj <= 1; ++dj) for (int di = -1; di <= 1; ++di) ground += h[((nj + dj) << W_SHIFT) | (ni + di)];
            double turn = far + DEAD_END_LAKE_GROUND * (ground / 9.0 - waterY) + DEAD_END_LAKE_WOBBLE * r * lakeNoise(nq);
            pushHeap(heapKeyFor(float(turn), nq));
        }
    }

    int growLake(int p, int ci, int cj, int r, int room, int waterY, int top,
                 const std::vector<int>& reach, const std::vector<uint8_t>& apart, std::vector<int>& out, bool toSeam) {
        if (floodMark.empty()) floodMark.assign(N, 0);
        int stamp = ++floodStamp;
        int edge = toSeam ? DEAD_END_LAKE_EDGE : 1;
        if (apart[p] || rimDistance(p) < edge || h[p] > float(waterY + DEAD_END_LAKE_DIG - 1)) return 0;
        int count = 0;
        int qi = p & W_MASK, qj = p >> W_SHIFT;
        int loI = qi, hiI = qi, loJ = qj, hiJ = qj;
        auto signum = [](int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); };
        while (true) {
            int q = (qj << W_SHIFT) | qi;
            if (count > 0 && !lakeMayTake(q, waterY, top, reach, apart, true, -1, edge)) return 0;
            floodMark[q] = stamp;
            out[count++] = q;
            if (qi == ci && qj == cj) break;
            if (toSeam && qi != ci && qj != cj) {
                qi += signum(ci - qi);
                qj += signum(cj - qj);
            } else if (std::abs(ci - qi) >= std::abs(cj - qj)) {
                qi += signum(ci - qi);
            } else {
                qj += signum(cj - qj);
            }
        }
        loI = std::min(loI, ci); hiI = std::max(hiI, ci);
        loJ = std::min(loJ, cj); hiJ = std::max(hiJ, cj);
        double reachOut = DEAD_END_LAKE_REACH * r;
        heapSize = 0;
        for (int k = 0; k < count; ++k) queueAround(out[k], ci, cj, r, reachOut, waterY, top, reach, apart, edge);
        while (count < room && heapSize > 0) {
            int q = int(uint64_t(popHeap()) & CELL_KEY_MASK);
            out[count++] = q;
            loI = std::min(loI, q & W_MASK); hiI = std::max(hiI, q & W_MASK);
            loJ = std::min(loJ, q >> W_SHIFT); hiJ = std::max(hiJ, q >> W_SHIFT);
            queueAround(q, ci, cj, r, reachOut, waterY, top, reach, apart, edge);
        }
        while (heapSize > 0) floodMark[int(uint64_t(popHeap()) & CELL_KEY_MASK)] = 0;

        int pi = p & W_MASK, pj = p >> W_SHIFT, span = top - reach[p] + 1;
        for (bool grew = toSeam; grew;) {
            grew = false;
            for (int dj = -span; dj <= span && count < int(out.size()); ++dj) {
                int nj = pj + dj;
                if (nj < 1 || nj >= W - 1) continue;
                for (int di = -span; di <= span; ++di) {
                    int ni = pi + di;
                    if (ni < 1 || ni >= W - 1) continue;
                    int q = (nj << W_SHIFT) | ni;
                    if (floodMark[q] == stamp || !chan[q] || reach[q] <= reach[p] || reach[q] > top) continue;
                    if (floodMark[q + 1] != stamp && floodMark[q - 1] != stamp && floodMark[q + W] != stamp && floodMark[q - W] != stamp) continue;
                    if (!lakeMayTake(q, waterY, top, reach, apart, false, -1, edge)) continue;
                    floodMark[q] = stamp;
                    out[count++] = q;
                    loI = std::min(loI, ni); hiI = std::max(hiI, ni);
                    loJ = std::min(loJ, nj); hiJ = std::max(hiJ, nj);
                    grew = true;
                }
            }
        }
        for (int j = std::max(1, loJ); j <= std::min(W - 2, hiJ); ++j) {
            for (int i = std::max(1, loI); i <= std::min(W - 2, hiI); ++i) {
                int q = (j << W_SHIFT) | i;
                if (floodMark[q] == stamp) continue;
                int ar = (floodMark[q + 1] == stamp ? 1 : 0) + (floodMark[q - 1] == stamp ? 1 : 0)
                       + (floodMark[q + W] == stamp ? 1 : 0) + (floodMark[q - W] == stamp ? 1 : 0);
                if (ar >= 3 && lakeMayTake(q, waterY, top, reach, apart, false, -1, edge)) {
                    floodMark[q] = stamp;
                    out[count++] = q;
                }
            }
        }
        return count;
    }

    int joinLake(const std::vector<int>& chain, int length, const std::vector<int>& reach, const std::vector<uint8_t>& apart, bool toSeam) {
        if (lakeCells.empty()) return -1;
        if (floodMark.empty()) floodMark.assign(N, 0);
        std::vector<int> queue(N), parent(N), steps(N);
        int bestId = -1, bestAt = -1;
        int64_t bestScore = INT64_MIN;
        std::vector<int> bestParent;

        int loI = W, hiI = -1, loJ = W, hiJ = -1;
        for (int k = 0; k < length; ++k) {
            loI = std::min(loI, chain[k] & W_MASK); hiI = std::max(hiI, chain[k] & W_MASK);
            loJ = std::min(loJ, chain[k] >> W_SHIFT); hiJ = std::max(hiJ, chain[k] >> W_SHIFT);
        }
        int near = DEAD_END_LAKE_JOIN + 1;
        for (int id = 0; id < int(lakeCells.size()); ++id) {
            const auto& own = lakeCells[id];
            if (own.empty() || lakeId[own[0]] != id) continue;
            bool inReach = false, hasCore = false;
            for (int q : own) {
                int qi = q & W_MASK, qj = q >> W_SHIFT;
                if (qi >= loI - near && qi <= hiI + near && qj >= loJ - near && qj <= hiJ + near) inReach = true;
                if (inCore(q)) hasCore = true;
            }
            if (!inReach || !hasCore) continue;
            int waterY = lakeWaterY[id];
            int stamp = ++floodStamp;
            int head = 0, tail = 0;
            for (int q : own) {
                floodMark[q] = stamp; parent[q] = -1; steps[q] = 0; queue[tail++] = q;
            }
            int at = -1;
            int64_t atScore = INT64_MIN;
            while (head < tail) {
                int q = queue[head++];
                if (steps[q] >= DEAD_END_LAKE_JOIN) continue;
                int qi = q & W_MASK, qj = q >> W_SHIFT;
                for (int k = 0; k < 8; k += 2) {
                    int nq = ((qj + DZ[k]) << W_SHIFT) | (qi + DX[k]);
                    if (floodMark[nq] == stamp) continue;
                    floodMark[nq] = stamp;
                    int on = chan[nq] ? reach[nq] : -1;
                    if (on >= 0 && chain[on] == nq) {
                        if (rimDistance(nq) >= (toSeam ? DEAD_END_LAKE_EDGE : 1) && !apart[nq]
                                && h[nq] <= float(waterY + DEAD_END_LAKE_DIG - 1) && riverLevel(nq) >= waterY) {
                            parent[nq] = q;
                            steps[nq] = steps[q] + 1;
                            int64_t score = (steps[nq] <= DEAD_END_LAKE_JOIN_NEAR)
                                ? (1ll << 40) + on : ((int64_t(DEAD_END_LAKE_JOIN - steps[nq]) << 20) + on);
                            if (score > atScore) { atScore = score; at = nq; }
                        }
                        continue;
                    }
                    if (!lakeMayTake(nq, waterY, length - 1, reach, apart, toSeam, id, toSeam ? DEAD_END_LAKE_EDGE : 1)) continue;
                    parent[nq] = q;
                    steps[nq] = steps[q] + 1;
                    queue[tail++] = nq;
                }
            }
            if (at >= 0 && atScore > bestScore) {
                bestId = id; bestAt = at; bestScore = atScore; bestParent = parent;
            }
        }
        if (bestId < 0) return -1;
        int inlet = reach[bestAt] + 1;
        int waterY = lakeWaterY[bestId];
        const auto& old = lakeCells[bestId];
        std::vector<int> way;
        for (int q = bestAt; q >= 0 && lakeId[q] != bestId; q = bestParent[q]) way.push_back(q);
        int stamp = ++floodStamp;
        int side = std::max(1, int(std::ceil(cellWidth(bestAt) / double(CELL))));
        std::vector<int> grown = old;
        for (int q : way) {
            if (floodMark[q] != stamp) { floodMark[q] = stamp; grown.push_back(q); }
            int qi = q & W_MASK, qj = q >> W_SHIFT;
            for (int dj = -side; dj <= side; ++dj) {
                for (int di = -side; di <= side; ++di) {
                    if (di * di + dj * dj > side * side) continue;
                    int ni = qi + di, nj = qj + dj;
                    if (ni < 1 || nj < 1 || ni >= W - 1 || nj >= W - 1) continue;
                    int nq = (nj << W_SHIFT) | ni;
                    if (floodMark[nq] == stamp || !lakeMayTake(nq, waterY, inlet - 1, reach, apart, toSeam, bestId,
                            toSeam ? DEAD_END_LAKE_EDGE : 1)) continue;
                    floodMark[nq] = stamp;
                    grown.push_back(nq);
                }
            }
        }
        for (size_t k = old.size(); k < grown.size(); ++k) {
            int q = grown[k];
            lakeId[q] = bestId;
            lakeShore[q] = -1;
            chan[q] = 0;
            h[q] = std::min(h[q], float(waterY - 1));
            breached[q] = float(waterY);
        }
        lakeCells[bestId] = std::move(grown);
        for (int k = 0; k < inlet; ++k) chan[chain[k]] = 0;
        inflowsIntoLake(chain, inlet, bestId);
        markDeadEndLake(bestId);
        return inlet;
    }

    int growInto(const std::vector<int>& chain, int length, int full, int r, const std::vector<int>& reach,
                 const std::vector<uint8_t>& apart, std::vector<int>& cells, bool toSeam,
                 std::vector<int>& streams, int from) {
        int room = int(dwRound(DW_PI * double(r) * double(r)));
        int inset = r + (toSeam ? 1 : DEAD_END_LAKE_GAP);
        double reachOut = DEAD_END_LAKE_REACH * double(r);
        for (int m = 0; m < length; ++m) {
            int ci = mthClamp(chain[m] & W_MASK, HALO + inset, HALO + CORE - 1 - inset);
            int cj = mthClamp(chain[m] >> W_SHIFT, HALO + inset, HALO + CORE - 1 - inset);
            int top = m;
            while (top + 1 < length && std::hypot(double((chain[top + 1] & W_MASK) - ci),
                    double((chain[top + 1] >> W_SHIFT) - cj)) <= reachOut) {
                top++;
            }
            if (toSeam) top = std::max(top, std::min(length - 1, m + DEAD_END_LAKE_GAP + 2));
            int waterY = riverLevel(chain[m]);
            int count = growLake(chain[m], ci, cj, r, room, waterY, top, reach, apart, cells, toSeam);
            if (double(count) < DEAD_END_LAKE_FULL * double(room)) continue;
            int inlet = m + 1;
            for (int k = m + 1; k < full; ++k) {
                if (floodMark[chain[k]] == floodStamp) inlet = k + 1;
            }
            makeLake(chain, inlet, std::vector<int>(cells.begin(), cells.begin() + count), waterY);
            return streamsBelow(chain, inlet, streams, from);
        }
        return -1;
    }

    std::vector<int> chainReach(const std::vector<int>& chain, int length) const {
        std::vector<int> reach(N, -2), walk(N);
        for (int k = 0; k < length; ++k) reach[chain[k]] = k;
        for (int s = 0; s < N; ++s) {
            if (reach[s] != -2) continue;
            int count = 0, q = s;
            while (q >= 0 && reach[q] == -2) {
                reach[q] = -3;
                walk[count++] = q;
                q = (sink[q] || lakeId[q] >= 0) ? -1 : downstream(q);
            }
            int to = (q >= 0 && reach[q] >= 0) ? reach[q] : -1;
            while (count > 0) reach[walk[--count]] = to;
        }
        return reach;
    }

    std::vector<uint8_t> keptApart(const std::vector<int>& chain, int length, const std::vector<int>& reach) const {
        std::vector<uint8_t> apart(N, 0);
        int loI = W, hiI = -1, loJ = W, hiJ = -1;
        for (int k = 0; k < length; ++k) {
            loI = std::min(loI, chain[k] & W_MASK); hiI = std::max(hiI, chain[k] & W_MASK);
            loJ = std::min(loJ, chain[k] >> W_SHIFT); hiJ = std::max(hiJ, chain[k] >> W_SHIFT);
        }
        int reachOut = 3 * DEAD_END_LAKE_MAX + DEAD_END_LAKE_GAP + 4;
        int i0 = std::max(0, loI - reachOut), i1 = std::min(W - 1, hiI + reachOut);
        int j0 = std::max(0, loJ - reachOut), j1 = std::min(W - 1, hiJ + reachOut);
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                int q = (j << W_SHIFT) | i;
                int r = sink[q] ? 2 : (chan[q] && reach[q] < 0 && inCore(q)) ? 1 + int(cellWidth(q) / 16.0) : -1;
                if (r < 0) continue;
                for (int dj = -r; dj <= r; ++dj) {
                    for (int di = -r; di <= r; ++di) {
                        int ni = i + di, nj = j + dj;
                        if (ni >= 0 && nj >= 0 && ni < W && nj < W) apart[(nj << W_SHIFT) | ni] = 1;
                    }
                }
            }
        }
        return apart;
    }

    int endInLake(int c, std::vector<int>& streams, int from) {
        std::vector<int> chain(2 * (DEAD_END_LAKE_SEARCH + 1));
        int length = 0;
        for (int p = c; length < int(chain.size());) {
            chain[length++] = p;
            int u = mainUp(p);
            if (u < 0 || !inCore(u) || !chan[u] || lakeId[u] >= 0) break;
            p = u;
        }
        int full = length;
        length = std::min(length, DEAD_END_LAKE_SEARCH + 1);
        std::vector<int> reach = chainReach(chain, length);
        std::vector<uint8_t> apart = keptApart(chain, length, reach);
        int joined = joinLake(chain, length, reach, apart, false);
        if (joined >= 0) return streamsBelow(chain, joined, streams, from);
        int want = mthClamp(int(dwRound(cellWidth(c) * DEAD_END_LAKE_SPAN)), DEAD_END_LAKE_MIN, DEAD_END_LAKE_MAX);
        std::vector<int> cells(N);
        for (int r = want;; r = std::max(DEAD_END_LAKE_LAST, r * 4 / 5)) {
            int made = growInto(chain, length, full, r, reach, apart, cells, false, streams, from);
            if (made >= 0) return made;
            if (r == DEAD_END_LAKE_LAST) break;
        }
        int edge = joinLake(chain, length, reach, apart, true);
        if (edge >= 0) return streamsBelow(chain, edge, streams, from);
        return growInto(chain, length, full, DEAD_END_LAKE_LAST, reach, apart, cells, true, streams, from);
    }

    int lakeBeside(int c) const {
        int ci = c & W_MASK, cj = c >> W_SHIFT;
        int level = INT32_MIN;
        for (int a = 0; a < 8; ++a) {
            int ni = ci + DX[a], nj = cj + DZ[a];
            if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
            int nc = (nj << W_SHIFT) | ni;
            if (lakeId[nc] < 0 || !inCore(nc)) continue;
            if (level == INT32_MIN) level = riverLevel(c);
            if (lakeWaterY[lakeId[nc]] <= level) return a;
        }
        return -1;
    }

    bool reopenBelow(int s, bool toSeam) {
        std::vector<int> way(CORE * CORE);
        int level = dwRoundF(breached[s]);
        int count = 0;
        for (int c = s; count < int(way.size());) {
            int into = lakeBeside(c);
            if (into >= 0) {
                dir[c] = int8_t(into);
                for (int k = 0; k < count; ++k) chan[way[k]] = 1;
                return true;
            }
            int q = downstream(c);
            if (q < 0) return false;
            if ((lakeId[q] >= 0 && lakeWaterY[lakeId[q]] > level - CHANNEL_INCISION)
                    || (chan[q] && !sink[q] && dwRoundF(breached[q]) > level)) return false;
            bool water = (lakeId[q] >= 0) || (chan[q] && inCore(q)) || sink[q];
            if (water || !inCore(q)) {
                if (!water && !toSeam) return false;
                for (int k = 0; k < count; ++k) chan[way[k]] = 1;
                return true;
            }
            way[count++] = q;
            c = q;
        }
        return false;
    }

    bool dryUp(int s) {
        std::vector<int> tree(N);
        std::vector<uint8_t> seen(N, 0);
        int count = 0;
        tree[count++] = s;
        seen[s] = 1;
        for (int k = 0; k < count; ++k) {
            int d = tree[k];
            if (inflow[d] || !inCore(d)) return false;
            int di = d & W_MASK, dj = d >> W_SHIFT;
            for (int a = 0; a < 8; ++a) {
                int ni = di + DX[a], nj = dj + DZ[a];
                if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
                int u = (nj << W_SHIFT) | ni;
                if (!seen[u] && chan[u] && downstream(u) == d) {
                    seen[u] = 1;
                    tree[count++] = u;
                }
            }
        }
        for (int k = 0; k < count; ++k) chan[tree[k]] = 0;
        return true;
    }

    bool comesBack(int, int d) const {
        int wx = cx0 + (d & W_MASK), wz = cz0 + (d >> W_SHIFT);
        int n = neighbourOf(wx, wz);
        if (n < 0 || !around[n]) return false;
        int16_t exitIdx = exitOfIn(n, wx, wz);
        if (exitIdx >= 0 && withdrawn[n][exitIdx]) exitIdx = wayOut[n][exitIdx];
        if (exitIdx < 0) return false;
        const auto& e = around[n]->exits[exitIdx];
        int back = windowCell(e.cellX, e.cellZ);
        return back >= 0 && inCore(back) && inflow[back];
    }

    bool fromLake(int c) const {
        for (int p = c, steps = 0; steps < N; ++steps) {
            int u = mainUp(p);
            if (u < 0) return false;
            if (lakeId[u] >= 0) return inCore(u);
            if (!chan[u] || !inCore(u)) return false;
            p = u;
        }
        return false;
    }

    void deadEndLakes() {
        if (!lakesEnabled) return;
        for (const auto& s : seamLakes) makeSeamLake(s);
        std::vector<uint8_t> riverAlone(N, 0);
        for (const auto& e : ownSeams->exits) {
            int c = windowCell(e.fromX, e.fromZ);
            if (c >= 0 && e.channel) riverAlone[c] = 1;
        }
        std::vector<int64_t> ends;
        ends.reserve(5 * CORE);
        for (int j = HALO; j < HALO + CORE; ++j) {
            bool rimRow = (j == HALO || j == HALO + CORE - 1);
            for (int i = HALO; i < HALO + CORE; ++i) {
                if (!rimRow && i != HALO && i != HALO + CORE - 1) continue;
                int c = (j << W_SHIFT) | i;
                if (!chan[c]) continue;
                int d = downstream(c);
                if (d < 0 || inCore(d) || sink[d]) continue;
                bool corner = neighbourOf(cx0 + (d & W_MASK), cz0 + (d >> W_SHIFT)) < 0;
                if (riverAlone[c] && !dropped[c] && !corner) continue;
                if (lakeId[d] >= 0 && lakeWaterY[lakeId[d]] <= riverLevel(c)) continue;
                if (!dropped[c] && comesBack(c, d)) continue;
                ends.push_back((int64_t(INT32_MAX - wet[c]) << CELL_KEY_BITS) | int64_t(c));
            }
        }
        for (int c = 0; c < N && int(ends.size()) < 5 * CORE; ++c) {
            if (!chan[c] || !inCore(c)) continue;
            int d = downstream(c);
            int lake = (d < 0 || !inCore(d)) ? -1 : ((lakeId[d] >= 0) ? lakeId[d] : lakeShore[d]);
            if (lake < 0 || !sharedLake(lake) || !heldUnder(c, lakeWaterY[lake])) continue;
            ends.push_back((int64_t(INT32_MAX - wet[c]) << CELL_KEY_BITS) | int64_t(c));
        }
        std::sort(ends.begin(), ends.end());
        std::vector<int> streams(N);
        for (int64_t e : ends) {
            int c = int(uint64_t(e) & CELL_KEY_MASK);
            if (!chan[c]) continue;
            int count = endInLake(c, streams, 0);
            if (count < 0 && fromLake(c) && dryUp(c)) continue;
            for (int k = 0; k < count; ++k) {
                int s = streams[k];
                if (!chan[s] || reopenBelow(s, false)) continue;
                int more = endInLake(s, streams, count);
                if (more >= 0) count = more;
                else if (dryUp(s)) {}
                else reopenBelow(s, true);
            }
        }
    }

    void widenAsTold() {
        const auto& own = ownSeams->exits;
        const auto& exitOf = ownSeams->exitOf;
        for (int n = 0; n < 4; ++n) {
            if (!around[n]) continue;
            int ntx = tile.tileX + SEAM_NEIGHBOURS[n][0], ntz = tile.tileZ + SEAM_NEIGHBOURS[n][1];
            for (int x = 0; x < int(own.size()); ++x) {
                const auto& e = own[x];
                if (!e.channel || !inTile(ntx, ntz, e.cellX, e.cellZ)) continue;
                int f = windowCell(e.fromX, e.fromZ);
                if (f < 0 || dropped[f] || !chan[f] || downstream(f) != windowCell(e.cellX, e.cellZ)) continue;
                for (int c = f; c >= 0 && inCore(c) && chan[c]; c = mainUp(c)) {
                    int ci = coreIndex(c);
                    if (exitOf[ci] != x || wetAlone[ci] <= wet[c]) break;
                    wet[c] = wetAlone[ci];
                }
            }
        }
    }

    void carryWidths() {
        for (int c = 0; c < N; ++c) {
            int d = (chan[c] && inCore(c)) ? downstream(c) : -1;
            if (d >= 0 && !inCore(d) && chan[d]) wet[d] = INT32_MIN;
        }
        for (int c = 0; c < N; ++c) {
            int d = (chan[c] && inCore(c)) ? downstream(c) : -1;
            if (d >= 0 && !inCore(d) && chan[d]) wet[d] = std::max(wet[d], wet[c]);
        }
    }

    void packageLakes() {
        constexpr int nr = WINDOW_REGIONS * WINDOW_REGIONS;
        std::array<uint8_t, nr> touched{};
        packaged.resize(lakeCells.size());
        packagedValid.assign(lakeCells.size(), 0);
        for (size_t id = 0; id < lakeCells.size(); ++id) {
            const auto& cells = lakeCells[id];
            if (cells.empty()) continue;
            int waterY = lakeWaterY[id];
            int loI = W, hiI = -1, loJ = W, hiJ = -1;
            touched.fill(0);
            for (int c : cells) {
                int i = c & W_MASK, j = c >> W_SHIFT;
                if (i < loI) loI = i; if (i > hiI) hiI = i;
                if (j < loJ) loJ = j; if (j > hiJ) hiJ = j;
                touched[regionIndex(c)] = 1;
            }
            int w = hiI - loI + 1, hh = hiJ - loJ + 1;
            auto bits = WatershedLake::newBits(w, hh);
            for (int c : cells) WatershedLake::setBit(bits, w, (c & W_MASK) - loI, (c >> W_SHIFT) - loJ);
            int gw = w + 2, gh = hh + 2;
            std::vector<int16_t> coarse(gw * gh);
            for (int lz = 0; lz < gh; ++lz) {
                int j = mthClamp(loJ - 1 + lz, 0, W - 1);
                for (int lx = 0; lx < gw; ++lx) {
                    int i = mthClamp(loI - 1 + lx, 0, W - 1);
                    int c = (j << W_SHIFT) | i;
                    int ground = dwRoundF(h[c]);
                    if (lakeId[c] != int(id) && ground <= waterY) ground = waterY + 1;
                    coarse[lz * gw + lx] = int16_t(ground);
                }
            }
            packaged[id].init(waterY, cx0 + loI, cz0 + loJ, w, hh, std::move(bits), std::move(coarse),
                    isDeadEndLake(int(id)) ? DEAD_END_LAKE_DIG : WatershedLake::SHAVE, allocLakeIdFn());
            packagedValid[id] = 1;
            for (int r = 0; r < nr; ++r) {
                if (!touched[r] || !isCoreRegion(r)) continue;
                tile.lakesByRegion[localRegionSlot(r)].push_back(packaged[id]);
            }
        }
    }

    int lakeLevelBeside(int c, double width, int water) const {
        int r = 1 + int(width / double(CELL));
        int ci = c & W_MASK, cj = c >> W_SHIFT;
        int best = INT32_MIN;
        for (int dj = -r; dj <= r; ++dj) {
            int nj = cj + dj;
            if (nj < 0 || nj >= W) continue;
            for (int di = -r; di <= r; ++di) {
                int ni = ci + di;
                if (ni < 0 || ni >= W) continue;
                int id = lakeId[(nj << W_SHIFT) | ni];
                if (id < 0) continue;
                int y = lakeWaterY[id];
                if (y <= water && y > best) best = y;
            }
        }
        return best;
    }

    void lakeFloors() {
        std::fill(lakeFloorAt.begin(), lakeFloorAt.end(), INT32_MIN);
        for (int c = 0; c < N; ++c) {
            if (lakeId[c] >= 0) continue;
            int water = dwRoundF(breached[c]);
            int f = INT32_MIN;
            if (!lakeWaterY.empty()) f = lakeLevelBeside(c, cellWidth(c), water);
            int d = downstream(c);
            if ((sink[c] || (d >= 0 && sink[d])) && water >= seaLevel - 1) f = std::max(f, seaLevel - 1);
            lakeFloorAt[c] = f;
        }
        for (int k = 0; k < orderCount; ++k) {
            int c = order[k], d = downstream(c);
            if (d < 0 || lakeId[c] >= 0) continue;
            int f = lakeFloorAt[d];
            if (f > lakeFloorAt[c] && f <= dwRoundF(breached[c])) lakeFloorAt[c] = f;
        }
    }

    bool mayGoUnder(int c) const {
        int i = (c & W_MASK) - HALO, j = (c >> W_SHIFT) - HALO;
        return i >= UNDER_SEAM && j >= UNDER_SEAM && i < CORE - UNDER_SEAM && j < CORE - UNDER_SEAM
            && !sink[c] && !mainRiver[c] && !cut[c] && !nearRiver[c] && !upLake[c] && !inflow[c]
            && lakeId[c] < 0 && lakeShore[c] < 0 && (ground0.empty() || ground0[c] <= h[c] + 1.0f);
    }

    bool clearBelow(int foot) const {
        int c = foot;
        for (int k = 0; k < UNDER_CLEAR; ++k) {
            c = downstream(c);
            if (c < 0 || !mayGoUnder(c)) return false;
        }
        return true;
    }

    int underLevels(int foot, int run, float deepest, int reach, const std::vector<int>& cells, std::vector<float>& levels) {
        float water = breached[foot];
        bool running = true;
        int covered = 0;
        for (int k = 0; k < reach; ++k) {
            int u = cells[k];
            water += running ? UNDER_RUN : UNDER_DIVE;
            if (water >= breached[u]) { underCount = k; return covered; }
            if (water - float(CHANNEL_INCISION) < float(lakeFloorAt[u])) return -1;
            levels[k] = water;
            if (h[u] - (water - float(CHANNEL_INCISION)) >= float(UNDER_COVER)) covered++;
            if (breached[u] - water >= deepest || k + 1 >= run) running = false;
        }
        return -1;
    }

    void goUnder(int foot, int longest, float deepest, double chance, std::vector<uint8_t>& taken,
                 std::vector<int>& cells, std::vector<float>& levels) {
        int reach = 0;
        for (int c = foot; reach < longest + UNDER_DIVE_CELLS;) {
            int u = mainUp(c);
            if (u < 0 || !chan[u] || taken[u] || !mayGoUnder(u)) break;
            cells[reach++] = u;
            c = u;
        }
        int bestRun = 0;
        for (int run = std::min(longest, reach); run > 0 && bestRun == 0; --run) {
            if (underLevels(foot, run, deepest, reach, cells, levels) >= UNDER_MIN_CELLS) bestRun = run;
        }
        if (bestRun == 0) return;
        uint64_t hash = uint64_t(hashCommonMix(int64_t(
            uint64_t(seed) ^ 0x7E57ADA5ull ^ (uint64_t(int64_t(cx0 + (foot & W_MASK))) * 341873128712ull)
            ^ (uint64_t(int64_t(cz0 + (foot >> W_SHIFT))) * 132897987541ull))));
        if (double(hash >> 11) * 0x1.0p-53 >= chance) return;
        underLevels(foot, bestRun, deepest, reach, cells, levels);
        int count = underCount;
        int stem = count, head = cells[count];
        for (int k = -1; k < count; ++k) {
            int d = (k < 0) ? foot : cells[k];
            float level = (k < 0) ? breached[foot] : levels[k];
            int i = d & W_MASK, j = d >> W_SHIFT;
            for (int n = 0; n < 8; ++n) {
                int ni = i + DX[n], nj = j + DZ[n];
                if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
                int v = (nj << W_SHIFT) | ni;
                if (!chan[v] || downstream(v) != d) continue;
                if ((k + 1 < stem && v == cells[k + 1]) || v == head) continue;
                float to = level + UNDER_DIVE;
                if (breached[v] <= to) continue;
                if (taken[v] || !mayGoUnder(v) || to - float(CHANNEL_INCISION) < float(lakeFloorAt[v])) return;
                cells[count] = v;
                levels[count++] = to;
            }
        }
        for (int k = 0; k < count; ++k) {
            breached[cells[k]] = levels[k];
            taken[cells[k]] = 1;
            underCells[cells[k]] = 1;
        }
        taken[foot] = 1;
        int up = head, down = foot;
        for (int k = 0; k < UNDER_GAP; ++k) {
            if (up >= 0) { taken[up] = 1; up = mainUp(up); }
            if (down >= 0) { taken[down] = 1; down = downstream(down); }
        }
    }

    void goUnderground() {
        if (!cfg.mountainRiverTunnels) return;
        double chance = tunnelFrequencyChance(cfg.mountainTunnelFrequency);
        int longest = cfg.mountainTunnelLength >> CS;
        float deepest = float(cfg.mountainTunnelDepth);
        std::vector<uint8_t> taken(N, 0);
        std::vector<int> cells(N);
        std::vector<float> levels(N);
        underCells.assign(N, 0);
        for (int k = 0; k < orderCount; ++k) {
            int foot = order[k];
            if (!chan[foot] || taken[foot] || !mayGoUnder(foot) || !clearBelow(foot)) continue;
            goUnder(foot, longest, deepest, chance, taken, cells, levels);
        }
    }

    MainRiverPoint nearestMainRiver(double x, double z) const {
        MainRiverPoint res;
        if (!mainRivers) return res;
        std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>> edges;
        mainRivers->collectNearbyEdges(x, z, edges);
        if (edges.empty()) return res;
        double bestOut = std::numeric_limits<double>::max(), bx = x, bz = z, dx = 0.0, dz = 0.0, bestR = 0.0;
        const RiverGraphEdge* bestEdge = nullptr;
        const std::vector<RiverGraphEdge>* bestVec = nullptr;
        for (const auto& entry : edges) {
            const auto& regionEdges = *entry.first;
            const auto& edge = regionEdges[entry.second];
            double maxR = edge.maxWaterRadius(regionEdges);
            const auto& s = edge.fractal.segments;
            for (size_t k = 0; k + 3 < s.size(); k += 2) {
                double t = projectAlongLine(s[k], s[k + 1], s[k + 2], s[k + 3], x, z);
                double px = s[k] + t * (s[k + 2] - s[k]);
                double pz = s[k + 1] + t * (s[k + 3] - s[k + 1]);
                double d = std::hypot(px - x, pz - z);
                if (d - maxR >= bestOut) continue;
                double r = hydrology_const::routeWaterRadius(edge, px, pz, regionEdges, cfg);
                if (d - r < bestOut) {
                    bestOut = d - r;
                    bestR = r;
                    bx = px; bz = pz;
                    dx = s[k + 2] - s[k]; dz = s[k + 3] - s[k + 1];
                    bestEdge = &edge;
                    bestVec = &regionEdges;
                }
            }
        }
        if (!bestEdge) return res;
        res.valid = true;
        res.x = bx; res.z = bz;
        res.width = bestEdge->widthAt(bx, bz, *bestVec);
        res.waterRadius = bestR;
        double dl = std::hypot(dx, dz);
        if (dl >= 1.0e-9) { res.dirX = dx / dl; res.dirZ = dz / dl; }
        return res;
    }

    int aimIntoLake(int prev, int c, int n) const {
        auto waterAhead = [&](int cell, int di, int dj, int lake) {
            int i = cell & W_MASK, j = cell >> W_SHIFT, run = 0;
            while (run < MOUTH_AIM_CELLS) {
                i += di; j += dj;
                if (i < 0 || j < 0 || i >= W || njOutOfBounds(j) || lakeId[(j << W_SHIFT) | i] != lake) break;
                run++;
            }
            return run;
        };
        int lake = lakeId[n];
        int ci = c & W_MASK, cj = c >> W_SHIFT;
        int ai = prev >= 0 ? ci - (prev & W_MASK) : (n & W_MASK) - ci;
        int aj = prev >= 0 ? cj - (prev >> W_SHIFT) : (n >> W_SHIFT) - cj;
        int best = n, most = waterAhead(c, (n & W_MASK) - ci, (n >> W_SHIFT) - cj, lake);
        for (int k = 0; k < 8; ++k) {
            if (DX[k] * ai + DZ[k] * aj < 0) continue;
            int ni = ci + DX[k], nj = cj + DZ[k];
            if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
            int m = (nj << W_SHIFT) | ni;
            if (m == n || lakeId[m] != lake) continue;
            int run = waterAhead(c, DX[k], DZ[k], lake);
            if (run > most) { most = run; best = m; }
        }
        return best;
    }
    static bool njOutOfBounds(int j) { return j < 0 || j >= W; }

    int extendInto(std::vector<int>& buf, int len, int from, int into, int lake) const {
        auto waterAhead = [&](int cell, int di, int dj) {
            int i = cell & W_MASK, j = cell >> W_SHIFT, run = 0;
            while (run < MOUTH_AIM_CELLS) {
                i += di; j += dj;
                if (i < 0 || j < 0 || i >= W || j >= W || lakeId[(j << W_SHIFT) | i] != lake) break;
                run++;
            }
            return run;
        };
        int di = (into & W_MASK) - (from & W_MASK), dj = (into >> W_SHIFT) - (from >> W_SHIFT);
        int ei = di, ej = dj;
        int i = into & W_MASK, j = into >> W_SHIFT;
        for (int k = 0; k < MOUTH_EXTRA_CELLS; ++k) {
            if (lake >= 0) {
                int at = (j << W_SHIFT) | i;
                int most = waterAhead(at, di, dj), bi = di, bj = dj;
                for (int d = 0; d < 8; ++d) {
                    if (DX[d] * di + DZ[d] * dj < 0 || DX[d] * ei + DZ[d] * ej < 0 || (DX[d] == di && DZ[d] == dj)) continue;
                    int run = waterAhead(at, DX[d], DZ[d]);
                    if (run > most) { most = run; bi = DX[d]; bj = DZ[d]; }
                }
                di = bi; dj = bj;
            }
            i += di; j += dj;
            if (i < 0 || j < 0 || i >= W || j >= W) break;
            int c = (j << W_SHIFT) | i;
            bool stillWater = (lake < 0) ? (sink[c] && !mainRiver[c]) : (lakeId[c] == lake);
            if (!stillWater || (lake >= 0 && inCore(into) && rimDistance(c) < 1)) break;
            buf[len++] = c;
        }
        return len;
    }

    int leadFromLake(std::vector<int>& buf, int s) const {
        int from = -1;
        int si = s & W_MASK, sj = s >> W_SHIFT;
        for (int k = 0; k < 8; ++k) {
            int ni = si + DX[k], nj = sj + DZ[k];
            if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
            int nc = (nj << W_SHIFT) | ni;
            if (lakeId[nc] >= 0 && downstream(nc) == s) { from = nc; break; }
        }
        if (from < 0) return 0;
        int lake = lakeId[from];
        int di = (from & W_MASK) - si, dj = (from >> W_SHIFT) - sj;
        int n = 1, i = from & W_MASK, j = from >> W_SHIFT;
        for (int k = 1; k < LAKE_LEAD_CELLS; ++k) {
            i += di; j += dj;
            if (i < 0 || j < 0 || i >= W || j >= W) break;
            if (lakeId[(j << W_SHIFT) | i] != lake) break;
            n++;
        }
        i = from & W_MASK; j = from >> W_SHIFT;
        for (int k = 0; k < n; ++k) {
            buf[n - 1 - k] = (j << W_SHIFT) | i;
            i += di; j += dj;
        }
        return n;
    }

    void prepareBends() {
        bendCrossing.assign(N, 0);
        for (int c = 0; c < N; ++c) {
            if (inflow[c]) bendCrossing[c] = 1;
            if (!chan[c]) continue;
            int d = downstream(c);
            if (d >= 0 && inCore(c) != inCore(d)) { bendCrossing[c] = 1; bendCrossing[d] = 1; }
        }
        bendKeep.assign(N, 0.0f);
        bendNx.assign(N, 0.0f);
        bendNz.assign(N, 0.0f);
        bendK.assign(N, 0.0f);
        bendPhase.assign(N, 0.0f);
        bendFlat.assign(N, 0.0f);

        std::vector<uint8_t> known(N, 0);
        std::vector<int> stack(N);
        for (int s = 0; s < N; ++s) {
            if (known[s]) continue;
            int sp = 0, c = s;
            while (c >= 0 && !known[c] && sp < N) { stack[sp++] = c; c = downstream(c); }
            int k = sp - 1;
            double phase;
            if (c >= 0 && known[c]) {
                phase = bendPhase[c];
            } else {
                int e = stack[k--];
                int64_t wx = cx0 + (e & W_MASK), wz = cz0 + (e >> W_SHIFT);
                uint64_t hm = uint64_t(hashCommonMix(int64_t(
                    uint64_t(seed) ^ uint64_t(wx) * 0x9E3779B97F4A7C15ull ^ uint64_t(wz) * 0xC2B2AE3D27D4EB4Full)));
                phase = DW_PI * 2.0 * double(hm >> 11) * 0x1.0p-53;
                bendPhase[e] = float(phase);
                known[e] = 1;
            }
            for (; k >= 0; --k) {
                int cc = stack[k];
                int d = dir[cc];
                double rate = 1.0 + MEANDER_WAVE_SPREAD * valueNoise(
                        double(cx0 + (cc & W_MASK)) / MEANDER_WAVE_CELLS,
                        double(cz0 + (cc >> W_SHIFT)) / MEANDER_WAVE_CELLS, 9);
                phase += DW_PI * 2.0 * (d < 0 ? 1.0f : DIST[d]) * double(CELL) / (MEANDER_WAVELENGTH * 2.0 * cellWidth(cc)) * rate;
                bendPhase[cc] = float(phase);
                known[cc] = 1;
            }
        }
        for (int c = 0; c < N; ++c) {
            if (!chan[c]) continue;
            int up = mainUp(c), up2 = up >= 0 ? mainUp(up) : -1;
            int dn = downstream(c), dn2 = dn >= 0 ? downstream(dn) : -1;
            int a = up2 >= 0 ? up2 : up >= 0 ? up : c;
            int b = dn2 >= 0 ? dn2 : dn >= 0 ? dn : c;
            double ax = (c & W_MASK) - (a & W_MASK), az = (c >> W_SHIFT) - (a >> W_SHIFT);
            double bx = (b & W_MASK) - (c & W_MASK), bz = (b >> W_SHIFT) - (c >> W_SHIFT);
            double al = std::hypot(ax, az), bl = std::hypot(bx, bz);
            double fx = 0.0, fz = 0.0;
            if (al > 0.0) { fx += ax / al; fz += az / al; }
            if (bl > 0.0) { fx += bx / bl; fz += bz / bl; }
            double fl = std::hypot(fx, fz);
            if (fl < 1.0e-6) continue;
            fx /= fl; fz /= fl;
            bendNx[c] = float(-fz);
            bendNz[c] = float(fx);
            if (al > 0.0 && bl > 0.0) {
                double turn = std::acos(mthClamp((ax * bx + az * bz) / (al * bl), -1.0, 1.0));
                double side = (bx / bl - ax / al) * -fz + (bz / bl - az / al) * fx;
                bendK[c] = float(dwSignum(side) * turn / (0.5 * (al + bl) * double(CELL)));
            }
            double run = (al + bl) * double(CELL);
            double sl = (run > 0.0) ? std::max(0.0, double(breached[a] - breached[b])) / run : 0.0;
            bendFlat[c] = float(dwClampedMap(sl, MEANDER_FLAT_SLOPE, MEANDER_STEEP_SLOPE, 1.0, MEANDER_STEEP_SHARE));
            bendKeep[c] = float(keepAt(c));
        }
    }

    bool noSway(int c) const { return sink[c] || cut[c] || lakeId[c] >= 0 || lakeShore[c] >= 0; }
    double keepAt(int c) const {
        int i = c & W_MASK, j = c >> W_SHIFT;
        int near = MEANDER_KEEP_CELLS, nearMain = MEANDER_KEEP_MAIN_CELLS;
        for (int r = 0; r < std::max(near, nearMain); ++r) {
            bool hit = false, main = false;
            for (int dj = -r; dj <= r; ++dj) {
                int nj = j + dj;
                if (nj < 0 || nj >= W) continue;
                for (int di = -r; di <= r; ++di) {
                    if (std::max(std::abs(di), std::abs(dj)) != r) continue;
                    int ni = i + di;
                    if (ni < 0 || ni >= W) continue;
                    int nc = (nj << W_SHIFT) | ni;
                    if (noSway(nc) || bendCrossing[nc]) hit = true;
                    if (mainRiver[nc]) main = true;
                }
            }
            if (hit && r < near) near = r;
            if (main && r < nearMain) nearMain = r;
        }
        return std::min(dwSmoothstep(double(near) / double(MEANDER_KEEP_CELLS)),
                        dwSmoothstep(double(nearMain) / double(MEANDER_KEEP_MAIN_CELLS)));
    }
    bool besideMain(int c) const {
        int i = c & W_MASK, j = c >> W_SHIFT;
        for (int dj = -MEANDER_MAIN_GAP_CELLS; dj <= MEANDER_MAIN_GAP_CELLS; ++dj) {
            int nj = j + dj;
            if (nj < 0 || nj >= W) continue;
            for (int di = -MEANDER_MAIN_GAP_CELLS; di <= MEANDER_MAIN_GAP_CELLS; ++di) {
                int ni = i + di;
                if (ni >= 0 && ni < W && mainRiver[(nj << W_SHIFT) | ni]) return true;
            }
        }
        return false;
    }
    bool meetsSoon(int a, int b) const {
        int down[MEANDER_JOIN_CELLS + 1];
        int n = 0;
        for (int c = b; c >= 0 && n <= MEANDER_JOIN_CELLS; c = downstream(c)) down[n++] = c;
        for (int k = 0, c = a; c >= 0 && k <= MEANDER_JOIN_CELLS; ++k, c = downstream(c)) {
            for (int i = 0; i < n; ++i) if (down[i] == c) return true;
        }
        return false;
    }
    double halfWay(double s, double width, int c, double level) const {
        double gap = MEANDER_RIVER_GAP + std::min(MEANDER_GAP_MAX, MEANDER_GAP_PER_BLOCK * std::abs(double(breached[c]) - level));
        return std::max(0.0, 0.5 * (s + width - cellWidth(c) - gap));
    }
    double lacksBeside(double x, double z, double ux, double uz, double width, double level, int c0, int c1) const {
        int up = mainUp(c0), down = downstream(c1);
        double reach = width + MAX_WIDTH + MEANDER_RIVER_APART + MEANDER_GAP_MAX;
        for (double s = 2.0; s <= reach; s += 2.0) {
            int ci = int(dwRound((x + ux * s) / double(CELL))) - cx0;
            int cj = int(dwRound((z + uz * s) / double(CELL))) - cz0;
            if (ci < 0 || cj < 0 || ci >= W || cj >= W) return 0.0;
            int c = (cj << W_SHIFT) | ci;
            if (!chan[c] || c == c0 || c == c1 || c == up || c == down) continue;
            double rise = std::abs(double(breached[c]) - level);
            if (meetsSoon(c, c1) && rise < MEANDER_JOIN_LEVEL) return 0.0;
            return std::max(0.0, width + cellWidth(c) + MEANDER_RIVER_APART + std::min(MEANDER_GAP_MAX, MEANDER_GAP_PER_BLOCK * rise) - s);
        }
        return 0.0;
    }
    static double edgeRoom(double inside, double rate, double keep) {
        if (rate <= 1.0e-9) return std::numeric_limits<double>::infinity();
        return std::max(0.0, (inside - std::min(inside, keep)) / rate);
    }
    double seamRoom(double x, double z, double ux, double uz, double width) const {
        double x0 = double((cx0 + HALO) << CS), x1 = double((cx0 + HALO + CORE) << CS);
        double z0 = double((cz0 + HALO) << CS), z1 = double((cz0 + HALO + CORE) << CS);
        double keep = 0.5 * width + MEANDER_SEAM_MARGIN;
        return std::min(std::min(edgeRoom(x - x0, -ux, keep), edgeRoom(x1 - x, ux, keep)),
                        std::min(edgeRoom(z - z0, -uz, keep), edgeRoom(z1 - z, uz, keep)));
    }
    bool roomFor(double x, double z, double level, double width) const {
        double g = groundAt(h, x, z);
        return g <= level + MEANDER_ROOM_PER_WIDTH * width + MEANDER_ROOM_RISE && g >= level - MEANDER_ROOM_DROP;
    }
    double valleyRoom(double x, double z, double ux, double uz, double level, double limit, double width, int c0, int c1) const {
        double top = level + MEANDER_ROOM_PER_WIDTH * width + MEANDER_ROOM_RISE;
        double bottom = level - MEANDER_ROOM_DROP;
        int bi = int(dwRound(x / double(CELL))) - cx0, bj = int(dwRound(z / double(CELL))) - cz0;
        int up = mainUp(c0), down = downstream(c1);
        double room = 0.0, before = groundAt(h, x, z);
        for (double d = 2.0;; d += 2.0) {
            double s = std::min(d, limit);
            double px = x + ux * s, pz = z + uz * s;
            int ci = int(dwRound(px / double(CELL))) - cx0, cj = int(dwRound(pz / double(CELL))) - cz0;
            if (ci < 0 || cj < 0 || ci >= W || cj >= W) return room;
            int c = (cj << W_SHIFT) | ci;
            if (noSway(c) || besideMain(c)) return room;
            if (chan[c] && c != c0 && c != c1 && c != up && c != down) {
                if (!meetsSoon(c, c1) || std::abs(double(breached[c]) - level) >= MEANDER_JOIN_LEVEL) {
                    return std::min(room, halfWay(s, width, c, level));
                }
                if (std::max(std::abs(ci - bi), std::abs(cj - bj)) >= 2) return room;
            }
            double g = groundAt(h, px, pz);
            if (g > top) return (g > before) ? room + (s - room) * mthClamp((top - before) / (g - before), 0.0, 1.0) : room;
            if (g < bottom) return (g < before) ? room + (s - room) * mthClamp((before - bottom) / (before - g), 0.0, 1.0) : room;
            room = s;
            before = g;
            if (s >= limit) break;
        }
        double far = limit + MAX_WIDTH + MEANDER_RIVER_GAP + MEANDER_GAP_MAX;
        for (double s = limit + 2.0; s <= far; s += 2.0) {
            int ci = int(dwRound((x + ux * s) / double(CELL))) - cx0;
            int cj = int(dwRound((z + uz * s) / double(CELL))) - cz0;
            if (ci < 0 || cj < 0 || ci >= W || cj >= W) break;
            int c = (cj << W_SHIFT) | ci;
            if (!chan[c] || c == c0 || c == c1 || c == up || c == down) continue;
            if (meetsSoon(c, c1) && std::abs(double(breached[c]) - level) < MEANDER_JOIN_LEVEL) break;
            return std::min(room, halfWay(s, width, c, level));
        }
        return room;
    }

    double sway(double x, double z, int c0, int c1, double t, double tn, double ends) {
        swayNormal[0] = 0.0; swayNormal[1] = 0.0;
        double keep = mthLerp(t, double(bendKeep[c0]), double(bendKeep[c1])) * ends;
        if (keep <= 0.0) return 0.0;
        double tp = bendKeep[c0] <= 0.0f ? 1.0 : (bendKeep[c1] <= 0.0f ? 0.0 : t);
        double tq = bendKeep[c0] <= 0.0f ? 1.0 : (bendKeep[c1] <= 0.0f ? 0.0 : tn);
        double nx = mthLerp(tq, double(bendNx[c0]), double(bendNx[c1]));
        double nz = mthLerp(tq, double(bendNz[c0]), double(bendNz[c1]));
        double nl = std::hypot(nx, nz);
        if (nl < 1.0e-6) return 0.0;
        nx /= nl; nz /= nl;
        swayNormal[0] = nx; swayNormal[1] = nz;
        double r = mthLerp(tp, cellWidth(c0), cellWidth(c1));
        double phase = mthLerp(tp, double(bendPhase[c0]), double(bendPhase[c1]));
        double strength = 0.65 + 0.35 * std::sin(phase / MEANDER_STRENGTH_SPAN + 1.3);
        double wave = 0.72 * std::sin(phase) + 0.28 * std::sin(phase / 0.47 + 0.9);
        double lambda = MEANDER_WAVELENGTH * 2.0 * r;
        double d = std::min(MEANDER_MAX_SWING, MEANDER_AMPLITUDE * lambda * strength)
                 * mthLerp(tp, double(bendFlat[c0]), double(bendFlat[c1])) * keep * wave;
        double bend = mthLerp(tq, double(bendK[c0]), double(bendK[c1]));
        if (d * bend > 0.0) d = dwSignum(d) * std::min(std::abs(d), MEANDER_BEND_SHARE / std::abs(bend));
        double level = mthLerp(tp, double(breached[c0]), double(breached[c1]));
        d += keep * 0.5 * (lacksBeside(x, z, -nx, -nz, r, level, c0, c1) - lacksBeside(x, z, nx, nz, r, level, c0, c1));
        if (std::abs(d) < 0.05) return 0.0;
        double edge = MEANDER_EDGE_SHARE * r;
        double s = dwSignum(d);
        double room = valleyRoom(x, z, s * nx, s * nz, level, std::abs(d) + edge, r, c0, c1) - edge;
        return s * std::max(0.0, std::min(std::min(std::abs(d), room), seamRoom(x, z, s * nx, s * nz, r)));
    }

    static double heading(int a, int b) {
        return std::atan2(double((b >> W_SHIFT) - (a >> W_SHIFT)), double((b & W_MASK) - (a & W_MASK))) * (180.0 / DW_PI);
    }

    static void turnIntoConfluence(std::vector<double>& pts, int m, int prev, int jc, int dn, int main) {
        double mainIn = heading(main, jc);
        double current = mainIn + 0.5 * dwWrapDegrees(heading(jc, dn) - mainIn);
        double off0 = dwWrapDegrees(heading(prev, jc) - mainIn);
        double turns = dwWrapDegrees(current - mainIn);
        double side = (std::abs(off0) > 179.0 && turns != 0.0) ? dwSignum(turns) : (off0 < 0.0 ? -1.0 : 1.0);
        double off = (side * off0 >= 0.0) ? side * off0 : side * off0 + 360.0;
        double along = side * dwWrapDegrees(current - mainIn);
        double lo = std::max(JUNCTION_APART, along - JUNCTION_Y), hi = along + JUNCTION_Y;
        double want = (lo <= hi) ? mthClamp(off, lo, hi) : 0.5 * (lo + hi);
        double turn = side * mthClamp(want - off, -JUNCTION_MAX_TURN, JUNCTION_MAX_TURN);
        if (turn == 0.0) return;
        double ox = pts[(m - 1) * 3], oz = pts[(m - 1) * 3 + 1];
        for (int j = 0; j < 3; ++j) {
            int p = m - 2 - j;
            if (p < 1) break;
            double a = (turn * JUNCTION_TURN[j]) * (DW_PI / 180.0), cs = std::cos(a), sn = std::sin(a);
            double dx = pts[p * 3] - ox, dz = pts[p * 3 + 1] - oz;
            pts[p * 3] = ox + dx * cs - dz * sn;
            pts[p * 3 + 1] = oz + dx * sn + dz * cs;
        }
    }

    float toldOver(int c, int d) const {
        int16_t x = ownSeams->exitOf[coreIndex(c)];
        if (x < 0) return std::numeric_limits<float>::quiet_NaN();
        const auto& e = ownSeams->exits[x];
        return (e.cellX == cx0 + (d & W_MASK) && e.cellZ == cz0 + (d >> W_SHIFT))
            ? e.level : std::numeric_limits<float>::quiet_NaN();
    }

    double coreEdgeDistance(double x, double z) const {
        double x0 = double((cx0 + HALO) << CS), x1 = double((cx0 + HALO + CORE) << CS);
        double z0 = double((cz0 + HALO) << CS), z1 = double((cz0 + HALO + CORE) << CS);
        return std::min(std::min(x - x0, x1 - x), std::min(z - z0, z1 - z));
    }

    bool lakeWaterAt(double x, double z, int lake) const {
        if (lake < int(packaged.size()) && packagedValid[lake]) return packaged[lake].inside(x, z);
        int ci = (mthFloor(x) >> CS) - cx0, cj = (mthFloor(z) >> CS) - cz0;
        if (ci < 0 || cj < 0 || ci >= W || cj >= W) return false;
        return lakeId[(cj << W_SHIFT) | ci] == lake;
    }

    void walkMouthIntoWater(std::vector<double>& pts, int mouthCell, int lake) const {
        int m = int(pts.size()) / 3;
        if (m < 2) return;
        int last = (m - 1) * 3;
        double x0 = pts[last], z0 = pts[last + 1];
        double dx = x0 - pts[last - 3], dz = z0 - pts[last - 2];
        double dl = std::hypot(dx, dz);
        if (dl < 1.0e-6) return;
        dx /= dl; dz /= dl;
        double keep = lake >= 0 ? cellWidth(mouthCell) + 2.0 : 0.0;
        double edge0 = coreEdgeDistance(x0, z0);
        double firstWet = std::numeric_limits<double>::quiet_NaN(), lastWet = std::numeric_limits<double>::quiet_NaN();
        for (int s = 0; s <= MOUTH_WALK_BLOCKS + 2 * MOUTH_OVERSHOOT_BLOCKS; ++s) {
            double x = x0 + dx * s, z = z0 + dz * s;
            if (s > 0 && keep > 0.0 && coreEdgeDistance(x, z) < std::min(keep, edge0)) break;
            bool w = (lake >= 0) ? lakeWaterAt(x, z, lake) : (heights.surfaceAt(mthFloor(x), mthFloor(z)) < seaLevel);
            if (w) {
                if (std::isnan(firstWet)) firstWet = double(s);
                lastWet = double(s);
                if (s - firstWet >= 2.0 * MOUTH_OVERSHOOT_BLOCKS) break;
            } else if (!std::isnan(firstWet) || s >= MOUTH_WALK_BLOCKS) {
                break;
            }
        }
        if (!std::isnan(firstWet)) {
            double in = firstWet + std::min(double(MOUTH_OVERSHOOT_BLOCKS), (lastWet - firstWet) / 2.0);
            pts[last] = x0 + dx * in;
            pts[last + 1] = z0 + dz * in;
        } else {
            pts[last] = double((cx0 + (mouthCell & W_MASK)) << CS);
            pts[last + 1] = double((cz0 + (mouthCell >> W_SHIFT)) << CS);
        }
    }

    int nodesInMain(const std::vector<double>& xs, const std::vector<double>& zs) const {
        int k = 0;
        while (k + 1 < int(xs.size())) {
            double x = xs[k + 1], z = zs[k + 1];
            MainRiverPoint p = nearestMainRiver(x, z);
            if (!p.valid) break;
            double r = p.waterRadius;
            if (std::hypot(x - p.x, z - p.z) + hydrology_const::getRiverJitter(x, z) * r / 10.0 >= r) break;
            k++;
        }
        return k;
    }

    bool gorgeAtMouth(const std::vector<int>& buf, int from, int to) const {
        for (int k = to; k >= std::max(from, to - MAIN_MOUTH_GORGE_CELLS); --k) {
            if (cut[buf[k]]) return true;
        }
        return false;
    }

    static void mouthRamp(std::vector<int>& ys, std::vector<uint8_t>& fixed, int inWater, int lip) {
        int n = int(ys.size()), river = ys[0];
        int edge = std::min(inWater, n - 1);
        while (edge + 1 < n && ys[edge + 1] <= river) edge++;
        if (edge + 1 >= n || ys[edge + 1] >= lip) return;
        for (int k = 1; k <= edge; ++k) if (!fixed[k]) ys[k] = river;
        for (int k = edge + 1; k < n && !fixed[k]; ++k) {
            int ct = MAIN_MOUTH_CUT - (k - edge - 1);
            int cap = std::max(river + (k - edge) * MAIN_MOUTH_RAMP, ys[k] - ct);
            if (ct <= 0 || ys[k] <= cap) break;
            ys[k] = cap;
        }
        for (int k = edge + 1; k <= std::min(n - 1, edge + MAIN_MOUTH_RAPIDS); ++k) fixed[k] = 1;
    }

    static int lineCell(int ci, int cj, int si, int sj, int s, int steps) {
        int i = ci + int(dwRound(double(si - ci) * double(s) / double(steps)));
        int j = cj + int(dwRound(double(sj - cj) * double(s) / double(steps)));
        return (j << W_SHIFT) | i;
    }

    bool byTheSea(int c) const {
        int ci = c & W_MASK, cj = c >> W_SHIFT;
        for (int dj = -SEA_SHORE_CELLS; dj <= SEA_SHORE_CELLS; ++dj) {
            for (int di = -SEA_SHORE_CELLS; di <= SEA_SHORE_CELLS; ++di) {
                int ni = ci + di, nj = cj + dj;
                if (ni < 0 || nj < 0 || ni >= W || nj >= W) continue;
                int nc = (nj << W_SHIFT) | ni;
                if (sink[nc] && !mainRiver[nc]) return true;
            }
        }
        return false;
    }

    int joinOnContact(std::vector<int>& buf, int len) const {
        if (ground0.empty()) return len;
        for (int i = std::max(0, len - 1 - JOIN_SCAN_CELLS); i < len - 2; ++i) {
            int c = buf[i];
            if (lakeId[c] >= 0) continue;
            double x = double((cx0 + (c & W_MASK)) << CS), z = double((cz0 + (c >> W_SHIFT)) << CS);
            MainRiverPoint p = nearestMainRiver(x, z);
            if (!p.valid) continue;
            double toWater = std::hypot(x - p.x, z - p.z) - p.waterRadius;
            if (toWater >= MAIN_JOIN_CONTACT * cellWidth(c) + MAIN_JOIN_CONTACT_MARGIN) continue;
            if (hydrology_const::gorgeShare(groundAt(ground0, p.x, p.z)) >= 0.5) return len;
            buf[i + 1] = buf[len - 1];
            return i + 2;
        }
        return len;
    }

    int seaOnContact(std::vector<int>& buf, int len) const {
        int ground = len;
        for (int k = 0; k < len; ++k) if (sink[buf[k]]) { ground = k; break; }
        for (int i = std::max(0, ground - 1 - JOIN_SCAN_CELLS); i < ground - 1; ++i) {
            int c = buf[i];
            if (lakeId[c] >= 0 || lakeShore[c] >= 0) continue;
            double contact = MAIN_JOIN_CONTACT * cellWidth(c) + MAIN_JOIN_CONTACT_MARGIN + 0.5 * CELL;
            int r = int(std::ceil(contact / double(CELL)));
            int ci = c & W_MASK, cj = c >> W_SHIFT;
            int sea = -1;
            double nearest = contact;
            for (int dj = -r; dj <= r; ++dj) {
                for (int di = -r; di <= r; ++di) {
                    int ni = ci + di, nj = cj + dj;
                    if (ni < 0 || nj < 0 || ni >= W || nj >= W || (di == 0 && dj == 0)) continue;
                    int nc = (nj << W_SHIFT) | ni;
                    if (mainRiver[nc] || (!sink[nc] && !(h[nc] < float(seaLevel) - 0.5f && byTheSea(nc)))) continue;
                    double d = std::hypot(double(di), double(dj)) * double(CELL);
                    if (d < nearest) { nearest = d; sea = nc; }
                }
            }
            if (sea < 0) continue;
            int si = sea & W_MASK, sj = sea >> W_SHIFT;
            int steps = std::max(std::abs(si - ci), std::abs(sj - cj));
            float top = breached[c] + SEA_JOIN_RISE;
            bool clear = true;
            for (int s = 1; s < steps && clear; ++s) {
                int lc = lineCell(ci, cj, si, sj, s, steps);
                clear = !sink[lc] && lakeId[lc] < 0 && h[lc] <= top;
            }
            if (!clear) continue;
            int n = i + 1;
            for (int s = 1; s <= steps; ++s) buf[n++] = lineCell(ci, cj, si, sj, s, steps);
            return extendInto(buf, n, buf[n - 2], buf[n - 1], -1);
        }
        return len;
    }

    void joinSquare(std::vector<double>& pts, int m, const MainRiverPoint& sinkPt) const {
        int last = (m - 1) * 3, bank = (m - 2) * 3;
        MainRiverPoint near = nearestMainRiver(pts[bank], pts[bank + 1]);
        const MainRiverPoint& at = near.valid ? near : sinkPt;
        double nx = pts[bank] - at.x, nz = pts[bank + 1] - at.z;
        double nl = std::hypot(nx, nz);
        if (nl < 1.0e-6) {
            pts[last] = at.x; pts[last + 1] = at.z;
            return;
        }
        nx /= nl; nz /= nl;
        double r = normalRiverWaterRadius(at.width);
        double in = std::min(MAIN_JOIN_IN * r, 0.5 * nl);
        pts.resize((m + 1) * 3);
        pts[last] = at.x + nx * in;
        pts[last + 1] = at.z + nz * in;
        pts[last + 2] = double(m - 1);
        pts[last + 3] = at.x;
        pts[last + 4] = at.z;
        pts[last + 5] = double(m - 1);
    }

    static double chainBlocks(const std::vector<int>& buf, int from, int to) {
        double blocks = 0.0;
        for (int k = from; k < to; ++k) {
            int a = buf[k], b = buf[k + 1];
            bool diagonal = (a & W_MASK) != (b & W_MASK) && (a >> W_SHIFT) != (b >> W_SHIFT);
            blocks += diagonal ? double(CELL) * 1.41421356 : double(CELL);
        }
        return blocks;
    }

    void emit(const std::vector<int>& buf, int from, int to, int ownerRegion,
              bool tapersAtSource, bool startsAtLake, bool opensAtConfluence,
              bool endIsMouth, bool hitMain, int inletLakeY, const MainRiverPoint& mainAt, double mouthOffset) {
        int m = to - from + 1;
        if (m < 2 || !isCoreRegion(ownerRegion)) return;
        std::vector<double> pts(m * 3);
        for (int k = 0; k < m; ++k) {
            int c = buf[from + k];
            pts[k * 3] = double((cx0 + (c & W_MASK)) << CS);
            pts[k * 3 + 1] = double((cz0 + (c >> W_SHIFT)) << CS);
            pts[k * 3 + 2] = double(k);
        }
        if (hitMain) {
            if (mainAt.valid) joinSquare(pts, m, mainAt);
        } else if (endIsMouth) {
            int lake = (inletLakeY == INT32_MIN) ? -1 : lakeId[buf[to]];
            walkMouthIntoWater(pts, buf[to], lake);
        } else if (upCount[buf[to]] >= 2) {
            int jc = buf[to], dn = downstream(jc), main = mainUp(jc);
            if (dn >= 0 && main >= 0 && main != buf[to - 1]) turnIntoConfluence(pts, m, buf[to - 1], jc, dn, main);
        }

        pts = splinePts(pts, SPLINE_STRIDE);
        std::vector<double> course = resamplePts(pts, NODE_SPACING);
        int nc = int(course.size()) / 3;
        if (nc < 2) return;

        double total = double(nc - 1) * NODE_SPACING;
        double mouthFade = std::min(MEANDER_MOUTH_FADE, 0.5 * total);
        double sourceFade = std::min(MEANDER_SOURCE_FADE, 0.5 * total);
        bool joins = !endIsMouth && upCount[buf[to]] >= 2;
        std::vector<double> sx(nc, 0.0), sz(nc, 0.0);
        for (int k = 0; k < nc; ++k) {
            double u = mthClamp(course[k * 3 + 2], 0.0, double(m - 1));
            int i0 = std::min(m - 2, int(u));
            double t = u - i0;
            double ends = (endIsMouth ? dwSmoothstep((total - k * NODE_SPACING) / mouthFade) : 1.0)
                        * (tapersAtSource ? dwSmoothstep(k * NODE_SPACING / sourceFade) : 1.0);
            double tj = (joins && i0 == m - 2) ? 0.0 : t;
            double d = sway(course[k * 3], course[k * 3 + 1], buf[from + i0], buf[from + i0 + 1], tj, tj, ends);
            sx[k] = d * swayNormal[0];
            sz[k] = d * swayNormal[1];
        }
        if (joins) {
            int jc = buf[to], on = downstream(jc);
            double d = (on >= 0)
                ? sway(course[(nc - 1) * 3], course[(nc - 1) * 3 + 1], jc, on, 0.0, 0.0, 1.0)
                : sway(course[(nc - 1) * 3], course[(nc - 1) * 3 + 1], buf[to - 1], buf[to], 1.0, 1.0, 1.0);
            double ex = d * swayNormal[0], ez = d * swayNormal[1];
            double gap = std::hypot(ex - sx[nc - 1], ez - sz[nc - 1]);
            double want = MEANDER_JOIN_BLEND + MEANDER_JOIN_PER_BLOCK * gap;
            bool whole = tapersAtSource && want > total;
            double blend = std::max(NODE_SPACING, std::min(want, total));
            for (int k = 0; k < nc; ++k) {
                double w = whole ? 1.0 : dwSmoothstep(1.0 - (total - k * NODE_SPACING) / blend);
                double mx = (ex - sx[k]) * w, mz = (ez - sz[k]) * w;
                double f = 1.0;
                double len = std::hypot(mx, mz);
                if (k < nc - 1 && len > 1.0) {
                    double u = mthClamp(course[k * 3 + 2], 0.0, double(m - 1));
                    int i0 = std::min(m - 2, int(u));
                    int a = buf[from + i0], b = buf[from + i0 + 1];
                    double level = mthLerp(u - i0, double(breached[a]), double(breached[b]));
                    double width = mthLerp(u - i0, cellWidth(a), cellWidth(b));
                    double px = course[k * 3] + sx[k], pz = course[k * 3 + 1] + sz[k];
                    for (double s = 2.0;; s += 2.0) {
                        double q = std::min(1.0, s / len);
                        if (!roomFor(px + mx * q, pz + mz * q, level, width)) { f = (s - 2.0) / len; break; }
                        if (q >= 1.0) break;
                    }
                }
                sx[k] += mx * f;
                sz[k] += mz * f;
            }
        }

        std::vector<double> radius(nc);
        for (int k = 0; k < nc; ++k) {
            double u = mthClamp(course[k * 3 + 2], 0.0, double(m - 1));
            int i0 = std::min(m - 2, int(u));
            radius[k] = cellWidth((u - i0 < 0.5) ? buf[from + i0] : buf[from + i0 + 1]);
        }
        for (int k = 1; k < nc - 1; ++k) {
            double here = std::hypot(sx[k], sz[k]);
            double next = std::max(std::hypot(sx[k - 1], sz[k - 1]), std::hypot(sx[k + 1], sz[k + 1]));
            if (here - next > MEANDER_SPIKE) {
                double f = next / here;
                sx[k] *= f; sz[k] *= f;
            }
        }
        untangle(course, sx, sz, radius, nc);
        std::vector<double> nodes = cutNecks(course, sx, sz, radius, nc);
        int n = int(nodes.size()) / 3;
        if (n < 2) return;
        std::vector<double> xs(n), zs(n), ws(n);
        for (int k = 0; k < n; ++k) { xs[k] = nodes[k * 3]; zs[k] = nodes[k * 3 + 1]; }
        std::vector<int> ys(n), tys(n);
        std::vector<uint8_t> fixed(n, 0), underNode(n, 0);
        float upWater = std::numeric_limits<float>::infinity();
        float overLevel = (!inCore(buf[to]) && inCore(buf[to - 1]))
            ? toldOver(buf[to - 1], buf[to]) : std::numeric_limits<float>::quiet_NaN();
        for (int k = 0; k < n; ++k) {
            double u = mthClamp(nodes[k * 3 + 2], 0.0, double(m - 1));
            int i0 = std::min(m - 2, int(u));
            double t = u - i0;
            int c0 = buf[from + i0], c1 = buf[from + i0 + 1];
            int cc = (t < 0.5) ? c0 : c1;
            double tw = (cut[c0] != cut[c1]) ? (t < 0.5 ? 0.0 : 1.0) : t;
            float b1 = (c1 == buf[to] && !std::isnan(overLevel)) ? std::max(breached[c1], overLevel) : breached[c1];
            float water = std::min(upWater, float(double(breached[c0]) * (1.0 - tw) + double(b1) * tw));
            float terrain = float(double(h[c0]) * (1.0 - tw) + double(h[c1]) * tw);
            upWater = water;
            double width = cellWidth(cc);
            bool inLake = lakeId[cc] >= 0 || lakeShore[cc] >= 0;
            bool lake0 = lakeId[c0] >= 0 || lakeShore[c0] >= 0, lake1 = lakeId[c1] >= 0 || lakeShore[c1] >= 0;
            double incision = mthLerp(tw, lake0 ? 0.0 : double(CHANNEL_INCISION), lake1 ? 0.0 : double(CHANNEL_INCISION));
            if (startsAtLake && k < LAKE_SILL_NODES) {
                incision = std::floor(incision * double(k) / double(LAKE_SILL_NODES));
            }
            fixed[n - 1 - k] = (inLake || (startsAtLake && k < LAKE_SILL_NODES)) ? 1 : 0;
            ys[k] = int(dwRound(double(water) - incision));
            if (!underCells.empty() && underCells[cc]) underNode[n - 1 - k] = 1;
            if (ys[k] < lakeFloorAt[cc]) {
                ys[k] = lakeFloorAt[cc];
                fixed[n - 1 - k] = 1;
            }
            if (ys[k] < seaLevel - 1) ys[k] = seaLevel - 1;
            tys[k] = dwRoundF(terrain);
            ws[k] = width;
        }
        std::reverse(xs.begin(), xs.end());
        std::reverse(zs.begin(), zs.end());
        std::reverse(ys.begin(), ys.end());
        std::reverse(tys.begin(), tys.end());
        std::reverse(ws.begin(), ws.end());
        if (endIsMouth) ys[0] = (inletLakeY != INT32_MIN) ? inletLakeY : (seaLevel - 1);
        for (int k = 1; k < n - 1; ++k) {
            if (tys[k] - tys[k - 1] >= WATERFALL_MIN_DROP) {
                ys[k] = std::max(ys[k], std::min(tys[k] - 1, ys[k + 1]));
            }
        }
        for (int k = 1; k < n; ++k) if (ys[k] < ys[k - 1]) ys[k] = ys[k - 1];
        collapseCliffs(ys, tys, fixed);
        if (hitMain) {
            mouthRamp(ys, fixed, nodesInMain(xs, zs),
                    gorgeAtMouth(buf, from, to) ? seaLevel + 1 + int(hydrology_const::LOWLAND_FALL_MIN) : INT32_MAX);
        }
        if (opensAtConfluence) {
            junctionHead(ys, tys, fixed, int(std::ceil((cellWidth(buf[from]) + JUNCTION_HEAD_MARGIN) / NODE_SPACING)));
        }
        int headCell = buf[from];
        std::vector<int> poolGround = tys;
        for (int k = 0; k < n; ++k) if (underNode[k]) poolGround[k] = ys[k];
        int64_t poolSeed = int64_t(uint64_t(seed)
            ^ (uint64_t(int64_t(cx0 + (headCell & W_MASK))) * 341873128712ull)
            ^ (uint64_t(int64_t(cz0 + (headCell >> W_SHIFT))) * 132897987541ull)
            ^ 0x5DEECE66Dull);
        stepPools(ys, poolGround, fixed, poolSeed, waterfalls, opensAtConfluence);

        std::vector<double> sm(n);
        for (int k = 0; k < n; ++k) {
            double a = ws[std::max(0, k - 1)], b = ws[k], c = ws[std::min(n - 1, k + 1)];
            sm[k] = (a + b + c) / 3.0;
        }
        for (int k = n - 2; k >= 0; --k) if (sm[k] < sm[k + 1]) sm[k] = sm[k + 1];
        if (tapersAtSource) {
            double taper = std::min(SOURCE_TAPER, 0.5 * double(n - 1) * NODE_SPACING);
            for (int k = n - 1; k >= 0; --k) {
                double s = double(n - 1 - k) * NODE_SPACING;
                if (s >= taper) break;
                sm[k] *= std::pow(s / taper, SOURCE_TIP_POWER);
            }
        }
        if (mainAt.valid) {
            double cap0 = MAIN_JOIN_WIDTH * normalRiverWaterRadius(mainAt.width);
            for (int k = 0; k < n; ++k) {
                double cap = cap0 * (1.0 + (mouthOffset + double(k) * NODE_SPACING) / MAIN_JOIN_TAPER);
                if (sm[k] > cap) sm[k] = cap;
            }
        }
        WatershedPath path;
        path.init(n, n, endIsMouth && hitMain, std::move(xs), std::move(zs), std::move(ys), std::move(tys), std::move(sm));
        tile.pathsByRegion[localRegionSlot(ownerRegion)].push_back(std::move(path));
    }

    bool startsReach(int s) const {
        if (!chan[s]) return false;
        return upCount[s] != 1 || upLake[s] || !sameRegionUp[s] || inflow[s];
    }

    void traceChannels() {
        prepareBends();
        for (int c = 0; c < N; ++c) {
            bool fromL = lakeId[c] >= 0;
            if (!chan[c] && !fromL) continue;
            int d = downstream(c);
            if (d < 0) continue;
            if (fromL) upLake[d] = 1;
            else {
                if (upCount[d] < 100) upCount[d]++;
                if (regionIndex(c) == regionIndex(d)) sameRegionUp[d] = 1;
            }
        }
        std::vector<int> buf(N + 4);
        for (int s = 0; s < N; ++s) {
            if (!startsReach(s)) continue;
            int ownerRegion = regionIndex(s);
            if (!isCoreRegion(ownerRegion)) continue;
            int len = upLake[s] ? leadFromLake(buf, s) : 0;
            int c = s;
            bool endIsMouth = false, hitMain = false;
            int inletLakeY = INT32_MIN;
            while (true) {
                buf[len++] = c;
                int n = downstream(c);
                if (n < 0) break;
                if (sink[n]) {
                    buf[len++] = n;
                    endIsMouth = true;
                    hitMain = mainRiver[n] != 0;
                    if (!hitMain) len = extendInto(buf, len, c, n, -1);
                    break;
                }
                if (lakeId[n] >= 0) {
                    int into = aimIntoLake(len >= 2 ? buf[len - 2] : -1, c, n);
                    buf[len++] = into;
                    endIsMouth = true;
                    inletLakeY = lakeWaterY[lakeId[n]];
                    len = extendInto(buf, len, c, into, lakeId[n]);
                    break;
                }
                if (!chan[n]) break;
                if (startsReach(n)) { buf[len++] = n; break; }
                c = n;
            }
            if (len < 2) continue;
            if (hitMain) len = joinOnContact(buf, len);
            else if (endIsMouth && inletLakeY == INT32_MIN) len = seaOnContact(buf, len);
            bool isTrueSource = (upCount[s] == 0 && !upLake[s] && !inflow[s]);
            MainRiverPoint mainAt;
            if (hitMain) {
                int sc = buf[len - 1];
                mainAt = nearestMainRiver(double((cx0 + (sc & W_MASK)) << CS), double((cz0 + (sc >> W_SHIFT)) << CS));
            }
            int from = 0;
            while (from < len - 1) {
                int to = std::min(len - 1, from + MAX_REACH_CELLS - 1);
                bool last = (to == len - 1);
                emit(buf, from, to, ownerRegion,
                        from == 0 && isTrueSource,
                        from == 0 && upLake[s] != 0,
                        from == 0 && !upLake[s] && upCount[s] >= 2,
                        last && endIsMouth,
                        last && hitMain,
                        last ? inletLakeY : INT32_MIN,
                        mainAt, !mainAt.valid ? 0.0 : chainBlocks(buf, to, len - 1));
                from = to;
            }
        }
    }

    static int64_t nodeKey(const WatershedPath& p, int i) {
        return int64_t((uint64_t(uint32_t(int32_t(dwRound(p.segmentsX[i])))) << 32)
                     ^ uint64_t(uint32_t(int32_t(dwRound(p.segmentsZ[i])))));
    }

    void lowerAlongside() {
        std::vector<WatershedPath*> all;
        for (auto& list : tile.pathsByRegion) {
            for (auto& p : list) all.push_back(&p);
        }
        int totalP = int(all.size());
        std::unordered_map<const WatershedPath*, int> ptrIdx;
        ptrIdx.reserve(totalP * 2 + 1);
        for (int i = 0; i < totalP; ++i) ptrIdx[all[i]] = i;

        std::unordered_map<int64_t, std::vector<int>> byMouth, byHead;
        for (int i = 0; i < totalP; ++i) {
            const auto* p = all[i];
            if (p->validNodeCount < 2) continue;
            byMouth[nodeKey(*p, 0)].push_back(i);
            byHead[nodeKey(*p, p->validNodeCount - 1)].push_back(i);
        }
        auto mainIntoIdx = [&](const std::vector<int>& in, int outIdx) -> int {
            const auto& out = *all[outIdx];
            int oh = out.validNodeCount - 1;
            double ox = out.segmentsX[oh - 1] - out.segmentsX[oh], oz = out.segmentsZ[oh - 1] - out.segmentsZ[oh];
            double ol = std::hypot(ox, oz);
            int best = -1;
            double bestWidth = -1.0, bestAlign = -2.0;
            for (int pidx : in) {
                const auto& p = *all[pidx];
                double width = 0.0;
                for (int i = std::min(2, p.validNodeCount - 1); i <= std::min(8, p.validNodeCount - 1); ++i) {
                    width = std::max(width, p.segmentWidths[i]);
                }
                double ix = p.segmentsX[0] - p.segmentsX[1], iz = p.segmentsZ[0] - p.segmentsZ[1];
                double il = std::hypot(ix, iz);
                double align = (il > 1.0e-6 && ol > 1.0e-6) ? (ix * ox + iz * oz) / (il * ol) : 0.0;
                if (width > bestWidth + 0.5 || (std::abs(width - bestWidth) <= 0.5 && align > bestAlign)) {
                    best = pidx; bestWidth = width; bestAlign = align;
                }
            }
            return best;
        };
        auto mouthInWater = [&](int tidx) -> bool {
            const auto& t = *all[tidx];
            double x = t.segmentsX[0], z = t.segmentsZ[0];
            for (int mIdx = 0; mIdx < totalP; ++mIdx) {
                if (mIdx == tidx) continue;
                const auto& m = *all[mIdx];
                if (x < m.minX - MAX_WIDTH || x > m.maxX + MAX_WIDTH || z < m.minZ - MAX_WIDTH || z > m.maxZ + MAX_WIDTH) continue;
                auto d = m.getClosestNodeData(x, z);
                if (d.distance < d.width && d.y <= t.segmentsY[0]) return true;
            }
            return false;
        };

        std::vector<int> work;
        std::vector<int> startAt(totalP, INT32_MAX);
        std::vector<int> doneAt(totalP, INT32_MAX);
        std::vector<std::vector<int>> lowered(totalP);
        std::vector<uint8_t> hasLowered(totalP, 0), hasDone(totalP, 0);
        for (int ti = 0; ti < totalP; ++ti) {
            const auto& t = *all[ti];
            if (t.validNodeCount < 2) continue;
            int64_t mouth = nodeKey(t, 0);
            auto hit = byHead.find(mouth);
            if (hit != byHead.end()) {
                const auto& in = byMouth[mouth];
                if (in.size() < 2 || mainIntoIdx(in, hit->second[0]) == ti) continue;
            } else if (!mouthInWater(ti)) {
                continue;
            }
            startAt[ti] = t.segmentsY[0];
            work.push_back(ti);
        }
        size_t head = 0;
        while (head < work.size()) {
            int ti = work[head++];
            const auto& t = *all[ti];
            int start = startAt[ti];
            if (hasDone[ti] && doneAt[ti] <= start) continue;
            hasDone[ti] = 1;
            doneAt[ti] = start;
            int n = t.validNodeCount;
            std::vector<int> ys = t.segmentsY;
            ys[0] = std::min(ys[0], start);
            int k = 1;
            for (; k < n; ++k) {
                double x = t.segmentsX[k], z = t.segmentsZ[k], w = t.segmentWidths[k];
                double reach = w + MAX_WIDTH + ALONGSIDE_GAP;
                int lowest = INT32_MAX;
                for (int mi = 0; mi < totalP; ++mi) {
                    if (mi == ti) continue;
                    const auto& m = *all[mi];
                    if (x < m.minX - reach || x > m.maxX + reach || z < m.minZ - reach || z > m.maxZ + reach) continue;
                    auto d = m.getClosestNodeData(x, z);
                    if (d.distance >= w + d.width + ALONGSIDE_GAP) continue;
                    int y = hasLowered[mi]
                        ? std::min(lowered[mi][d.nodeIndex], lowered[mi][std::min(d.nodeIndex + 1, m.validNodeCount - 1)])
                        : d.y;
                    if (y <= t.segmentsY[k]) lowest = std::min(lowest, y);
                }
                if (lowest == INT32_MAX || t.segmentsY[k] - lowest > ALONGSIDE_DROP) break;
                ys[k] = std::min(ys[k], std::max(lowest, ys[k - 1]));
            }
            lowered[ti] = std::move(ys);
            hasLowered[ti] = 1;
            if (k < n) continue;
            auto upIt = byMouth.find(nodeKey(t, n - 1));
            if (upIt == byMouth.end()) continue;
            for (int ui : upIt->second) {
                if (ui == ti) continue;
                startAt[ui] = std::min(startAt[ui], lowered[ti][n - 1]);
                work.push_back(ui);
            }
        }
        for (int ti = 0; ti < totalP; ++ti) {
            if (!hasLowered[ti] || lowered[ti] == all[ti]->segmentsY) continue;
            auto& p = *all[ti];
            p.init(p.nodeCount, p.validNodeCount, p.hitMainRiver,
                   std::move(p.segmentsX), std::move(p.segmentsZ),
                   std::move(lowered[ti]), std::move(p.segmentsTerrainY), std::move(p.segmentWidths));
        }
    }

    void routeAndSeams() {
        route();
        ownSeams = seams();
    }

    void finishAfterRoute() {
        importSeams();
        fuseWide(true);
        for (int pass = 0;; ++pass) {
            breach();
            computeChannels();
            for (int round = 0; round < LAKE_UNDERCUT_ROUNDS && lowerUndercutLakes(); ++round) {
                breach();
                computeChannels();
            }
            if (pass == SEAM_BOUNCE_PASSES || !bounce()) break;
            accumulateCore();
        }
        deadEndLakes();
        if (seamsImported && !deadEndLakeIdSet.empty()) accumulateCore();
        widenAsTold();
        carryWidths();
        if (lakesEnabled) packageLakes();
        lakeFloors();
        goUnderground();
        traceChannels();
        lowerAlongside();
    }

    void run() {
        routeAndSeams();
        finishAfterRoute();
    }
};

} // namespace

// ---------- BasinTileEngine methods ----------
namespace {
std::atomic<uint64_t> gBasinEpoch{1};

template <typename Fn>
struct ScopeExit {
    Fn fn;
    ~ScopeExit() { fn(); }
};
}

void BasinTileEngine::reset(int64_t worldSeed, const DynamicWatersConfig& cfg, const WorldNoises* noises,
                            MainRiversEngine* mainRivers, BasinCallbacks cb) {
    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        seamCache_.clear();
        regionCache_.clear();
        buildingSeams_.clear();
        buildingTiles_.clear();
        nextLakeId_ = 1;
        epoch_.store(gBasinEpoch.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_release);
    }
    seed_ = worldSeed;
    cfg_ = cfg;
    noises_ = noises;
    mainRivers_ = mainRivers;
    cb_ = std::move(cb);
    heights_.reset(cb_.sampleCoarseCell, cb_.sampleSurfaceAt, cb_.seaLevel);
}

void BasinTileEngine::setCallbacks(BasinCallbacks cb) {
    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        cb_ = std::move(cb);
    }
    heights_.setCallbacks(cb_.sampleCoarseCell, cb_.sampleSurfaceAt, cb_.seaLevel);
}

std::shared_ptr<const BasinTileEngine::TileSeams> BasinTileEngine::getSeams(int tx, int tz) {
    int64_t key = packKey(tx, tz);
    {
        std::unique_lock<std::mutex> lk(cacheMutex_);
        for (;;) {
            auto it = seamCache_.find(key);
            if (it != seamCache_.end()) return it->second;
            if (buildingSeams_.find(key) == buildingSeams_.end()) {
                buildingSeams_.insert(key);
                break;
            }
            cacheCv_.wait(lk);
        }
    }
    bool committed = false;
    ScopeExit guard{[&]() {
        if (!committed) {
            std::lock_guard<std::mutex> lk(cacheMutex_);
            buildingSeams_.erase(key);
            cacheCv_.notify_all();
        }
    }};

    int thresh = channelThreshold(cfg_.mountainRiverDensity);
    BasinBuilder b(tx, tz, cfg_, *noises_, mainRivers_, heights_, cb_,
                   nullptr, nullptr, seed_, cb_.seaLevel, thresh, cfg_.enableMountainLakes, true);
    b.route();
    auto s = b.seams();

    std::lock_guard<std::mutex> lk(cacheMutex_);
    auto [it, ins] = seamCache_.try_emplace(key, s);
    buildingSeams_.erase(key);
    committed = true;
    cacheCv_.notify_all();
    return it->second;
}

void BasinTileEngine::ensureTileBuilt(int tx, int tz) {
    const int64_t tileKey = packKey(tx, tz);
    const int64_t rkey0 = packKey(tx * BasinTileData::REGIONS_PER_SIDE, tz * BasinTileData::REGIONS_PER_SIDE);
    {
        std::unique_lock<std::mutex> lk(cacheMutex_);
        for (;;) {
            if (regionCache_.find(rkey0) != regionCache_.end()) return;
            if (buildingTiles_.find(tileKey) == buildingTiles_.end()) {
                buildingTiles_.insert(tileKey);
                break;
            }
            cacheCv_.wait(lk);
        }
    }
    bool committed = false;
    ScopeExit guard{[&]() {
        if (!committed) {
            std::lock_guard<std::mutex> lk(cacheMutex_);
            buildingTiles_.erase(tileKey);
            cacheCv_.notify_all();
        }
    }};

    int thresh = channelThreshold(cfg_.mountainRiverDensity);
    auto getSeamsFn = [this](int ntx, int ntz) { return this->getSeams(ntx, ntz); };
    auto allocIdFn = [this]() -> uint64_t {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        return nextLakeId_++;
    };
    BasinBuilder b(tx, tz, cfg_, *noises_, mainRivers_, heights_, cb_,
                   getSeamsFn, allocIdFn, seed_, cb_.seaLevel, thresh, cfg_.enableMountainLakes, false);
    b.run();
    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        seamCache_.try_emplace(tileKey, b.ownSeams);
        for (int rj = 0; rj < BasinTileData::REGIONS_PER_SIDE; ++rj) {
            for (int ri = 0; ri < BasinTileData::REGIONS_PER_SIDE; ++ri) {
                int slot = rj * BasinTileData::REGIONS_PER_SIDE + ri;
                int rx = tx * BasinTileData::REGIONS_PER_SIDE + ri;
                int rz = tz * BasinTileData::REGIONS_PER_SIDE + rj;
                int64_t rkey = packKey(rx, rz);
                if (regionCache_.find(rkey) != regionCache_.end()) continue;
                auto grid = std::make_shared<RegionSpatialGrid>();
                grid->build(rx, rz, std::move(b.tile.pathsByRegion[slot]), std::move(b.tile.lakesByRegion[slot]));
                regionCache_.try_emplace(rkey, std::move(grid));
            }
        }
        epoch_.store(gBasinEpoch.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_release);
        buildingTiles_.erase(tileKey);
        committed = true;
        cacheCv_.notify_all();
    }
}

const BasinTileEngine::RegionNeighborhood3x3& BasinTileEngine::getNeighborhood3x3(int homeRx, int homeRz) {
    static const RegionNeighborhood3x3 emptyNb{};
    if (!cfg_.enabled || !cfg_.enableWatersheds || !noises_) return emptyNb;

    const uint64_t curEpoch = epoch_.load(std::memory_order_acquire);
    thread_local std::array<RegionNeighborhood3x3, 16> tls{};
    const size_t slotIdx = (uint64_t(uint32_t(homeRx)) ^ (uint64_t(uint32_t(homeRz)) * 0x9E3779B97F4A7C15ull)) & 15u;
    auto& slot = tls[slotIdx];
    if (slot.owner == this && slot.epoch == curEpoch && slot.homeRx == homeRx && slot.homeRz == homeRz) {
        return slot;
    }

    slot.anyPaths = false;
    slot.anyLakes = false;
    slot.pathsMinX = 1.0e30; slot.pathsMaxX = -1.0e30; slot.pathsMinZ = 1.0e30; slot.pathsMaxZ = -1.0e30;
    slot.lakesMinX = 1.0e30; slot.lakesMaxX = -1.0e30; slot.lakesMinZ = 1.0e30; slot.lakesMaxZ = -1.0e30;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const int idx = (dx + 1) * 3 + (dz + 1);
            auto ref = getRegion(homeRx + dx, homeRz + dz);
            const RegionSpatialGrid* g = ref.get();
            slot.refs[idx] = std::move(ref);
            slot.grids[idx] = g;
            if (g) {
                if (!g->paths.empty()) {
                    slot.anyPaths = true;
                    slot.pathsMinX = std::min(slot.pathsMinX, g->pathsMinX);
                    slot.pathsMaxX = std::max(slot.pathsMaxX, g->pathsMaxX);
                    slot.pathsMinZ = std::min(slot.pathsMinZ, g->pathsMinZ);
                    slot.pathsMaxZ = std::max(slot.pathsMaxZ, g->pathsMaxZ);
                }
                if (!g->lakes.empty()) {
                    slot.anyLakes = true;
                    slot.lakesMinX = std::min(slot.lakesMinX, g->lakesMinX);
                    slot.lakesMaxX = std::max(slot.lakesMaxX, g->lakesMaxX);
                    slot.lakesMinZ = std::min(slot.lakesMinZ, g->lakesMinZ);
                    slot.lakesMaxZ = std::max(slot.lakesMaxZ, g->lakesMaxZ);
                }
            }
        }
    }
    slot.owner = this;
    slot.epoch = epoch_.load(std::memory_order_acquire);
    slot.homeRx = homeRx;
    slot.homeRz = homeRz;
    return slot;
}

std::shared_ptr<const RegionSpatialGrid> BasinTileEngine::getRegion(int rx, int rz) {
    if (!cfg_.enabled || !cfg_.enableWatersheds || !noises_) {
        static const std::shared_ptr<const RegionSpatialGrid> empty = std::make_shared<RegionSpatialGrid>();
        return empty;
    }
    const uint64_t curEpoch = epoch_.load(std::memory_order_acquire);
    const int64_t key = packKey(rx, rz);
    struct TlsSlot {
        const BasinTileEngine* owner = nullptr;
        uint64_t epoch = 0;
        int64_t key = 0;
        std::shared_ptr<const RegionSpatialGrid> grid;
    };
    thread_local std::array<TlsSlot, 64> tls{};
    const size_t slotIdx = (uint64_t(uint32_t(rx)) ^ (uint64_t(uint32_t(rz)) * 0x9E3779B97F4A7C15ull)) & 63u;
    auto& slot = tls[slotIdx];
    if (slot.owner == this && slot.epoch == curEpoch && slot.key == key && slot.grid) {
        return slot.grid;
    }

    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        auto it = regionCache_.find(key);
        if (it != regionCache_.end()) {
            slot.owner = this;
            slot.epoch = curEpoch;
            slot.key = key;
            slot.grid = it->second;
            return it->second;
        }
    }
    ensureTileBuilt(tileCoord(rx), tileCoord(rz));
    std::shared_ptr<const RegionSpatialGrid> found;
    const uint64_t newEpoch = epoch_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        auto it = regionCache_.find(key);
        if (it != regionCache_.end()) found = it->second;
    }
    if (!found) {
        static const std::shared_ptr<const RegionSpatialGrid> empty = std::make_shared<RegionSpatialGrid>();
        return empty;
    }
    slot.owner = this;
    slot.epoch = newEpoch;
    slot.key = key;
    slot.grid = found;
    return found;
}

void BasinTileEngine::prewarm(int minBlockX, int minBlockZ, int maxBlockX, int maxBlockZ) {
    if (!cfg_.enabled || !cfg_.enableWatersheds || !noises_) return;
    const int minRx = floorDiv(minBlockX, hydrology_const::WATERSHED_REGION_SIZE) - 1;
    const int maxRx = floorDiv(maxBlockX, hydrology_const::WATERSHED_REGION_SIZE) + 1;
    const int minRz = floorDiv(minBlockZ, hydrology_const::WATERSHED_REGION_SIZE) - 1;
    const int maxRz = floorDiv(maxBlockZ, hydrology_const::WATERSHED_REGION_SIZE) + 1;
    const int minTx = tileCoord(minRx), maxTx = tileCoord(maxRx);
    const int minTz = tileCoord(minRz), maxTz = tileCoord(maxRz);

    std::vector<std::pair<int, int>> missingTiles;
    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        for (int tz = minTz; tz <= maxTz; ++tz) {
            for (int tx = minTx; tx <= maxTx; ++tx) {
                int64_t rkey0 = packKey(tx * BasinTileData::REGIONS_PER_SIDE, tz * BasinTileData::REGIONS_PER_SIDE);
                if (regionCache_.find(rkey0) == regionCache_.end()) {
                    missingTiles.emplace_back(tx, tz);
                }
            }
        }
    }
    if (missingTiles.empty()) return;

    const unsigned nThreads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    const int thresh = channelThreshold(cfg_.mountainRiverDensity);
    auto getSeamsFn = [this](int ntx, int ntz) { return this->getSeams(ntx, ntz); };
    auto allocIdFn = [this]() -> uint64_t {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        return nextLakeId_++;
    };

    std::vector<std::unique_ptr<BasinBuilder>> builders(missingTiles.size());
    // Phase 1: route + compute seams once per missing tile, and cache seams.
    {
        std::atomic<size_t> next{0};
        const unsigned workersCount = std::min<unsigned>(nThreads, unsigned(missingTiles.size()));
        std::vector<std::thread> workers;
        workers.reserve(workersCount);
        for (unsigned t = 0; t < workersCount; ++t) {
            workers.emplace_back([&]() {
                for (;;) {
                    size_t i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= missingTiles.size()) break;
                    const int tx = missingTiles[i].first;
                    const int tz = missingTiles[i].second;
                    auto b = std::make_unique<BasinBuilder>(
                        tx, tz, cfg_, *noises_, mainRivers_, heights_, cb_,
                        getSeamsFn, allocIdFn, seed_, cb_.seaLevel, thresh, cfg_.enableMountainLakes, false);
                    b->routeAndSeams();
                    {
                        std::lock_guard<std::mutex> lk(cacheMutex_);
                        seamCache_.try_emplace(packKey(tx, tz), b->ownSeams);
                    }
                    builders[i] = std::move(b);
                }
            });
        }
        for (auto& w : workers) w.join();
    }

    // Phase 2: finish routing/channel/lake packaging using the same BasinBuilder instances (outer seams pulled lazily only when taken).
    {
        std::atomic<size_t> next{0};
        const unsigned workersCount = std::min<unsigned>(nThreads, unsigned(missingTiles.size()));
        std::vector<std::thread> workers;
        workers.reserve(workersCount);
        for (unsigned t = 0; t < workersCount; ++t) {
            workers.emplace_back([&]() {
                for (;;) {
                    size_t i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= missingTiles.size()) break;
                    const int tx = missingTiles[i].first;
                    const int tz = missingTiles[i].second;
                    auto b = std::move(builders[i]);
                    b->finishAfterRoute();
                    std::lock_guard<std::mutex> lk(cacheMutex_);
                    for (int rj = 0; rj < BasinTileData::REGIONS_PER_SIDE; ++rj) {
                        for (int ri = 0; ri < BasinTileData::REGIONS_PER_SIDE; ++ri) {
                            int slot = rj * BasinTileData::REGIONS_PER_SIDE + ri;
                            int rx = tx * BasinTileData::REGIONS_PER_SIDE + ri;
                            int rz = tz * BasinTileData::REGIONS_PER_SIDE + rj;
                            int64_t rkey = packKey(rx, rz);
                            if (regionCache_.find(rkey) != regionCache_.end()) continue;
                            auto grid = std::make_shared<RegionSpatialGrid>();
                            grid->build(rx, rz, std::move(b->tile.pathsByRegion[slot]), std::move(b->tile.lakesByRegion[slot]));
                            regionCache_.try_emplace(rkey, std::move(grid));
                        }
                    }
                }
            });
        }
        for (auto& w : workers) w.join();
    }

    std::lock_guard<std::mutex> lk(cacheMutex_);
    epoch_.store(gBasinEpoch.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_release);
}

} // namespace dw
