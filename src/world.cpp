#include "world.hpp"
#include "mc_random.hpp"
#include "vanilla_noises_generated.hpp"
#include <cmath>
#include <limits>
#include <bit>
#include <algorithm>

static uint32_t worldSeedInt(uint64_t value){
    if(value<=0x7fffffffull||value>=0xffffffff80000000ull)return static_cast<uint32_t>(value);
    value=(value^(value>>33))*static_cast<uint64_t>(-49064778989728563ll);value=(value^(value>>33))*static_cast<uint64_t>(-4265267296055464877ll);value^=value>>33;return static_cast<uint32_t>(value^(value>>32));
}
void buildUpliftInputs(const nlohmann::ordered_json& preset,int64_t seed,GraphSeeds& output,bool islands){
    using Pointer=nlohmann::ordered_json::json_pointer;
    auto value=[&](const char* path,float fallback){auto p=Pointer(path);double v=preset.contains(p)?preset.at(p).get<double>():fallback;if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<float>::max())throw std::runtime_error("Uplift cannot represent "+std::string(path));return static_cast<float>(v);};
    auto type=preset.at(Pointer("/world/continent/continentType")).get<std::string>();
    uint32_t kind=type=="UPLIFT"?0:type=="MULTI"?1:type=="SINGLE"?2:type=="MULTI_IMPROVED"?3:999;
    if(kind==999)throw std::runtime_error("Unknown FTF continent type: "+type);
    auto shape=preset.at(Pointer("/world/continent/continentShape")).get<std::string>();uint32_t distance=shape=="EUCLIDEAN"?0:shape=="MANHATTAN"?1:shape=="NATURAL"?2:999;
    if(distance==999)throw std::runtime_error("Unknown FTF continent distance: "+shape);
    bool simple=kind==1||kind==2;
    glm::vec4 continent{value("/world/continent/continentScale",4000),value("/world/continent/continentJitter",.8f),value("/world/continent/continentSkipping",.25f),value("/world/continent/continentSizeVariance",.25f)};
    glm::vec4 noise{value("/world/continent/continentNoiseOctaves",5),value("/world/continent/continentNoiseGain",.26f),value("/world/continent/continentNoiseLacunarity",4.33f),0};
    glm::vec4 points{value("/world/controlPoints/deepOcean",.102f),value("/world/controlPoints/shallowOcean",.13144f),value("/world/controlPoints/coast",.3576f),value("/world/controlPoints/inland",.4717f)};
    glm::vec4 levels{std::max(10.0f,value("/world/properties/oceanDepth",63)),std::max(0.0f,value("/world/properties/worldDepth",64)),value("/world/properties/seaLevel",63),std::min(value("/world/properties/worldHeight",256),256.0f)};
    if(continent.x<2||continent.x>536870880.0f||std::floor(continent.x)!=continent.x)throw std::runtime_error("Uplift paused: continent scale must fit a positive FTF tectonic scale and cliff scale");
    if(!simple){if(noise.x<1||noise.x>12||std::floor(noise.x)!=noise.x||noise.y<=0||noise.z<0)throw std::runtime_error("Uplift paused: noise octaves must be 1..12, gain positive and lacunarity non-negative");
    float amp=noise.y,bound=0;for(int i=0;i<static_cast<int>(noise.x);++i){bound+=.9f*amp;amp*=noise.y;}
    if(!std::isfinite(bound)||!std::isfinite(amp)||bound<=0)throw std::runtime_error("Continent preview paused: noise normalization overflowed");}
    if(!(points.x<points.y&&points.y<points.z&&points.z<points.w&&points.w>0))throw std::runtime_error("Uplift paused: require Deep Ocean < Shallow Ocean < Coast < Inland and positive Inland");
    if(levels.w<=0||levels.x>100000000||levels.y>100000000)throw std::runtime_error("Uplift paused: world depth and ocean depth exceed the preview's noise domain");
    auto root=static_cast<uint64_t>(seed);
    // Border plains consume root+0..4. Uplift uses +5..11. Climate consumes
    // seven more parent seeds (+12..18) before deep ocean takes root+19.
    output.worldSeeds0={worldSeedInt(root+5),worldSeedInt(root+6),worldSeedInt(root+7),worldSeedInt(root+8)};
    output.worldSeeds1={worldSeedInt(root+9),worldSeedInt(root+10),worldSeedInt(root+11),worldSeedInt(root+19)};
    noise.w=static_cast<float>(kind|(distance<<4));
    if(simple)output.worldSeeds1.w=worldSeedInt(root+16);
    output.continent=continent;output.continentNoise=noise;output.controlPoints=points;output.worldLevels=levels;
    bool enabled=islands&&(!preset.contains(Pointer("/island/enableArchipelago"))||preset.at(Pointer("/island/enableArchipelago")).get<bool>());
    output.island0=output.island1=output.island2=output.island3=glm::vec4(0);output.islandMeta=glm::uvec4(0);
    if(!enabled)return;
    output.island0={value("/island/islandDensity",.4f),value("/island/islandSize",150),value("/island/islandHeight",.1f),value("/island/islandBaseScale",.1f)};
    output.island1={value("/island/islandVerticalScale",1.5f),value("/island/islandHorizontalScale",5),value("/island/mountainChance",.5f),value("/island/mountainScale",.4f)};
    output.island2={value("/island/volcanoChance",.5f),value("/island/volcanismScale",.85f),value("/island/volcanismHorizontalScale",.7f),value("/island/mountainHorizontalScale",.7f)};
    output.island3={value("/island/offshoreDepth",.275f),value("/island/beachWidth",.225f),value("/island/beachCoverage",.6f),value("/island/macroDensityPercentage",1)};
    if(std::abs(output.island0.y)>1000000.0f)throw std::runtime_error("Island size exceeds the preview's integer noise domain");
    output.islandMeta={1,worldSeedInt(root+(simple?17:20)),std::bit_cast<uint32_t>(value("/world/controlPoints/islandCoast",.074f)),0};
}

