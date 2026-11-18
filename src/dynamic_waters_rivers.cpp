#include "dynamic_waters_rivers.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <limits>
#include <thread>

namespace dw {

namespace {

constexpr double DW_PI = 3.14159265358979323846;
constexpr float DW_PI_F = 3.1415927f;

inline int64_t javaRound(double v) { return int64_t(std::floor(v + 0.5)); }
inline int32_t javaRoundF(float v) { return int32_t(std::floor(v + 0.5f)); }

inline float mthSin(float v) {
    static const std::array<float, 65536> tab = []() {
        std::array<float, 65536> a{};
        for (int i = 0; i < 65536; ++i) a[i] = float(std::sin(double(i) * DW_PI * 2.0 / 65536.0));
        return a;
    }();
    return tab[uint32_t(int32_t(v * 10430.378f)) & 65535u];
}

inline float mthCos(float v) {
    static const std::array<float, 65536> tab = []() {
        std::array<float, 65536> a{};
        for (int i = 0; i < 65536; ++i) a[i] = float(std::sin(double(i) * DW_PI * 2.0 / 65536.0));
        return a;
    }();
    return tab[uint32_t(int32_t(v * 10430.378f + 16384.0f)) & 65535u];
}

// ---------- RiverPathFractal helpers ----------
constexpr double JITTER_MAX = double(0.2f);
constexpr double JITTER_MIN = double(0.05f);
constexpr int MAX_BISECTIONS = 10;

const std::array<double, MAX_BISECTIONS>& encompassingRanges() {
    static const std::array<double, MAX_BISECTIONS> r = []() {
        std::array<double, MAX_BISECTIONS> arr{};
        double sqrt2 = double(std::sqrt(2.0));
        double delta = 0.0, midpointDelta = 0.0, prevMidpoint = 0.0, norm = 1.0;
        for (int i = 0; i < MAX_BISECTIONS; ++i) {
            arr[i] = std::max(delta, midpointDelta);
            prevMidpoint = midpointDelta;
            midpointDelta = double(0.5f) * (delta + midpointDelta) + sqrt2 * JITTER_MAX * norm;
            delta = std::max(prevMidpoint, delta);
            norm *= double(0.5f) + JITTER_MAX;
        }
        return arr;
    }();
    return r;
}

inline double randomJitter(mc::Xoroshiro& random) {
    double value = double(2.0f) * random.nextDouble() - 1.0;
    return (JITTER_MAX - JITTER_MIN) * value + (value < 0.0 ? -JITTER_MIN : JITTER_MIN);
}

std::vector<double> bisectPath(mc::Xoroshiro& random, int bisections, std::vector<double> segments) {
    for (int i = 0; i < bisections; ++i) {
        std::vector<double> split((segments.size() << 1) - 2);
        split[0] = segments[0];
        split[1] = segments[1];
        size_t si = 2;
        for (size_t idx = 0; idx + 2 < segments.size(); idx += 2) {
            double sx = segments[idx], sy = segments[idx + 1];
            double dx = segments[idx + 2], dy = segments[idx + 3];
            double n = normInf(sx - dx, sy - dy);
            double bx = randomJitter(random) * n + (sx + dx) * double(0.5f);
            double by = randomJitter(random) * n + (sy + dy) * double(0.5f);
            split[si] = bx;
            split[si + 1] = by;
            split[si + 2] = dx;
            split[si + 3] = dy;
            si += 4;
        }
        segments = std::move(split);
    }
    return segments;
}

// ---------- RiverInstance geometry & builders ----------
constexpr double MIN_BRANCH_ANGLE = double(0.25f);
constexpr int MIN_BRANCH_DISTANCE = 2;
constexpr int MIN_RIVER_EDGE_COUNT = 6;
constexpr float RIVER_STEP_LENGTH = 180.0f;
constexpr int RIVER_DEPTH = 20;
constexpr float RIVER_FEATHER = 90.0f;
constexpr int SAMPLE_STEP = 32;
constexpr int HALO_BLOCKS = 8 * SAMPLE_STEP;
constexpr double NEIGHBOUR_ROOM = 3.0;

struct InstanceEdge {
    RiverVertex source;
    RiverVertex drain;
};

int orientation(const RiverVertex& p, const RiverVertex& q, const RiverVertex& r) {
    double value = (q.y - p.y) * (r.x - q.x) - (q.x - p.x) * (r.y - q.y);
    if (value == 0.0) return 0;
    return value > 0.0 ? 1 : 2;
}

bool intersectCollinear(const RiverVertex& p, const RiverVertex& q, const RiverVertex& r) {
    return q.x <= std::max(p.x, r.x) && q.x >= std::min(p.x, r.x)
        && q.y <= std::max(p.y, r.y) && q.y >= std::min(p.y, r.y);
}

bool intersectSegments(const RiverVertex& p1, const RiverVertex& q1, const RiverVertex& p2, const RiverVertex& q2) {
    int o1 = orientation(p1, q1, p2);
    int o2 = orientation(p1, q1, q2);
    int o3 = orientation(p2, q2, p1);
    int o4 = orientation(p2, q2, q1);
    return (o1 != o2 && o3 != o4)
        || (o1 == 0 && intersectCollinear(p1, p2, q1))
        || (o2 == 0 && intersectCollinear(p1, q2, q1))
        || (o3 == 0 && intersectCollinear(p2, p1, q2))
        || (o4 == 0 && intersectCollinear(p2, q1, q2));
}

struct RiverBuilder {
    static int allocUid() {
        static std::atomic<int> gUid{1};
        return gUid.fetch_add(1, std::memory_order_relaxed);
    }

    std::deque<InstanceEdge> branchQueue;
    mc::Xoroshiro random;
    std::vector<InstanceEdge> edges;
    RiverVertex root;
    std::vector<InstanceEdge> branch;
    int depth;
    double featherSq;

    RiverBuilder(mc::Xoroshiro rnd, double drainX, double drainY, double angle, double length, int dep, double feather)
        : random(rnd), depth(dep), featherSq(feather * feather) {
        root = {drainX, drainY, angle, length, 0, allocUid()};
    }

    int score() const { return -int(edges.size()) - 10 * int(branchQueue.size()); }

    RiverVertex computeNext(const RiverVertex& prev, double length, int distance) {
        double nextAngle = prev.angle;
        if (distance != 0) {
            double mag = random.nextDouble() * double(0.5f) + double(0.2f);
            double sgn = random.nextBoolean() ? 1.0 : -1.0;
            nextAngle += mag * sgn;
        }
        double nextLength = length * (random.nextDouble() * double(0.08f) + double(0.92f));
        double dx = mthCos(float(nextAngle)) * nextLength;
        double dy = mthSin(float(nextAngle)) * nextLength;
        return {prev.x + dx, prev.y + dy, nextAngle, nextLength, distance + 1, allocUid()};
    }

    bool intersectAny(const InstanceEdge& edge) const {
        for (const auto& e : edges) {
            if (e.source.uid != edge.drain.uid && e.drain.uid != edge.drain.uid) {
                double d = distancePointToLineSq(e.source.x, e.source.y, e.drain.x, e.drain.y, edge.source.x, edge.source.y);
                if (d < featherSq || intersectSegments(e.source, e.drain, edge.source, edge.drain))
                    return true;
            }
        }
        return false;
    }

    void pruneRiverIfTooShort() {
        if (int(edges.size()) < MIN_RIVER_EDGE_COUNT) {
            edges.clear();
            branchQueue.clear();
        }
    }

    template<class Ctx>
    bool buildInitialBranch(Ctx& ctx) {
        RiverVertex prev = root;
        int len = depth + random.nextInt(1 + int(depth * 0.3f));
        for (int i = 0; i < len; ++i) {
            RiverVertex next = computeNext(prev, prev.length, prev.distance);
            InstanceEdge nextEdge{next, prev};
            if (ctx.intersectAny(nextEdge)) {
                pruneRiverIfTooShort();
                return true;
            }
            edges.push_back(nextEdge);
            prev = next;
            if (prev.distance < depth && prev.distance >= MIN_BRANCH_DISTANCE)
                branchQueue.push_back(nextEdge);
        }
        return false;
    }

    template<class Ctx>
    bool buildBranch(Ctx& ctx) {
        if (branchQueue.empty()) {
            pruneRiverIfTooShort();
            return true;
        }
        InstanceEdge prevEdge = branchQueue.front();
        branchQueue.pop_front();
        RiverVertex prev = prevEdge.drain;
        int prevDist = prev.distance + random.nextInt(3);
        RiverVertex branchNext = computeNext(prev, prev.length, prevDist);
        double deltaAngle = std::abs(prevEdge.source.angle - branchNext.angle);
        if (deltaAngle < MIN_BRANCH_ANGLE || (2.0 * DW_PI - deltaAngle < MIN_BRANCH_ANGLE))
            return false;

        branch.clear();
        InstanceEdge first{branchNext, prev};
        if (ctx.intersectAny(first)) return false;
        branch.push_back(first);

        prev = branchNext;
        for (int i = 0; i < depth - prev.distance + random.nextInt(1 + int(depth * 0.3f)); ++i) {
            RiverVertex next = computeNext(prev, prev.length, prevDist);
            InstanceEdge nextEdge{next, prev};
            if (ctx.intersectAny(nextEdge)) break;
            branch.push_back(nextEdge);
            prev = next;
            prevDist = next.distance;
            if (prevDist < depth && prevDist >= MIN_BRANCH_DISTANCE)
                branchQueue.push_back(nextEdge);
        }
        edges.insert(edges.end(), branch.begin(), branch.end());
        return false;
    }
};

struct RegionRiverContext {
    std::vector<RiverBuilder> builders;
    std::function<bool(const RiverVertex&, const RiverVertex&)> legalFn;

    bool intersectAny(const InstanceEdge& edge) const {
        if (!legalFn(edge.drain, edge.source)) return true;
        for (const auto& b : builders) {
            if (b.intersectAny(edge)) return true;
        }
        return false;
    }

