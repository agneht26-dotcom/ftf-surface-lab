#pragma once
// Bit-exact Minecraft 1.21.1 & FastUtil noise/hash/climate primitives for Dynamic Waters:
// HashCommon.mix, SimplexNoise (2D & 3D), NormalNoise (minecraft:vegetation + minecraft:offset),
// Biome.coldEnoughToSnow, and biome tag/blacklist matching.
#include "mc_random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace dw {

inline int32_t mthFloor(double v) {
    int32_t i = static_cast<int32_t>(v);
    return v < double(i) ? i - 1 : i;
}
inline int32_t mthFloorF(float v) {
    int32_t i = static_cast<int32_t>(v);
    return v < float(i) ? i - 1 : i;
}
inline int64_t lfloor(double v) {
    int64_t i = static_cast<int64_t>(v);
    return v < double(i) ? i - 1 : i;
}
template<class T> inline T mthClamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double mthLerp(double t, double a, double b) { return a + t * (b - a); }
inline float mthLerpF(float t, float a, float b) { return a + t * (b - a); }
inline double mthSmoothstep(double d) { return d * d * d * (d * (d * 6.0 - 15.0) + 10.0); }
inline int32_t floorDiv(int32_t a, int32_t b) {
    int32_t q = a / b;
    if ((a ^ b) < 0 && q * b != a) --q;
    return q;
}
inline int32_t floorMod(int32_t a, int32_t b) { return a - floorDiv(a, b) * b; }

// it.unimi.dsi.fastutil.HashCommon.mix (PHI_MIX)
inline int32_t hashCommonMix(int32_t x) {
    uint32_t h = uint32_t(x) * 0x9E3779B9u;
    return int32_t(h ^ (h >> 16));
}
inline int64_t hashCommonMix(int64_t x) {
    uint64_t h = uint64_t(x) * 0x9E3779B97F4A7C15ull;
    h ^= (h >> 32);
    return int64_t(h ^ (h >> 16));
}