void buildClimateInputs(const nlohmann::ordered_json& preset,int64_t seed,GraphSeeds& output,bool enabled){
    output.climateMeta=output.climateSeeds0=output.climateSeeds1=output.climateSeeds2=glm::uvec4(0);
    output.climateShape=output.climateEdge=output.climateEffects=output.temperature0=output.temperature1=output.moisture0=output.moisture1=glm::vec4(0);
    if(!enabled)return;
    using Pointer=nlohmann::ordered_json::json_pointer;
    auto value=[&](const std::string& path,float fallback){auto p=Pointer(path);double v=preset.contains(p)?preset.at(p).get<double>():fallback;if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<float>::max())throw std::runtime_error("Climate cannot represent "+path);return static_cast<float>(v);};
    auto integer=[&](const std::string& path,int fallback){double v=value(path,static_cast<float>(fallback));if(v<std::numeric_limits<int32_t>::min()||v>2147483520.0||std::floor(v)!=v)throw std::runtime_error("Climate integer outside source domain: "+path);return static_cast<int>(v);};
    auto offset=[&](const std::string& path){auto p=Pointer(path);return preset.contains(p)?preset.at(p).get<int32_t>():0;};
    auto root=static_cast<uint64_t>(seed);int continentKind=static_cast<int>(output.continentNoise.w)&3;uint64_t parent=root-((continentKind==1||continentKind==2)?3:0);uint64_t moistRoot=root+static_cast<uint64_t>(static_cast<int64_t>(offset("/climate/moisture/seedOffset"))),tempRoot=root+static_cast<uint64_t>(static_cast<int64_t>(offset("/climate/temperature/seedOffset")));
    output.climateSeeds0={worldSeedInt(parent+12),worldSeedInt(parent+13),worldSeedInt(parent+14),worldSeedInt(parent+17)};
    output.climateSeeds1={worldSeedInt(parent+18),worldSeedInt(moistRoot),worldSeedInt(moistRoot+1),worldSeedInt(moistRoot+2)};
    output.climateSeeds2={worldSeedInt(tempRoot),worldSeedInt(tempRoot+1),worldSeedInt(parent+15),0};
    const auto type=preset.at(Pointer("/climate/biomeEdgeShape/type")).get<std::string>();
    uint32_t kind=type=="SIMPLEX"?0:type=="SIMPLEX2"||type=="SIMPLEX_RIDGE"?1:type=="PERLIN"?2:type=="PERLIN2"?3:type=="PERLIN_RIDGE"||type=="RIDGE"?4:type=="CONSTANT"||type=="CONST"?5:type=="BILLOW"?6:type=="VORONOI"||type=="CELL"?7:type=="VORONOI_EDGE"||type=="CELL_EDGE"?8:type=="CUBIC"?9:type=="SIN"?10:type=="WHITE"||type=="RAND"?11:999;
    if(kind==999)throw std::runtime_error("Climate paused: biome edge noise type "+type+" is not yet connected");
    output.climateMeta={1,kind,static_cast<uint32_t>(integer("/world/properties/worldHeight",256)),0};
    uint64_t terrainRoot=root+static_cast<uint64_t>(static_cast<int64_t>(offset("/terrain/general/terrainSeedOffset")));
    output.biomeSeeds={worldSeedInt(terrainRoot+48291),worldSeedInt(terrainRoot+73519),worldSeedInt(root+48291),worldSeedInt(root+73519)};
    output.biomeSeeds2={worldSeedInt(terrainRoot+713+48291),worldSeedInt(terrainRoot+719+48291),worldSeedInt(parent+20),0};
    output.biomeOptions={value("/world/controlPoints/beach",.221f),value("/terrain/general/mountainVariety",0),0,0};

    output.climateShape={value("/climate/biomeShape/biomeSize",586),value("/climate/biomeShape/biomeWarpScale",38),value("/climate/biomeShape/biomeWarpStrength",332),value("/climate/biomeShape/macroNoiseSize",4)};
    output.climateEdge={value("/climate/biomeEdgeShape/scale",137),value("/climate/biomeEdgeShape/octaves",5),value("/climate/biomeEdgeShape/lacunarity",10.5f),value("/climate/biomeEdgeShape/gain",1.601f)};
    output.climateEffects={value("/climate/altitudeCoolingStrength",2.2f),value("/climate/rainShadowStrength",1.8f),value("/climate/coastalMoistureBoost",.2f),value("/climate/biomeEdgeShape/strength",300)};
    auto range=[&](const std::string& name,glm::vec4& first,glm::vec4& second){std::string path="/climate/"+name+"/";float a=value(path+"min",0),b=value(path+"max",1),lo=std::clamp(std::min(a,b),0.0f,1.0f),hi=std::clamp(std::max(a,b),lo,1.0f);first={value(path+"scale",4),value(path+"falloff",4),lo,hi};second={std::clamp(value(path+"bias",0),-1.0f,1.0f),value(path+"distributionBias",0),0,0};};
    range("temperature",output.temperature0,output.temperature1);range("moisture",output.moisture0,output.moisture1);
    if(output.climateShape.x<1||output.climateShape.y<1||output.climateShape.w<1||output.climateEdge.x<1||output.climateEdge.y<1||output.climateEdge.y>12||output.climateEdge.z<0||output.climateEdge.w<=0||output.temperature0.x<1||output.temperature0.x>536870880.0f||output.moisture0.x<1||output.moisture0.x>800000000.0f||output.temperature0.y<0||output.temperature0.y>64||output.moisture0.y<0||output.moisture0.y>64||output.climateMeta.z<=output.worldLevels.z)throw std::runtime_error("Climate paused: invalid source noise scale, octave/falloff count or world height span");
}