    void runBuild() {
        // Exact OpenJDK PriorityQueue binary min-heap behavior
        std::vector<int> heap;
        auto push = [&](int bi) {
            heap.push_back(bi);
            int k = int(heap.size()) - 1;
            while (k > 0) {
                int p = (k - 1) >> 1;
                if (builders[heap[k]].score() >= builders[heap[p]].score()) break;
                std::swap(heap[k], heap[p]);
                k = p;
            }
        };
        auto pop = [&]() -> int {
            int res = heap[0];
            int last = heap.back();
            heap.pop_back();
            if (!heap.empty()) {
                int n = int(heap.size());
                int half = n >> 1;
                int k = 0;
                heap[0] = last;
                while (k < half) {
                    int c = (k << 1) + 1;
                    int r = c + 1;
                    if (r < n && builders[heap[r]].score() < builders[heap[c]].score()) c = r;
                    if (builders[last].score() <= builders[heap[c]].score()) break;
                    heap[k] = heap[c];
                    k = c;
                }
                heap[k] = last;
            }
            return res;
        };

        for (int i = 0; i < int(builders.size()); ++i) {
            if (builders[i].buildInitialBranch(*this))
                push(i);
        }
        while (!heap.empty()) {
            int bi = pop();
            if (!builders[bi].buildBranch(*this))
                push(bi);
        }
    }
};

// ---------- RiverNetworkBuilder helpers ----------
inline int drainSpacing(DensityLevel l) {
    switch (l) {
        case DensityLevel::VERY_LOW: return 2048;
        case DensityLevel::LOW: return 1024;
        case DensityLevel::MEDIUM:
        case DensityLevel::HIGH: return 704;
        case DensityLevel::ULTRA: return 512;
        case DensityLevel::EXTREME: return 410;
        case DensityLevel::INSANE: return 256;
    }
    return 704;
}

inline float drainKeep(DensityLevel l) {
    return l == DensityLevel::MEDIUM ? 0.65f : 1.0f;
}

inline float unitHash(int64_t seed, int32_t x, int32_t z) {
    uint64_t u = uint64_t(seed) ^ (uint64_t(int64_t(x)) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(int64_t(z)) * 0xC2B2AE3D27D4EB4Full);
    int64_t h = hashCommonMix(int64_t(u));
    return float(uint64_t(h) >> 40) * 0x1.0p-24f;
}

float findBestStartingAngle(const MainRiversEngine::RawRegion& field, mc::Xoroshiro& random, int x, int z) {
    float bestMetric = std::numeric_limits<float>::min(); // Float.MIN_VALUE in Java (>0!) Wait check RiverNetworkBuilder.java line 488!
    // Java Float.MIN_VALUE == 1.4E-45f! Match literally!
    int bestCount = 0;
    float bestAngle = std::numeric_limits<float>::quiet_NaN();

    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue;
            int nx = x + dx * SAMPLE_STEP, nz = z + dz * SAMPLE_STEP;
            if (field.isLand(nx, nz)) {
                float metric = float(field.distToOcean(nx, nz) - std::abs(dx) - std::abs(dz));
                if (metric > bestMetric || (metric == bestMetric && random.nextInt(1 + bestCount) == 0)) {
                    if (metric > bestMetric) {
                        bestMetric = metric;
                        bestCount = 0;
                    }
                    bestCount += 1;
                    bestAngle = float(std::atan2(double(dz), double(dx)));
                }
            }
        }
    }
    if (!std::isnan(bestAngle)) {
        bestAngle += random.nextFloat() * 0.2f - 0.1f;
    } else {
        bestAngle = random.nextFloat() * float(DW_PI) * 2.0f;
    }
    return bestAngle;
}

void computeBox(const std::vector<double>& s, double out[4]) {
    double mnx = std::numeric_limits<double>::max(), mxx = -std::numeric_limits<double>::max();
    double mnz = std::numeric_limits<double>::max(), mxz = -std::numeric_limits<double>::max();
    for (size_t k = 0; k + 1 < s.size(); k += 2) {
        mnx = std::min(mnx, s[k]);
        mxx = std::max(mxx, s[k]);
        mnz = std::min(mnz, s[k + 1]);
        mxz = std::max(mxz, s[k + 1]);
    }
    out[0] = mnx; out[1] = mxx; out[2] = mnz; out[3] = mxz;
}

inline bool inBox(const double* b, int at, double x, double z, double reach) {
    return x >= b[at] - reach && x <= b[at + 1] + reach && z >= b[at + 2] - reach && z <= b[at + 3] + reach;
}

double segDistSq(const std::vector<double>& s, double x, double z) {
    double m = std::numeric_limits<double>::max();
    for (size_t i = 0; i + 3 < s.size(); i += 2) {
        m = std::min(m, distancePointToLineSq(s[i], s[i + 1], s[i + 2], s[i + 3], x, z));
    }
    return m;
}

void annotateNetwork(std::vector<RiverGraphEdge>& edges) {
    // Vertex equality map: (x, y, angle, length, distance) -> edge index
    struct VKey {
        double x, y, a, l; int d;
        bool operator==(const VKey& o) const {
            return x == o.x && y == o.y && a == o.a && l == o.l && d == o.d;
        }
    };
    struct VHash {
        size_t operator()(const VKey& k) const {
            auto hb = [](double v) -> size_t {
                uint64_t u; std::memcpy(&u, &v, 8);
                return size_t(u ^ (u >> 32));
            };
            size_t h = hb(k.x);
            h = h * 31 + hb(k.y);
            h = h * 31 + hb(k.a);
            h = h * 31 + hb(k.l);
            h = h * 31 + size_t(k.d);
            return h;
        }
    };
    std::unordered_map<VKey, int, VHash> bySource;
    bySource.reserve(edges.size());
    for (int i = 0; i < int(edges.size()); ++i) {
        const auto& s = edges[i].source;
        bySource[{s.x, s.y, s.angle, s.length, s.distance}] = i;
    }
    for (int i = 0; i < int(edges.size()); ++i) {
        const auto& d = edges[i].drain;
        auto it = bySource.find({d.x, d.y, d.angle, d.length, d.distance});
        if (it != bySource.end()) {
            edges[i].drainEdgeIdx = it->second;
            edges[it->second].sourceEdge = true;
        } else {
            edges[i].drainEdgeIdx = -1;
        }
    }
    for (int i = 0; i < int(edges.size()); ++i) {
        if (!edges[i].sourceEdge) {
            int width = RiverGraphEdge::MIN_WIDTH;
            int cursor = i;
            int hops = 0, maxHops = int(edges.size()) + 1;
            while (cursor >= 0 && hops++ < maxHops) {
                edges[cursor].width = std::max(edges[cursor].width, width);
                edges[cursor].cachedDrainWidth = edges[cursor].width; // kept in sync
                cursor = edges[cursor].drainEdgeIdx;
                width = std::min(width + RiverGraphEdge::WIDTH_STEP, RiverGraphEdge::MAX_WIDTH);
            }
        }
    }
    for (auto& e : edges) e.cachedDrainWidth = e.width;
}

// ---------- RiverShaper ----------
namespace shaper {

constexpr double STEP = 4.0;
constexpr double FINE = 1.5;
constexpr double WAVELENGTH = 6.5;
constexpr double AMPLITUDE = 0.4;
constexpr double CLEARANCE_SHARE = 0.4;
constexpr double CURVATURE_SHARE = 0.6;
constexpr double MAX_SWING = 60.0;
constexpr double JOIN_PULL = 0.9;
constexpr double GRID_SAMPLE = 8.0;
constexpr int GRID_CELL = 32;
constexpr int CLEARANCE_CELLS = 5;

constexpr int DELTA_MIN_WIDTH = 11;
constexpr float DELTA_CHANCE = 0.95f;
constexpr double DELTA_REACH = 9.0;
constexpr double DELTA_ABOVE = 4.0;
constexpr double DELTA_FLATS = 55.0;

constexpr double DELTA_ARM_SHARE = 0.75;
constexpr double DELTA_SPLIT_SHARE = 0.72;
constexpr double DELTA_CREEK_SHARE = 0.3;
constexpr int DELTA_CREEK_WIDTH = 3;
constexpr double DELTA_FAN = 1.2;
constexpr double DELTA_FAN_MORE = 0.3;
constexpr double DELTA_SPLIT_EVERY = 56.0;
constexpr double DELTA_SPLIT_TURN = 0.5;
constexpr double DELTA_SPLIT_SPREAD = 0.8;
constexpr int DELTA_LEVELS = 4;
constexpr int DELTA_BIG_MOUTH = 18;
constexpr int DELTA_MIN_ARM = 2;
constexpr double DELTA_CREEK_EVERY = 14.0;
constexpr double DELTA_CREEK_TURN = 0.85;
constexpr double DELTA_CREEK_LENGTH = 22.0;
constexpr double DELTA_CREEK_GROWTH = 14.0;
constexpr int DELTA_CREEK_DEPTH = 4;
constexpr double DELTA_SPREAD_MAX = 1.0;
constexpr double DELTA_COAST_FAR = 2.0;
constexpr double DELTA_COAST_SLACK = 16.0;

constexpr double DELTA_EASE[5] = {1.0, 0.75, 0.5, 0.25, 0.0};
constexpr double DELTA_OUT = 24.0;
constexpr double DELTA_GAP = 4.0;
constexpr double DELTA_SWING = 0.85;
constexpr double DELTA_CREEK_SWING = 1.3;
constexpr double DELTA_WAVE_WIDTHS = 5.5;
constexpr double DELTA_WAVE_MIN = 16.0;
constexpr double DELTA_MAX_ARM = 480.0;
constexpr int DELTA_MAX_CHANNELS = 300;
constexpr double DELTA_STEP = 2.5;
constexpr double DELTA_TAPER = 1.5;
constexpr double DELTA_RIVER_GAP = 8.0;

constexpr int NONE_ID = std::numeric_limits<int>::min();
constexpr int TRUNK_ID = -1;

inline int64_t gridKey(double x, double z, int cellSize) {
    int64_t cx = floorDiv(int32_t(mthFloor(x)), cellSize);
    int64_t cz = floorDiv(int32_t(mthFloor(z)), cellSize);
    return (cx << 32) ^ (cz & 0xFFFFFFFFll);
}

inline int64_t ijKey(int64_t i, int64_t j) { return (i << 32) ^ (j & 0xFFFFFFFFll); }

inline double smooth(double t) {
    double c = mthClamp(t, 0.0, 1.0);
    return c * c * (3.0 - 2.0 * c);
}

inline int64_t mixSeed(int64_t seed, int64_t a, int64_t b) {
    uint64_t h = uint64_t(seed) ^ (uint64_t(a) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(b) * 0xC2B2AE3D27D4EB4Full);
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    return int64_t(h ^ (h >> 31));
}

inline double unit(int64_t h) { return double(uint64_t(h) >> 11) * 0x1.0p-53; }

void catmullRom(double x0, double z0, double x1, double z1, double x2, double z2, double x3, double z3,
                double t, double& outX, double& outZ) {
    double t0 = 0.0;
    double t1 = t0 + std::max(1.0E-4, std::sqrt(std::hypot(x1 - x0, z1 - z0)));
    double t2 = t1 + std::max(1.0E-4, std::sqrt(std::hypot(x2 - x1, z2 - z1)));
    double t3 = t2 + std::max(1.0E-4, std::sqrt(std::hypot(x3 - x2, z3 - z2)));
    double u = t1 + t * (t2 - t1);
    double a1x = ((t1 - u) * x0 + (u - t0) * x1) / (t1 - t0), a1z = ((t1 - u) * z0 + (u - t0) * z1) / (t1 - t0);
    double a2x = ((t2 - u) * x1 + (u - t1) * x2) / (t2 - t1), a2z = ((t2 - u) * z1 + (u - t1) * z2) / (t2 - t1);
    double a3x = ((t3 - u) * x2 + (u - t2) * x3) / (t3 - t2), a3z = ((t3 - u) * z2 + (u - t2) * z3) / (t3 - t2);
    double b1x = ((t2 - u) * a1x + (u - t0) * a2x) / (t2 - t0), b1z = ((t2 - u) * a1z + (u - t0) * a2z) / (t2 - t0);
    double b2x = ((t3 - u) * a2x + (u - t1) * a3x) / (t3 - t1), b2z = ((t3 - u) * a2z + (u - t1) * a3z) / (t3 - t1);
    outX = ((t2 - u) * b1x + (u - t1) * b2x) / (t2 - t1);
    outZ = ((t2 - u) * b1z + (u - t1) * b2z) / (t2 - t1);
}

struct Chain {
    std::vector<int> edgeIndices;
    int parentIdx = -1;
    int parentVertex = -1;
    int id = 0;
    std::vector<double> qx, qz, qr;
    std::vector<int> vertexAt;
};

struct LandQuery {
    std::function<bool(double, double)> isLand;
    std::function<double(double, double)> height;
    bool flat(double x, double z) const { return !(height(x, z) > DELTA_FLATS); }
};

struct GridPt { double x, z; int id; };
struct RiversEntry { double x, z, r; int river, chain, at; };

struct Rivers {
    static constexpr int CELL = 32;
    static constexpr int BESIDE = -2;
    std::unordered_map<int64_t, std::vector<RiversEntry>> cells;
    std::unordered_map<int, int> cutBelow;
    double widest = 0.0;
    std::vector<double> mouths;