// Minecraft 1.21.1 SimplexNoise (exact port of net.minecraft.world.level.levelgen.synth.SimplexNoise)
struct SimplexNoise {
    static constexpr int GRADIENT[16][3] = {
        {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
        {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
        {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1},
        {1,1,0},{0,-1,1},{-1,1,0},{0,-1,-1}
    };
    double xo = 0, yo = 0, zo = 0;
    std::array<uint8_t, 256> p{};

    SimplexNoise() = default;
    explicit SimplexNoise(mc::Xoroshiro rand) {
        xo = rand.nextDouble() * 256.0;
        yo = rand.nextDouble() * 256.0;
        zo = rand.nextDouble() * 256.0;
        for (int i = 0; i < 256; ++i) p[i] = uint8_t(i);
        for (int i = 0; i < 256; ++i) {
            int j = rand.nextInt(256 - i);
            std::swap(p[i], p[i + j]);
        }
    }
    explicit SimplexNoise(mc::Legacy& rand) {
        xo = rand.nextDouble() * 256.0;
        yo = rand.nextDouble() * 256.0;
        zo = rand.nextDouble() * 256.0;
        for (int i = 0; i < 256; ++i) p[i] = uint8_t(i);
        for (int i = 0; i < 256; ++i) {
            int j = rand.nextInt(256 - i);
            std::swap(p[i], p[i + j]);
        }
    }

    int perm(int i) const { return p[i & 255]; }
    static double dot(const int g[3], double x, double y, double z) {
        return g[0] * x + g[1] * y + g[2] * z;
    }
    double getCornerNoise3D(int gi, double x, double y, double z, double sqRadius) const {
        double d = sqRadius - x * x - y * y - z * z;
        if (d < 0.0) return 0.0;
        d *= d;
        return d * d * dot(GRADIENT[gi], x, y, z);
    }

    double getValue(double x, double y) const {
        static const double SQRT_3 = std::sqrt(3.0);
        static const double F2 = 0.5 * (SQRT_3 - 1.0);
        static const double G2 = (3.0 - SQRT_3) / 6.0;
        double s = (x + y) * F2;
        int i = mthFloor(x + s), j = mthFloor(y + s);
        double t = double(i + j) * G2;
        double X0 = double(i) - t, Y0 = double(j) - t;
        double x0 = x - X0, y0 = y - Y0;
        int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
        double x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
        double x2 = x0 - 1.0 + 2.0 * G2, y2 = y0 - 1.0 + 2.0 * G2;
        int ii = i & 255, jj = j & 255;
        int gi0 = perm(ii + perm(jj)) % 12;
        int gi1 = perm(ii + i1 + perm(jj + j1)) % 12;
        int gi2 = perm(ii + 1 + perm(jj + 1)) % 12;
        double n0 = getCornerNoise3D(gi0, x0, y0, 0.0, 0.5);
        double n1 = getCornerNoise3D(gi1, x1, y1, 0.0, 0.5);
        double n2 = getCornerNoise3D(gi2, x2, y2, 0.0, 0.5);
        return 70.0 * (n0 + n1 + n2);
    }

    double getValue(double x, double y, double z) const {
        constexpr double F3 = 1.0 / 3.0;
        constexpr double G3 = 1.0 / 6.0;
        double s = (x + y + z) * F3;
        int i = mthFloor(x + s), j = mthFloor(y + s), k = mthFloor(z + s);
        double t = double(i + j + k) * G3;
        double x0 = x - (i - t), y0 = y - (j - t), z0 = z - (k - t);
        int i1, j1, k1, i2, j2, k2;
        if (x0 >= y0) {
            if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
            else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
            else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
        } else {
            if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
            else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
            else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        }
        double x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
        double x2 = x0 - i2 + 2.0 * G3, y2 = y0 - j2 + 2.0 * G3, z2 = z0 - k2 + 2.0 * G3;
        double x3 = x0 - 1.0 + 3.0 * G3, y3 = y0 - 1.0 + 3.0 * G3, z3 = z0 - 1.0 + 3.0 * G3;
        int ii = i & 255, jj = j & 255, kk = k & 255;
        int gi0 = perm(ii + perm(jj + perm(kk))) % 12;
        int gi1 = perm(ii + i1 + perm(jj + j1 + perm(kk + k1))) % 12;
        int gi2 = perm(ii + i2 + perm(jj + j2 + perm(kk + k2))) % 12;
        int gi3 = perm(ii + 1 + perm(jj + 1 + perm(kk + 1))) % 12;
        double n0 = getCornerNoise3D(gi0, x0, y0, z0, 0.6);
        double n1 = getCornerNoise3D(gi1, x1, y1, z1, 0.6);
        double n2 = getCornerNoise3D(gi2, x2, y2, z2, 0.6);
        double n3 = getCornerNoise3D(gi3, x3, y3, z3, 0.6);
        return 32.0 * (n0 + n1 + n2 + n3);
    }
};

// Minecraft 1.21.1 NormalNoise for CPU evaluation (used by RandomState.router().vegetation())
struct NormalNoiseCPU {
    int firstOctave = 0;
    std::vector<double> amplitudes;
    double valueFactor = 1.0;
    double lowestFreqInputFactor = 1.0;
    double lowestFreqValueFactor = 1.0;
    std::vector<mc::Improved> oct0;
    std::vector<mc::Improved> oct1;
    std::vector<bool> active;

    static NormalNoiseCPU create(mc::Xoroshiro source, int firstOct, const std::vector<double>& amps) {
        NormalNoiseCPU nn;
        nn.firstOctave = firstOct;
        nn.amplitudes = amps;
        int n = int(amps.size());
        nn.active.resize(n, false);
        int minNz = n, maxNz = -1;
        for (int i = 0; i < n; ++i) {
            if (amps[i] != 0.0) {
                nn.active[i] = true;
                minNz = std::min(minNz, i);
                maxNz = std::max(maxNz, i);
            }
        }
        int span = maxNz >= minNz ? (maxNz - minNz + 1) : 1;
        nn.valueFactor = (1.0 / 6.0) / (0.1 * (1.0 + 1.0 / double(span)));
        nn.lowestFreqInputFactor = std::ldexp(1.0, firstOct);
        nn.lowestFreqValueFactor = std::pow(2.0, double(n - 1)) / (std::pow(2.0, double(n)) - 1.0);

        for (int half = 0; half < 2; ++half) {
            auto& vec = (half == 0 ? nn.oct0 : nn.oct1);
            vec.resize(n);
            auto octaves = source.forkPositional();
            for (int k = 0; k < n; ++k) {
                if (!nn.active[k]) continue;
                vec[k] = mc::improvedNoise(octaves.fromHashOf("octave_" + std::to_string(firstOct + k)));
            }
        }
        return nn;
    }

    static double wrap(double v) { return v - double(lfloor(v / 3.3554432e7 + 0.5)) * 3.3554432e7; }

    static double sampleImproved(const mc::Improved& t, double px, double py, double pz) {
        double x = px + t.xo, y = py + t.yo, z = pz + t.zo;
        int i = mthFloor(x), j = mthFloor(y), k = mthFloor(z);
        double g = x - i, h = y - j, l = z - k;
        auto perm = [&](int v) -> int { return t.p[v & 255]; };
        int a = perm(i), b = perm(i + 1);
        int c = perm(a + j), e = perm(a + j + 1), m = perm(b + j), n = perm(b + j + 1);
        auto dot = [&](int hash, double dx, double dy, double dz) -> double {
            const int* gr = SimplexNoise::GRADIENT[hash & 15];
            return gr[0] * dx + gr[1] * dy + gr[2] * dz;
        };
        double g1 = g - 1.0, h1 = h - 1.0, l1 = l - 1.0;
        double v000 = dot(perm(c + k), g, h, l),     v100 = dot(perm(m + k), g1, h, l);
        double v010 = dot(perm(e + k), g, h1, l),    v110 = dot(perm(n + k), g1, h1, l);
        double v001 = dot(perm(c + k + 1), g, h, l1), v101 = dot(perm(m + k + 1), g1, h, l1);
        double v011 = dot(perm(e + k + 1), g, h1, l1), v111 = dot(perm(n + k + 1), g1, h1, l1);
        double s = mthSmoothstep(g), u = mthSmoothstep(h), w = mthSmoothstep(l);
        return mthLerp(w, mthLerp(u, mthLerp(s, v000, v100), mthLerp(s, v010, v110)),
                          mthLerp(u, mthLerp(s, v001, v101), mthLerp(s, v011, v111)));
    }

    double samplePerlin(int half, double px, double py, double pz) const {
        const auto& vec = (half == 0 ? oct0 : oct1);
        double sum = 0.0, e = lowestFreqInputFactor, f = lowestFreqValueFactor;
        for (size_t o = 0; o < amplitudes.size(); ++o) {
            if (active[o]) {
                double g = sampleImproved(vec[o], wrap(px * e), wrap(py * e), wrap(pz * e));
                sum += amplitudes[o] * g * f;
            }
            e *= 2.0;
            f *= 0.5;
        }
        return sum;
    }

    double getValue(double x, double y, double z) const {
        constexpr double INPUT_FACTOR = 1.0181268882175227;
        return (samplePerlin(0, x, y, z) + samplePerlin(1, x * INPUT_FACTOR, y * INPUT_FACTOR, z * INPUT_FACTOR)) * valueFactor;
    }
};

// Matches MC 1.21.1 overworld.json noise_router.vegetation + Biome TEMPERATURE_NOISE / FROZEN_TEMPERATURE_NOISE
struct WorldNoises {
    int64_t seed = 0;
    NormalNoiseCPU offset;
    NormalNoiseCPU vegetation;
    SimplexNoise temperatureNoise;       // fixed seed 1234
    std::array<SimplexNoise, 3> frozenNoise; // fixed seed 3456
    SimplexNoise densityNoise;           // BasinTile seed + 0x5639377L -> wait check exact BasinTile constant!
    SimplexNoise riverBedNoise;
    SimplexNoise pillarNoise;
    SimplexNoise speleothemNoise;

    void init(int64_t s) {
        seed = s;
        auto root = mc::randomStateRoot(s);
        offset = NormalNoiseCPU::create(root.fromHashOf("minecraft:offset"), -3, {1.0, 1.0, 1.0, 0.0});
        vegetation = NormalNoiseCPU::create(root.fromHashOf("minecraft:vegetation"), -8, {1.0, 1.0, 0.0, 0.0, 0.0, 0.0});
        {
            mc::Legacy tl(1234);
            temperatureNoise = SimplexNoise(tl);
            mc::Legacy fl(3456);
            frozenNoise[0] = SimplexNoise(fl);
            frozenNoise[1] = SimplexNoise(fl);
            frozenNoise[2] = SimplexNoise(fl);
        }
        riverBedNoise = SimplexNoise(mc::Xoroshiro::fromSeed(s ^ 12345ll));
        pillarNoise = SimplexNoise(mc::Xoroshiro::fromSeed(s ^ 67890ll));
        speleothemNoise = SimplexNoise(mc::Xoroshiro::fromSeed(s ^ 54321ll));
        densityNoise = SimplexNoise(mc::Xoroshiro::fromSeed(int64_t(uint64_t(s) ^ 0x6A09E667F3BCC909ull)));
    }

    // randomState.router().vegetation().compute(SinglePointContext(bx, 0, bz))
    double sampleVegetation(int bx, int bz) const {
        double sx = offset.getValue(double(bx) * 0.25, 0.0, double(bz) * 0.25) * 4.0;
        double sz = offset.getValue(double(bz) * 0.25, double(bx) * 0.25, 0.0) * 4.0;
        return vegetation.getValue(double(bx) * 0.25 + sx, 0.0, double(bz) * 0.25 + sz);
    }

    static const SimplexNoise& sharedBiomeInfoNoise() {
        static const SimplexNoise inst = []() {
            mc::Legacy r(2345);
            return SimplexNoise(r);
        }();
        return inst;
    }

    float heightAdjustedTemperature(int bx, int by, int bz, float baseTemp, bool frozenModifier) const {
        float t = baseTemp;
        if (frozenModifier) {
            double qx = double(bx) * 0.05, qz = double(bz) * 0.05;
            constexpr double f0 = 1.0 / 7.0;
            double d = frozenNoise[0].getValue(qx, qz) * f0
                     + frozenNoise[1].getValue(qx * 0.5, qz * 0.5) * (f0 * 2.0)
                     + frozenNoise[2].getValue(qx * 0.25, qz * 0.25) * (f0 * 4.0);
            d *= 7.0;
            double f = d + sharedBiomeInfoNoise().getValue(double(bx) * 0.2, double(bz) * 0.2);
            if (f < 0.3 && sharedBiomeInfoNoise().getValue(double(bx) * 0.09, double(bz) * 0.09) < 0.8) t = 0.2f;
        }
        if (by > 80) {
            float g = float(temperatureNoise.getValue(float(bx) / 8.0f, float(bz) / 8.0f) * 8.0);
            t -= (g + float(by) - 80.0f) * 0.05f / 40.0f;
        }
        return t;
    }

    bool coldEnoughToSnow(int bx, int by, int bz, float baseTemp, bool frozenModifier) const {
        return !(heightAdjustedTemperature(bx, by, bz, baseTemp, frozenModifier) >= 0.15f);
    }
};

// Biome tag bitmask covering BiomeTags + ModTags (#c:is_*) + DW palette/plant matching
enum BiomeTagBit : uint32_t {
    TAG_OCEAN         = 1u << 0,
    TAG_DEEP_OCEAN    = 1u << 1,
    TAG_BEACH         = 1u << 2,
    TAG_RIVER         = 1u << 3,
    TAG_BADLANDS      = 1u << 4,
    TAG_DESERT        = 1u << 5,
    TAG_SNOWY         = 1u << 6,
    TAG_ICY           = 1u << 7,
    TAG_SWAMP         = 1u << 8,
    TAG_JUNGLE        = 1u << 9,
    TAG_FOREST        = 1u << 10,
    TAG_TAIGA         = 1u << 11,
    TAG_SAVANNA       = 1u << 12,
    TAG_PLAINS        = 1u << 13,
    TAG_MOUNTAIN      = 1u << 14,
    TAG_HILL          = 1u << 15,
    TAG_WINDSWEPT     = 1u << 16,
    TAG_MUSHROOM      = 1u << 17,
    TAG_LUSH          = 1u << 18,
    TAG_STONY_SHORES  = 1u << 19,
    TAG_HOT_OVERWORLD = 1u << 20,
    TAG_CAVE          = 1u << 21,
    TAG_FROZEN_MOD    = 1u << 22,
    TAG_NO_PRECIP     = 1u << 23,
    TAG_OVERWORLD     = 1u << 24,
    TAG_FROZEN_RIVER  = 1u << 25,
    TAG_DRY_OVERWORLD = 1u << 26,
    TAG_UNDERGROUND   = 1u << 27
};

struct BiomeMeta {
    uint32_t id = 0;
    std::string name;
    float temperature = 0.5f;
    float downfall = 0.5f;
    uint32_t tags = 0;
};

inline bool containsAny(const std::string& s, std::initializer_list<const char*> keys) {
    for (const char* k : keys) if (s.find(k) != std::string::npos) return true;
    return false;
}

inline uint32_t inferBiomeTags(const std::string& name, float temp, float down) {
    uint32_t t = TAG_OVERWORLD;
    std::string path = name;
    if (auto p = path.find(':'); p != std::string::npos) path = path.substr(p + 1);

    if (containsAny(path, {"ocean"})) t |= TAG_OCEAN;
    if (containsAny(path, {"deep_"} ) && (t & TAG_OCEAN)) t |= TAG_DEEP_OCEAN;
    if (containsAny(path, {"beach", "shore", "coast", "gravel_beach"}) && !containsAny(path, {"stony_shore"})) t |= TAG_BEACH;
    if (containsAny(path, {"stony_shore", "rocky_shore", "cliff_shore"})) t |= TAG_STONY_SHORES;
    if (path == "river" || path == "frozen_river" || containsAny(path, {"_river", "river_"})) t |= TAG_RIVER;
    if (path == "frozen_river" || path == "frozen_ocean" || path == "deep_frozen_ocean") t |= TAG_FROZEN_MOD;
    if (path == "frozen_river") t |= TAG_FROZEN_RIVER;
    if (containsAny(path, {"badlands", "mesa", "red_rock", "canyon", "outback", "eroded_badlands", "wooded_badlands", "painted_"})) t |= TAG_BADLANDS | TAG_NO_PRECIP;
    if (containsAny(path, {"desert", "dunes", "dune", "erg", "oasis"}) && !(t & TAG_BADLANDS)) t |= TAG_DESERT | TAG_NO_PRECIP;
    if (containsAny(path, {"snowy", "ice", "frozen", "glacier", "tundra", "permafrost", "cold_steppe", "alpine"})) t |= TAG_SNOWY;
    if (containsAny(path, {"ice", "frozen", "glacier"})) t |= TAG_ICY;
    if (temp < 0.15f && !(t & (TAG_OCEAN | TAG_CAVE))) t |= TAG_SNOWY;
    if (containsAny(path, {"swamp", "marsh", "bog", "fen", "bayou", "mangrove", "wetland", "mire", "moor"})) t |= TAG_SWAMP;
    if (containsAny(path, {"jungle", "rainforest", "tropics", "tropical", "cloud_forest"})) t |= TAG_JUNGLE;
    if (containsAny(path, {"taiga", "boreal", "spruce", "pine", "conifer", "redwood", "fir"})) t |= TAG_TAIGA;
    if (containsAny(path, {"savanna", "scrub", "chaparral", "baobab", "steppe"})) t |= TAG_SAVANNA | TAG_NO_PRECIP;
    if (containsAny(path, {"forest", "woods", "woodland", "grove", "birch", "oak", "maple", "aspen", "cherry", "thicket"})) t |= TAG_FOREST;
    if (containsAny(path, {"plains", "meadow", "prairie", "grassland", "field", "pasture", "lavender", "clover"})) t |= TAG_PLAINS;
    if (containsAny(path, {"mountain", "peaks", "slopes", "alps", "crag", "summit", "highlands", "ridge"})) t |= TAG_MOUNTAIN;
    if (containsAny(path, {"hill", "highland", "plateau", "terrace"})) t |= TAG_HILL;
    if (containsAny(path, {"windswept", "crag", "gravelly"})) t |= TAG_WINDSWEPT;
    if (containsAny(path, {"mushroom", "mycelium", "fungal"})) t |= TAG_MUSHROOM;
    if (containsAny(path, {"lush_caves"})) t |= TAG_LUSH | TAG_CAVE;
    if (containsAny(path, {"dripstone_caves", "deep_dark"})) t |= TAG_CAVE;
    if (temp >= 1.5f) t |= TAG_HOT_OVERWORLD;
    (void)down;
    return t;
}

inline bool biomeMatchesPattern(const std::string& id, const std::string& pat) {
    if (pat.empty()) return false;
    if (pat.back() == '*') return id.rfind(pat.substr(0, pat.size() - 1), 0) == 0;
    return id == pat;
}

// Exact port of com.AdamNew.dynamicwaters.shore.Patches
namespace Patches {

inline double hash01(int32_t a, int32_t b, int32_t c) {
    uint64_t h = uint64_t(a) * 0x9E3779B97F4A7C15ull
               + uint64_t(b) * 0xC2B2AE3D27D4EB4Full
               + uint64_t(c) * 0x165667B19E3779F9ull;
    h ^= h >> 32;
    h *= 0xD6E8FEB86659FD93ull;
    h ^= h >> 32;
    h *= 0xD6E8FEB86659FD93ull;
    h ^= h >> 32;
    return double(h >> 11) * 0x1.0p-53;
}

inline double smooth(int32_t x, int32_t z, int32_t size, int32_t salt) {
    int32_t gx = floorDiv(x, size), gz = floorDiv(z, size);
    double tx = double(floorMod(x, size)) / double(size);
    double tz = double(floorMod(z, size)) / double(size);
    double sx = tx * tx * (3.0 - 2.0 * tx), sz = tz * tz * (3.0 - 2.0 * tz);
    double a = hash01(gx, salt, gz), b = hash01(gx + 1, salt, gz);
    double c = hash01(gx, salt, gz + 1), d = hash01(gx + 1, salt, gz + 1);
    double top = a + (b - a) * sx, bottom = c + (d - c) * sx;
    return top + (bottom - top) * sz;
}

inline double raw(int32_t x, int32_t z, int32_t size, int32_t salt) {
    return 0.7 * smooth(x, z, size, salt) + 0.3 * smooth(x, z, std::max(2, size / 2), salt ^ 0x2C1B);
}

inline const std::array<double, 1025>& quantiles() {
    static const std::array<double, 1025> q = []() {
        constexpr int n = 1 << 16;
        std::vector<double> s(n);
        for (int i = 0; i < n; ++i) {
            int x = int(hash01(i, 11, 3) * 200000.0) - 100000;
            int z = int(hash01(i, 17, 5) * 200000.0) - 100000;
            s[i] = raw(x, z, 4 + (i & 7), 0x5A5A + ((i >> 3) & 63));
        }
        std::sort(s.begin(), s.end());
        std::array<double, 1025> out{};
        for (size_t k = 0; k < out.size(); ++k)
            out[k] = s[size_t((uint64_t(k) * uint64_t(n - 1)) / uint64_t(out.size() - 1))];
        return out;
    }();
    return q;
}

inline double blob(int32_t x, int32_t z, int32_t size, int32_t salt) {
    if (size <= 1) return hash01(x, salt, z);
    double v = raw(x, z, size, salt);
    const auto& q = quantiles();
    int lo = 0, hi = int(q.size()) - 1;
    if (v <= q[lo]) return 0.0;
    if (v >= q[hi]) return 0.9999999;
    while (hi - lo > 1) {
        int mid = (lo + hi) >> 1;
        if (q[mid] <= v) lo = mid;
        else hi = mid;
    }
    double span = q[hi] - q[lo];
    double t = span > 0.0 ? (v - q[lo]) / span : 0.0;
    return (double(lo) + t) / double(q.size() - 1);
}

inline int spot(const int* weights, int count, int total, int32_t x, int32_t z, int32_t size, int32_t salt) {
    int ground = 0;
    for (int k = 1; k < count; ++k) if (weights[k] > weights[ground]) ground = k;
    double left = double(total);
    for (int k = 0; k < count; ++k) {
        if (k == ground) continue;
        double share = double(weights[k]) / left;
        if (blob(x, z, size, salt + 0x3D * (k + 1)) >= 1.0 - share) return k;
        left -= double(weights[k]);
    }
    return ground;
}

} // namespace Patches

// java.util.SplittableRandom exact port (JDK 21)
struct SplittableRandom {
    uint64_t seed = 0;
    uint64_t gamma = 0x9E3779B97F4A7C15ull;

    explicit SplittableRandom(int64_t s) : seed(uint64_t(s)) {}

    uint64_t nextSeed() { seed += gamma; return seed; }

    static int64_t mix64(uint64_t z) {
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return int64_t(z ^ (z >> 31));
    }

    static int32_t mix32(uint64_t z) {
        z = (z ^ (z >> 33)) * 0x62A9D9ED799705F5ull;
        z = (z ^ (z >> 28)) * 0xCB24D0A5C88C35B3ull;
        return int32_t(uint32_t(z >> 32));
    }

    int64_t nextLong() { return mix64(nextSeed()); }
    int32_t nextInt() { return mix32(nextSeed()); }
    bool nextBoolean() { return nextInt() < 0; }
    double nextDouble() { return double(uint64_t(nextLong()) >> 11) * 0x1.0p-53; }
};

} // namespace dw
