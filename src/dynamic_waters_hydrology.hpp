#pragma once
// Dynamic Waters 13.1.1 Complete Column Hydrology, Carving, Flow, Biomes, Freezing, ShoreSkin, Boulders & WaterPlants.
// Ports HydrologyManager, LakeHollows, MountainRiverBiome, BiomeFreezeMixin, MultiNoiseBiomeSourceMixin,
// ShoreSkin, RiverbankPalettes, RiverbankPalette, WaterPlants, WaterPlantSets, WaterPlant, Boulders, and BiomeMatch.
#include "dynamic_waters_basin.hpp"
#include "dynamic_waters_config.hpp"
#include "dynamic_waters_noise.hpp"
#include "dynamic_waters_rivers.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace dw {

// Block IDs used across Dynamic Waters terrain, shores, boulders, and plants
enum class DwBlock : uint16_t {
    Keep = 0,
    Air,
    Water,
    FlowingWater,
    Ice,
    PackedIce,
    Stone,
    Deepslate,
    Cobblestone,
    MossyCobblestone,
    Andesite,
    Diorite,
    Granite,
    Tuff,
    Calcite,
    DripstoneBlock,
    Gravel,
    Sand,
    RedSand,
    Sandstone,
    SmoothSandstone,
    RedSandstone,
    Clay,
    Mud,
    MuddyMangroveRoots,
    Dirt,
    CoarseDirt,
    RootedDirt,
    Podzol,
    GrassBlock,
    Mycelium,
    MossBlock,
    MossCarpet,
    SnowBlock,
    Terracotta,
    OrangeTerracotta,
    RedTerracotta,
    YellowTerracotta,
    BrownTerracotta,
    WhiteTerracotta,
    LightGrayTerracotta,
    Kelp,
    KelpPlant,
    Seagrass,
    TallSeagrass,
    SeaPickle,
    LilyPad,
    SugarCane,
    OakLog,
    SpruceLog,
    Custom
};

struct DwBlockRef {
    DwBlock id = DwBlock::Keep;
    std::string name; // canonical "minecraft:..." id

    bool isAir() const { return id == DwBlock::Air; }
    bool isWater() const { return id == DwBlock::Water || id == DwBlock::FlowingWater; }
    bool isKeep() const { return id == DwBlock::Keep; }
    bool isFalling() const {
        return id == DwBlock::Sand || id == DwBlock::RedSand || id == DwBlock::Gravel;
    }
    bool isDirtLike() const {
        return id == DwBlock::GrassBlock || id == DwBlock::Dirt || id == DwBlock::CoarseDirt
            || id == DwBlock::RootedDirt || id == DwBlock::Podzol || id == DwBlock::Mycelium
            || id == DwBlock::MossBlock || id == DwBlock::Mud || id == DwBlock::MuddyMangroveRoots;
    }
    bool isSandLike() const { return id == DwBlock::Sand || id == DwBlock::RedSand; }
    bool isRock() const { return id == DwBlock::Stone || id == DwBlock::Deepslate; }
};

DwBlockRef parseDwBlock(const std::string& rawId);
const char* dwBlockName(DwBlock b);
uint32_t dwBlockRgb24(DwBlock b); // Canonical RGB color (0xRRGGBB) for FTF viewport rendering

struct BiomeMatch {
    std::vector<std::string> biomes;
    std::vector<uint32_t> tags;
    std::vector<std::string> notBiomes;
    std::vector<uint32_t> notTags;
    bool hasClimate = false;
    float minTemp = -10.0f, maxTemp = 10.0f, minDownfall = 0.0f, maxDownfall = 1.0f;

    bool isDefault() const { return biomes.empty() && tags.empty() && !hasClimate; }
    bool byClimateOnly() const { return biomes.empty() && tags.empty() && hasClimate; }
    double climateWindow() const {
        return hasClimate ? double(maxTemp - minTemp) * double(maxDownfall - minDownfall) : 1e30;
    }
    bool matches(const BiomeMeta& meta) const;
    bool excluded(const BiomeMeta& meta) const;
    bool namesBiome(const BiomeMeta& meta) const;
    bool hasAnyTag(const BiomeMeta& meta) const;
    bool inClimate(const BiomeMeta& meta) const;
};