    void add(double x, double z, double r, int river, int chain, int at) {
        cells[gridKey(x, z, CELL)].push_back({x, z, r, river, chain, at});
        widest = std::max(widest, r);
    }

    void addCourse(const std::vector<double>& pts, double r, int river) {
        for (size_t i = 0; i + 3 < pts.size(); i += 2) {
            double dx = pts[i + 2] - pts[i], dz = pts[i + 3] - pts[i + 1];
            int n = std::max(1, int(std::ceil(std::hypot(dx, dz) / 3.0)));
            for (int k = 0; k < n; ++k) add(pts[i] + dx * k / n, pts[i + 1] + dz * k / n, r, river, -1, -1);
        }
        if (pts.size() >= 2) add(pts[pts.size() - 2], pts[pts.size() - 1], r, river, -1, -1);
    }

    void cut(int chain, int at) { cutBelow[chain] = at; }

    bool theirs(double x, double z, double r, double mx, double mz) const {
        double own = std::hypot(x - mx, z - mz) + DELTA_RIVER_GAP + 2.0 * r;
        for (size_t i = 0; i + 1 < mouths.size(); i += 2) {
            if (std::hypot(x - mouths[i], z - mouths[i + 1]) < own) return true;
        }
        return false;
    }

    bool hit(double x, double z, double r, int own) const {
        int span = int(std::ceil((r + widest + DELTA_RIVER_GAP) / CELL));
        int64_t cx = floorDiv(int32_t(mthFloor(x)), CELL), cz = floorDiv(int32_t(mthFloor(z)), CELL);
        for (int64_t i = cx - span; i <= cx + span; ++i) {
            for (int64_t j = cz - span; j <= cz + span; ++j) {
                auto it = cells.find(ijKey(i, j));
                if (it == cells.end()) continue;
                for (const auto& p : it->second) {
                    if (p.river == own) continue;
                    auto cit = cutBelow.find(p.chain);
                    if (cit != cutBelow.end() && p.at >= 0 && p.at < cit->second) continue;
                    double reach = r + p.r + DELTA_RIVER_GAP;
                    double dx = p.x - x, dz = p.z - z;
                    if (dx * dx + dz * dz < reach * reach) return true;
                }
            }
        }
        return false;
    }
};

struct Occupied {
    static constexpr int DELTA_CELL = 16;
    struct Pt { double x, z, r; int id; };
    std::unordered_map<int64_t, std::vector<Pt>> cells;

    void add(double x, double z, double r, int id) {
        cells[gridKey(x, z, DELTA_CELL)].push_back({x, z, r, id});
    }

