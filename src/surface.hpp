#pragma once
#include <map>
#include <string>
#include <optional>
#include "gpu.hpp"
#include <glm/glm.hpp>
#include <array>
#include <functional>

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
    GraphSeeds(){familyScales.fill(glm::vec4(0));choices.fill(glm::uvec4(0));mountainScales.fill(glm::vec4(0));}
};
static_assert(sizeof(GraphSeeds)==47744);   // stays below vkCmdUpdateBuffer's 65536-byte limit
static_assert(sizeof(TerrainSample)==32);
// One Minecraft NormalNoise of an exact mod surface rule (assets/mod-exact-surface-rules.json, normal edition only).
struct ExactNoiseSpec {std::string name;int firstOctave=0;std::vector<double> amplitudes;};
constexpr size_t exactNoiseCapacity=32+1024*70;   // words: 8 gradient factories, then up to 1024 ImprovedNoise tables
struct alignas(16) SurfaceInspection {glm::uvec4 request{};glm::vec4 values{};glm::uvec4 ids{};glm::vec4 extra{};};
static_assert(sizeof(SurfaceInspection)==64);
#include "generation_state.hpp"
struct alignas(16) GenerationCacheHeader {glm::ivec4 bounds[2]{};glm::uvec4 info{};};
struct EffectsBlock {glm::vec4 effects,screen,viewport,sun,shadow;glm::uvec4 terrain;glm::vec4 biomeArea,cue,hole,water,fog,cloud,cloud2;}; // ViewEffects SSBO; the first 144 bytes are what terrain/tree shaders read
static_assert(sizeof(EffectsBlock)==208);
struct StageTiming { double generate{},filter{},draw{},total{},cache{},world{},ocean{},compose{};bool valid{},generated{}; };
struct SurfaceFrame {
    Image color,depth,scene; // scene: shaded terrain before the atmosphere sub-pass writes `color`
    VkDescriptorSet texture{},inputs{};
    VkFramebuffer framebuffer{};
    VkQueryPool queries{},stepQueries{}; // stepQueries: per-dispatch timestamps when Surface::profileSteps is on
    std::vector<std::string> stepNames;
    uint64_t revision{};
    bool recorded{};
    bool generated{};
    uint32_t workStage=0,workTiles=0;
};
class Surface {
public:
    explicit Surface(Gpu& device):gpu(device){}
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
    bool frustumCull=false,distanceCull=false,backfaceCull=false,treeFrustumCull=true;float maxDrawDistance=50000;uint32_t culledPatches=0;
    glm::vec4 screenFocus{},viewportSize{},sun{-.47f,.8f,.33f,1},sunShadow{.65f,1500,.35f,0};
    TerrainParams lastParams{};
    glm::uvec4 terrainMask{};glm::vec4 biomeSearchArea{};
    // Preview trees (only the whole-world surface owns the resources). The caller sets `trees` every frame; placement reruns when its inputs change.
    struct TreeSettings {bool enabled=false,wholeMap=true;glm::vec2 center{},camera{},viewDirection{};bool viewTest=false;float viewCos=.55f,maxDistance=0;float radius=600,density=1,lodDistance=140;uint32_t typeMask=0xfff,seed=7;};
    TreeSettings trees;Surface* treeDetail=nullptr;float treeDetailRadius=0; // the high-detail patch drawn this frame (radius 0 = none)
    TerrainParams treeDetailParams{};
    bool treesAvailable()const{return treeInstances.handle!=nullptr;}
    SurfaceInspection inspect(glm::vec2 world);
    std::optional<SurfaceInspection> findBiome(uint32_t biome,glm::vec2 center,float radius);
    std::vector<uint32_t> generatedBiomes();   // canonical biome ids present anywhere in the last generated grid
    Surface* treeRing=nullptr;TerrainParams treeRingParams{};Surface* finerBand=nullptr;DrawParams finerBandDraw{};glm::vec4 detailHole{}; // xy absolute centre, z inner hole, w centred coarse field
    // Render-only atmosphere (second sub-pass): realistic water, height fog, volumetric clouds. All zero = exact copy of the scene.
    struct Atmosphere {glm::vec4 water{},fog{},cloud{},cloud2{};glm::mat4 inverseVP{1};glm::vec4 eye{},domain{},clip{};}; // inverseVP is camera-relative; clip: near, far
    Atmosphere atmosphere;
    glm::vec4 scaleCue{}; // camera x,y,z (absolute world) and strength of the 1-block / 16-block scale grid
    glm::vec4 effects{1,.34f,0,1}; // AO intensity, shadow intensity, elapsed seconds, shadow speed
    bool updateRivers(const std::vector<uint32_t>& data);
    static VkDeviceSize estimatedFieldBytes(uint32_t n){return static_cast<VkDeviceSize>(n)*n*68;}
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
    // Several full-resolution windows of the height field in one copy (waits for the GPU): origin.xy and size.zw per window, row-major results.
    std::vector<std::vector<float>> heightWindows(const std::vector<glm::ivec4>& windows);
    // Asynchronous variant: record a copy of the height field into this frame's staging buffer; read it back when that
    // frame slot comes round again (its fence has signalled), without ever stalling the queue.
    void recordHeightCopy(VkCommandBuffer cmd,uint32_t frame,const TerrainParams& params,uint64_t revision,glm::ivec2 offset={0,0},uint32_t side=0); // side>0: copy only a side x side window starting at offset
    bool collectHeights(uint32_t frame,std::vector<float>& heights,TerrainParams& params,uint64_t& revision);
    std::vector<TerrainSample> samples(uint32_t frame);
    void exportCells(const std::filesystem::path& file,bool extended=false);
    void screenshot(uint32_t frame,const std::filesystem::path& file);
    void destroy();
private:
    struct AsyncHeights {Buffer staging;uint32_t resolution=0;bool pending=false;TerrainParams params{};uint64_t revision=0;};
    std::vector<AsyncHeights> asyncHeights;
    bool sharedPipelines=false;Surface* pipelineOwner=nullptr;
    Buffer treeInstances,treeArgs,treeMesh,treeTable;VkPipeline treePlacePipeline{},treeDrawPipeline{};VkPipelineLayout treePlaceLayout{},treeDrawLayout{};
    std::vector<uint32_t> treeFirst,treeCount;uint32_t treeDrawTypeCount=12;float treeCullRadius=40;std::array<uint8_t,256> lastTreeKey{};bool treeKeyValid=false;
    void initializeTrees();void buildTreePipelines();
    void placeTrees(VkCommandBuffer cmd,const TerrainParams& params,uint64_t revision);
    void drawTrees(VkCommandBuffer cmd,const TerrainParams& params,const DrawParams& draw);
    void buildCompute(VkShaderModule module,VkPipeline& pipeline,int variant,bool filterStage);
    VkPipeline variantPipeline(uint32_t variant);
    Buffer indices,graphSeeds,mountainCache,cellState,riverNetworks,biomeTable,effectsBuffer,inspectionBuffer,sinTable;
    std::vector<uint32_t> riverWords;
    Buffer exactNoise;std::vector<ExactNoiseSpec> exactNoiseSpecs;std::vector<std::string> exactGradients;glm::uvec4 exactNoiseKey{0};bool exactNoiseValid=false;   // binding 16: seeded tables of the exact mod surface rule
    Image rawField,field,colors;
    VkDescriptorSet fields{};
    VkDescriptorSetLayout layout{};
    VkPipelineLayout computeLayout{},drawLayout{},atmosphereLayout{};VkPipeline atmospherePipeline{};VkDescriptorSetLayout inputLayout{};
    std::array<VkPipeline,16> generate{};
    std::array<VkPipeline,9> worldFilters{};
    VkPipeline filter{},drawPipeline{},drawPipelineCulled{},buildMountainCache{},buildMountainSegments{},buildWorldMap{},buildOceanFloor{},buildIslandSurface{},buildClimate{},buildRivers{},buildBiomes{},buildRecolor{},buildInspection{};
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
