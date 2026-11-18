#pragma once
#include <map>
#include <string>
#include <optional>
#include <atomic>
#include <mutex>
#include <thread>
#include "gpu.hpp"
#include <glm/glm.hpp>
#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include "dynamic_waters_config.hpp"
#include "dynamic_waters_hydrology.hpp"

struct alignas(16) TerrainParams {
    glm::uvec4 meta{256,12345,0,0};
    glm::vec4 domain{8192,0,0,0};
    glm::vec4 land{850,1100,63,.9f};
    glm::vec4 detail{.85f,.035f,.5f,0};
    glm::vec4 climate{0,0,2600,0};
    glm::uvec4 seeds0{};
    glm::uvec4 seeds1{};
    glm::vec4 ftfScales{1,1,1,256}; // global horizontal, base, height, terrain scale factor
};
static_assert(sizeof(TerrainParams)==128);
struct alignas(16) DrawParams {
    glm::mat4 vp{1};
    glm::vec4 view{};
    glm::uvec4 meta{};
    glm::vec4 focus{}; // local center X/Z, base extent, signed radius (negative = base)
    glm::vec4 limits{}; // preset World Height, highlight enabled, world origin X/Z
};
static_assert(sizeof(DrawParams)==128);
struct TerrainSample { std::array<float,4> field,color; };
struct alignas(16) GraphSeeds {
    std::array<uint32_t,16> values{};
    glm::uvec4 regionSeeds{0},mixMeta{0}; // choice count, repeated-slot count, region size, shuffle seed
    std::array<std::array<uint32_t,16>,9> familySeeds{};
    std::array<uint32_t,16> borderSeeds{};
    std::array<uint32_t,56> mixSeeds{};
    std::array<glm::vec4,8> familyScales{}; // base, height, horizontal, weight
    std::array<glm::uvec4,64> choices{}; // cumulative slots, source A, source B (UINT_MAX = single), pair index
    std::array<std::array<uint32_t,8>,9> mountainSeeds{}; // low, center, high; three graphs each
    std::array<glm::vec4,3> mountainScales{}; // base, height, horizontal, reserved
    glm::uvec4 mountainMeta{0}; // fancy, legacy, varied, chains enabled
    std::array<std::array<uint32_t,8>,3> chainSeeds{};
    glm::uvec4 volcanoSeeds{0},chainShapeSeeds{0};
    glm::uvec4 worldSeeds0{0},worldSeeds1{0};
    glm::vec4 continent{0},continentNoise{0},controlPoints{0},worldLevels{0};
    glm::vec4 island0{0},island1{0},island2{0},island3{0};
    glm::uvec4 islandMeta{0}; // enabled, salt, islandCoast float bits, reserved
    glm::uvec4 climateMeta{0},climateSeeds0{0},climateSeeds1{0},climateSeeds2{0};
    glm::vec4 climateShape{0},climateEdge{0},climateEffects{0},temperature0{0},temperature1{0},moisture0{0},moisture1{0};
    glm::vec4 filter0{0},filter1{0},filter2{0},filter3{0};glm::uvec4 filterMeta{0};
    glm::uvec4 biomeSeeds{0},biomeSeeds2{0};glm::vec4 biomeOptions{0};
    glm::vec4 scree0{0},scree1{0},surfaceOptions{0};glm::uvec4 surfaceMeta{0};
    glm::vec4 filter4{0},filter5{0};glm::uvec4 filterMeta2{0}; // FTF version profile: erosion constants, trig table, early ceiling, pipeline flags
    glm::uvec4 strataMeta{0}; // FTF StrataRule: stack count, region size, float bits of 1/size and 1/(size/4)
    std::array<uint32_t,100*41> strata{}; // per stack: layer count, 35 layer depth floats, 5 words of 4-bit materials (soil, sediment, clay, rock tag entries)
    // Minecraft surface context for the world seed (world.cpp buildVanillaSurface): RandomState root positional factory (lo, hi as
    // 32-bit pairs), vertical_gradient factories, SurfaceSystem clay bands, Biome temperature simplex permutations, NormalNoise octaves.
    glm::uvec4 surfaceRandom{0};std::array<glm::uvec4,2> gradientRandoms{};
    std::array<uint32_t,48> clayBands{};          // 192 band codes as bytes: terracotta, orange, yellow, brown, red, white, light gray
    std::array<uint32_t,256> simplexPerms{};      // TEMPERATURE_NOISE (1234), FROZEN_TEMPERATURE_NOISE (3456) levels 0, 1, 2: 256 bytes each
    std::array<uint32_t,96*70> improvedNoises{};  // ImprovedNoise: xo, yo, zo doubles then the 256-byte permutation, in generated noise order
    glm::uvec4 regionSalts{0};   // FTF region rendezvous salts (lo, hi): the weighted region provider seed ^ 0x5D6F7A2C8B9134E1, the replacement layer seed ^ 0x61C8E4A2793DB50F
    glm::vec4 climateErosion0{0},climateErosion1{0};   // climateFixes2 (1.0.0025+): erosion scale, falloff, min, max; bias, distributionBias, 0, 0
    glm::uvec4 climateExtraSeeds{0};                 // erosionSeed+0, +1, +2, tempType|(moistType<<8)|(erosionType<<16)
    glm::vec4 upliftExtra{0};                        // 1.0.0025+: macroWarpStrength, coastalComplexity, bridgeChance, tectonicPlateUplift
    glm::vec4 upliftExtra2{0};                       // 1.0.0025+: tectonicRounding, saddleHeight (0.75), 0, 0
    glm::uvec4 upliftSeeds{0};                       // 1.0.0025+: bridgeSeed (root+12), tectonicSeed (root+13), 0, 0
    GraphSeeds(){familyScales.fill(glm::vec4(0));choices.fill(glm::uvec4(0));mountainScales.fill(glm::vec4(0));}
};
static_assert(sizeof(GraphSeeds)==47856);   // stays below vkCmdUpdateBuffer's 65536-byte limit
static_assert(sizeof(TerrainSample)==32);
// Exact mod surface noise descriptor (empty in the shareable edition).
struct ExactNoiseSpec {std::string name;int firstOctave=0;std::vector<double> amplitudes;};
constexpr size_t exactNoiseCapacity=32+1024*70;
constexpr size_t cornerCapacity=1u<<20;   // ints: binding 17, preliminary surface levels per 16-block corner (biomes.comp CORNER_CAPACITY)   // words: 8 gradient factories, then up to 1024 ImprovedNoise tables
struct alignas(16) SurfaceInspection {glm::uvec4 request{};glm::vec4 values{};glm::uvec4 ids{};glm::vec4 extra{};};
static_assert(sizeof(SurfaceInspection)==64);
struct VoronoiBufferKey {
    bool active{};
    int32_t baseX{},baseZ{},gridW{},gridH{};
    glm::uvec4 worldSeeds0{},upliftSeeds{};
    glm::vec4 continent{},upliftExtra{},upliftExtra2{};
    bool operator==(const VoronoiBufferKey&)const=default;
};
#include "generation_state.hpp"
struct alignas(16) GenerationCacheHeader {glm::ivec4 bounds[2]{};glm::uvec4 info{};};
struct EffectsBlock {glm::vec4 effects,screen,viewport,sun,shadow;glm::uvec4 terrain;glm::vec4 cue,hole,water,fog,cloud,cloud2;}; // ViewEffects SSBO; the first 128 bytes are what terrain shaders read
static_assert(sizeof(EffectsBlock)==192);
struct StageTiming { double generate{},filter{},draw{},total{},cache{},world{},ocean{},compose{};bool valid{},generated{}; };
struct SurfaceFrame {
    Image color,depth,scene; // scene: shaded terrain before the atmosphere sub-pass writes `color`
    VkDescriptorSet texture{},inputs{},taaSet{};
    VkFramebuffer framebuffer{};
    VkQueryPool queries{},stepQueries{}; // stepQueries: per-dispatch timestamps when Surface::profileSteps is on
    std::vector<std::string> stepNames;
    uint64_t revision{};
    bool recorded{};
    bool generated{};
    uint32_t workStage=0,workTiles=0;
};