    int hit(double x, double z, double r, double gap, int self, int skip, double sx, double sz, double clear,
            double& atX, double& atZ) const {
        int64_t cx = floorDiv(int32_t(mthFloor(x)), DELTA_CELL), cz = floorDiv(int32_t(mthFloor(z)), DELTA_CELL);
        for (int64_t i = cx - 2; i <= cx + 2; ++i) {
            for (int64_t j = cz - 2; j <= cz + 2; ++j) {
                auto it = cells.find(ijKey(i, j));
                if (it == cells.end()) continue;
                for (const auto& p : it->second) {
                    if (p.id == self || p.id == skip) continue;
                    if (clear > 0.0 && (p.x - sx) * (p.x - sx) + (p.z - sz) * (p.z - sz) < clear * clear) continue;
                    double reach = r + p.r + gap;
                    double dx = p.x - x, dz = p.z - z;
                    if (dx * dx + dz * dz < reach * reach) {
                        atX = p.x;
                        atZ = p.z;
                        return p.id;
                    }
                }
            }
        }
        return NONE_ID;
    }
};

struct Sprout {
    double x, z, heading;
    int width;
    double parentRadius;
    int level;
    bool arm;
    double bearing;
    int parent;
};

struct Channel {
    std::vector<double> pts;
    int width;
    double flareRadius, flareLength, tipLength;
    bool main;
};

double coastDist(double x, double z, double bx, double bz, const LandQuery& land) {
    for (double d = 8.0; d <= DELTA_MAX_ARM; d += 8.0) {
        if (!land.isLand(x + bx * d, z + bz * d)) return d;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double seaward(double x, double z, double bearing, double sea, const LandQuery& land) {
    double off = mthClamp(std::atan2(std::sin(bearing - sea), std::cos(bearing - sea)), -DELTA_SPREAD_MAX, DELTA_SPREAD_MAX);
    double straight = coastDist(x, z, std::cos(sea), std::sin(sea), land);
    if (std::isnan(straight)) return sea + off;
    for (double ease : DELTA_EASE) {
        double b = sea + off * ease;
        double far = coastDist(x, z, std::cos(b), std::sin(b), land);
        if (!std::isnan(far) && far <= DELTA_COAST_FAR * straight + DELTA_COAST_SLACK) return b;
    }
    return sea;
}

int deltaWidthFor(double radius) {
    return std::max(0, int(javaRound(radius - RiverGraphEdge::deltaWaterRadius(0))));
}

bool growChannel(const Sprout& sp, int id, int levels, double sea, Occupied& occupied, const Rivers& rivers,
                 int river, const RiverGraphEdge& mouth, const LandQuery& land, SplittableRandom& rnd,
                 std::deque<Sprout>& todo, Channel& outCh) {
    double r = RiverGraphEdge::deltaWaterRadius(sp.width);
    double lambda = std::max(DELTA_WAVE_MIN, DELTA_WAVE_WIDTHS * 2.0 * r) * (0.8 + 0.4 * rnd.nextDouble());
    double swing = (sp.arm ? DELTA_SWING : DELTA_CREEK_SWING) * (0.75 + 0.5 * rnd.nextDouble());
    double phase = rnd.nextDouble() * DW_PI * 2.0;
    double length = sp.arm ? DELTA_MAX_ARM : (DELTA_CREEK_LENGTH + DELTA_CREEK_GROWTH * sp.width) * (0.6 + 0.8 * rnd.nextDouble());

    double free = 1.6 * (sp.parentRadius + r + DELTA_GAP) + DELTA_STEP;
    std::vector<Sprout> sent;
    std::vector<double> pts;
    pts.reserve(128);
    pts.push_back(sp.x);
    pts.push_back(sp.z);
    double x = sp.x, z = sp.z, heading = sp.heading, s = 0.0, out = -1.0;
    double nextArm = DELTA_SPLIT_EVERY * (0.6 + 0.8 * rnd.nextDouble());
    double nextCreek = free + DELTA_CREEK_EVERY * (0.3 + 0.7 * rnd.nextDouble());
    int side = rnd.nextBoolean() ? 1 : -1;
    bool blind = true;
    int joined = NONE_ID;

    while (s < length) {
        if (sp.arm) {
            double off = sp.bearing - heading;
            heading += 0.06 * std::atan2(std::sin(off), std::cos(off));
        }
        double h = heading + swing * smooth(s / (0.5 * lambda)) * std::sin(2.0 * DW_PI * s / lambda + phase);
        double nx = x + std::cos(h) * DELTA_STEP, nz = z + std::sin(h) * DELTA_STEP;
        if (out < 0.0 && !land.isLand(nx, nz)) {
            blind = false;
            if (!sp.arm) break;
            out = 0.0;
        }
        if (out >= 0.0) {
            out += DELTA_STEP;
            if (out > DELTA_OUT) break;
        } else {
            if (!land.flat(nx, nz) || rivers.hit(nx, nz, r, river)
                || rivers.theirs(nx, nz, r, mouth.drain.x, mouth.drain.y)) {
                break;
            }
            bool leaving = s < free;
            double atX = 0.0, atZ = 0.0;
            int other = occupied.hit(nx, nz, r, DELTA_GAP, id, leaving ? sp.parent : NONE_ID, sp.x, sp.z,
                                     leaving ? free : 0.0, atX, atZ);
            if (other != NONE_ID) {
                if (sp.arm) {
                    pts.push_back(atX);
                    pts.push_back(atZ);
                    blind = false;
                    joined = other;
                }
                break;
            }
        }
        x = nx; z = nz; s += DELTA_STEP;
        pts.push_back(x);
        pts.push_back(z);
        if (out >= 0.0) continue;
        if (sp.arm && sp.level < levels && sp.width > DELTA_MIN_ARM && s >= nextArm) {
            double turn = side * DELTA_SPLIT_TURN * (0.75 + 0.5 * rnd.nextDouble());
            int narrower = std::max(DELTA_MIN_ARM, int(javaRound(double(sp.width) * DELTA_SPLIT_SHARE)));
            sent.push_back({x, z, h + turn, narrower, r, sp.level + 1, true,
                            seaward(x, z, sp.bearing + DELTA_SPLIT_SPREAD * turn, sea, land), id});
            side = -side;
            nextArm = s + DELTA_SPLIT_EVERY * (0.7 + 0.6 * rnd.nextDouble());
        }
        if (s >= nextCreek) {
            if (sp.level < DELTA_CREEK_DEPTH) {
                int w = sp.arm ? std::min(DELTA_CREEK_WIDTH, int(javaRound(double(sp.width) * DELTA_CREEK_SHARE)))
                               : std::max(0, sp.width - 1);
                sent.push_back({x, z, h + side * DELTA_CREEK_TURN * (0.8 + 0.6 * rnd.nextDouble()), w, r,
                                sp.level + 1, false, std::numeric_limits<double>::quiet_NaN(), id});
                if (rnd.nextDouble() < 0.75) side = -side;
            }
            nextCreek = s + DELTA_CREEK_EVERY * (sp.arm ? 1.0 : 0.75) * (0.6 + 0.8 * rnd.nextDouble());
        }
    }
    if (pts.size() < 8) return false;
    if (sp.arm && sp.level == 0 && out < 0.0 && (joined == NONE_ID || joined == TRUNK_ID)) return false;

    for (size_t k = 0; k + 1 < pts.size(); k += 2) occupied.add(pts[k], pts[k + 1], r, id);
    for (const auto& spt : sent) todo.push_back(spt);
    outCh = {std::move(pts), sp.width, sp.arm ? sp.parentRadius : 0.0,
             sp.arm ? DELTA_TAPER * sp.parentRadius : 0.0,
             blind ? std::min(0.45 * s, 6.0 + 3.0 * sp.width) : 0.0,
             sp.arm && sp.level == 0};
    return true;
}

RiverGraphEdge edgeOf(const Channel& ch, int distance) {
    const auto& p = ch.pts;
    size_t n = p.size() / 2;
    RiverVertex from{p[0], p[1], std::atan2(p[3] - p[1], p[2] - p[0]), 0.0, distance, -1};
    RiverVertex to{p[2 * n - 2], p[2 * n - 1], std::atan2(p[2 * n - 1] - p[2 * n - 3], p[2 * n - 2] - p[2 * n - 4]), 0.0, distance, -1};
    RiverGraphEdge e;
    e.source = from;
    e.drain = to;
    e.fractal = RiverPathFractal::ofPoints(p);
    e.width = ch.width;
    e.cachedDrainWidth = ch.width;
    e.delta = true;
    e.deltaMain = ch.main;
    e.flareRadius = ch.flareRadius;
    e.flareLength = ch.flareLength;
    e.tipLength = ch.tipLength;
    e.sourceEdge = true;
    e.drainEdgeIdx = -1;
    size_t mid = (p.size() / 4) * 2;
    e.initBoundsFromCenter(double(javaRound(p[mid])), double(javaRound(p[mid + 1])));
    return e;
}

double clearance(const std::unordered_map<int64_t, std::vector<GridPt>>& grid, double x, double z, int own) {
    int64_t cx = floorDiv(int32_t(mthFloor(x)), GRID_CELL), cz = floorDiv(int32_t(mthFloor(z)), GRID_CELL);
    double far = (CLEARANCE_CELLS + 1) * GRID_CELL;
    double best = far * far;
    for (int64_t i = cx - CLEARANCE_CELLS; i <= cx + CLEARANCE_CELLS; ++i) {
        for (int64_t j = cz - CLEARANCE_CELLS; j <= cz + CLEARANCE_CELLS; ++j) {
            auto it = grid.find(ijKey(i, j));
            if (it == grid.end()) continue;
            for (const auto& p : it->second) {
                if (p.id == own) continue;
                double dx = p.x - x, dz = p.z - z, d = dx * dx + dz * dz;
                if (d < best) best = d;
            }
        }
    }
    return std::sqrt(best);
}

double bendRadius(const std::vector<std::array<double, 2>>& pts, int k, int half) {
    int a = std::max(0, k - half), b = std::min(int(pts.size()) - 1, k + half);
    if (b - a < 2) return 1.0E9;
    const auto& pa = pts[a]; const auto& pk = pts[k]; const auto& pb = pts[b];
    double t1 = std::atan2(pk[1] - pa[1], pk[0] - pa[0]), t2 = std::atan2(pb[1] - pk[1], pb[0] - pk[0]);
    double turn = std::abs(t2 - t1);
    if (turn > DW_PI) turn = 2.0 * DW_PI - turn;
    double arc = std::hypot(pk[0] - pa[0], pk[1] - pa[1]) + std::hypot(pb[0] - pk[0], pb[1] - pk[1]);
    return turn < 1.0E-6 ? 1.0E9 : arc / turn;
}

std::vector<double> erodeAndEase(const std::vector<double>& v, int r) {
    int m = int(v.size());
    std::vector<double> lo(m), out(m);
    for (int k = 0; k < m; ++k) {
        double x = v[k];
        for (int j = std::max(0, k - r); j <= std::min(m - 1, k + r); ++j) x = std::min(x, v[j]);
        lo[k] = x;
    }
    for (int k = 0; k < m; ++k) {
        double sum = 0.0;
        int cnt = 0;
        for (int j = std::max(0, k - r); j <= std::min(m - 1, k + r); ++j) {
            sum += lo[j];
            ++cnt;
        }
        out[k] = std::min(lo[k] * 1.5, sum / double(cnt));
    }
    return out;
}

void shapeChain(Chain& c, const std::vector<Chain>& chains, std::vector<RiverGraphEdge>& edges,
                const std::unordered_map<int64_t, std::vector<GridPt>>& grid, int64_t seed) {
    int nv = int(c.edgeIndices.size()) + 1;
    std::vector<double> px(nv), pz(nv);
    px[0] = edges[c.edgeIndices[0]].drain.x;
    pz[0] = edges[c.edgeIndices[0]].drain.y;
    for (int i = 0; i < int(c.edgeIndices.size()); ++i) {
        px[i + 1] = edges[c.edgeIndices[i]].source.x;
        pz[i + 1] = edges[c.edgeIndices[i]].source.y;
    }

    double bx = 2.0 * px[0] - px[1], bz = 2.0 * pz[0] - pz[1];
    if (c.parentIdx >= 0) {
        const Chain& p = chains[c.parentIdx];
        int at = p.vertexAt[c.parentVertex];
        px[0] = p.qx[at];
        pz[0] = p.qz[at];
        int down = std::max(0, at - 6);
        double fx = p.qx[down] - p.qx[at], fz = p.qz[down] - p.qz[at];
        double fl = std::hypot(fx, fz);
        if (fl > 1.0E-6) {
            double len = std::hypot(px[1] - px[0], pz[1] - pz[0]) * JOIN_PULL;
            bx = px[0] + fx / fl * len;
            bz = pz[0] + fz / fl * len;
        }
    }
    double ax = 2.0 * px[nv - 1] - px[nv - 2], az = 2.0 * pz[nv - 1] - pz[nv - 2];

    std::vector<std::array<double, 2>> pts;
    std::vector<int> vertexAt(nv);
    for (int i = 0; i < nv - 1; ++i) {
        double x0 = (i == 0 ? bx : px[i - 1]), z0 = (i == 0 ? bz : pz[i - 1]);
        double x3 = (i + 2 < nv ? px[i + 2] : ax), z3 = (i + 2 < nv ? pz[i + 2] : az);
        int n = std::max(2, int(std::ceil(std::hypot(px[i + 1] - px[i], pz[i + 1] - pz[i]) / FINE)));
        vertexAt[i] = int(pts.size());
        for (int j = 0; j < n; ++j) {
            double ox = 0.0, oz = 0.0;
            catmullRom(x0, z0, px[i], pz[i], px[i + 1], pz[i + 1], x3, z3, double(j) / n, ox, oz);
            pts.push_back({ox, oz});
        }
    }
    vertexAt[nv - 1] = int(pts.size());
    pts.push_back({px[nv - 1], pz[nv - 1]});
    int m = int(pts.size());

    std::vector<double> s(m, 0.0), nx(m), nz(m), full(m);
    for (int k = 1; k < m; ++k) {
        s[k] = s[k - 1] + std::hypot(pts[k][0] - pts[k - 1][0], pts[k][1] - pts[k - 1][1]);
    }
    for (int k = 0; k < m; ++k) {
        const auto& a = pts[std::max(0, k - 1)];
        const auto& b = pts[std::min(m - 1, k + 1)];
        double tx = b[0] - a[0], tz = b[1] - a[1], tl = std::max(1.0E-9, std::hypot(tx, tz));
        nx[k] = -tz / tl;
        nz[k] = tx / tl;
    }
    for (int i = 0; i < nv - 1; ++i) {
        const auto& e = edges[c.edgeIndices[i]];
        int downWidth = (i == 0) ? e.width : edges[c.edgeIndices[i - 1]].width;
        int span = std::max(1, vertexAt[i + 1] - vertexAt[i]);
        for (int k = vertexAt[i]; k <= vertexAt[i + 1]; ++k) {
            double t = double(k - vertexAt[i]) / span;
            full[k] = 2.0 * normalRiverWaterRadius(mthLerp(t, double(downWidth), double(e.width)));
        }
    }
    double total = s[m - 1];

    std::vector<double> room(m, 0.0);
    int every = std::max(1, int(javaRound(GRID_SAMPLE / FINE)));
    int bendHalf = int(javaRound(10.0 / FINE));
    for (int k = 0; k < m; k += every) {
        double clear = clearance(grid, pts[k][0], pts[k][1], c.id);
        double bend = bendRadius(pts, k, bendHalf);
        double r = std::min(CLEARANCE_SHARE * std::max(0.0, clear - full[k] * 0.5 - 6.0), CURVATURE_SHARE * bend);
        for (int j = k; j < std::min(m, k + every); ++j) room[j] = r;
    }

    auto eased = erodeAndEase(room, int(javaRound(24.0 / FINE)));

    int64_t h = mixSeed(seed, javaRound(px[nv - 1]), javaRound(pz[nv - 1]));
    double phase1 = unit(h) * DW_PI * 2.0;
    double phase2 = unit(mixSeed(h, 1, 2)) * DW_PI * 2.0;
    double phase3 = unit(mixSeed(h, 3, 4)) * DW_PI * 2.0;
    c.qx.resize(m);
    c.qz.resize(m);
    for (int k = 0; k < m; ++k) {
        double lambda = WAVELENGTH * full[k];
        if (k > 0) {
            double ds = s[k] - s[k - 1];
            phase1 += 2.0 * DW_PI * ds / lambda;
            phase2 += 2.0 * DW_PI * ds / (0.47 * lambda);
        }
        double strength = 0.65 + 0.35 * std::sin(2.0 * DW_PI * s[k] / (4.3 * lambda) + phase3);
        double taper = smooth(s[k] / (0.6 * lambda)) * smooth((total - s[k]) / (0.6 * lambda));
        double amplitude = std::min(std::min(AMPLITUDE * lambda * strength, eased[k]), MAX_SWING) * taper;
        double d = amplitude * (0.72 * std::sin(phase1) + 0.28 * std::sin(phase2));
        c.qx[k] = pts[k][0] + nx[k] * d;
        c.qz[k] = pts[k][1] + nz[k] * d;
    }
    c.qr.resize(m);
    for (int k = 0; k < m; ++k) c.qr[k] = 0.5 * full[k];
    c.vertexAt = std::move(vertexAt);

    int keep = std::max(1, int(javaRound(STEP / FINE)));
    for (int i = 0; i < nv - 1; ++i) {
        int from = c.vertexAt[i], to = c.vertexAt[i + 1];
        std::vector<double> points;
        for (int k = to; k > from; k -= keep) {
            points.push_back(c.qx[k]);
            points.push_back(c.qz[k]);
        }
        points.push_back(c.qx[from]);
        points.push_back(c.qz[from]);
        edges[c.edgeIndices[i]].fractal = RiverPathFractal::ofPoints(std::move(points));
    }
}

void buildDelta(const Chain& c, std::vector<RiverGraphEdge>& edges, std::vector<bool>& removed,
                std::vector<RiverGraphEdge>& extraEdges, int64_t h, const LandQuery& land,
                const std::vector<bool>& joined, Rivers& rivers, int inlandReach) {
    const auto& mouth = edges[c.edgeIndices[0]];
    int nv = int(c.edgeIndices.size()) + 1;
    int freeIdx = 1;
    while (freeIdx < nv - 1 && !joined[freeIdx]) ++freeIdx;
    const auto& qx = c.qx;
    const auto& qz = c.qz;
    int top = c.vertexAt[freeIdx], head = c.vertexAt[nv - 1];
    if (top < 8) return;
    std::vector<double> s(head + 1, 0.0);
    for (int k = 1; k <= head; ++k) s[k] = s[k - 1] + std::hypot(qx[k] - qx[k - 1], qz[k] - qz[k - 1]);
    double full = 2.0 * normalRiverWaterRadius(mouth.width);

    int coastAt = 0;
    while (coastAt < top && !land.isLand(qx[coastAt], qz[coastAt])) ++coastAt;
    double highest = std::min(0.85 * s[top], s[head] - DELTA_ABOVE * full);
    if (highest < 90.0) return;

    if (inlandReach <= 0) return;
    double back = mthClamp(s[coastAt] + std::min(DELTA_REACH * full, std::max(90.0, double(inlandReach))), 90.0, highest);
    int a = 0;
    while (a < top && s[a] < back) ++a;
    while (a > coastAt && !land.flat(qx[a], qz[a])) --a;
    if (s[a] - s[coastAt] < full) return;
    if (a < 4 || a >= top) return;
    double apexX = qx[a], apexZ = qz[a];

    double fx = qx[a - 4] - apexX, fz = qz[a - 4] - apexZ;
    double sx = qx[0] - apexX, sz = qz[0] - apexZ;
    double fl = std::hypot(fx, fz), sl = std::hypot(sx, sz);
    if (fl < 1.0E-6 || sl < 1.0E-6) return;
    fx /= fl; fz /= fl; sx /= sl; sz /= sl;

    int k = 0;
    while (k + 1 < freeIdx && c.vertexAt[k + 1] <= a) ++k;
    int heldIdx = c.edgeIndices[k];
    RiverGraphEdge& held = edges[heldIdx];

    SplittableRandom rnd(h);
    constexpr int count = 2;
    double fan = DELTA_FAN + DELTA_FAN_MORE * rnd.nextDouble();
    double skew = 0.1 * (rnd.nextDouble() - 0.5);
    double sea = std::atan2(sz, sx), current = std::atan2(fz, fx);
    double straight = coastDist(apexX, apexZ, sx, sz, land);
    if (std::isnan(straight)) return;
    int levels = mouth.width >= DELTA_BIG_MOUTH ? DELTA_LEVELS : DELTA_LEVELS - 1;
    double trunk = normalRiverWaterRadius(held.width);
    int width = std::max(DELTA_MIN_ARM + 1, deltaWidthFor(trunk * DELTA_ARM_SHARE));

    Occupied occupied;
    for (int j = a; j <= top && s[j] - s[a] <= 6.0 * trunk; ++j) occupied.add(qx[j], qz[j], trunk, TRUNK_ID);
    std::deque<Sprout> todo;
    std::vector<Channel> channels;
    int arms = 0;
    for (int b = 0; b < count; ++b) {
        double middle = skew - fan + fan * (2 * b + 1) / double(count);
        for (double ease : DELTA_EASE) {
            double bearing = sea + middle * ease;
            double far = coastDist(apexX, apexZ, std::cos(bearing), std::sin(bearing), land);
            if (std::isnan(far) || far > DELTA_COAST_FAR * straight + DELTA_COAST_SLACK) continue;
            double toward = std::atan2(std::sin(bearing - current), std::cos(bearing - current));
            Channel ch;
            if (!growChannel({apexX, apexZ, current + 0.6 * toward, width, trunk, 0, true, bearing, TRUNK_ID},
                             int(channels.size()), levels, sea, occupied, rivers, c.id, mouth, land, rnd, todo, ch))
                continue;
            channels.push_back(std::move(ch));
            ++arms;
            break;
        }
    }
    if (arms < 2) return;
    while (!todo.empty() && int(channels.size()) < DELTA_MAX_CHANNELS) {
        Sprout sp = todo.front();
        todo.pop_front();
        Channel ch;
        if (growChannel(sp, int(channels.size()), levels, sea, occupied, rivers, c.id, mouth, land, rnd, todo, ch))
            channels.push_back(std::move(ch));
    }

    int keep = std::max(1, int(javaRound(STEP / FINE)));
    std::vector<double> points;
    for (int j = c.vertexAt[k + 1]; j > a; j -= keep) {
        points.push_back(qx[j]);
        points.push_back(qz[j]);
    }
    points.push_back(apexX);
    points.push_back(apexZ);
    if (points.size() < 4) return;
    held.fractal = RiverPathFractal::ofPoints(std::move(points));
    held.drainEdgeIdx = -1; // linkToDrain(null) leaves cachedDrainWidth and sourceEdge intact
    for (int j = 0; j < k; ++j) removed[c.edgeIndices[j]] = true;
    for (const auto& ch : channels) extraEdges.push_back(edgeOf(ch, mouth.source.distance));

    rivers.cut(c.id, a);
    for (const auto& ch : channels) rivers.addCourse(ch.pts, RiverGraphEdge::deltaWaterRadius(ch.width), c.id);
}

std::vector<RiverGraphEdge> shapeRegion(std::vector<RiverGraphEdge> edges, int64_t seed, const LandQuery& land,
                                        const std::vector<const std::vector<double>*>& beside,
                                        const std::vector<int>& besideWidths,
                                        const std::vector<double>& besideMouths, int inlandReach) {
    if (edges.empty()) return edges;
    int n = int(edges.size());
    std::vector<std::vector<int>> upstream(n);
    for (int i = 0; i < n; ++i) {
        int d = edges[i].drainEdgeIdx;
        if (d >= 0 && d < n) upstream[d].push_back(i);
    }
    std::vector<int> size(n, 0);
    auto calcSize = [&](auto& self, int idx, int depth) -> int {
        if (size[idx] > 0) return size[idx];
        if (size[idx] < 0 || depth >= 128) return 0;
        size[idx] = -1;
        int s = 1;
        for (int u : upstream[idx]) s += self(self, u, depth + 1);
        size[idx] = s;
        return s;
    };
    for (int i = 0; i < n; ++i) calcSize(calcSize, i, 0);

    std::vector<Chain> chains;
    std::vector<int> chainOf(n, -1);
    std::deque<Chain> todo;
    std::deque<int> starts;
    for (int i = 0; i < n; ++i) {
        int d = edges[i].drainEdgeIdx;
        if (d < 0 || d >= n) {
            todo.emplace_back();
            starts.push_back(i);
        }
    }
    while (!todo.empty()) {
        Chain c = std::move(todo.front());
        todo.pop_front();
        int e = starts.front();
        starts.pop_front();
        c.id = int(chains.size());
        chains.push_back(std::move(c));
        Chain& stored = chains.back();
        while (e >= 0) {
            stored.edgeIndices.push_back(e);
            chainOf[e] = stored.id;
            int mainIdx = -1;
            for (int u : upstream[e]) {
                if (mainIdx < 0 || edges[u].width > edges[mainIdx].width
                    || (edges[u].width == edges[mainIdx].width && size[u] > size[mainIdx])) {
                    mainIdx = u;
                }
            }
            for (int u : upstream[e]) {
                if (u == mainIdx) continue;
                Chain sub;
                sub.parentIdx = stored.id;
                sub.parentVertex = int(stored.edgeIndices.size());
                todo.push_back(std::move(sub));
                starts.push_back(u);
            }
            e = mainIdx;
        }
    }

    std::unordered_map<int64_t, std::vector<GridPt>> grid;
    for (int i = 0; i < n; ++i) {
        const auto& e = edges[i];
        double sx = e.source.x, sz = e.source.y, dx = e.drain.x - sx, dz = e.drain.y - sz;
        int steps = std::max(1, int(std::ceil(std::hypot(dx, dz) / GRID_SAMPLE)));
        int cid = chainOf[i];
        for (int k = 0; k <= steps; ++k) {
            double x = sx + dx * k / steps, z = sz + dz * k / steps;
            grid[gridKey(x, z, GRID_CELL)].push_back({x, z, cid});
        }
    }

    for (auto& ch : chains) shapeChain(ch, chains, edges, grid, seed);

    std::vector<std::vector<bool>> joined(chains.size());
    for (size_t i = 0; i < chains.size(); ++i) joined[i].assign(chains[i].edgeIndices.size() + 1, false);
    for (const auto& ch : chains) {
        if (ch.parentIdx >= 0) joined[ch.parentIdx][ch.parentVertex] = true;
    }

    Rivers rivers;
    // Root finder
    auto rootId = [&](int cid) {
        while (chains[cid].parentIdx >= 0) cid = chains[cid].parentIdx;
        return cid;
    };
    for (const auto& ch : chains) {
        int river = rootId(ch.id);
        for (size_t k = 0; k < ch.qx.size(); k += 2) rivers.add(ch.qx[k], ch.qz[k], ch.qr[k], river, ch.id, int(k));
    }
    for (size_t i = 0; i < beside.size(); ++i) {
        double r = normalRiverWaterRadius(besideWidths[i]);
        double swingR = r + std::min(MAX_SWING, AMPLITUDE * WAVELENGTH * 2.0 * r);
        rivers.addCourse(*beside[i], swingR, Rivers::BESIDE);
    }
    rivers.mouths = besideMouths;

    std::vector<bool> removed(n, false);
    std::vector<RiverGraphEdge> extraEdges;
    for (const auto& ch : chains) {
        if (ch.parentIdx >= 0) continue;
        const auto& mouth = edges[ch.edgeIndices[0]];
        if (mouth.drainEdgeIdx >= 0 || mouth.width < DELTA_MIN_WIDTH) continue;
        int64_t h = mixSeed(seed ^ int64_t(0x4F1BBCDCBFA53E0Bull), javaRound(mouth.drain.x), javaRound(mouth.drain.y));
        if (unit(h) >= DELTA_CHANCE) continue;
        buildDelta(ch, edges, removed, extraEdges, h, land, joined[ch.id], rivers, inlandReach);
    }

    // Compact remaining edges while preserving drainEdgeIdx indices!
    std::vector<int> remap(n, -1);
    std::vector<RiverGraphEdge> out;
    out.reserve(n + extraEdges.size());
    for (int i = 0; i < n; ++i) {
        if (!removed[i]) {
            remap[i] = int(out.size());
            out.push_back(std::move(edges[i]));
        }
    }
    for (auto& e : out) {
        if (e.drainEdgeIdx >= 0) e.drainEdgeIdx = remap[e.drainEdgeIdx];
    }
    for (auto& e : extraEdges) out.push_back(std::move(e));
    return out;
}

} // namespace shaper

} // namespace

// ---------- RiverPathFractal public methods ----------
RiverPathFractal RiverPathFractal::createFractal(mc::Xoroshiro& random, int bisections, double sx, double sy, double dx, double dy) {
    RiverPathFractal f;
    f.segments = bisectPath(random, bisections, {sx, sy, dx, dy});
    f.norm = encompassingRanges()[bisections] * normInf(sx - dx, sy - dy);
    f.computeBounds();
    return f;
}

RiverPathFractal RiverPathFractal::ofPoints(std::vector<double> pts) {
    RiverPathFractal f;
    f.segments = std::move(pts);
    size_t n = f.segments.size();
    double nr = 0.0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        nr = std::max(nr, std::sqrt(distancePointToLineSq(f.segments[0], f.segments[1],
            f.segments[n - 2], f.segments[n - 1], f.segments[i], f.segments[i + 1])));
    }
    f.norm = nr + 1.0;
    f.computeBounds();
    return f;
}