void buildWorldFilterInputs(const nlohmann::ordered_json& preset,int64_t seed,GraphSeeds& output,bool enabled,int ftfProfile){
    output.filter0=output.filter1=output.filter2=output.filter3=glm::vec4(0);output.filterMeta=glm::uvec4(0);output.filter4=output.filter5=glm::vec4(0);output.filterMeta2=glm::uvec4(0);
    // Per-version erosion constants (Erosion.java of each build): wavelength, gate start, gate span, depth cap per slope, headroom margin.
    constexpr float flatStart10=.15f,flatFull10=.6f,flatStart14=.05f,flatFull14=.15f;
    const bool chunkFilters=ftfProfile==1;
    output.filter4=chunkFilters?glm::vec4(300.0f,flatStart14,std::max(1e-5f,flatFull14-flatStart14),.08f):glm::vec4(96.0f,flatStart10,std::max(1e-5f,flatFull10-flatStart10),.5f);
    output.filter5=glm::vec4(chunkFilters?1.5f:.5f,0,0,0);
    // meta2: profile, table sine/cosine (1.0.0013+), ceiling in the terrain stage (1.0.0013+), bit0 = cliff relaxation + Phacelle erosion run (always, for the supported versions).
    output.filterMeta2={static_cast<uint32_t>(ftfProfile),chunkFilters?1u:0u,chunkFilters?1u:0u,1u};
    output.filter5.y=preset.at(nlohmann::ordered_json::json_pointer("/world/properties/worldHeight")).get<float>(); // the terrain-stage ceiling needs it even with the filters off
    if(!enabled)return;
    using Pointer=nlohmann::ordered_json::json_pointer;
    auto value=[&](const std::string& key,float fallback){auto p=Pointer("/erosionFilters/"+key);double v=preset.contains(p)?preset.at(p).get<double>():fallback;if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<float>::max())throw std::runtime_error("World filters cannot represent "+key);return static_cast<float>(v);};
    output.filter0={value("scale",.233f),value("strengthMultiplier",81),value("gullySharpness",.2f),value("flowOctaves",8)};
    output.filter1={value("lacunarity",.636f),value("gain",.254f),value("phacelleScale",1.918f),value("phacelleOffset",.5f)};
    output.filter2={value("phacelleNormalization",0),value("slopeOnsetBase",.529f),value("slopeOnsetRidge",1.559f),value("slopeOnsetOctave",.76f)};
    output.filter3={value("roundingMin",100),value("roundingMax",50),preset.at(Pointer("/world/properties/worldHeight")).get<float>(),preset.at(Pointer("/world/controlPoints/beach")).get<float>()};
    uint32_t root=worldSeedInt(static_cast<uint64_t>(seed));output.filterMeta={1,root+12768u,root+7919u,output.mountainMeta.z};
    if(output.filter0.x<=0||output.filter1.z<=0||output.filter0.w<1||output.filter0.w>32||std::floor(output.filter0.w)!=output.filter0.w||output.filter1.x<=0||output.filter1.y<0||output.filter3.z<=output.worldLevels.z)throw std::runtime_error("World filters paused: require positive source scales/lacunarity, non-negative gain and flow octaves1..32");
}