struct RiverbankPalette {
    static constexpr int RAPIDS = 0;
    static constexpr int SILT = 1;
    static constexpr int SHORE = 2;
    static constexpr int SLOPE = 3;
    static constexpr int BARS = 4;
    static constexpr int FLATS = 5;

    struct Mix {
        std::vector<DwBlockRef> blocks; // DwBlock::Keep = leave natural ground
        std::vector<int> weights;
        int totalWeight = 0;
        bool hasNonKeep = false;

        DwBlockRef pick(int32_t x, int32_t z, int32_t size, int32_t salt) const;
    };

    std::string source;
    BiomeMatch where;
    bool enabled = true;
    std::array<Mix, 6> places{};
    int patchSize = 5;
    int shoreHeight = 1;

    const Mix* place(int p) const {
        if (places[p].hasNonKeep) return &places[p];
        if (p == FLATS && places[BARS].hasNonKeep) return &places[BARS];
        if ((p == FLATS || p == BARS) && places[SILT].hasNonKeep) return &places[SILT];
        return nullptr;
    }
};

struct WaterPlant {
    enum class At : uint8_t { Bed = 0, Surface = 1, Shore = 2 };

    std::string id;
    DwBlockRef block;
    bool ownState = false;
    At at = At::Bed;
    double chance = 0.05;
    int minDepth = 1, maxDepth = 64;
    bool calm = false;
    int patchSize = 0;
    double patchShare = 0.3;
    bool alone = false;
    int minHeight = 1, maxHeight = 1;
    int32_t salt = 0;

    bool isKelp() const { return block.id == DwBlock::Kelp || block.id == DwBlock::KelpPlant; }
    bool isDouble() const { return block.id == DwBlock::TallSeagrass; }
    bool isPickles() const { return block.id == DwBlock::SeaPickle && !ownState; }
};

struct WaterPlantSet {
    static constexpr int LOWLAND = 0;
    static constexpr int MOUNTAIN = 1;
    static constexpr int LAKE = 2;

    std::string source;
    BiomeMatch where;
    bool enabled = true;
    std::array<std::vector<WaterPlant>, 3> kinds;
};

struct RiverCarving {
    int targetY = 0;
    int waterLevel = 62;
    int sealFloorY = 0;
    int cavernRoofY = -999;
    bool modifies = false;
    bool isPillar = false;
    bool isWatershed = false;
    bool isMountainRiver = false;
    bool isWatershedWall = false;
    bool isMainRiverWater = false;
    bool isLake = false;
    int wallTopY = -999;
    int skinLevel = INT32_MIN;
    bool pointBar = false;
    bool riverSplit = false;
    bool deltaFlat = false;
    double deltaShore = 1e300;
    bool deltaWater = false;
    bool dressCut = false;
    bool gorgeWall = false;
    bool gorgeSteep = false;
    bool gorgeSeal = false;
    double distanceFromCenter = 1e300;
    double channelWidth = 0.0;
    double waterRadius = 0.0;
    double channelSlope = 0.0;

    bool holdsWater() const { return modifies && !isWatershedWall && waterLevel > targetY; }
};

struct Ravine {
    double px = 0.0, pz = 0.0, ux = 0.0, uz = 0.0, length = 0.0, width = 0.0;
    int start = 0, top = 0;
    std::vector<double> from;
    std::vector<int> level;
    double curveAmp = 0.0, curvePhase = 0.0;
    bool valid = false;

    double curve(double along) const;
    double lateral(double ox, double oz, double along) const { return ox * uz - oz * ux - curve(along); }
    double centre(const SimplexNoise& bedNoise, double along) const;
    double across(const SimplexNoise& bedNoise, double x, double z, double ox, double oz, double along) const;
    int poolLevel(const SimplexNoise& bedNoise, double x, double z, double along) const;
    int waterAt(const SimplexNoise& bedNoise, double x, double z) const;
    int levelAt(double t) const;
    double sideLine(double t) const;
};