void RiverPathFractal::computeBounds() {
    double x0 = std::numeric_limits<double>::max(), x1 = -std::numeric_limits<double>::max();
    double y0 = std::numeric_limits<double>::max(), y1 = -std::numeric_limits<double>::max();
    for (size_t i = 0; i + 1 < segments.size(); i += 2) {
        x0 = std::min(x0, segments[i]);
        x1 = std::max(x1, segments[i]);
        y0 = std::min(y0, segments[i + 1]);
        y1 = std::max(y1, segments[i + 1]);
    }
    minX = x0; maxX = x1; minY = y0; maxY = y1;
}

double RiverPathFractal::boxDistanceSq(double x, double y) const {
    double dx = x < minX ? minX - x : (x > maxX ? x - maxX : 0.0);
    double dy = y < minY ? minY - y : (y > maxY ? y - maxY : 0.0);
    return dx * dx + dy * dy;
}

double RiverPathFractal::intersectDistance(double x, double y) const {
    double m = std::numeric_limits<double>::max();
    for (size_t i = 0; i + 3 < segments.size(); i += 2) {
        double d = distancePointToLineSq(segments[i], segments[i + 1], segments[i + 2], segments[i + 3], x, y);
        if (d < m) m = d;
    }
    return m;
}

double RiverPathFractal::nearestPoint(double x, double y, double& outX, double& outZ) const {
    double m = std::numeric_limits<double>::max();
    int minIdx = -1;
    for (size_t i = 0; i + 3 < segments.size(); i += 2) {
        double d = distancePointToLineSq(segments[i], segments[i + 1], segments[i + 2], segments[i + 3], x, y);
        if (d < m) { m = d; minIdx = int(i); }
    }
    if (minIdx >= 0) {
        double ax = segments[minIdx], ay = segments[minIdx + 1];
        double dx = segments[minIdx + 2] - ax, dy = segments[minIdx + 3] - ay;
        double lenSq = dx * dx + dy * dy;
        double t = lenSq > 0.0 ? mthClamp(((x - ax) * dx + (y - ay) * dy) / lenSq, 0.0, 1.0) : 0.0;
        outX = ax + t * dx;
        outZ = ay + t * dy;
    }
    return m;
}