struct DynamicWatersSample {
    bool active = false;
    bool modifies = false;
    bool isMainRiverWater = false;
    bool isMountainRiver = false;
    bool isLake = false;
    bool isWatershedWall = false;
    bool isIceSurface = false;
    bool overrideRiverBiome = false;
    bool riverBiomeIsFrozen = false;
    int origY = 63;
    int finalSolidY = 63;
    int waterTopY = 0;
    double flowX = 0.0;
    double flowZ = 0.0;
    std::string surfaceBlockName;
    std::string aboveWaterBlockName;
};

struct CpuCellState {
    float riverMask;
    float sediment;
    uint32_t terrain;
    float waterHeight;
    float gateMin;
    float erosion;
    float weirdness;
    uint32_t biome;
};
static_assert(sizeof(CpuCellState) == 32, "CpuCellState must match shaders/cell_state.glsl");

struct DwBiomeEntry {
    std::string name;
    uint32_t mapColor = 0x7cbd6bu;
    uint32_t grassColor = 0x7cbd6bu;
    uint32_t foliageColor = 0x59ae30u;
    uint32_t waterColor = 0x3f76e4u;
    float temperature = 0.5f;
    float downfall = 0.5f;
    uint32_t tags = 0;
};

class Surface {
public:
    explicit Surface(Gpu& device):gpu(device){}
    ~Surface(){waitForDwAsync(true);}
    Gpu& gpu;
    std::vector<SurfaceFrame> frames;
    uint32_t resolution{},width{},height{};
    uint32_t indexCount{};
    bool distanceLod=false;glm::vec2 lodCamera{};float lodBandSize=1000,lodAltitude=0;uint32_t lodBands=0;float lodBandEnd[8]{};uint32_t lodBandLevel[8]{}; /* lodBands>0: custom bands (end distance, detail level 0..5 = 1x..32x) */ // lodAltitude: camera height above the terrain; bands use 3D distance
    std::array<uint32_t,6> lodFirst{},lodCount{};
    std::array<uint32_t,6> lodPatches{};uint64_t drawnTriangles=0;
    uint32_t patchCells{},patchCount{};
    uint64_t fieldRevision{};
    VkDeviceSize fieldBytes{};
    uint32_t mountainCacheSamples{};
    bool useMountainCache{true};
    bool hasRivers{};
    bool frustumCull=false,distanceCull=false,backfaceCull=false;float maxDrawDistance=50000;uint32_t culledPatches=0;
    glm::vec4 screenFocus{},viewportSize{},sun{-.47f,.8f,.33f,1},sunShadow{.65f,1500,.35f,0};
    TerrainParams lastParams{};
    glm::uvec4 terrainMask{};
    SurfaceInspection inspect(glm::vec2 world);
    std::vector<uint32_t> generatedBiomes();   // canonical biome ids present anywhere in the last generated grid
    Surface* finerBand=nullptr;DrawParams finerBandDraw{};glm::vec4 detailHole{}; // xy absolute centre, z inner hole, w centred coarse field
    // Render-only atmosphere (second sub-pass): realistic water, height fog, volumetric clouds. All zero = exact copy of the scene.
    struct Atmosphere {glm::vec4 water{},fog{},cloud{},cloud2{};glm::mat4 inverseVP{1};glm::vec4 eye{},domain{},clip{};}; // inverseVP is camera-relative; clip: near, far
    Atmosphere atmosphere;
    struct TaaState {bool enabled=false;glm::mat4 invCurrVP{1},prevVP{1};glm::vec2 originDelta{0},jitterUv{0};bool reset=true;float alpha=0.08f,sharpness=0.5f;} taa;
    glm::vec4 scaleCue{}; // camera x,y,z (absolute world) and strength of the 1-block / 16-block scale grid
    glm::vec4 effects{1,.34f,0,1}; // AO intensity, shadow intensity, elapsed seconds, shadow speed
    bool updateRivers(const std::vector<uint32_t>& data);
    bool updateVoronoi(const TerrainParams& params,const GraphSeeds& seeds);
    static VkDeviceSize estimatedFieldBytes(uint32_t n){return static_cast<VkDeviceSize>(n)*n*72;}
    void initialize(Surface* shared=nullptr,std::function<void(uint32_t,uint32_t)> progress={},bool allVariants=false);
    void ensure(uint32_t grid,uint32_t w,uint32_t h);
    StageTiming timing(uint32_t frame);
    void record(VkCommandBuffer cmd,uint32_t frame,const TerrainParams& params,const GraphSeeds& seeds,const DrawParams& draw,uint64_t revision,Surface* detail=nullptr,const DrawParams* detailDraw=nullptr,bool generateOnly=false);
    void bindCoarse(Surface& base);
    // A: background refinement is never drawn until all phases finish.
    bool profileSteps=false;std::map<std::string,std::pair<double,uint32_t>> stepProfile; // --profile-steps: GPU ms and count per generation step
    std::string stepName(VkPipeline pipeline)const;
    bool progressiveGeneration=false,selectiveGeneration=true;float generationBudgetMs=8;uint32_t biomeBandGroups=1;
    VkDeviceSize stageCacheBytes()const{return preClimate.handle?VkDeviceSize(resolution)*resolution*96:0;}
    float generationProgress()const;bool generationPending()const{return workActive;}
    GenerationStage lastGenerationStage=GenerationStage::None;
    std::array<uint64_t,5> stageRuns{};uint64_t generationDispatches=0,cancelledGenerations=0;
    void exchangeTerrain(Surface& other);void cancelGeneration();
    GenerationStage updateStage(const TerrainParams& p,const GraphSeeds& g)const;