struct WatershedContext {
    double distance = 1e300;
    double width = 3.0;
    const WatershedPath* bestPath = nullptr;
    int bestY = 0;
    int bestNodeIndex = -1;
    double flowDirX = 0.0, flowDirZ = 0.0;
};

struct HydrologyCallbacks {
    int seaLevel = 63;
    std::function<int(int cellX, int cellZ)> sampleCoarseCell;   // OCEAN_FLOOR_WG at cell*8
    std::function<int(int blockX, int blockZ)> sampleSurfaceAt;  // OCEAN_FLOOR_WG at block
    std::function<bool(int blockX, int blockZ)> isMouthBiomeAt;
    std::function<bool(int blockX, int blockZ)> isOceanBiomeAt;
    std::function<BiomeMeta(int blockX, int blockY, int blockZ)> biomeAt;
    std::function<DwBlockRef(int blockX, int blockY, int blockZ)> naturalTopBlockAt; // optional surface ground query
    std::string assetsDir = "assets/dynamicwaters";
};

// Complete per-column visual & block-level result for integration into FTF viewport & verification
struct ColumnOutcome {
    RiverCarving carving;
    int finalSolidY = 0;       // top solid block Y (inclusive; MC topSolidBlock = preCarveSurfaceY)
    int waterTopY = INT32_MIN; // INT32_MIN if dry, else water/ice surface block Y
    bool isIceSurface = false; // BiomeFreezeMixin frozen top layer
    bool overrideRiverBiome = false;
    bool riverBiomeIsFrozen = false;
    DwBlockRef surfaceBlock{DwBlock::Keep, ""};     // solid surface block override (ShoreSkin / gorgeDress / river bed / bank)
    DwBlockRef aboveWaterBlock{DwBlock::Keep, ""};  // boulder crown / lily_pad / sugar_cane / kelp / seagrass / log if any
    int aboveWaterTopY = INT32_MIN;                 // top block Y if boulder or tall plant rises above water/bed
    double flowX = 0.0, flowZ = 0.0;
};

class HydrologyEngine {
public:
    static constexpr double GORGE_NARROW_FROM = 8.0;
    static constexpr double GORGE_NARROW_TO = 40.0;
    static constexpr double GORGE_NARROW_MIN = 0.6;
    static constexpr double STREAM_DEPTH_RATIO = 0.48;
    static constexpr int MAX_BOWL = 6;
    static constexpr double TUNNEL_FROM_DEPTH = 8.0;
    static constexpr int TUNNEL_WAIST = 3;
    static constexpr int TUNNEL_LOOK_NODES = 8;
    static constexpr int TUNNEL_CRUST = 3;
    static constexpr double PORTAL_CRUST = 8.0;
    static constexpr double PORTAL_FLARE = 5.0;
    static constexpr double CAVE_SWELL = 2.0;
    static constexpr double CAVE_ROUGH = 1.0;
    static constexpr int JUNCTION_CHAMBER = 2;
    static constexpr double BANK_ZONE_PER_WIDTH = 2.0;
    static constexpr double BANK_ZONE_MIN = 5.0;
    static constexpr int SEAL_MIN = 5;
    static constexpr double SEAL_PER_WIDTH = 5.0;
    static constexpr int MAX_BANK_CUT = 8;
    static constexpr double STEEP_BANK_TALUS = 3.0;
    static constexpr double BANK_HOLD_REACH = 1.5;
    static constexpr int BANK_HOLD_SPAN = 4;
    static constexpr int BANK_HOLD_OVER = 3;
    static constexpr double BANK_HOLD_NEAR = 10.0;
    static constexpr int CAP_YIELD_RISE = 4;
    static constexpr double CAP_YIELD_EDGE = 2.0;
    static constexpr int CAP_YIELD_UP = 6;
    static constexpr double NORMAL_RIVER_DEPTH = 7.0;
    static constexpr int LAKE_SHORE_LIP = 3;
    static constexpr double LAKE_LIP_RIVER_GAP = 4.0;
    static constexpr int LAKE_SHORE_CUT = 12;
    static constexpr int LAKE_DEPTH = 8;
    static constexpr int POOL_FILL_DEPTH = 8;
    static constexpr int POOL_SEA_CLEAR = 32;
    static constexpr int POOL_MAX_CELLS = 64;
    static constexpr int WATERSHED_SCAN_RADIUS = 1;