double RiverPathFractal::nearestAlong(double x, double y, double out[6]) const {
    double m = std::numeric_limits<double>::max(), walked = 0.0, along = 0.0;
    for (size_t i = 0; i + 3 < segments.size(); i += 2) {
        double ax = segments[i], ay = segments[i + 1];
        double dx = segments[i + 2] - ax, dy = segments[i + 3] - ay;
        double len = std::hypot(dx, dy);
        double d = distancePointToLineSq(ax, ay, segments[i + 2], segments[i + 3], x, y);
        if (d < m && len > 0.0) {
            m = d;
            double t = mthClamp(((x - ax) * dx + (y - ay) * dy) / (len * len), 0.0, 1.0);
            out[0] = ax + t * dx;
            out[1] = ay + t * dy;
            out[2] = dx / len;
            out[3] = dy / len;
            along = walked + t * len;
        }
        walked += len;
    }
    out[4] = along;
    out[5] = walked;
    return m;
}

RiverFlowDir RiverPathFractal::calculateFlow(double x, double y) const {
    if (segments.size() < 4) return RiverFlowDir::NONE;
    double m = std::numeric_limits<double>::max();
    int minIdx = -1;
    for (size_t i = 0; i + 3 < segments.size(); i += 2) {
        double d = distancePointToLineSq(segments[i], segments[i + 1], segments[i + 2], segments[i + 3], x, y);
        if (d < m) { m = d; minIdx = int(i); }
    }
    if (minIdx == -1) return RiverFlowDir::NONE;
    double sx = segments[minIdx], sy = segments[minIdx + 1], dx = segments[minIdx + 2], dy = segments[minIdx + 3];
    double angle = std::atan2(-(dy - sy), dx - sx);
    int ord = int(javaRound(8.0 * (angle / double(DW_PI_F))));
    if (ord < 0) ord += 16;
    return static_cast<RiverFlowDir>((ord >= 0 && ord < 16) ? ord : 16);
}

void RiverPathFractal::calculateContinuousFlow(double x, double y, double& vx, double& vz) const {
    if (segments.size() < 4) { vx = 0.0; vz = 0.0; return; }
    double minDist = std::numeric_limits<double>::max();
    int minIdx = -1;
    for (size_t i = 0; i + 3 < segments.size(); i += 2) {
        double d = distancePointToLineSq(segments[i], segments[i + 1], segments[i + 2], segments[i + 3], x, y);
        if (d < minDist) { minDist = d; minIdx = int(i); }
    }
    if (minIdx == -1) { vx = 0.0; vz = 0.0; return; }

    double sourceX = segments[minIdx], sourceY = segments[minIdx + 1];
    double drainX = segments[minIdx + 2], drainY = segments[minIdx + 3];
    double dx = drainX - sourceX;
    double dy = drainY - sourceY;
    double lenSq = dx * dx + dy * dy;
    double t = 0.5;
    if (lenSq > 0.0) {
        t = ((x - sourceX) * dx + (y - sourceY) * dy) / lenSq;
        t = std::max(0.0, std::min(1.0, t));
    }

    double angle;
    if (t < 0.25 && minIdx >= 2) {
        double prevAngle = std::atan2(-(segments[minIdx + 1] - segments[minIdx - 1]), segments[minIdx] - segments[minIdx - 2]);
        double currAngle = std::atan2(-dy, dx);
        double wPrev = 0.5 - 2.0 * t;
        double wCurr = 0.5 + 2.0 * t;
        double ax = wPrev * mthCos(float(prevAngle)) + wCurr * mthCos(float(currAngle));
        double ay = wPrev * mthSin(float(prevAngle)) + wCurr * mthSin(float(currAngle));
        angle = std::atan2(ay, ax);
    } else if (t > 0.75 && minIdx < int(segments.size()) - 4) {
        double currAngle = std::atan2(-dy, dx);
        double nextAngle = std::atan2(-(segments[minIdx + 5] - segments[minIdx + 3]), segments[minIdx + 4] - segments[minIdx + 2]);
        double u = t - 0.75;
        double wCurr = 1.0 - 2.0 * u;
        double wNext = 2.0 * u;
        double ax = wCurr * mthCos(float(currAngle)) + wNext * mthCos(float(nextAngle));
        double ay = wCurr * mthSin(float(currAngle)) + wNext * mthSin(float(nextAngle));
        angle = std::atan2(ay, ax);
    } else {
        angle = std::atan2(-dy, dx);
    }

    vx = double(mthCos(float(angle)));
    vz = -double(mthSin(float(angle)));
}

// ---------- RiverGraphEdge methods ----------
void RiverGraphEdge::initBoundsFromCenter(double cx, double cz) {
    int icx = int(javaRound(cx)), icz = int(javaRound(cz));
    minPartX = (icx - MAX_AFFECTING_BLOCK_DISTANCE) >> 8;
    minPartZ = (icz - MAX_AFFECTING_BLOCK_DISTANCE) >> 8;
    maxPartX = (icx + MAX_AFFECTING_BLOCK_DISTANCE) >> 8;
    maxPartZ = (icz + MAX_AFFECTING_BLOCK_DISTANCE) >> 8;
}

int RiverGraphEdge::drainWidth(const std::vector<RiverGraphEdge>& regionEdges) const {
    int down = (drainEdgeIdx < 0 || drainEdgeIdx >= int(regionEdges.size())) ? cachedDrainWidth : regionEdges[drainEdgeIdx].width;
    return mthClamp(down, width, width + WIDTH_STEP);
}

double RiverGraphEdge::widthAt(double exactX, double exactZ, const std::vector<RiverGraphEdge>& regionEdges) const {
    double t = projectAlongLine(source.x, source.y, drain.x, drain.y, exactX, exactZ);
    return mthLerp(t, double(width), double(drainWidth(regionEdges)));
}

double RiverGraphEdge::widthSq(double exactX, double exactZ, const std::vector<RiverGraphEdge>& regionEdges) const {
    double rw = widthAt(exactX, exactZ, regionEdges);
    return rw * rw;
}

double RiverGraphEdge::deltaRadius(double exactX, double exactZ) const {
    double r = deltaWaterRadius(width);
    if (flareLength > 0.0 && flareRadius > r) {
        double t = std::hypot(exactX - source.x, exactZ - source.y) / flareLength;
        if (t < 1.0) r = mthLerp(t * t * (3.0 - 2.0 * t), flareRadius, r);
    }
    if (tipLength > 0.0) {
        double t = std::hypot(exactX - drain.x, exactZ - drain.y) / tipLength;
        if (t < 1.0) r *= std::sqrt(t * t * (3.0 - 2.0 * t));
    }
    return r;
}

double RiverGraphEdge::maxWaterRadius(const std::vector<RiverGraphEdge>& regionEdges) const {
    if (delta) return std::max(flareRadius, deltaWaterRadius(width));
    return normalRiverWaterRadius(drainWidth(regionEdges));
}

// ---------- MainRiversEngine::RawRegion ----------
bool MainRiversEngine::RawRegion::isLand(int bx, int bz) const {
    int i = javaRoundF(float(bx - originX) / SAMPLE_STEP);
    int j = javaRoundF(float(bz - originZ) / SAMPLE_STEP);
    if (i < 0 || i >= cols || j < 0 || j >= rows) return false;
    return land[size_t(j) * cols + i] != 0;
}

bool MainRiversEngine::RawRegion::isShore(int bx, int bz) const {
    if (!isLand(bx, bz)) return false;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue;
            if (!isLand(bx + dx * SAMPLE_STEP, bz + dz * SAMPLE_STEP)) return true;
        }
    }
    return false;
}

int MainRiversEngine::RawRegion::distToOcean(int bx, int bz) const {
    int i = javaRoundF(float(bx - originX) / SAMPLE_STEP);
    int j = javaRoundF(float(bz - originZ) / SAMPLE_STEP);
    if (i < 0 || i >= cols || j < 0 || j >= rows) return -1;
    return distanceToOcean[size_t(j) * cols + i];
}

// ---------- MainRiversEngine ----------
namespace {
std::atomic<uint64_t> gRiverEpoch{1};

template <typename Fn>
struct RiverScopeExit {
    Fn fn;
    ~RiverScopeExit() { fn(); }
};
}

void MainRiversEngine::reset(int64_t worldSeed, const DynamicWatersConfig& cfg, TerrainQueryCallbacks cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    seed_ = worldSeed;
    cfg_ = cfg;
    cb_ = std::move(cb);
    grown_.clear();
    regions_.clear();
    partition_.clear();
    growingRegions_.clear();
    buildingRegions_.clear();
    const uint64_t ep = gRiverEpoch.fetch_add(1, std::memory_order_relaxed) + 1;
    resetEpoch_.store(ep, std::memory_order_release);
    epoch_.store(ep, std::memory_order_release);
}

void MainRiversEngine::setCallbacks(TerrainQueryCallbacks cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    cb_ = std::move(cb);
}