// FTF StrataRule "freeterraforged:overworld_strata" (PresetSurfaceRuleData.makeStrataRule): 100 stacks drawn from the world's
// "freeterraforged:geology" random exactly as StrataRule.generateStrata does. Materials index the FTF block tags in datapack order:
// soil = dirt, coarse_dirt; sediment = sand, gravel; clay = clay; rock = granite, andesite, stone, diorite.
static void buildStrata(int64_t seed,int size,GraphSeeds& output){
    static int64_t cachedSeed=0;static bool cached=false;static std::array<uint32_t,100*41> stacks{};
    if(!cached||cachedSeed!=seed){
        struct Strata {uint32_t first,count;int attempts,minLayers,maxLayers;float minDepth,maxDepth;};
        static const Strata kinds[4]={{0,2,3,0,1,.1f,.25f},{2,2,3,0,2,.05f,.15f},{4,1,3,0,2,.05f,.1f},{5,4,3,10,30,.1f,1.5f}};
        auto random=mc::randomFactory(mc::randomStateRoot(seed),"freeterraforged:geology").fromHashOf("freeterraforged:overworld_strata");stacks.fill(0);
        for(uint32_t stack=0;stack<100;++stack){uint32_t* words=&stacks[stack*41];uint32_t n=0;
            for(const auto& s:kinds){
                int last=-1;float r=random.nextFloat()*float(s.maxLayers-s.minLayers);int layers=s.minLayers+(r>=0.0f?int(r+.5f):int(r-.5f));random.nextInt();   // NoiseUtil.round; the strata seed is unused by the depth noise
                for(int i=0;i<layers;++i){
                    int attempts=s.attempts,index=random.nextInt(int(s.count));while(--attempts>=0&&index==last)index=random.nextInt(int(s.count));if(index==last)continue;last=index;
                    float depth=s.minDepth+random.nextFloat()*(s.maxDepth-s.minDepth);random.nextInt();   // the layer's seed shift: the Perlin depth noise ignores it
                    words[1+n]=std::bit_cast<uint32_t>(depth);words[36+n/8]|=(s.first+uint32_t(index))<<(4*(n%8));++n;
                }
            }
            words[0]=n;
        }
        cachedSeed=seed;cached=true;
    }
    output.strata=stacks;output.strataMeta={100u,uint32_t(size),std::bit_cast<uint32_t>(1.0f/float(size)),std::bit_cast<uint32_t>(1.0f/float(size/4))};
}