    void reset(int64_t worldSeed, const DynamicWatersConfig& cfg, HydrologyCallbacks cb);
    void setCallbacks(HydrologyCallbacks cb);
    void prewarm(int minBlockX, int minBlockZ, int maxBlockX, int maxBlockZ);

    bool isInitialized() const { return initialized_; }
    const DynamicWatersConfig& config() const { return cfg_; }
    int64_t seed() const { return seed_; }
    int seaLevel() const { return cb_.seaLevel; }

    MainRiversEngine& mainRivers() { return mainRivers_; }
    const MainRiversEngine& mainRivers() const { return mainRivers_; }
    BasinTileEngine& basin() { return basin_; }
    const BasinTileEngine& basin() const { return basin_; }
    const WorldNoises& noises() const { return noises_; }

    void loadAssets(const std::string& assetsDir);
    const RiverbankPalette* paletteForBiome(const BiomeMeta& meta) const;
    const WaterPlantSet* plantSetForBiome(const BiomeMeta& meta) const;

    WatershedContext evaluateWatershedContext(double x, double z, bool cacheOnly = false) const;
    const WatershedLake* findLake(double x, double z, int columnY = INT32_MIN, bool cacheOnly = false, bool dugShores = false) const;
    const WatershedLake* lakeBeside(double x, double z, int columnY = INT32_MIN, bool cacheOnly = false) const;
    bool lakeDrainsHere(const WatershedLake& lake, double x, double z) const;
    bool lakeWaterAt(const WatershedLake& lake, double x, double z, int ground) const;
    bool poolFilled(int x, int z, int top, const BiomeMeta& biome) const;

    RiverCarving calculateRiverCarving(double x, double z, int originalY, int seaLevel) const;
    RiverCarving lowlandCut(double x, double z, int ground, int seaLevel) const;

    void getRiverFlow(double x, int y, double z, double& outFx, double& outFz, bool useLandOceanCheck = true) const;
    bool isMountainRiverWater(int x, int y, int z) const;
    bool inMountainRiverMiddle(double x, int y, double z) const;
    bool nearMountainFall(int x, int y, int z) const;
    bool inLowlandWater(double x, double z) const;
    bool inGorgeReach(double x, double z) const;
    bool inRavine(double x, double z) const;
    bool riverSplitLand(double x, double z) const;
    bool riverIsland(double x, double z) const;

    // Exact biome injection (MultiNoiseBiomeSourceMixin & MountainRiverBiome)
    bool getLowlandRiverBiomeOverride(int wx, int wy, int wz, bool& outFrozen) const;
    bool getMountainRiverBiomeOverride(int wx, int waterTopY, int wz, const BiomeMeta& hereMeta, bool& outFrozen) const;

    // Exact column evaluation including ShoreSkin, gorgeDress, WaterPlants, Boulders, Ice freeze rules.
    // Note: originalGroundY is OCEAN_FLOOR_WG - 1 (top solid block Y, matching NoiseChunkGeneratorMixin).
    ColumnOutcome evaluateColumn(int wx, int wz, int originalGroundY) const;

    // Fast surface evaluation with optional 1-block neighbor water cache for exact stepNear / fallNear / WaterPlants::SHORE.
    static double riverBedNoise(const WorldNoises& noises, double x, double z) {
        return noises.riverBedNoise.getValue(x * 0.05, z * 0.05) * 1.5;
    }

private:
    struct HeadFeed {
        double distToSource = 1e300;
        double width = 0.0;
        bool valid = false;
    };

    enum class LakeHollowKind : uint8_t { None = 0, Flooded = 1, Open = 2 };