std::shared_ptr<MainRiversEngine::RawRegion> MainRiversEngine::growRegion(int rx, int rz) {
    auto raw = std::make_shared<RawRegion>();
    raw->rx = rx;
    raw->rz = rz;
    int minX = rx * REGION_SIZE, minZ = rz * REGION_SIZE;
    int originX = minX - HALO_BLOCKS, originZ = minZ - HALO_BLOCKS;
    int width = REGION_SIZE + 2 * HALO_BLOCKS;
    int cols = (width + SAMPLE_STEP - 1) / SAMPLE_STEP + 1;
    int rows = (width + SAMPLE_STEP - 1) / SAMPLE_STEP + 1;
    raw->originX = originX;
    raw->originZ = originZ;
    raw->cols = cols;
    raw->rows = rows;
    raw->land.resize(size_t(cols) * rows, 0);

    for (int i = 0; i < cols; ++i) {
        for (int j = 0; j < rows; ++j) {
            int x = originX + i * SAMPLE_STEP, z = originZ + j * SAMPLE_STEP;
            raw->land[size_t(j) * cols + i] = cb_.isOceanAt(x, z) ? 0 : 1;
        }
    }

    raw->distanceToOcean.assign(size_t(cols) * rows, -1);
    std::deque<int> q;
    for (int i = 0; i < int(raw->land.size()); ++i) {
        if (raw->land[i] == 0) {
            raw->distanceToOcean[i] = 0;
            q.push_back(i);
        }
    }
    while (!q.empty()) {
        int idx = q.front();
        q.pop_front();
        int x = idx % cols, z = idx / cols;
        int nd = raw->distanceToOcean[idx] + 1;
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dz == 0) continue;
                int nx = x + dx, nz = z + dz;
                if (nx < 0 || nx >= cols || nz < 0 || nz >= rows) continue;
                int nIdx = nz * cols + nx;
                if (raw->distanceToOcean[nIdx] == -1) {
                    raw->distanceToOcean[nIdx] = nd;
                    q.push_back(nIdx);
                }
            }
        }
    }

    int64_t rSeed = (seed_ + int64_t(rx) * 341873128712ll + int64_t(rz) * 132897987541ll) ^ 84213987123ll;
    mc::Xoroshiro random = mc::Xoroshiro::fromSeed(rSeed);

    RegionRiverContext rctx;
    rctx.legalFn = [rawPtr = raw.get()](const RiverVertex& prev, const RiverVertex& v) -> bool {
        if (v.distance > 3) {
            if (!rawPtr->isLand(int(javaRound(prev.x)), int(javaRound(prev.y)))) return false;
            if (!rawPtr->isLand(int(javaRound(v.x)), int(javaRound(v.y)))) return true;
            int prevD = rawPtr->distToOcean(int(javaRound(prev.x)), int(javaRound(prev.y)));
            int nextD = rawPtr->distToOcean(int(javaRound(v.x)), int(javaRound(v.y)));
            return nextD >= prevD || nextD >= 12;
        }
        return true;
    };

    const int DRAIN_STEP = drainSpacing(cfg_.normalRiverDensity);
    const float keep = drainKeep(cfg_.normalRiverDensity);
    const int64_t thinSeed = keep < 1.0f ? random.nextLong() : 0ll;
    for (int x = minX; x < minX + REGION_SIZE; x += DRAIN_STEP) {
        if (minX + REGION_SIZE - x < DRAIN_STEP / 2) continue;
        for (int z = minZ; z < minZ + REGION_SIZE; z += DRAIN_STEP) {
            if (minZ + REGION_SIZE - z < DRAIN_STEP / 2) continue;
            if (keep < 1.0f && unitHash(thinSeed, x, z) >= keep) continue;
            int bestX = -1, bestZ = -1;
            for (int dx = 0; dx < DRAIN_STEP; dx += SAMPLE_STEP) {
                for (int dz = 0; dz < DRAIN_STEP; dz += SAMPLE_STEP) {
                    if (raw->isShore(x + dx, z + dz)) {
                        bestX = x + dx; bestZ = z + dz;
                        break;
                    }
                }
                if (bestX != -1) break;
            }
            if (bestX == -1) {
                for (int dx = 0; dx < DRAIN_STEP; dx += SAMPLE_STEP) {
                    for (int dz = 0; dz < DRAIN_STEP; dz += SAMPLE_STEP) {
                        if (!raw->isLand(x + dx, z + dz) && random.nextFloat() < 0.05f) {
                            bestX = x + dx; bestZ = z + dz;
                            break;
                        }
                    }
                    if (bestX != -1) break;
                }
            }
            if (bestX != -1) {
                float bestAngle = findBestStartingAngle(*raw, random, bestX, bestZ);
                if (!std::isnan(bestAngle)) {
                    mc::Xoroshiro rng = mc::Xoroshiro::fromSeed(random.nextLong());
                    double ext = 60.0;
                    double startX = bestX + 0.5 - std::cos(double(bestAngle)) * ext;
                    double startZ = bestZ + 0.5 - std::sin(double(bestAngle)) * ext;
                    rctx.builders.emplace_back(rng, startX, startZ, double(bestAngle), double(RIVER_STEP_LENGTH),
                                               RIVER_DEPTH, double(RIVER_FEATHER));
                }
            }
        }
    }

    rctx.runBuild();

    for (const auto& b : rctx.builders) {
        for (const auto& ie : b.edges) {
            RiverGraphEdge e;
            e.source = ie.source;
            e.drain = ie.drain;
            e.fractal = RiverPathFractal::createFractal(random, 4, ie.source.x, ie.source.y, ie.drain.x, ie.drain.y);
            e.width = RiverGraphEdge::MIN_WIDTH;
            e.cachedDrainWidth = RiverGraphEdge::MIN_WIDTH;
            e.initBoundsFromCenter(double(javaRound(0.5 * (ie.source.x + ie.drain.x))),
                                   double(javaRound(0.5 * (ie.source.y + ie.drain.y))));
            raw->edges.push_back(std::move(e));
        }
    }
    if (!raw->edges.empty()) annotateNetwork(raw->edges);

    size_t n = raw->edges.size();
    raw->courses.resize(n);
    raw->widths.resize(n);
    raw->middles.resize(2 * n);
    raw->boxes.resize(4 * n);
    raw->reaches.assign(n, 0);
    raw->ends.resize(2 * n);
    raw->drains.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const auto& e = raw->edges[i];
        raw->courses[i] = e.fractal.segments;
        raw->widths[i] = e.width;
        raw->middles[2 * i] = (e.source.x + e.drain.x) * 0.5;
        raw->middles[2 * i + 1] = (e.source.y + e.drain.y) * 0.5;
        computeBox(raw->courses[i], &raw->boxes[4 * i]);
        raw->ends[2 * i] = e.drain.x;
        raw->ends[2 * i + 1] = e.drain.y;
        raw->drains[i] = e.drainEdgeIdx;
    }
    for (size_t i = 0; i < n; ++i) {
        int d = int(i);
        while (d >= 0 && d < int(n)) {
            raw->reaches[d]++;
            d = raw->drains[d];
        }
    }
    return raw;
}

std::shared_ptr<MainRiversEngine::RawRegion> MainRiversEngine::getOrGrow(int rx, int rz) {
    int64_t key = packRegion(rx, rz);
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            auto it = grown_.find(key);
            if (it != grown_.end()) return it->second;
            if (growingRegions_.find(key) == growingRegions_.end()) {
                growingRegions_.insert(key);
                break;
            }
            cv_.wait(lock);
        }
    }
    bool committed = false;
    RiverScopeExit guard{[&]() {
        if (!committed) {
            std::lock_guard<std::mutex> lock(mutex_);
            growingRegions_.erase(key);
            cv_.notify_all();
        }
    }};
    auto built = growRegion(rx, rz);
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = grown_.try_emplace(key, std::move(built));
    growingRegions_.erase(key);
    committed = true;
    cv_.notify_all();
    return it->second;
}

static bool yieldsToAround(const MainRiversEngine::RawRegion* own, int i, const MainRiversEngine::RawRegion* const around[], int aroundCount) {
    double mx = own->middles[2 * i], mz = own->middles[2 * i + 1];
    int width = own->widths[i], reach = own->reaches[i];
    for (int a = 0; a < aroundCount; ++a) {
        const auto* n = around[a];
        for (size_t j = 0; j < n->widths.size(); ++j) {
            double room = double(n->widths[j] + width + RiverGraphEdge::MIN_WIDTH) * NEIGHBOUR_ROOM;
            double nx = n->middles[2 * j], nz = n->middles[2 * j + 1];
            bool meets = (inBox(n->boxes.data(), 4 * int(j), mx, mz, room) && segDistSq(n->courses[j], mx, mz) < room * room)
                      || (inBox(own->boxes.data(), 4 * i, nx, nz, room) && segDistSq(own->courses[i], nx, nz) < room * room);
            if (!meets) continue;
            if (n->reaches[j] != reach) {
                if (n->reaches[j] > reach) return true;
                continue;
            }
            if (n->widths[j] != width) {
                if (n->widths[j] > width) return true;
                continue;
            }
            if (n->rx < own->rx || (n->rx == own->rx && n->rz < own->rz)) return true;
        }
    }
    return false;
}

std::vector<RiverGraphEdge> MainRiversEngine::generateRegion(int rx, int rz, const RawRegion& own, const RawRegion* around[8]) {
    std::vector<RiverGraphEdge> edges = own.edges; // copy Raw's edges since own stays immutable in cache
    int yielded = 0;
    std::vector<uint8_t> gone(edges.size(), 0);
    for (int i = int(edges.size()) - 1; i >= 0; --i) {
        if (yieldsToAround(&own, i, around, 8)) {
            gone[i] = 1;
            ++yielded;
        }
    }
    bool pruned = true;
    while (pruned) {
        pruned = false;
        for (size_t i = 0; i < edges.size(); ++i) {
            if (!gone[i] && edges[i].drainEdgeIdx >= 0 && gone[edges[i].drainEdgeIdx]) {
                gone[i] = 1;
                pruned = true;
            }
        }
    }
    std::vector<RiverGraphEdge> liveEdges;
    liveEdges.reserve(edges.size());
    for (size_t i = 0; i < edges.size(); ++i) {
        if (!gone[i]) liveEdges.push_back(std::move(edges[i]));
    }
    if (yielded > 0) {
        for (auto& e : liveEdges) {
            e.drainEdgeIdx = -1;
            e.sourceEdge = false;
            e.width = RiverGraphEdge::MIN_WIDTH;
        }
    }
    if (!liveEdges.empty()) annotateNetwork(liveEdges);

    std::vector<const std::vector<double>*> beside;
    std::vector<int> besideWidths;
    std::vector<double> besideMouths;
    const RawRegion* selfArr[1] = {&own};
    for (int a = 0; a < 8; ++a) {
        const auto* n = around[a];
        size_t m = n->widths.size();
        std::vector<uint8_t> ngone(m, 0);
        for (size_t j = 0; j < m; ++j) ngone[j] = yieldsToAround(n, int(j), selfArr, 1) ? 1 : 0;
        for (bool more = true; more;) {
            more = false;
            for (size_t j = 0; j < m; ++j) {
                if (!ngone[j] && n->drains[j] >= 0 && ngone[n->drains[j]]) {
                    ngone[j] = 1; more = true;
                }
            }
        }
        for (size_t j = 0; j < m; ++j) {
            if (ngone[j]) continue;
            beside.push_back(&n->courses[j]);
            besideWidths.push_back(n->widths[j]);
            if (n->drains[j] < 0 && n->widths[j] >= shaper::DELTA_MIN_WIDTH) {
                besideMouths.push_back(n->ends[2 * j]);
                besideMouths.push_back(n->ends[2 * j + 1]);
            }
        }
    }

    int64_t rSeed = (seed_ + int64_t(rx) * 341873128712ll + int64_t(rz) * 132897987541ll) ^ 84213987123ll;
    shaper::LandQuery land{
        [&own](double x, double z) { return own.isLand(int(javaRound(x)), int(javaRound(z))); },
        [this](double x, double z) -> double {
            int cx = floorDiv(int32_t(javaRound(x)), 8);
            int cz = floorDiv(int32_t(javaRound(z)), 8);
            return double(cb_.coarseHeightCell(cx, cz) - cb_.seaLevel);
        }
    };
    return shaper::shapeRegion(std::move(liveEdges), rSeed, land, beside, besideWidths, besideMouths, cfg_.deltaInlandReach);
}