// Minecraft 1.21.1 surface context for the world seed (RandomState / SurfaceSystem / Biome statics), bit-exact; see vanilla_surface.glsl.
static void buildVanillaSurface(int64_t seed,GraphSeeds& output){
    static int64_t cachedSeed=0;static bool cached=false;static GraphSeeds tables;
    if(!cached||cachedSeed!=seed){
        auto pack=[](const mc::Xoroshiro::Positional& f){return glm::uvec4(uint32_t(f.lo),uint32_t(f.lo>>32),uint32_t(f.hi),uint32_t(f.hi>>32));};
        auto root=mc::randomStateRoot(seed);tables.surfaceRandom=pack(root);
        const auto& gradients=vanillaGradientRandoms();if(gradients.size()>tables.gradientRandoms.size())throw std::runtime_error("Too many vertical_gradient randoms");
        for(size_t i=0;i<gradients.size();++i)tables.gradientRandoms[i]=pack(mc::randomFactory(root,gradients[i]));
        // SurfaceSystem.generateBands(root.fromHashOf("minecraft:clay_bands")).
        std::array<uint8_t,192> bands{};auto random=root.fromHashOf("minecraft:clay_bands");
        for(int i=0;i<192;++i){i+=random.nextInt(5)+1;if(i<192)bands[i]=1;}
        auto makeBands=[&](int minSize,uint8_t code){int count=random.nextInt(10)+6;for(int j=0;j<count;++j){int size=minSize+random.nextInt(3),start=random.nextInt(192);for(int m=0;start+m<192&&m<size;++m)bands[start+m]=code;}};
        makeBands(1,2);makeBands(2,3);makeBands(1,4);
        int whites=random.nextInt(7)+9;for(int j=0,k=0;j<whites&&k<192;++j,k+=random.nextInt(16)+4){bands[k]=5;if(k-1>0&&(random.nextLong()&1))bands[k-1]=6;if(k+1<192&&(random.nextLong()&1))bands[k+1]=6;}
        tables.clayBands.fill(0);for(int i=0;i<192;++i)tables.clayBands[i/4]|=uint32_t(bands[i])<<(8*(i%4));
        // Biome.TEMPERATURE_NOISE and FROZEN_TEMPERATURE_NOISE (fixed seeds; PerlinSimplexNoise creates the octave-0 level first).
        auto putPerm=[&](int index,const std::array<uint8_t,256>& p){for(int i=0;i<256;++i){uint32_t& w=tables.simplexPerms[index*64+i/4];if(i%4==0)w=0;w|=uint32_t(p[i])<<(8*(i%4));}};
        {mc::Legacy t(1234);putPerm(0,mc::simplexPermutation(t));mc::Legacy f(3456);putPerm(1,mc::simplexPermutation(f));putPerm(2,mc::simplexPermutation(f));putPerm(3,mc::simplexPermutation(f));}
        // RandomState.getOrCreateNoise: NormalNoise.create(root.fromHashOf(id)) builds two PerlinNoise, each from its own forkPositional().
        tables.improvedNoises.fill(0);uint32_t table=0;
        for(const auto& spec:vanillaNoiseSpecs()){auto source=root.fromHashOf(spec.name);
            for(int half=0;half<2;++half){auto octaves=source.forkPositional();
                for(size_t k=0;k<spec.amplitudes.size();++k){if(spec.amplitudes[k]==0.0)continue;if(table>=96)throw std::runtime_error("NormalNoise table overflow");
                    auto n=mc::improvedNoise(octaves.fromHashOf("octave_"+std::to_string(spec.firstOctave+int(k))));uint32_t* w=&tables.improvedNoises[table*70];
                    for(int c=0;c<3;++c){uint64_t bits=std::bit_cast<uint64_t>(c==0?n.xo:c==1?n.yo:n.zo);w[c*2]=uint32_t(bits);w[c*2+1]=uint32_t(bits>>32);}
                    for(int i=0;i<256;++i)w[6+i/4]|=uint32_t(n.p[i])<<(8*(i%4));++table;}}}
        cachedSeed=seed;cached=true;
    }
    output.surfaceRandom=tables.surfaceRandom;output.gradientRandoms=tables.gradientRandoms;output.clayBands=tables.clayBands;output.simplexPerms=tables.simplexPerms;output.improvedNoises=tables.improvedNoises;
}