    std::vector<float> pickHeights(uint32_t side=129);
    std::vector<float> heightField(); // whole grid, row-major, raw FTF heights (one copy, waits for the GPU)
    std::vector<uint32_t> readBiomes(); // whole grid, row-major, raw biome ids from cellState (one copy, waits for the GPU)
    // Several full-resolution windows of the height field in one copy (waits for the GPU): origin.xy and size.zw per window, row-major results.
    std::vector<std::vector<float>> heightWindows(const std::vector<glm::ivec4>& windows);
    // Asynchronous variant: record a copy of the height field into this frame's staging buffer; read it back when that
    // frame slot comes round again (its fence has signalled), without ever stalling the queue.
    void recordHeightCopy(VkCommandBuffer cmd,uint32_t frame,const TerrainParams& params,uint64_t revision,glm::ivec2 offset={0,0},uint32_t side=0); // side>0: copy only a side x side window starting at offset
    bool collectHeights(uint32_t frame,std::vector<float>& heights,TerrainParams& params,uint64_t& revision);
    void recordPickHeights(VkCommandBuffer cmd,uint32_t frame,uint64_t revision,uint32_t side=129);
    bool collectPickHeights(uint32_t frame,std::vector<float>& heights,uint64_t& revision);
    std::vector<TerrainSample> samples(uint32_t frame);
    void exportCells(const std::filesystem::path& file,bool extended=false);
    void screenshot(uint32_t frame,const std::filesystem::path& file);
    void destroy();