    double fedWaterRadius(const RiverGraphEdge& edge, double x, double z, double dist, const HeadFeed& feed) const;
    HeadFeed headFeed(const RiverGraphEdge& edge, bool cacheOnly = false) const;
    double gorgeWaterRadius(const RiverGraphEdge& edge, double x, double z, bool cacheOnly = false) const;
    double flowWaterRadius(const RiverGraphEdge& edge, double x, double z) const;

    int64_t riverSplitHash(const RiverGraphEdge& edge) const;
    bool riverSplits(const RiverGraphEdge& edge) const;
    double riverSplitDepth(const RiverGraphEdge& edge, double x, double z, double radius) const;
    bool onRiverSplit(double x, double z, double in) const;
    void aroundRiverSplit(double x, double z, double& fx, double& fz) const;

    const Ravine* ravineOf(const WatershedPath& path, int seaLevel) const;
    const Ravine* feederRavine(const WatershedPath& path, int seaLevel) const;
    const Ravine* ravineAbove(const WatershedPath& path, int seaLevel) const;
    bool entersGorge(const WatershedPath& path, int seaLevel) const;
    bool leftDry(const WatershedPath& path, int seaLevel, std::vector<const WatershedPath*>& visited) const;
    bool leftDry(const WatershedPath& path, int seaLevel) const;
    bool drainedByRavines(const WatershedPath& path, int seaLevel, std::vector<const WatershedPath*>& visited) const;
    bool feedsDelta(const WatershedPath& path) const;
    bool walksToDelta(const WatershedPath& path, std::vector<const WatershedLake*>& through) const;
    const WatershedPath* joins(const WatershedPath& p) const;
    const WatershedPath* downstreamOf(const WatershedPath& p) const;
    bool mouthInDelta(const WatershedPath& p) const;
    bool lakeFeedsDelta(const WatershedPath& p, std::vector<const WatershedLake*>& through) const;
    Ravine buildRavine(const WatershedPath& path, int seaLevel) const;
    const RiverGraphEdge* nearestRiver(double x, double z, double on[2]) const;
    double toRiverWater(const RiverGraphEdge& river, double x, double z, double on[2]) const;
    double ravineWall(const RiverGraphEdge& river, double x, double z, int seaLevel, double on[2]) const;
    int feedersTop(double px, double pz, double reach, int top) const;

    bool atRavineRim(const WatershedContext& ctx, int seaLevel) const;
    static int rimLevel(const WatershedPath& path, int node, int seaLevel);
    static double rimDistance(const WatershedPath& path, int node, double x, double z, int seaLevel, double reach);
    static double funnelDistance(const WatershedPath& path, int node, int start, double x, double z, double reach);
    bool joinsPath(const WatershedPath& q, const WatershedPath& path) const;

    double channelFullWidth(const WatershedPath::RiverNodeData& data, double x, double z) const;
    static int channelBowl(const WatershedPath::RiverNodeData& data, double chanW);
    bool capYields(const WatershedPath& self, const WatershedPath::RiverNodeData& own, double x, double z, int colX, int colZ, int homeRx, int homeRz) const;
    bool capGoesOn(const WatershedPath& self, double x, double z, int colX, int colZ, int homeRx, int homeRz) const;
    int bankHold(const WatershedPath& path, int node, int ownY, double x, double z, double chanW,
                 const Ravine* ravine, const Ravine* fed, bool overRim, int seaLevel, bool wetOnly) const;
    static bool underStep(const WatershedPath& path, const WatershedPath::RiverNodeData& nd, double x, double z);
    bool inRavineWay(const Ravine& r, double x, double z, bool ragged) const;

    static int lakeDepth(double shoreDistance);
    static bool laysLake(const WatershedLake& lake, int seaLevel) {
        return lake.waterY > seaLevel - 1 || lake.dig > WatershedLake::SHAVE;
    }
    LakeHollowKind lakeHollowKind(const WatershedLake& lake, int x, int z, int ground, int lipDepth) const;
    bool lakeHollowBesideWater(const WatershedLake& lake, int x, int z) const;
    int lipDepth(const WatershedLake& lake, bool covered, LakeHollowKind hollow, double x, double z, int ground) const;