// Seeded tables of an exact mod surface rule (prepare_exact_mod_rules.py), from the RandomState root that buildVanillaSurface
// packs into surfaceRandom: 8 vertical_gradient factories (4 words each), then the NormalNoise octave tables (70 words each) in
// the program's noise order, laid out as buildVanillaSurface lays out the vanilla ones.
std::vector<uint32_t> exactSurfaceNoiseWords(glm::uvec4 root,const std::vector<ExactNoiseSpec>& noises,const std::vector<std::string>& gradients){
    mc::Xoroshiro::Positional factory{uint64_t(root.x)|(uint64_t(root.y)<<32),uint64_t(root.z)|(uint64_t(root.w)<<32)};
    if(gradients.size()>8)throw std::runtime_error("Too many exact vertical_gradient randoms");
    std::vector<uint32_t> words(32,0u);
    for(size_t i=0;i<gradients.size();++i){auto f=mc::randomFactory(factory,gradients[i]);uint32_t* w=&words[i*4];w[0]=uint32_t(f.lo);w[1]=uint32_t(f.lo>>32);w[2]=uint32_t(f.hi);w[3]=uint32_t(f.hi>>32);}
    for(const auto& spec:noises){auto source=factory.fromHashOf(spec.name);
        for(int half=0;half<2;++half){auto octaves=source.forkPositional();
            for(size_t k=0;k<spec.amplitudes.size();++k){if(spec.amplitudes[k]==0.0)continue;
                auto n=mc::improvedNoise(octaves.fromHashOf("octave_"+std::to_string(spec.firstOctave+int(k))));size_t at=words.size();words.resize(at+70,0u);uint32_t* w=&words[at];
                for(int c=0;c<3;++c){uint64_t bits=std::bit_cast<uint64_t>(c==0?n.xo:c==1?n.yo:n.zo);w[c*2]=uint32_t(bits);w[c*2+1]=uint32_t(bits>>32);}
                for(int i=0;i<256;++i)w[6+i/4]|=uint32_t(n.p[i])<<(8*(i%4));}}}
    return words;
}

void buildSurfaceInputs(const nlohmann::ordered_json& preset,int64_t seed,GraphSeeds& output,bool enabled){
    output.scree0=output.scree1=output.surfaceOptions=glm::vec4(0);output.surfaceMeta=glm::uvec4(0);output.strataMeta=glm::uvec4(0);output.strata.fill(0);if(!enabled)return;
    using P=nlohmann::ordered_json::json_pointer;
    auto value=[&](const char* path,float fallback){auto p=P(path);double v=preset.contains(p)?preset.at(p).get<double>():fallback;if(!std::isfinite(v)||std::abs(v)>std::numeric_limits<float>::max())throw std::runtime_error("Surface colour value outside floating-point domain: "+std::string(path));return static_cast<float>(v);};
    auto toggle=[&](const char* path,bool fallback){auto p=P(path);return preset.contains(p)?preset.at(p).get<bool>():fallback;};
    output.scree0={value("/surface/erosion/rockVariance",30),value("/surface/erosion/rockMin",140),value("/surface/erosion/dirtVariance",40),value("/surface/erosion/dirtMin",95)};
    output.scree1={value("/surface/erosion/rockSteepness",.65f),value("/surface/erosion/dirtSteepness",.475f),value("/surface/erosion/screeSteepness",.4f),.023529412f};
    uint32_t flags=(toggle("/miscellaneous/erosionDecorator",toggle("/miscellaneous/screeDecorator",true))?1u:0u)|(toggle("/miscellaneous/strataDecorator",true)?2u:0u)|(toggle("/miscellaneous/naturalSnowDecorator",true)?4u:0u)|(toggle("/miscellaneous/smoothLayerDecorator",true)?8u:0u);
    output.surfaceMeta={flags,output.climateSeeds0.w,worldSeedInt(static_cast<uint64_t>(seed)),0};
    output.surfaceOptions={value("/miscellaneous/strataRegionSize",889),value("/world/properties/worldHeight",512),0,0};
    if((flags&2u)&&(output.surfaceOptions.x<4||output.surfaceOptions.x>2147483520.0f))throw std::runtime_error("Strata region size must be at least 4 (FTF warps at size/4)");
    if(flags&2u)buildStrata(seed,int(output.surfaceOptions.x),output);
    buildVanillaSurface(seed,output);
}