    dw::DynamicWatersConfig dwConfig{};
    int64_t dwWorldSeed = 0;
    bool dwPending = false;
    GraphSeeds dwLastSeeds{};
    std::vector<glm::vec4> dwOrigRawField;
    std::vector<glm::vec4> dwOrigField;
    std::vector<uint32_t> dwOrigColors;
    std::vector<CpuCellState> dwOrigCells;
    TerrainParams dwOrigParams{};
    uint64_t dwOrigRevision = 0;
    std::vector<DwBiomeEntry> dwBiomes;
    mutable std::vector<uint8_t> dwBiomeOceanOrBlacklisted;
    mutable std::string dwBiomeBlacklistCached;
    mutable size_t dwBiomeCountCached = 0;
    std::unordered_map<std::string, uint32_t> dwBlockIds;
    uint32_t dwRiverBiomeId = 31;
    uint32_t dwFrozenRiverBiomeId = 17;
    mutable std::unique_ptr<dw::HydrologyEngine> dwEngine;
    uint64_t dwEngineHash = 0;
    int64_t dwEngineSeed = 0;
    uint64_t dwEngineBaseRev = 0;
    int dwEngineSeaLevel = 63;
    uint32_t dwEngineRes = 0;
    mutable const Surface* dwDetailSample = nullptr;
    mutable std::mutex dwEngineUseMutex;
    std::atomic<bool> dwCancelRequested{false};
    glm::vec2 dwFocusCenter{0.0f, 0.0f};
    bool dwHasFocus = false;
    int dwPreviewRadius = 4096;
    bool dwPinnedFocus = false;
    glm::vec2 dwAppliedFocus{0.0f, 0.0f};
    bool dwHasAppliedFocus = false;
    int dwAppliedRadius = 0;
    bool dwAppliedPinned = false;
    bool dwPreviouslyCarved = false;

    struct DwAsyncJob {
        std::thread thread;
        std::atomic<bool> done{false};
        bool running = false;
        uint64_t expectedRev = 0;
        uint32_t res = 0;
        TerrainParams params{};
        dw::DynamicWatersConfig cfgSnapshot{};
        int64_t worldSeed = 0;
        uint64_t cfgHash = 0;
        int seaY = 63;
        GraphSeeds seedsSnapshot{};
        glm::vec4 holeSnapshot{};
        glm::vec2 appliedFocus{0.0f, 0.0f};
        int appliedRadius = 0;
        bool appliedPinned = false;
        bool hasCarved = false;
        std::vector<glm::vec4> rawVec;
        std::vector<glm::vec4> fieldVec;
        std::vector<uint32_t> colorVec;
        std::vector<CpuCellState> cellVec;
    };
    std::unique_ptr<DwAsyncJob> dwAsync;