std::shared_ptr<const std::vector<RiverGraphEdge>> MainRiversEngine::getRegion(int rx, int rz) {
    if (!cfg_.enabled || cfg_.normalRiverMode != NormalRiverMode::DYNAMIC) return nullptr;
    int64_t key = packRegion(rx, rz);
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            auto it = regions_.find(key);
            if (it != regions_.end()) return it->second;
            if (buildingRegions_.find(key) == buildingRegions_.end()) {
                buildingRegions_.insert(key);
                break;
            }
            cv_.wait(lock);
        }
    }
    bool committed = false;
    RiverScopeExit guard{[&]() {
        if (!committed) {
            std::lock_guard<std::mutex> lock(mutex_);
            buildingRegions_.erase(key);
            cv_.notify_all();
        }
    }};

    auto own = getOrGrow(rx, rz);
    const RawRegion* aroundPtrs[8]{};
    std::shared_ptr<RawRegion> aroundHolders[8];
    int idx = 0;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx != 0 || dz != 0) {
                aroundHolders[idx] = getOrGrow(rx + dx, rz + dz);
                aroundPtrs[idx] = aroundHolders[idx].get();
                ++idx;
            }
        }
    }

    auto edgesVec = std::make_shared<std::vector<RiverGraphEdge>>(generateRegion(rx, rz, *own, aroundPtrs));
    std::unordered_map<int64_t, std::vector<PartitionEntry>> toAdd;
    for (int ei = 0; ei < int(edgesVec->size()); ++ei) {
        const auto& e = (*edgesVec)[ei];
        for (int px = e.minPartX; px <= e.maxPartX; ++px) {
            for (int pz = e.minPartZ; pz <= e.maxPartZ; ++pz) {
                int64_t pk = (int64_t(uint32_t(px))) | (int64_t(uint32_t(pz)) << 32);
                toAdd[pk].push_back({edgesVec, ei});
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto [regIt, inserted] = regions_.try_emplace(key, edgesVec);
    if (inserted) {
        for (const auto& [pk, entries] : toAdd) {
            auto pit = partition_.find(pk);
            auto merged = std::make_shared<std::vector<PartitionEntry>>();
            if (pit != partition_.end() && pit->second) {
                merged->reserve(pit->second->size() + entries.size());
                *merged = *pit->second;
            } else {
                merged->reserve(entries.size());
            }
            merged->insert(merged->end(), entries.begin(), entries.end());
            partition_[pk] = std::move(merged);
        }
        epoch_.store(gRiverEpoch.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_release);
    }
    buildingRegions_.erase(key);
    committed = true;
    cv_.notify_all();
    return regIt->second;
}

void MainRiversEngine::prewarm(int minBlockX, int minBlockZ, int maxBlockX, int maxBlockZ) {
    if (!cfg_.enabled || cfg_.normalRiverMode != NormalRiverMode::DYNAMIC) return;
    const int minRx = floorDiv(minBlockX, REGION_SIZE) - QUERY_RADIUS;
    const int maxRx = floorDiv(maxBlockX, REGION_SIZE) + QUERY_RADIUS;
    const int minRz = floorDiv(minBlockZ, REGION_SIZE) - QUERY_RADIUS;
    const int maxRz = floorDiv(maxBlockZ, REGION_SIZE) + QUERY_RADIUS;

    std::vector<std::pair<int, int>> missingGrow;
    std::vector<std::pair<int, int>> missingReg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int rz = minRz - 1; rz <= maxRz + 1; ++rz) {
            for (int rx = minRx - 1; rx <= maxRx + 1; ++rx) {
                if (grown_.find(packRegion(rx, rz)) == grown_.end()) {
                    missingGrow.emplace_back(rx, rz);
                }
            }
        }
        for (int rz = minRz; rz <= maxRz; ++rz) {
            for (int rx = minRx; rx <= maxRx; ++rx) {
                if (regions_.find(packRegion(rx, rz)) == regions_.end()) {
                    missingReg.emplace_back(rx, rz);
                }
            }
        }
    }

    const unsigned nThreads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    if (!missingGrow.empty()) {
        std::atomic<size_t> next{0};
        const unsigned workersCount = std::min<unsigned>(nThreads, unsigned(missingGrow.size()));
        std::vector<std::thread> workers;
        workers.reserve(workersCount);
        for (unsigned t = 0; t < workersCount; ++t) {
            workers.emplace_back([&]() {
                for (;;) {
                    size_t i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= missingGrow.size()) break;
                    getOrGrow(missingGrow[i].first, missingGrow[i].second);
                }
            });
        }
        for (auto& w : workers) w.join();
    }
    if (!missingReg.empty()) {
        std::atomic<size_t> next{0};
        const unsigned workersCount = std::min<unsigned>(nThreads, unsigned(missingReg.size()));
        std::vector<std::thread> workers;
        workers.reserve(workersCount);
        for (unsigned t = 0; t < workersCount; ++t) {
            workers.emplace_back([&]() {
                for (;;) {
                    size_t i = next.fetch_add(1, std::memory_order_relaxed);
                    if (i >= missingReg.size()) break;
                    getRegion(missingReg[i].first, missingReg[i].second);
                }
            });
        }
        for (auto& w : workers) w.join();
    }
}

namespace {
struct TlsHomeRegionCache {
    const MainRiversEngine* owner = nullptr;
    uint64_t resetEpoch = 0;
    int homeRx = 0;
    int homeRz = 0;
};

struct TlsNearbyEdgesSlot {
    const MainRiversEngine* owner = nullptr;
    uint64_t epoch = 0;
    int cx0 = 0;
    int cz0 = 0;
    bool valid = false;
    std::array<std::shared_ptr<const std::vector<MainRiversEngine::PartitionEntry>>, 9> cells{};
    std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>> edges;
};

template <typename EnsureFn>
void ensureHomeWindowTls(const MainRiversEngine* eng, uint64_t resetEpoch, int homeRx, int homeRz, EnsureFn&& ensureFn) {
    thread_local std::array<TlsHomeRegionCache, 16> tls{};
    const size_t slotIdx = (uint64_t(uint32_t(homeRx)) ^ (uint64_t(uint32_t(homeRz)) * 0x9E3779B97F4A7C15ull)) & 15u;
    auto& slot = tls[slotIdx];
    if (slot.owner == eng && slot.resetEpoch == resetEpoch && slot.homeRx == homeRx && slot.homeRz == homeRz) {
        return;
    }
    ensureFn(homeRx, homeRz);
    slot.owner = eng;
    slot.resetEpoch = resetEpoch;
    slot.homeRx = homeRx;
    slot.homeRz = homeRz;
}
} // namespace

uint64_t MainRiversEngine::fillNearbyPartitionCells(
    int cx0, int cz0,
    std::array<std::shared_ptr<const std::vector<PartitionEntry>>, 9>& outCells,
    std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>>& outEdges) {
    uint64_t liveEpoch = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        liveEpoch = epoch_.load(std::memory_order_acquire);
        size_t ci = 0;
        for (int dxCell = -1; dxCell <= 1; ++dxCell) {
            for (int dzCell = -1; dzCell <= 1; ++dzCell) {
                int cx = cx0 + dxCell, cz = cz0 + dzCell;
                int64_t pk = (int64_t(uint32_t(cx))) | (int64_t(uint32_t(cz)) << 32);
                auto it = partition_.find(pk);
                outCells[ci++] = (it != partition_.end()) ? it->second : nullptr;
            }
        }
    }
    outEdges.clear();
    for (const auto& cell : outCells) {
        if (!cell) continue;
        for (const auto& pe : *cell) {
            const auto* vecPtr = pe.edges.get();
            bool dup = false;
            for (const auto& ex : outEdges) {
                if (ex.first == vecPtr && ex.second == pe.idx) { dup = true; break; }
            }
            if (!dup) outEdges.emplace_back(vecPtr, pe.idx);
        }
    }
    return liveEpoch;
}

RiverContext MainRiversEngine::findNearest(double x, double z) {
    RiverContext ctx;
    if (!cfg_.enabled || cfg_.normalRiverMode != NormalRiverMode::DYNAMIC) return ctx;
    int bx = int(mthFloor(x)), bz = int(mthFloor(z));
    int homeRx = floorDiv(bx, REGION_SIZE), homeRz = floorDiv(bz, REGION_SIZE);
    const uint64_t curReset = resetEpoch_.load(std::memory_order_acquire);
    ensureHomeWindowTls(this, curReset, homeRx, homeRz, [&](int hrx, int hrz) {
        for (int drx = -QUERY_RADIUS; drx <= QUERY_RADIUS; ++drx) {
            for (int drz = -QUERY_RADIUS; drz <= QUERY_RADIUS; ++drz) {
                getRegion(hrx + drx, hrz + drz);
            }
        }
    });

    const int cx0 = bx >> 8, cz0 = bz >> 8;
    const uint64_t curEpoch = epoch_.load(std::memory_order_acquire);
    thread_local std::array<TlsNearbyEdgesSlot, 64> nearbyTls{};
    const size_t slotIdx = (uint64_t(uint32_t(cx0)) ^ (uint64_t(uint32_t(cz0)) * 0x9E3779B97F4A7C15ull)) & 63u;
    auto& slot = nearbyTls[slotIdx];
    if (slot.owner != this || !slot.valid || slot.epoch != curEpoch || slot.cx0 != cx0 || slot.cz0 != cz0) {
        slot.owner = this;
        slot.cx0 = cx0;
        slot.cz0 = cz0;
        slot.epoch = fillNearbyPartitionCells(cx0, cz0, slot.cells, slot.edges);
        slot.valid = true;
    }

    const std::vector<RiverGraphEdge>* bestEdges = nullptr;
    int bestIdx = -1;
    double bestDistSq = std::numeric_limits<double>::max();

    for (const auto& pr : slot.edges) {
        const auto& edge = (*pr.first)[pr.second];
        if (edge.fractal.boxDistanceSq(x, z) >= bestDistSq) continue;
        double dsq = edge.fractal.intersectDistance(x, z);
        if (dsq < bestDistSq) {
            bestDistSq = dsq;
            bestEdges = pr.first;
            bestIdx = pr.second;
        }
    }
    if (!bestEdges || bestIdx < 0) return ctx;
    const auto& nearest = (*bestEdges)[bestIdx];
    ctx.valid = true;
    ctx.regionEdges = bestEdges;
    ctx.edgeIdx = bestIdx;
    ctx.flow = nearest.fractal.calculateFlow(x, z);
    nearest.fractal.calculateContinuousFlow(x, z, ctx.flowVecX, ctx.flowVecZ);
    ctx.distSq = bestDistSq;
    ctx.widthSq = nearest.widthSq(x, z, *bestEdges);
    return ctx;
}

void MainRiversEngine::collectNearbyEdges(double x, double z, std::vector<std::pair<const std::vector<RiverGraphEdge>*, int>>& out) {
    out.clear();
    if (!cfg_.enabled || cfg_.normalRiverMode != NormalRiverMode::DYNAMIC) return;
    int bx = int(mthFloor(x)), bz = int(mthFloor(z));
    int homeRx = floorDiv(bx, REGION_SIZE), homeRz = floorDiv(bz, REGION_SIZE);
    const uint64_t curReset = resetEpoch_.load(std::memory_order_acquire);
    ensureHomeWindowTls(this, curReset, homeRx, homeRz, [&](int hrx, int hrz) {
        for (int drx = -QUERY_RADIUS; drx <= QUERY_RADIUS; ++drx) {
            for (int drz = -QUERY_RADIUS; drz <= QUERY_RADIUS; ++drz) {
                getRegion(hrx + drx, hrz + drz);
            }
        }
    });

    const int cx0 = bx >> 8, cz0 = bz >> 8;
    const uint64_t curEpoch = epoch_.load(std::memory_order_acquire);
    thread_local std::array<TlsNearbyEdgesSlot, 64> nearbyTls{};
    const size_t slotIdx = (uint64_t(uint32_t(cx0)) ^ (uint64_t(uint32_t(cz0)) * 0x9E3779B97F4A7C15ull)) & 63u;
    auto& slot = nearbyTls[slotIdx];
    if (slot.owner != this || !slot.valid || slot.epoch != curEpoch || slot.cx0 != cx0 || slot.cz0 != cz0) {
        slot.owner = this;
        slot.cx0 = cx0;
        slot.cz0 = cz0;
        slot.epoch = fillNearbyPartitionCells(cx0, cz0, slot.cells, slot.edges);
        slot.valid = true;
    }
    out.assign(slot.edges.begin(), slot.edges.end());
}

} // namespace dw
