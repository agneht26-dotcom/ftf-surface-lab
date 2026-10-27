#include "mix.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

static uint32_t mixedSeed(uint64_t seed){
    if(seed<=0x7fffffffull||seed>=0xffffffff80000000ull)return static_cast<uint32_t>(seed);
    seed=(seed^(seed>>33))*static_cast<uint64_t>(-49064778989728563ll);
    seed=(seed^(seed>>33))*static_cast<uint64_t>(-4265267296055464877ll);seed^=seed>>33;return static_cast<uint32_t>(seed^(seed>>32));
}
struct JavaRandom {
    uint64_t seed;
    explicit JavaRandom(int32_t value):seed((static_cast<uint64_t>(static_cast<int64_t>(value))^0x5deece66dull)&((1ull<<48)-1)){}
    uint32_t next(){seed=(seed*0x5deece66dull+11)&((1ull<<48)-1);return static_cast<uint32_t>(seed>>17);}
    uint32_t bounded(uint32_t bound){
        if((bound&(bound-1))==0)return static_cast<uint32_t>((static_cast<uint64_t>(bound)*next())>>31);
        uint32_t bits,value;do{bits=next();value=bits%bound;}while(bits-value+bound-1>=0x80000000u);return value;
    }
};
void buildLandMix(const nlohmann::ordered_json& preset,int64_t worldSeed,GraphSeeds& output,bool chains){
    GraphSeeds next{};next.values=output.values;next.regionSeeds=output.regionSeeds;
    auto number=[&](const std::string& path,float fallback){auto pointer=nlohmann::ordered_json::json_pointer(path);double value=preset.contains(pointer)?preset.at(pointer).get<double>():fallback;if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max())throw std::runtime_error("Land mix cannot represent "+path);return static_cast<float>(value);};
    const char* names[]={"plains","steppe","dales","hills","hills","plateau","badlands","torridonian"};
    for(uint32_t i=0;i<8;++i){std::string path="/terrain/"+std::string(names[i])+"/";
        next.familyScales[i]={number(path+"baseScale",1),number(path+"verticalScale",1),number(path+"horizontalScale",1),number(path+"weight",1)};
        if(next.familyScales[i].w<0)throw std::runtime_error("Land mix paused: weights must be non-negative; values remain saved for export");
    }
    for(uint32_t i=0;i<2;++i){if(i==1&&next.familyScales[i].w==0)continue;float horizontal=next.familyScales[i].z;if(horizontal<=0||std::round(250.0f*horizontal)<4||horizontal>static_cast<float>(std::numeric_limits<int32_t>::max()/500))throw std::runtime_error("Land mix paused: plains/steppe horizontal scales exceed the integer noise domain");}
    // Read the integer itself: converting a large signed seed offset through float loses bits.
    int64_t offset=preset.at(nlohmann::ordered_json::json_pointer("/terrain/general/terrainSeedOffset")).get<int64_t>();
    if(offset<std::numeric_limits<int32_t>::min()||offset>std::numeric_limits<int32_t>::max())throw std::runtime_error("Land mix seed offset exceeds an FTF integer");
    uint64_t root=static_cast<uint64_t>(worldSeed)+static_cast<uint64_t>(offset);
    const uint32_t starts[]={5,0,10,14,18,32,41,23,47};
    for(uint32_t i=0;i<9;++i)for(uint32_t j=0;j<16;++j)next.familySeeds[i][j]=mixedSeed(root+starts[i]+j);
    for(uint32_t j=0;j<16;++j)next.borderSeeds[j]=mixedSeed(static_cast<uint64_t>(worldSeed)+j);
    uint32_t regionSize=preset.at(nlohmann::ordered_json::json_pointer("/terrain/general/terrainRegionSize")).get<uint32_t>();
    if(regionSize<4||regionSize>2147483520u)throw std::runtime_error("Land mix paused: region size must be between 4 and the positive FTF integer limit");
    bool fancy=preset.at(nlohmann::ordered_json::json_pointer("/terrain/general/fancyMountains")).get<bool>();
    bool legacy=preset.at(nlohmann::ordered_json::json_pointer("/terrain/general/legacyMountainScaling")).get<bool>();
    float variety=number("/terrain/general/mountainVariety",0);
    float mountainWeight=number("/terrain/mountains/weight",1);
    if(mountainWeight<0)throw std::runtime_error("Land mix paused: mountain weight must be non-negative");
    glm::vec4 mountains{number("/terrain/mountains/baseScale",1),number("/terrain/mountains/verticalScale",1),number("/terrain/mountains/horizontalScale",1),0};
    next.mountainMeta={fancy,legacy,variety>0,chains};
    next.mountainScales[1]=mountains;
    next.mountainScales[0]=variety>0?mountains*glm::vec4(1-.15f*variety,1-.2f*variety,1+.35f*variety,0):mountains;
    next.mountainScales[2]=variety>0?mountains*glm::vec4(1+.15f*variety,1+.2f*variety,1-.35f*variety,0):mountains;
    for(uint32_t variant=0;variant<3;++variant){
        const auto& s=next.mountainScales[variant];
        if((mountainWeight>0||chains)&&(!std::isfinite(s.x)||!std::isfinite(s.y)||!std::isfinite(s.z)||s.z<=0||std::round((legacy?410.0f:610.0f)*s.z)<1||s.z>static_cast<float>(std::numeric_limits<int32_t>::max()/4500)))throw std::runtime_error("Land mix paused: mountain scales exceed the positive FTF noise domain");
        uint32_t at=variant==1?53:variant==0?713:713+13+(fancy?6:0);
        for(uint32_t type=0;type<3;++type){for(uint32_t j=0;j<8;++j)next.mountainSeeds[variant*3+type][j]=mixedSeed(root+at+j);at+=(type==0?3:type==1?4:6)+(fancy?2:0);}
        uint32_t chainAt=variant==1?2:variant==0?719:719+3+(fancy?2:0);
        for(uint32_t j=0;j<8;++j)next.chainSeeds[variant][j]=mixedSeed(root+chainAt+j);
    }
    if(chains&&!legacy&&std::round(1000.0f*mountains.z*2.25f)<1)throw std::runtime_error("Land mix paused: mountain-chain scale must round to a positive FTF integer");
    next.chainShapeSeeds={mixedSeed(root),mixedSeed(root+1),0,0};
    // All graphs are constructed in the original provider even when their weight is zero:
    // prefix 47, duplicate badlands 6, mountains 3+4+6 (+2 each if fancy), volcano 4.
    uint32_t pairStart=70+(fancy?6:0),pairCount=0;
    next.volcanoSeeds={mixedSeed(root+pairStart-4),mixedSeed(root+pairStart-3),mixedSeed(root+pairStart-2),mixedSeed(root+pairStart-1)};
    float volcanoWeight=number("/terrain/volcano/weight",1);if(volcanoWeight<0)throw std::runtime_error("Land mix paused: volcano weight must be non-negative");
    struct Node{uint32_t a,b,pair;float weight;};constexpr auto single=std::numeric_limits<uint32_t>::max();
    const uint32_t providerOrder[]={1,0,2,3,4,7,5,6};std::vector<uint32_t> active;std::vector<Node> nodes;
    for(auto i:providerOrder)if(next.familyScales[i].w>0){active.push_back(i);nodes.push_back({i,single,0,next.familyScales[i].w});}
    for(size_t i=0;i<active.size();++i)for(size_t j=i+1;j<active.size();++j){
        auto a=active[i],b=active[j];float weight=(next.familyScales[a].w+next.familyScales[b].w)/2;
        if(!std::isfinite(weight))throw std::runtime_error("Land mix paused: combined weight overflowed");
        next.mixSeeds[pairCount*2]=mixedSeed(root+pairStart+pairCount*2);next.mixSeeds[pairCount*2+1]=mixedSeed(root+pairStart+pairCount*2+1);
        nodes.push_back({a,b,pairCount++,weight});
    }
    nodes.push_back({8,single,0,next.familyScales[6].w});
    for(uint32_t i=0;i<3;++i)nodes.push_back({9+i,single,0,mountainWeight});
    nodes.push_back({12,single,0,volcanoWeight});
    uint32_t shuffleSeed=mixedSeed(root+pairStart+pairCount*2);JavaRandom random(static_cast<int32_t>(shuffleSeed));
    for(size_t i=nodes.size();i>1;--i)std::swap(nodes[i-1],nodes[random.bounded(static_cast<uint32_t>(i))]);
    float smallest=std::numeric_limits<float>::max();for(auto& n:nodes)if(n.weight>0)smallest=std::min(smallest,n.weight);
    if(smallest==std::numeric_limits<float>::max())throw std::runtime_error("Land mix paused: at least one implemented family needs a positive weight");
    uint32_t count=0,total=0;
    for(auto& n:nodes)if(n.weight>0){float ratio=n.weight/smallest;
        if(!std::isfinite(ratio)||ratio>8388606)throw std::runtime_error("Land mix paused: weight ratio exceeds the preview's exact indexing limit");
        auto repeat=static_cast<uint32_t>(std::floor(ratio+.5f));if(total+repeat>8388607u)throw std::runtime_error("Land mix paused: weighted slot count exceeds exact float indexing");
        total+=repeat;next.choices[count++]={total,n.a,n.b,n.pair};
    }
    next.mixMeta={count,total,regionSize,shuffleSeed};output=next;
}