    void configureDynamicWaters(const dw::DynamicWatersConfig& cfg, int64_t worldSeed, glm::vec2 focusCenter = {0.0f, 0.0f}, bool hasFocus = false, int previewRadius = 4096, bool pinnedFocus = false);
    const dw::DynamicWatersConfig& dynamicWatersConfig() const { return dwConfig; }
    bool dwAsyncBusy() const { return dwAsync && dwAsync->running; }
    void waitForDwAsync(bool cancel = false);
    bool startDynamicWatersAsync();
    bool pollDynamicWatersAsync();
    bool advanceDynamicWatersAsync();
    bool finishPendingDynamicWaters();
    void applyDynamicWatersCpuPass();
    void rebindDynamicWatersCallbacks();
    DynamicWatersSample inspectDynamicWaters(int wx, int wz) const;
private:
    bool prepareDwJob(DwAsyncJob& job);
    void executeDwJob(DwAsyncJob& job);
    bool uploadDwJob(const DwAsyncJob& job);
    struct AsyncHeights {Buffer staging;uint32_t resolution=0;bool pending=false;TerrainParams params{};uint64_t revision=0;};
    std::vector<AsyncHeights> asyncHeights,asyncPickHeights;
    bool sharedPipelines=false;Surface* pipelineOwner=nullptr;
    void buildCompute(VkShaderModule module,VkPipeline& pipeline,int variant,bool filterStage);
    VkPipeline variantPipeline(uint32_t variant);
    VkPipeline inspectionPipeline();
    VkPipeline cornersPipeline();
    Buffer indices,graphSeeds,mountainCache,cellState,regionCells,riverNetworks,voronoiCells,biomeTable,effectsBuffer,inspectionBuffer,sinTable;
    std::vector<uint32_t> riverWords,voronoiWords;
    VoronoiBufferKey voronoiKey{};
    Buffer exactNoise;std::vector<ExactNoiseSpec> exactNoiseSpecs;std::vector<std::string> exactGradients;glm::uvec4 exactNoiseKey{0};bool exactNoiseValid=false;Buffer cornerLevels;   // binding 17: corner pre-pass output; binding 16: seeded tables of the exact mod surface rule
    Image rawField,field,colors,taaHistory,taaOutput;
    VkDescriptorSet fields{};
    VkDescriptorSetLayout layout{};
    VkPipelineLayout computeLayout{},drawLayout{},atmosphereLayout{},taaLayout{};VkPipeline atmospherePipeline{},taaPipeline{};VkDescriptorSetLayout inputLayout{},taaSetLayout{};
    std::array<VkPipeline,16> generate{};
    std::array<VkPipeline,9> worldFilters{};
    VkPipeline filter{},drawPipeline{},drawPipelineCulled{},buildMountainCache{},buildMountainSegments{},buildWorldMap{},buildOceanFloor{},buildIslandSurface{},buildClimate{},buildRivers{},buildBiomes{},buildRecolor{},buildInspection{},buildCorners{};
    VkRenderPass renderPass{};
    VkSampler sampler{};
    struct WorkStep {VkPipeline pipeline{};uint32_t x=0,y=0,tile=16,action=0,row=0;}; // row: first workgroup row of a band (biomes.comp reads it from domain.w)
    std::vector<WorkStep> workSteps;size_t workIndex=0;uint32_t workX=0,workY=0,workTiles=1;
    uint64_t workRevision=0,riverEpoch=0;bool workActive=false,inputsValid=false,preClimateValid=false,preFilterValid=false;
    GenerationInputs completedInputs{},workInputs{};
    Image preClimate,preFilter;Buffer preClimateCells,preFilterCells;
    static GenerationCacheHeader cacheHeaderForGeneration(const TerrainParams&,const GraphSeeds&);
    void ensureCheckpoints();void checkpoint(VkCommandBuffer cmd,uint32_t action);
    bool generateWork(VkCommandBuffer cmd,uint32_t frame,const TerrainParams& p,const GraphSeeds& seeds,uint64_t revision);
    void drawMesh(VkCommandBuffer cmd,const DrawParams& draw);
    void releaseFrames();
    void releaseFields();
};