    int sealDepth(double dOut, double halfWidth) const;
    int sealJitter(double x, double z) const;
    int gorgeStratum(double x, double z, int y, int waterY) const;
    int vaultTop(const WatershedPath& path, const WatershedPath::RiverNodeData& data, double vaultR, double vaultH, int ground) const;

    RiverCarving carve(double x, double z, int originalY, int seaLevel, bool whole) const;

    bool poolPoint(int cx, int cz, int seaLevel) const;
    bool poolBody(int cx, int cz, int seaLevel) const;
    bool poolClear(int cx, int cz) const;
    static bool keepsStandingWater(const BiomeMeta& b);

    void lowlandFlow(double x, int y, double z, double& fx, double& fz, bool checkOceanBiome) const;
    void ravineFlowVec(double x, int y, double z, double& fx, double& fz, bool& found) const;

    DwBlockRef dressGorgeBlock(int wx, int wz, int topY, int waterY, bool steep, int groundY, const BiomeMeta& biome) const;
    DwBlockRef applyShoreSkinBlock(int wx, int wz, int sy, int originalY, const RiverCarving& c, bool wet, bool stepNear, int exposure, const BiomeMeta& landBiome) const;
    void applyBouldersColumn(int wx, int wz, int bedY, int waterLevel, const RiverCarving& c, const BiomeMeta& landBiome, bool fallNear, int& outCrownY, DwBlockRef& outBlock) const;
    void applyWaterPlantsColumn(int wx, int wz, int bedY, int waterLevel, bool wet, const RiverCarving& c, const BiomeMeta& landBiome, const BiomeMeta& waterBiome, bool stepNear, const DwBlockRef& groundBlock, int& outTopY, DwBlockRef& outBlock) const;

    bool initialized_ = false;
    int64_t seed_ = 0;
    DynamicWatersConfig cfg_{};
    HydrologyCallbacks cb_{};
    WorldNoises noises_;
    MainRiversEngine mainRivers_;
    BasinTileEngine basin_;

    std::vector<RiverbankPalette> palettes_;
    std::vector<WaterPlantSet> plantSets_;

    struct RavineChunk {
        std::array<Ravine, 256> items{};
    };
    static constexpr size_t RAVINE_MAX_CHUNKS = 4096;

    // Lazy hydrology caches (writes protected by cacheMutex_, reads lock-free)
    mutable std::mutex cacheMutex_;
    mutable std::vector<std::unique_ptr<RavineChunk>> ravineOwnedChunks_;
    mutable std::array<std::atomic<RavineChunk*>, RAVINE_MAX_CHUNKS> ravineChunks_{};
    mutable int ravineCount_ = 0;
    std::atomic<uint64_t> resetEpoch_{1};

    const Ravine* ravineAtLockFree(int idx) const {
        if (idx < 0 || size_t(idx) >= RAVINE_MAX_CHUNKS * 256) return nullptr;
        const RavineChunk* chunk = ravineChunks_[size_t(idx) >> 8].load(std::memory_order_acquire);
        return chunk ? &chunk->items[size_t(idx) & 255] : nullptr;
    }
    int pushRavineLocked(Ravine r) const {
        if (ravineCount_ >= int(RAVINE_MAX_CHUNKS * 256)) return -1;
        int idx = ravineCount_++;
        size_t c = size_t(idx) >> 8;
        size_t s = size_t(idx) & 255;
        RavineChunk* chunk = ravineChunks_[c].load(std::memory_order_relaxed);
        if (!chunk) {
            auto p = std::make_unique<RavineChunk>();
            chunk = p.get();
            ravineOwnedChunks_.push_back(std::move(p));
            ravineChunks_[c].store(chunk, std::memory_order_release);
        }
        chunk->items[s] = std::move(r);
        return idx;
    }

    mutable std::unordered_map<int64_t, HeadFeed> headFeedCache_;
    mutable std::unordered_map<int64_t, bool> poolBodyCache_;
    mutable std::unordered_map<int64_t, bool> poolClearCache_;
    mutable std::unordered_map<int64_t, int> hollowSurfaceCache_;
};

} // namespace dw
