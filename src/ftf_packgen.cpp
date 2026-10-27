#include "ftf_packgen.hpp"
#include <functional>
#include <cmath>
#include <set>
#include <algorithm>

namespace {
using J=PackJson;
const std::string custom="data/freeterraforged/freeterraforged/worldgen/",noise=custom+"noise/";
const std::string density="data/minecraft/worldgen/density_function/overworld/";
const std::string settings="data/minecraft/worldgen/noise_settings/overworld.json";
void visit(J& j,const std::function<void(J&)>& fn){if(j.is_structured())for(auto& v:j)visit(v,fn);fn(j);}
J binary(const char* op,J a,J b){return {{"type",std::string("minecraft:")+op},{"argument1",a},{"argument2",b}};}
J probability(float p,J input){if(p==0)return 1.0;return binary("add",static_cast<double>(1.0f-p),std::move(input));}
J edgeNoise(const J& e){
    int scale=e.at("scale");if(scale<1)throw std::runtime_error("FTF datapack biome-edge scale must be positive");
    std::string type=e.at("type");J node;
    if(type=="CONST"||type=="CONSTANT")return .5;
    if(type=="CELL"||type=="VORONOI")node={{"type","freeterraforged:worley"},{"frequency",1.0f/scale},{"distance",1.0},{"cell_function","CELL_VALUE"},{"distance_function","EUCLIDEAN"},{"lookup",0.0}};
    else if(type=="CELL_EDGE"||type=="VORONOI_EDGE")node={{"type","freeterraforged:worley_edge"},{"frequency",1.0f/scale},{"distance",1.0},{"edge_function","DISTANCE_2"},{"distance_function","EUCLIDEAN"}};
    else if(type=="SIN")node={{"type","freeterraforged:sin"},{"frequency",1.0},{"alpha",0.0}};
    else if(type=="RAND"||type=="WHITE")node={{"type","freeterraforged:white"},{"frequency",1.0}};
    else{
        const std::map<std::string,std::string> types={{"BILLOW","billow"},{"CUBIC","cubic"},{"PERLIN","perlin"},{"PERLIN2","perlin2"},{"RIDGE","perlin_ridge"},{"PERLIN_RIDGE","perlin_ridge"},{"SIMPLEX","simplex"},{"SIMPLEX2","simplex2"},{"SIMPLEX_RIDGE","simplex2"}};
        auto found=types.find(type);if(found==types.end())throw std::runtime_error("Unknown FTF biome-edge noise type: "+type);
        node={{"type","freeterraforged:"+found->second},{"frequency",1.0f/scale},{"octaves",e.at("octaves")},{"lacunarity",e.at("lacunarity").get<float>()},{"gain",e.at("gain").get<float>()}};
        if(type!="CUBIC")node["interpolation"]={{"value","CURVE3"}};
        if(type=="PERLIN"||type=="PERLIN2")node["seed"]=0;
    }
    if(type!="PERLIN"&&type!="PERLIN2")node={{"type","freeterraforged:shift"},{"input",node},{"shift",0}};
    return {{"type","freeterraforged:add"},{"input1",node},{"input2",-.5}};
}
}
Datapack generateFtfDatapack(const PackJson& preset,const Datapack& sourceTemplate){
    for(auto section:{"world","climate","terrain","rivers","miscellaneous"})if(!preset.contains(section)||!preset.at(section).is_object())throw std::runtime_error("FTF datapack preset requires section: "+std::string(section));
    Datapack generated=sourceTemplate;auto get=[&](const std::string& name){return J::parse(generated.files.at(name));};
    auto put=[&](const std::string& name,const J& j){generated.files[name]=j.dump(2)+"\n";};
    auto props=preset.at("world").at("properties");auto baseProps=sourceTemplate.original.at("world").at("properties");
    int height=props.at("worldHeight"),depth=props.value("worldDepth",64),sea=props.at("seaLevel");
    int baseHeight=baseProps.at("worldHeight"),baseDepth=baseProps.value("worldDepth",64),factor=std::min(height,256);
    if(height<=0||height>2032||depth<0||depth>2032||height+depth>4064||(height+depth)%16||depth%16||sea< -depth||sea>=height)throw std::runtime_error("Minecraft export needs a supported dimension height/min Y aligned to 16, and sea level inside the dimension. Preview and JSON export can use other values.");
    J caves=preset.value("caves",sourceTemplate.original.at("caves"));
    J misc=preset.value("miscellaneous",sourceTemplate.original.at("miscellaneous"));
    auto flag=[&](const char* name,bool fallback){return misc.value(name,fallback);};
    bool erode=flag("screeDecorator",flag("erosionDecorator",true)),snow=flag("naturalSnowDecorator",true),smooth=flag("smoothLayerDecorator",true),strata=flag("strataDecorator",true),features=flag("customBiomeFeatures",false);
    auto dimension=get("data/minecraft/dimension_type/overworld.json");dimension["min_y"]=-depth;dimension["height"]=height+depth;dimension["logical_height"]=height+depth;put("data/minecraft/dimension_type/overworld.json",dimension);
    auto ns=get(settings);ns["noise"]["min_y"]=-depth;ns["noise"]["height"]=height+depth;ns["sea_level"]=sea;ns["ore_veins_enabled"]=caves.at("largeOreVeins");
    auto& choice=ns.at("noise_router").at("final_density").at("argument1").at("argument").at("argument2");
    choice.at("max_exclusive")=static_cast<double>(caves.at("cheeseCaveDepthOffset").get<float>());
    if(caves.at("entranceCaveProbability").get<float>()<=0)choice.at("when_in_range")="minecraft:overworld/sloped_cheese";
    auto& surface=ns.at("surface_rule").at("sequence");if(!strata)surface.erase(surface.begin()+1,surface.end());
    std::string spawn=props.at("spawnType");
    if(spawn=="USER_SELECTED"){
        J target={{"offset",0.0}};for(auto axis:{"continentalness","depth","erosion","humidity","temperature","weirdness"})target[axis]=J::array({-2.0,2.0});ns["spawn_target"]=J::array({target});
    }else if(spawn=="WORLD_ORIGIN"||spawn=="ISLANDS")ns["spawn_target"]=J::array();
    else if(spawn!="CONTINENT_CENTER")throw std::runtime_error("Unknown FTF spawn type: "+spawn);
    put(settings,ns);
    auto entrance=get(density+"caves/entrances.json");put(density+"caves/entrances.json",probability(caves.at("entranceCaveProbability").get<float>(),entrance.at("argument2")));
    auto spaghetti=get(density+"caves/spaghetti_2d.json");put(density+"caves/spaghetti_2d.json",probability(caves.at("spaghettiCaveProbability").get<float>(),spaghetti.at("argument2")));
    auto noodle=get(density+"caves/noodle.json");noodle["max_exclusive"]=static_cast<double>(1.0f-caves.at("noodleCaveProbability").get<float>());put(density+"caves/noodle.json",noodle);
    // The source router bakes vertical gradients, bottom slides and noodle bounds.
    for(auto& [name,bytes]:generated.files)if(name.starts_with(density)||name==settings){
        auto tree=J::parse(bytes);visit(tree,[&](J& v){if(!v.is_object())return;auto type=v.value("type",std::string());
            if(type=="minecraft:y_clamped_gradient"){
                if(v.value("from_y",0)==-baseDepth&&v.value("to_y",0)==baseHeight){
                    double a=v.at("from_value"),b=v.at("to_value");v["from_y"]=-depth;v["to_y"]=height;
                    if(std::abs(a-(1+baseDepth/128.0))<1e-5&&std::abs(b-(1-baseHeight/128.0))<1e-5){v["from_value"]=1+depth/128.0;v["to_value"]=1-height/128.0;}
                    else if(a==baseDepth/8.0&&b==-baseHeight/8.0){v["from_value"]=depth/8.0;v["to_value"]=-height/8.0;}
                    else throw std::runtime_error("FTF source template has an unexpected vertical gradient");
                }else if(v.value("from_y",0)==-baseDepth&&v.value("to_y",0)==-baseDepth+24){v["from_y"]=-depth;v["to_y"]=-depth+24;}
            }
            if(type=="minecraft:range_choice"&&v.value("input",J())=="minecraft:y"&&v.value("min_inclusive",0.0)==-baseDepth+4.0&&v.value("max_exclusive",0.0)==baseHeight+1.0){v["min_inclusive"]=-depth+4.0;v["max_exclusive"]=height+1.0;}
            if(type=="freeterraforged:clamp_to_nearest_unit")v["resolution"]=factor;
            if(type=="minecraft:add"&&v.contains("argument2")&&v.at("argument2").is_object()){
                auto& b=v.at("argument2");
                if(b.value("noise",std::string())=="minecraft:cave_cheese")v=probability(caves.at("cheeseCaveProbability").get<float>(),b);
                else if(b.value("type",std::string())=="minecraft:mul"&&b.contains("argument2")&&b.at("argument2").is_object()&&b.at("argument2").value("noise",std::string())=="minecraft:spaghetti_2d_elevation"){
                    double min=std::floor(-depth/8.0);v["argument1"]=(8.0+min)/2.0;b["argument1"]=(8.0-min)/2.0;
                }
            }
        });bytes=tree.dump(2)+"\n";
    }
    put(noise+"terrain/ground.json",static_cast<float>(sea)/factor);
    put(noise+"climate/biome_edge_shape.json",edgeNoise(preset.at("climate").at("biomeEdgeShape")));
    auto desert=get(noise+"surface/desert.json");desert.at("input").at("input2")=16.0f/factor;put(noise+"surface/desert.json",desert);
    for(auto kind:{"deep","shallow"})for(auto direction:{"up","down","top"}){
        std::string name=noise+"surface/iceberg/"+kind+"/"+direction+".json";auto j=get(name);
        if(std::string(direction)=="top"){j.at("input1").at("input2")=3.0f/factor;j.at("input2")=2.0f/factor;}
        else j.at("input2")=static_cast<float>(std::string(kind)=="deep"?30:(std::string(direction)=="up"?20:15))/factor;put(name,j);
    }
    int size=misc.value("strataRegionSize",600);if(size<4)throw std::runtime_error("FTF datapack strata region size must be at least 4 (integer warp scale)");
    for(auto prefix:{"strata/","surface/strata/"}){
        auto name=noise+prefix+"selector.json";auto j=get(name);auto& inner=j.at("input");inner.at("domain").at("distance")=static_cast<float>(size)/2.0f;
        for(auto axis:{"x","z"})inner.at("domain").at(axis).at("frequency")=1.0f/(size/4);
        inner.at("input").at("input").at("frequency")=1.0f/size;put(name,j);
    }
    auto strataDepth=get(noise+"strata/depth.json");strataDepth["frequency"]=1.0f/size;put(noise+"strata/depth.json",strataDepth);
    // Optional registry entries follow the same conditions as Preset*Features.
    std::vector<std::string> remove;
    for(auto& [name,bytes]:generated.files){
        if(name.starts_with(custom+"biome_modifier/")){
            auto stem=std::filesystem::path(name).stem().string();bool keep=stem=="add_swamp_surface"||stem=="add_river_gasket"||(stem=="add_erosion"?erode:stem=="add_snow_processing"?(snow||smooth):features);if(!keep)remove.push_back(name);
        }else if(name.starts_with("data/freeterraforged/worldgen/configured_feature/")||name.starts_with("data/freeterraforged/worldgen/placed_feature/")){
            auto stem=std::filesystem::path(name).stem().string();bool keep=stem=="swamp_surface"||stem=="river_gasket"||(stem=="erode"||stem=="scree"?erode:stem=="decorate_snow"?(snow||smooth):features);if(!keep)remove.push_back(name);
        }else if(name.starts_with("data/minecraft/worldgen/configured_feature/disk_")&&!features)remove.push_back(name);
        else if(name.starts_with("data/minecraft/worldgen/placed_feature/"))remove.push_back(name);
    }for(auto& name:remove)generated.files.erase(name);
    auto blacklist=[&](const std::string& name,const std::string& feature){put("data/minecraft/worldgen/placed_feature/"+name+".json",{{"feature","minecraft:"+feature},{"placement",J::array({{{"type","freeterraforged:blacklist_dimension"},{"blacklist",J::array({"minecraft:overworld"})}}})}});};
    if(!flag("vanillaSprings",true))blacklist("spring_water","spring_water");
    if(!flag("vanillaLavaSprings",true)){blacklist("spring_lava","spring_lava_overworld");blacklist("spring_lava_frozen","spring_lava_frozen");}
    if(!flag("vanillaLavaLakes",true)){blacklist("lake_lava_surface","lake_lava");blacklist("lake_lava_underground","lake_lava");}
    if(strata){for(auto material:{"granite","diorite","andesite"})for(auto level:{"upper","lower"})blacklist(std::string("ore_")+material+"_"+level,std::string("ore_")+material);blacklist("ore_dirt","ore_dirt");blacklist("ore_gravel","ore_gravel");}
    auto surfaceSettings=preset.value("surface",sourceTemplate.original.at("surface"));
    auto scree=surfaceSettings.contains("scree")?surfaceSettings.at("scree"):surfaceSettings.at("erosion");
    auto cfg=J::parse(sourceTemplate.files.at("data/freeterraforged/worldgen/configured_feature/erode.json")).at("config");
    for(auto [json,presetKey]:{std::pair{"rock_var","rockVariance"},{"rock_min","rockMin"},{"dirt_var","dirtVariance"},{"dirt_min","dirtMin"},{"rock_steepness","rockSteepness"},{"dirt_steepness","dirtSteepness"},{"scree_steepness","screeSteepness"}})cfg[json]=scree.at(presetKey);
    if(erode)put("data/freeterraforged/worldgen/configured_feature/erode.json",{{"type","freeterraforged:erode"},{"config",cfg}});
    if(snow||smooth)put("data/freeterraforged/worldgen/configured_feature/decorate_snow.json",{{"type","freeterraforged:decorate_snow"},{"config",{{"erode",snow},{"smooth",smooth},{"erode_config",cfg}}}});
    generated.original=preset;generated.files[generated.presetPath]=preset.dump(2)+"\n";return generated;
}
