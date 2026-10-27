#include "surface.hpp"
#include "ftf_sin_table.hpp"
#include <imgui_impl_vulkan.h>
#include "world.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <exception>
#include <map>
#include <mutex>
#include <thread>

namespace {
struct PipelineJob {std::string name;std::function<void()> run;};
void runPipelineJobs(std::vector<PipelineJob>& jobs,const std::function<void(uint32_t,uint32_t)>& progress){
    const bool logTimes=std::getenv("FTF_PIPELINE_LOG")!=nullptr;std::atomic<size_t> next{0},done{0};std::exception_ptr failure;std::mutex lock;
    auto worker=[&]{for(;;){size_t i=next.fetch_add(1);if(i>=jobs.size())return;try{auto began=std::chrono::steady_clock::now();jobs[i].run();if(logTimes)std::fprintf(stderr,"pipeline %-34s %6.2f s\n",jobs[i].name.c_str(),std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count());}catch(...){std::lock_guard<std::mutex> guard(lock);if(!failure)failure=std::current_exception();}done.fetch_add(1);}};
    unsigned count=std::min<unsigned>(std::clamp(std::thread::hardware_concurrency(),2u,32u),static_cast<unsigned>(jobs.size()));
    std::vector<std::thread> threads;for(unsigned i=0;i<count;++i)threads.emplace_back(worker);
    while(progress&&done.load()<jobs.size()){progress(static_cast<uint32_t>(done.load()),static_cast<uint32_t>(jobs.size()));std::this_thread::sleep_for(std::chrono::milliseconds(33));}
    for(auto& t:threads)t.join();if(failure)std::rethrow_exception(failure);
}
// One height float plus four segment endpoint floats per anchor, within 16 MiB.
constexpr uint32_t mountainCacheCapacity=(4u*1024*1024)/5;
using MountainCacheHeader=GenerationCacheHeader;
static_assert(sizeof(MountainCacheHeader)==48);
MountainCacheHeader cacheHeader(const TerrainParams& params,const GraphSeeds& seeds){
    MountainCacheHeader header;
    if((params.meta.z!=4&&params.meta.z!=5)||!seeds.mountainMeta.x)return header;
    bool used=seeds.mountainMeta.w!=0;for(uint32_t i=0;i<seeds.mixMeta.x;++i)if(seeds.choices[i].y>=9&&seeds.choices[i].y<=11)used=true;
    if(!used)return header;
    float frequency=1.0f/params.ftfScales.x;uint64_t counts[2]{};
    for(uint32_t octave=0;octave<2;++octave){
        float grid=octave==0?128.0f:128.0f*.8f,scale=octave==0?1.0f:3.1f;
        int64_t low[2],high[2];
        for(uint32_t axis=0;axis<2;++axis){
            float a=(-.5f*params.domain.x+params.domain[axis+1])*frequency;
            float b=(.5f*params.domain.x+params.domain[axis+1])*frequency;
            double minimum=std::floor(static_cast<double>((a-2.0f)*scale)/grid)-4;
            double maximum=std::floor(static_cast<double>((b+2.0f)*scale)/grid)+4;
            if(!std::isfinite(minimum)||!std::isfinite(maximum)||minimum<-2147483000.0||maximum>2147483000.0)return {};
            low[axis]=static_cast<int64_t>(minimum);high[axis]=static_cast<int64_t>(maximum);
        }
        uint64_t width=high[0]-low[0]+1,height=high[1]-low[1]+1;
        if(width>mountainCacheCapacity||height>mountainCacheCapacity||width*height>mountainCacheCapacity)return {};
        counts[octave]=width*height;
        header.bounds[octave]={static_cast<int32_t>(low[0]),static_cast<int32_t>(low[1]),static_cast<int32_t>(width),static_cast<int32_t>(height)};
    }
    uint64_t stride=counts[0]+counts[1],total=stride*(seeds.mountainMeta.w?12:9);
    if(total>mountainCacheCapacity||total>65535u*64u)return {};
    header.info={static_cast<uint32_t>(counts[0]),static_cast<uint32_t>(stride),static_cast<uint32_t>(total),1};return header;
}
}

GenerationCacheHeader Surface::cacheHeaderForGeneration(const TerrainParams& p,const GraphSeeds& g){return cacheHeader(p,g);}

void Surface::initialize(Surface* shared,std::function<void(uint32_t,uint32_t)> progress,bool allVariants){
    auto biomePath=gpu.shaderRoot.parent_path()/"assets"/"biomes-1.21.1.bin";std::ifstream biomeInput(biomePath,std::ios::binary|std::ios::ate);if(!biomeInput)throw std::runtime_error("Missing Minecraft biome data: "+biomePath.string());size_t biomeBytes=static_cast<size_t>(biomeInput.tellg());if(biomeBytes<32||biomeBytes>4*1024*1024)throw std::runtime_error("Invalid Minecraft biome table");std::vector<char> biomeData(biomeBytes);biomeInput.seekg(0);biomeInput.read(biomeData.data(),biomeBytes);if(!biomeInput)throw std::runtime_error("Cannot read Minecraft biome table");
    auto materialPath=gpu.shaderRoot.parent_path()/"assets"/"materials-1.21.1.bin";std::ifstream materialInput(materialPath,std::ios::binary|std::ios::ate);if(!materialInput)throw std::runtime_error("Missing Minecraft block colours: "+materialPath.string());size_t materialBytes=static_cast<size_t>(materialInput.tellg());if(materialBytes<32||materialBytes>1024*1024)throw std::runtime_error("Invalid Minecraft material table");materialInput.seekg(0);biomeData.resize(biomeBytes+materialBytes);materialInput.read(biomeData.data()+biomeBytes,materialBytes);if(!materialInput)throw std::runtime_error("Cannot read Minecraft block colours");biomeBytes+=materialBytes;
    biomeTable=gpu.buffer(biomeBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);void* mapped{};vkCheck(vkMapMemory(gpu.device,biomeTable.memory,0,biomeBytes,0,&mapped));std::memcpy(mapped,biomeData.data(),biomeBytes);vkUnmapMemory(gpu.device,biomeTable.memory);

    riverNetworks=gpu.buffer(64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* emptyRivers{};vkCheck(vkMapMemory(gpu.device,riverNetworks.memory,0,64,0,&emptyRivers));std::memset(emptyRivers,0,64);vkUnmapMemory(gpu.device,riverNetworks.memory);
    graphSeeds=gpu.buffer(sizeof(GraphSeeds),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mountainCache=gpu.buffer(sizeof(MountainCacheHeader)+mountainCacheCapacity*5*sizeof(float),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    effectsBuffer=gpu.buffer(sizeof(EffectsBlock),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    sinTable=gpu.buffer(sizeof(ftfSinBits),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);{void* sinData{};vkCheck(vkMapMemory(gpu.device,sinTable.memory,0,sizeof(ftfSinBits),0,&sinData));std::memcpy(sinData,ftfSinBits,sizeof(ftfSinBits));vkUnmapMemory(gpu.device,sinTable.memory);}
    exactNoise=gpu.buffer(exactNoiseCapacity*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);exactNoiseValid=false;
    cornerLevels=gpu.buffer(cornerCapacity*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    {auto exactPath=gpu.shaderRoot.parent_path()/"assets"/"mod-exact-surface-rules.json";std::ifstream exactInput(exactPath);exactNoiseSpecs.clear();exactGradients.clear();   // normal edition only; absent in the shareable edition
        if(exactInput){nlohmann::json exact;exactInput>>exact;for(const auto& n:exact.at("noises"))exactNoiseSpecs.push_back({n.at("name").get<std::string>(),n.at("firstOctave").get<int>(),n.at("amplitudes").get<std::vector<double>>()});exactGradients=exact.at("gradients").get<std::vector<std::string>>();}}
    inspectionBuffer=gpu.buffer(sizeof(SurfaceInspection)+32*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(shared){
        sharedPipelines=true;pipelineOwner=shared->pipelineOwner?shared->pipelineOwner:shared;layout=shared->layout;computeLayout=shared->computeLayout;drawLayout=shared->drawLayout;
        generate=shared->generate;worldFilters=shared->worldFilters;filter=shared->filter;drawPipeline=shared->drawPipeline;drawPipelineCulled=shared->drawPipelineCulled;
        buildMountainCache=shared->buildMountainCache;buildMountainSegments=shared->buildMountainSegments;buildWorldMap=shared->buildWorldMap;
        buildOceanFloor=shared->buildOceanFloor;buildIslandSurface=shared->buildIslandSurface;buildClimate=shared->buildClimate;buildRivers=shared->buildRivers;buildBiomes=shared->buildBiomes;buildRecolor=shared->buildRecolor;buildInspection=shared->buildInspection;buildCorners=shared->buildCorners;
        atmosphereLayout=shared->atmosphereLayout;atmospherePipeline=shared->atmospherePipeline;inputLayout=shared->inputLayout;
        renderPass=shared->renderPass;sampler=shared->sampler;return;
    }
    VkDescriptorSetLayoutBinding bindings[15]{};
    for(uint32_t i=0;i<3;++i){bindings[i].binding=i;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;}
    bindings[3]={3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    bindings[4]={4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    bindings[5]={5,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    bindings[6]={6,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    bindings[7]={7,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};bindings[8]={8,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    bindings[9]={9,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    bindings[10]={10,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    bindings[11]={15,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};   // 11-14 were the tree buffers (trees removed in 1.10)
    bindings[12]={16,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    bindings[13]={17,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    bindings[14]={18,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    lc.bindingCount=15;lc.pBindings=bindings;
    vkCheck(vkCreateDescriptorSetLayout(gpu.device,&lc,nullptr,&layout));
    VkPushConstantRange cr{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(TerrainParams)};
    VkPipelineLayoutCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pc.setLayoutCount=1;pc.pSetLayouts=&layout;pc.pushConstantRangeCount=1;pc.pPushConstantRanges=&cr;
    vkCheck(vkCreatePipelineLayout(gpu.device,&pc,nullptr,&computeLayout));
    cr.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;cr.size=sizeof(DrawParams);
    vkCheck(vkCreatePipelineLayout(gpu.device,&pc,nullptr,&drawLayout));
    // Attachments: 0 final image (written by the atmosphere sub-pass), 1 depth, 2 shaded scene (sub-pass 0 output, read back as an input attachment).
    VkAttachmentDescription attachments[3]{};
    attachments[0].format=VK_FORMAT_R8G8B8A8_UNORM;attachments[0].samples=VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachments[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachments[0].finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1]=attachments[0];attachments[1].format=VK_FORMAT_D32_SFLOAT;attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[2]=attachments[0];attachments[2].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachments[2].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;attachments[2].finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference sceneRef{2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depthRef{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},finalRef{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference inputRefs[2]{{2,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}};
    VkSubpassDescription subs[2]{};
    subs[0].pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subs[0].colorAttachmentCount=1;subs[0].pColorAttachments=&sceneRef;subs[0].pDepthStencilAttachment=&depthRef;
    subs[1].pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subs[1].inputAttachmentCount=2;subs[1].pInputAttachments=inputRefs;subs[1].colorAttachmentCount=1;subs[1].pColorAttachments=&finalRef;
    VkSubpassDependency dependencies[4]{};
    dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL;dependencies[0].dstSubpass=0;
    dependencies[0].srcStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask=VK_ACCESS_SHADER_READ_BIT;dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1]=dependencies[0];dependencies[1].dstSubpass=1;dependencies[1].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependencies[1].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[2].srcSubpass=0;dependencies[2].dstSubpass=1;
    dependencies[2].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;dependencies[2].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[2].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;dependencies[2].dstAccessMask=VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;dependencies[2].dependencyFlags=VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[3].srcSubpass=1;dependencies[3].dstSubpass=VK_SUBPASS_EXTERNAL;
    dependencies[3].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependencies[3].dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[3].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependencies[3].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=3;rp.pAttachments=attachments;rp.subpassCount=2;rp.pSubpasses=subs;rp.dependencyCount=4;rp.pDependencies=dependencies;
    VkDescriptorSetLayoutBinding inputBindings[2]{{0,VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo inputLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};inputLayoutInfo.bindingCount=2;inputLayoutInfo.pBindings=inputBindings;
    vkCheck(vkCreateDescriptorSetLayout(gpu.device,&inputLayoutInfo,nullptr,&inputLayout));
    VkDescriptorSetLayout atmosphereSets[2]={layout,inputLayout};VkPushConstantRange atmosphereRange{VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Atmosphere::inverseVP)+3*sizeof(glm::vec4)};
    VkPipelineLayoutCreateInfo atmosphereLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};atmosphereLayoutInfo.setLayoutCount=2;atmosphereLayoutInfo.pSetLayouts=atmosphereSets;atmosphereLayoutInfo.pushConstantRangeCount=1;atmosphereLayoutInfo.pPushConstantRanges=&atmosphereRange;
    vkCheck(vkCreatePipelineLayout(gpu.device,&atmosphereLayoutInfo,nullptr,&atmosphereLayout));
    vkCheck(vkCreateRenderPass(gpu.device,&rp,nullptr,&renderPass));
    std::vector<PipelineJob> jobs;std::map<std::string,VkShaderModule> modules;
    auto moduleFor=[&](const char* file){auto found=modules.find(file);if(found==modules.end())found=modules.emplace(file,gpu.shader(file)).first;return found->second;};
    auto createCompute=[&](const char* filename,VkPipeline& pipeline,int variant,bool filterStage=false){
        VkShaderModule module=moduleFor(filename);
        jobs.push_back({std::string(filename)+":"+std::to_string(variant),[this,module,&pipeline,variant,filterStage]{
        buildCompute(module,pipeline,variant,filterStage);
        }});
    };
    // Terrain variants are expensive to compile cold; only the FTF surface variants (13, 14) start eagerly, the rest build on first use (or all with --warmup).
    for(uint32_t i=0;i<generate.size();++i)if(allVariants||i==13||i==14)createCompute("terrain.comp.spv",generate[i],static_cast<int>(i));
    createCompute("filter.comp.spv",filter,-1);
    createCompute("mountain_cache.comp.spv",buildMountainCache,-1);
    createCompute("mountain_segments.comp.spv",buildMountainSegments,-1);
    createCompute("world_map.comp.spv",buildWorldMap,-1);
    createCompute("ocean_floor.comp.spv",buildOceanFloor,-1);
    createCompute("island_surface.comp.spv",buildIslandSurface,-1);
    createCompute("climate.comp.spv",buildClimate,-1);
    createCompute("rivers.comp.spv",buildRivers,-1);
    createCompute("biomes.comp.spv",buildBiomes,-1);createCompute("biomes.comp.spv",buildRecolor,-3);createCompute("biomes.comp.spv",buildInspection,-2);createCompute("biomes.comp.spv",buildCorners,-4);
    for(uint32_t i=0;i<worldFilters.size();++i)createCompute("world_filter.comp.spv",worldFilters[i],static_cast<int>(i),true);
    jobs.push_back({"terrain.vert+frag",[this]{
    auto vertex=gpu.shader("terrain.vert.spv");auto fragment=gpu.shader("terrain.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=1;vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;rs.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};ds.depthTestEnable=VK_TRUE;ds.depthWriteEnable=VK_TRUE;ds.depthCompareOp=VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=0xf;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};cb.attachmentCount=1;cb.pAttachments=&blend;
    VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dy.dynamicStateCount=2;dy.pDynamicStates=dynamic;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};gp.stageCount=2;gp.pStages=stages;gp.pVertexInputState=&vi;gp.pInputAssemblyState=&ia;
    gp.pViewportState=&vp;gp.pRasterizationState=&rs;gp.pMultisampleState=&ms;gp.pDepthStencilState=&ds;gp.pColorBlendState=&cb;gp.pDynamicState=&dy;gp.layout=drawLayout;gp.renderPass=renderPass;
    vkCheck(vkCreateGraphicsPipelines(gpu.device,gpu.pipelineCache,1,&gp,nullptr,&drawPipeline));rs.cullMode=VK_CULL_MODE_BACK_BIT;vkCheck(vkCreateGraphicsPipelines(gpu.device,gpu.pipelineCache,1,&gp,nullptr,&drawPipelineCulled));vkDestroyShaderModule(gpu.device,vertex,nullptr);vkDestroyShaderModule(gpu.device,fragment,nullptr);
    }});
    jobs.push_back({"atmosphere",[this]{
    auto vertex=gpu.shader("atmosphere.vert.spv");auto fragment=gpu.shader("atmosphere.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=1;vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;rs.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=0xf;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};cb.attachmentCount=1;cb.pAttachments=&blend;
    VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dy.dynamicStateCount=2;dy.pDynamicStates=dynamic;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};gp.stageCount=2;gp.pStages=stages;gp.pVertexInputState=&vi;gp.pInputAssemblyState=&ia;
    gp.pViewportState=&vp;gp.pRasterizationState=&rs;gp.pMultisampleState=&ms;gp.pDepthStencilState=&ds;gp.pColorBlendState=&cb;gp.pDynamicState=&dy;gp.layout=atmosphereLayout;gp.renderPass=renderPass;gp.subpass=1;
    vkCheck(vkCreateGraphicsPipelines(gpu.device,gpu.pipelineCache,1,&gp,nullptr,&atmospherePipeline));vkDestroyShaderModule(gpu.device,vertex,nullptr);vkDestroyShaderModule(gpu.device,fragment,nullptr);
    }});
    // The terrain variants dominate a cold shader-cache start; compile everything on all cores while the caller shows progress.
    runPipelineJobs(jobs,progress);
    for(auto& entry:modules)vkDestroyShaderModule(gpu.device,entry.second,nullptr);
    VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.magFilter=VK_FILTER_LINEAR;sc.minFilter=VK_FILTER_LINEAR;sc.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;vkCheck(vkCreateSampler(gpu.device,&sc,nullptr,&sampler));
}
void Surface::buildCompute(VkShaderModule module,VkPipeline& pipeline,int variant,bool filterStage){
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.layout=computeLayout;cp.flags=VK_PIPELINE_CREATE_DISPATCH_BASE_BIT;
        cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=module;cp.stage.pName="main";
        const uint32_t specialization[]={filterStage?static_cast<uint32_t>(variant):variant==15?6u:variant>=13?5u:variant>=11?4u:variant==10?3u:(variant>=2?2u:static_cast<uint32_t>(variant)),variant>=2&&variant<10?static_cast<uint32_t>(variant-2):0u,(variant!=12&&variant!=14)?1u:0u};
        const VkSpecializationMapEntry entries[]={{0,0,4},{1,4,4},{2,8,4}};
        const VkSpecializationInfo info{3,entries,sizeof(specialization),specialization};if(variant>=0)cp.stage.pSpecializationInfo=&info;
        uint32_t inspectValue=1;VkSpecializationMapEntry inspectEntry{3,0,4};VkSpecializationInfo inspectInfo{1,&inspectEntry,4,&inspectValue};if(variant==-3)inspectEntry.constantID=4;if(variant==-4)inspectEntry.constantID=5;if(variant<=-2&&variant>=-4)cp.stage.pSpecializationInfo=&inspectInfo;
        vkCheck(vkCreateComputePipelines(gpu.device,gpu.pipelineCache,1,&cp,nullptr,&pipeline));
}
VkPipeline Surface::variantPipeline(uint32_t variant){
    Surface& owner=pipelineOwner?*pipelineOwner:*this;
    if(!owner.generate.at(variant)){VkShaderModule module=gpu.shader("terrain.comp.spv");try{owner.buildCompute(module,owner.generate[variant],static_cast<int>(variant),false);}catch(...){vkDestroyShaderModule(gpu.device,module,nullptr);throw;}vkDestroyShaderModule(gpu.device,module,nullptr);}
    return owner.generate[variant];
}
void Surface::ensure(uint32_t grid,uint32_t w,uint32_t h){
    w=std::max(w,1u);h=std::max(h,1u);
    if(resolution==grid&&width==w&&height==h&&frames.size()==gpu.swap.ImageCount)return;
    if(grid<2||grid>8192)throw std::runtime_error("Grid must be between 2 and 8192 samples per side");
    if(grid>gpu.properties.limits.maxImageDimension2D)throw std::runtime_error("Grid exceeds this GPU's image dimension limit");
    const bool recreateFields=resolution!=grid||!field.handle;
    if(recreateFields){
        if(static_cast<VkDeviceSize>(grid)*grid*32>gpu.properties.limits.maxStorageBufferRange)throw std::runtime_error("Surface metadata exceeds this GPU storage-buffer range; select a smaller grid");
        auto required=estimatedFieldBytes(grid);auto available=gpu.availableDeviceMemory();
        constexpr VkDeviceSize reserve=256ull*1024*1024;
        if(required+reserve>available+fieldBytes+stageCacheBytes())throw std::runtime_error("Not enough GPU memory for this grid; select a smaller resolution");
    }
    vkCheck(vkDeviceWaitIdle(gpu.device));releaseFrames();width=w;height=h;
    try{
    if(recreateFields){
        releaseFields();resolution=grid;patchCells=std::min(128u,grid-1);
        uint32_t tiles=(grid-1+patchCells-1)/patchCells;patchCount=tiles*tiles;
        std::vector<uint16_t> index;
        for(uint32_t level=0;level<6;++level){uint32_t stride=1u<<level,side=(patchCells+stride-1)/stride,row=side+1;lodFirst[level]=uint32_t(index.size());
            for(uint32_t y=0;y<side;++y)for(uint32_t x=0;x<side;++x){uint16_t a=uint16_t(y*row+x),b=a+1,c=uint16_t(a+row),d=c+1;index.insert(index.end(),{a,c,b,b,c,d});}
            lodCount[level]=uint32_t(index.size())-lodFirst[level];
        }indexCount=lodCount[0];
        indices=gpu.buffer(index.size()*sizeof(uint16_t),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* mapped{};vkCheck(vkMapMemory(gpu.device,indices.memory,0,indices.size,0,&mapped));std::memcpy(mapped,index.data(),static_cast<size_t>(indices.size));vkUnmapMemory(gpu.device,indices.memory);
        VkImageUsageFlags usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        rawField=gpu.image(grid,grid,VK_FORMAT_R32G32B32A32_SFLOAT,usage,VK_IMAGE_ASPECT_COLOR_BIT);
        field=gpu.image(grid,grid,VK_FORMAT_R32G32B32A32_SFLOAT,usage,VK_IMAGE_ASPECT_COLOR_BIT);
        colors=gpu.image(grid,grid,VK_FORMAT_R8G8B8A8_UNORM,usage,VK_IMAGE_ASPECT_COLOR_BIT);
        cellState=gpu.buffer(static_cast<VkDeviceSize>(grid)*grid*32,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        regionCells=gpu.buffer(static_cast<VkDeviceSize>(grid)*grid*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);   // binding 18: FTF biome-region cell per sample (climate.comp)
        fieldBytes=estimatedFieldBytes(grid);
        gpu.immediate([&](VkCommandBuffer cmd){VkImageMemoryBarrier barriers[3]{};VkImage handles[]={rawField.handle,field.handle,colors.handle};
            for(uint32_t i=0;i<3;++i){auto& b=barriers[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=handles[i];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;}
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0,0,nullptr,0,nullptr,3,barriers);
        });
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=gpu.descriptors;ai.descriptorSetCount=1;ai.pSetLayouts=&layout;vkCheck(vkAllocateDescriptorSets(gpu.device,&ai,&fields));
        VkDescriptorImageInfo images[]={{VK_NULL_HANDLE,rawField.view,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,field.view,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,colors.view,VK_IMAGE_LAYOUT_GENERAL}};
        VkWriteDescriptorSet writes[8]{};for(uint32_t i=0;i<3;++i){auto& wr=writes[i];wr.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;wr.dstSet=fields;wr.dstBinding=i;wr.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;wr.descriptorCount=1;wr.pImageInfo=&images[i];}
        VkDescriptorBufferInfo seedInfo{graphSeeds.handle,0,sizeof(GraphSeeds)};writes[3].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[3].dstSet=fields;writes[3].dstBinding=3;writes[3].descriptorCount=1;writes[3].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[3].pBufferInfo=&seedInfo;
        VkDescriptorBufferInfo cacheInfo{mountainCache.handle,0,mountainCache.size};writes[4].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[4].dstSet=fields;writes[4].dstBinding=4;writes[4].descriptorCount=1;writes[4].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[4].pBufferInfo=&cacheInfo;
        VkDescriptorBufferInfo cellInfo{cellState.handle,0,cellState.size};writes[5].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[5].dstSet=fields;writes[5].dstBinding=5;writes[5].descriptorCount=1;writes[5].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[5].pBufferInfo=&cellInfo;
        VkDescriptorBufferInfo riverInfo{riverNetworks.handle,0,riverNetworks.size};writes[6].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[6].dstSet=fields;writes[6].dstBinding=6;writes[6].descriptorCount=1;writes[6].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[6].pBufferInfo=&riverInfo;
        VkDescriptorBufferInfo biomeInfo{biomeTable.handle,0,biomeTable.size};writes[7].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[7].dstSet=fields;writes[7].dstBinding=7;writes[7].descriptorCount=1;writes[7].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[7].pBufferInfo=&biomeInfo;
        vkUpdateDescriptorSets(gpu.device,8,writes,0,nullptr);
        VkDescriptorBufferInfo effectsInfo{effectsBuffer.handle,0,sizeof(EffectsBlock)};VkWriteDescriptorSet effectWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};effectWrite.dstSet=fields;effectWrite.dstBinding=9;effectWrite.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;effectWrite.descriptorCount=1;effectWrite.pBufferInfo=&effectsInfo;vkUpdateDescriptorSets(gpu.device,1,&effectWrite,0,nullptr);
        VkDescriptorBufferInfo queryInfo{inspectionBuffer.handle,0,sizeof(SurfaceInspection)+32*4};effectWrite.dstBinding=10;effectWrite.pBufferInfo=&queryInfo;vkUpdateDescriptorSets(gpu.device,1,&effectWrite,0,nullptr);
        VkDescriptorBufferInfo sinInfo{sinTable.handle,0,sinTable.size};effectWrite.dstBinding=15;effectWrite.pBufferInfo=&sinInfo;vkUpdateDescriptorSets(gpu.device,1,&effectWrite,0,nullptr);
        VkDescriptorBufferInfo exactInfo{exactNoise.handle,0,exactNoise.size};effectWrite.dstBinding=16;effectWrite.pBufferInfo=&exactInfo;vkUpdateDescriptorSets(gpu.device,1,&effectWrite,0,nullptr);
        VkDescriptorBufferInfo cornerInfo{cornerLevels.handle,0,cornerLevels.size};effectWrite.dstBinding=17;effectWrite.pBufferInfo=&cornerInfo;vkUpdateDescriptorSets(gpu.device,1,&effectWrite,0,nullptr);
        VkDescriptorBufferInfo regionInfo{regionCells.handle,0,regionCells.size};effectWrite.dstBinding=18;effectWrite.pBufferInfo=&regionInfo;vkUpdateDescriptorSets(gpu.device,1,&effectWrite,0,nullptr);
        bindCoarse(*this);
    }
    frames.resize(gpu.swap.ImageCount);
    for(auto& frame:frames){
        frame.color=gpu.image(w,h,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        frame.depth=gpu.image(w,h,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        frame.scene=gpu.image(w,h,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        {VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=gpu.descriptors;ai.descriptorSetCount=1;ai.pSetLayouts=&inputLayout;vkCheck(vkAllocateDescriptorSets(gpu.device,&ai,&frame.inputs));
         VkDescriptorImageInfo infos[2]{{VK_NULL_HANDLE,frame.scene.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{VK_NULL_HANDLE,frame.depth.view,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}};
         VkWriteDescriptorSet iw[2]{};for(uint32_t i=0;i<2;++i){iw[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;iw[i].dstSet=frame.inputs;iw[i].dstBinding=i;iw[i].descriptorCount=1;iw[i].descriptorType=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;iw[i].pImageInfo=&infos[i];}
         vkUpdateDescriptorSets(gpu.device,2,iw,0,nullptr);}
        VkImageView views[]={frame.color.view,frame.depth.view,frame.scene.view};VkFramebufferCreateInfo fc{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fc.renderPass=renderPass;fc.attachmentCount=3;fc.pAttachments=views;fc.width=w;fc.height=h;fc.layers=1;
        vkCheck(vkCreateFramebuffer(gpu.device,&fc,nullptr,&frame.framebuffer));
        frame.texture=ImGui_ImplVulkan_AddTexture(sampler,frame.color.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if(gpu.timestamps){VkQueryPoolCreateInfo qc{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qc.queryType=VK_QUERY_TYPE_TIMESTAMP;qc.queryCount=7;vkCheck(vkCreateQueryPool(gpu.device,&qc,nullptr,&frame.queries));qc.queryCount=64;vkCheck(vkCreateQueryPool(gpu.device,&qc,nullptr,&frame.stepQueries));}
    }
    }catch(...){releaseFrames();width=height=0;if(recreateFields){releaseFields();resolution=0;}throw;}
}
StageTiming Surface::timing(uint32_t frameIndex){
    StageTiming value{};auto& f=frames.at(frameIndex);
    if(f.stepQueries&&!f.stepNames.empty()){uint64_t stamps[64]{};const uint32_t n=uint32_t(f.stepNames.size());
        if(vkGetQueryPoolResults(gpu.device,f.stepQueries,0,n+1,sizeof(stamps),stamps,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)==VK_SUCCESS){double factor=gpu.properties.limits.timestampPeriod/1e6;
            for(uint32_t i=0;i<n;++i){auto& e=stepProfile[f.stepNames[i]];e.first+=double(stamps[i+1]-stamps[i])*factor;++e.second;}}
        f.stepNames.clear();}
    if(!f.recorded||!f.queries)return value;
    uint64_t times[7]{};auto result=vkGetQueryPoolResults(gpu.device,f.queries,0,7,sizeof(times),times,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT);
    if(result==VK_NOT_READY)return value;vkCheck(result);double factor=gpu.properties.limits.timestampPeriod/1e6;
    value.generate=(times[1]-times[0])*factor;value.filter=(times[2]-times[1])*factor;value.draw=(times[3]-times[2])*factor;value.total=(times[3]-times[0])*factor;
    value.cache=(times[4]-times[0])*factor;value.world=(times[5]-times[4])*factor;value.ocean=(times[6]-times[5])*factor;value.compose=(times[1]-times[6])*factor;value.generated=f.generated;value.valid=true;return value;
}
bool Surface::updateRivers(const std::vector<uint32_t>& data){
    if(data.empty()||data==riverWords)return false;if(data.size()<16)throw std::runtime_error("Incomplete river network buffer");VkDeviceSize bytes=data.size()*sizeof(uint32_t);if(bytes>gpu.properties.limits.maxStorageBufferRange)throw std::runtime_error("River networks exceed GPU storage range; reduce view extent or river count");
    vkCheck(vkDeviceWaitIdle(gpu.device));if(bytes>riverNetworks.size){gpu.release(riverNetworks);riverNetworks=gpu.buffer(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);}
    void* mapped{};vkCheck(vkMapMemory(gpu.device,riverNetworks.memory,0,bytes,0,&mapped));std::memcpy(mapped,data.data(),static_cast<size_t>(bytes));vkUnmapMemory(gpu.device,riverNetworks.memory);
    if(fields){VkDescriptorBufferInfo info{riverNetworks.handle,0,riverNetworks.size};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=fields;write.dstBinding=6;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&info;vkUpdateDescriptorSets(gpu.device,1,&write,0,nullptr);}
    ++riverEpoch;riverWords=data;hasRivers=data[1]>0;return true;
}
void Surface::record(VkCommandBuffer cmd,uint32_t frameIndex,const TerrainParams& params,const GraphSeeds& seeds,const DrawParams& draw,uint64_t revision,Surface* detail,const DrawParams* detailDraw,bool generateOnly){
    lastParams=params;auto& f=frames.at(frameIndex);
    bool requested=fieldRevision!=revision||workActive;
    bool compute=requested?generateWork(cmd,frameIndex,params,seeds,revision):false;
    if(!requested){if(f.queries){vkCmdResetQueryPool(cmd,f.queries,0,7);for(uint32_t i:{0u,4u,5u,6u,1u,2u})vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,f.queries,i);}f.workTiles=0;}
    auto stamp=[&](uint32_t i,VkPipelineStageFlagBits stage){if(f.queries)vkCmdWriteTimestamp(cmd,stage,f.queries,i);};
    // Update display controls every frame, independently of terrain generation.
    VkMemoryBarrier effectsBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};effectsBarrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;effectsBarrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&effectsBarrier,0,nullptr,0,nullptr);
    EffectsBlock display{effects,screenFocus,viewportSize,sun,sunShadow,terrainMask,scaleCue,detailHole,atmosphere.water,atmosphere.fog,atmosphere.cloud,atmosphere.cloud2};vkCmdUpdateBuffer(cmd,effectsBuffer.handle,0,sizeof(display),&display);
    effectsBarrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;effectsBarrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,1,&effectsBarrier,0,nullptr,0,nullptr);
    if(generateOnly){stamp(3,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);f.recorded=true;f.generated=compute;f.revision=revision;return;}
    VkClearValue clear[3]{};clear[2].color={{.060f,.085f,.105f,1}};clear[1].depthStencil={1,0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rp.renderPass=renderPass;rp.framebuffer=f.framebuffer;rp.renderArea.extent={width,height};rp.clearValueCount=3;rp.pClearValues=clear;
    vkCmdBeginRenderPass(cmd,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,drawPipeline);
    VkViewport viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};VkRect2D scissor{{0,0},{width,height}};
    vkCmdSetViewport(cmd,0,1,&viewport);vkCmdSetScissor(cmd,0,1,&scissor);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,drawLayout,0,1,&fields,0,nullptr);
    drawMesh(cmd,draw);
    if(detail&&detailDraw){
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,drawLayout,0,1,&detail->fields,0,nullptr);
        detail->drawMesh(cmd,*detailDraw);
        if(detail->finerBand){vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,drawLayout,0,1,&detail->finerBand->fields,0,nullptr);detail->finerBand->drawMesh(cmd,detail->finerBandDraw);}
    }
    vkCmdNextSubpass(cmd,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,atmospherePipeline);
    VkDescriptorSet atmosphereSets[2]={fields,f.inputs};vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,atmosphereLayout,0,2,atmosphereSets,0,nullptr);
    vkCmdPushConstants(cmd,atmosphereLayout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(Atmosphere::inverseVP)+3*sizeof(glm::vec4),&atmosphere.inverseVP);
    vkCmdDraw(cmd,3,1,0,0);
    vkCmdEndRenderPass(cmd);
    stamp(3,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);f.recorded=true;f.generated=compute;f.revision=fieldRevision;
}
std::vector<TerrainSample> Surface::samples(uint32_t frameIndex){
    (void)frames.at(frameIndex);vkCheck(vkDeviceWaitIdle(gpu.device));
    const auto count=static_cast<size_t>(resolution)*resolution;const VkDeviceSize floatBytes=count*16;
    auto staging=gpu.buffer(count*20,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    gpu.immediate([&](VkCommandBuffer cmd){VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={resolution,resolution,1};
        vkCmdCopyImageToBuffer(cmd,field.handle,VK_IMAGE_LAYOUT_GENERAL,staging.handle,1,&copy);
        copy.bufferOffset=floatBytes;vkCmdCopyImageToBuffer(cmd,colors.handle,VK_IMAGE_LAYOUT_GENERAL,staging.handle,1,&copy);
    });
    std::vector<TerrainSample> result(count);void* data{};vkCheck(vkMapMemory(gpu.device,staging.memory,0,staging.size,0,&data));
    auto* values=static_cast<float*>(data);auto* packed=static_cast<uint8_t*>(data)+floatBytes;
    for(size_t i=0;i<count;++i){std::memcpy(result[i].field.data(),values+i*4,16);for(size_t c=0;c<3;++c)result[i].color[c]=packed[i*4+c]/255.0f;result[i].color[3]=packed[i*4+3];}
    vkUnmapMemory(gpu.device,staging.memory);gpu.release(staging);return result;
}
void Surface::exportCells(const std::filesystem::path& file,bool extended){
    vkCheck(vkDeviceWaitIdle(gpu.device));auto staging=gpu.buffer(cellState.size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    gpu.immediate([&](VkCommandBuffer cmd){VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);VkBufferCopy copy{0,0,cellState.size};vkCmdCopyBuffer(cmd,cellState.handle,staging.handle,1,&copy);});
    void* data{};vkCheck(vkMapMemory(gpu.device,staging.memory,0,staging.size,0,&data));if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());std::ofstream out(file,std::ios::binary);if(extended)out.write(static_cast<const char*>(data),static_cast<std::streamsize>(staging.size));else for(size_t i=0;i<static_cast<size_t>(resolution)*resolution;++i)out.write(static_cast<const char*>(data)+i*32,20);vkUnmapMemory(gpu.device,staging.memory);gpu.release(staging);if(!out)throw std::runtime_error("Cannot export surface metadata");
}
void Surface::screenshot(uint32_t frameIndex,const std::filesystem::path& file){
    vkCheck(vkDeviceWaitIdle(gpu.device));auto& f=frames.at(frameIndex);
    auto staging=gpu.buffer(static_cast<VkDeviceSize>(width)*height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    gpu.immediate([&](VkCommandBuffer cmd){
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        b.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=f.color.handle;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};vkCmdCopyImageToBuffer(cmd,f.color.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,staging.handle,1,&copy);
        b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
    });
    if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file,std::ios::binary);if(!out)throw std::runtime_error("Cannot write screenshot");out<<"P6\n"<<width<<" "<<height<<"\n255\n";
    void* data{};vkCheck(vkMapMemory(gpu.device,staging.memory,0,staging.size,0,&data));auto* bytes=static_cast<unsigned char*>(data);
    for(size_t i=0;i<static_cast<size_t>(width)*height;++i)out.write(reinterpret_cast<char*>(bytes+i*4),3);
    vkUnmapMemory(gpu.device,staging.memory);gpu.release(staging);
}
void Surface::releaseFrames(){
    for(auto& f:frames){if(f.texture)ImGui_ImplVulkan_RemoveTexture(f.texture);
        if(f.framebuffer)vkDestroyFramebuffer(gpu.device,f.framebuffer,nullptr);if(f.queries)vkDestroyQueryPool(gpu.device,f.queries,nullptr);if(f.stepQueries)vkDestroyQueryPool(gpu.device,f.stepQueries,nullptr);
        if(f.inputs)vkFreeDescriptorSets(gpu.device,gpu.descriptors,1,&f.inputs);
        gpu.release(f.color);gpu.release(f.depth);gpu.release(f.scene);}
    frames.clear();
}
void Surface::releaseFields(){
    if(fields)vkCheck(vkFreeDescriptorSets(gpu.device,gpu.descriptors,1,&fields));fields={};
    gpu.release(preClimate);gpu.release(preFilter);gpu.release(preClimateCells);gpu.release(preFilterCells);inputsValid=preClimateValid=preFilterValid=workActive=false;workSteps.clear();gpu.release(rawField);gpu.release(field);gpu.release(colors);gpu.release(cellState);gpu.release(regionCells);gpu.release(indices);fieldRevision=0;fieldBytes=0;
}
void Surface::destroy(){
    if(!gpu.device)return;vkDeviceWaitIdle(gpu.device);releaseFrames();releaseFields();
    for(auto& h:asyncHeights)gpu.release(h.staging);asyncHeights.clear();
    gpu.release(graphSeeds);gpu.release(mountainCache);gpu.release(riverNetworks);gpu.release(effectsBuffer);gpu.release(inspectionBuffer);gpu.release(sinTable);gpu.release(exactNoise);gpu.release(cornerLevels);
    if(sharedPipelines){gpu.release(biomeTable);return;}
    if(atmospherePipeline)vkDestroyPipeline(gpu.device,atmospherePipeline,nullptr);if(atmosphereLayout)vkDestroyPipelineLayout(gpu.device,atmosphereLayout,nullptr);if(inputLayout)vkDestroyDescriptorSetLayout(gpu.device,inputLayout,nullptr);
    if(buildMountainCache)vkDestroyPipeline(gpu.device,buildMountainCache,nullptr);
    if(buildMountainSegments)vkDestroyPipeline(gpu.device,buildMountainSegments,nullptr);
    if(buildWorldMap)vkDestroyPipeline(gpu.device,buildWorldMap,nullptr);
    if(buildOceanFloor)vkDestroyPipeline(gpu.device,buildOceanFloor,nullptr);
    if(buildIslandSurface)vkDestroyPipeline(gpu.device,buildIslandSurface,nullptr);
    if(buildRivers)vkDestroyPipeline(gpu.device,buildRivers,nullptr);
    gpu.release(biomeTable);if(buildRecolor)vkDestroyPipeline(gpu.device,buildRecolor,nullptr);if(buildBiomes)vkDestroyPipeline(gpu.device,buildBiomes,nullptr);if(buildInspection)vkDestroyPipeline(gpu.device,buildInspection,nullptr);if(buildCorners)vkDestroyPipeline(gpu.device,buildCorners,nullptr);
    if(buildClimate)vkDestroyPipeline(gpu.device,buildClimate,nullptr);
    for(auto pipeline:worldFilters)if(pipeline)vkDestroyPipeline(gpu.device,pipeline,nullptr);
    if(drawPipelineCulled)vkDestroyPipeline(gpu.device,drawPipelineCulled,nullptr);if(drawPipeline)vkDestroyPipeline(gpu.device,drawPipeline,nullptr);for(auto pipeline:generate)if(pipeline)vkDestroyPipeline(gpu.device,pipeline,nullptr);if(filter)vkDestroyPipeline(gpu.device,filter,nullptr);
    if(renderPass)vkDestroyRenderPass(gpu.device,renderPass,nullptr);if(sampler)vkDestroySampler(gpu.device,sampler,nullptr);
    if(drawLayout)vkDestroyPipelineLayout(gpu.device,drawLayout,nullptr);if(computeLayout)vkDestroyPipelineLayout(gpu.device,computeLayout,nullptr);if(layout)vkDestroyDescriptorSetLayout(gpu.device,layout,nullptr);
}

void Surface::bindCoarse(Surface& base){
    VkDescriptorImageInfo image{VK_NULL_HANDLE,base.field.view,VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=fields;write.dstBinding=8;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;write.pImageInfo=&image;
    vkUpdateDescriptorSets(gpu.device,1,&write,0,nullptr);
}
std::vector<float> Surface::pickHeights(uint32_t side){
    vkCheck(vkDeviceWaitIdle(gpu.device));auto staging=gpu.buffer(side*side*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::vector<VkBufferImageCopy> copies;copies.reserve(side*side);
    for(uint32_t y=0;y<side;++y)for(uint32_t x=0;x<side;++x){VkBufferImageCopy c{};c.bufferOffset=(y*side+x)*16;c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageOffset={int(x*(resolution-1)/(side-1)),int(y*(resolution-1)/(side-1)),0};c.imageExtent={1,1,1};copies.push_back(c);}
    gpu.immediate([&](VkCommandBuffer cmd){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&b,0,nullptr,0,nullptr);vkCmdCopyImageToBuffer(cmd,field.handle,VK_IMAGE_LAYOUT_GENERAL,staging.handle,uint32_t(copies.size()),copies.data());});
    void* mapped{};vkCheck(vkMapMemory(gpu.device,staging.memory,0,staging.size,0,&mapped));std::vector<float> values(side*side);for(size_t i=0;i<values.size();++i)values[i]=static_cast<float*>(mapped)[i*4];vkUnmapMemory(gpu.device,staging.memory);gpu.release(staging);return values;
}

std::vector<float> Surface::heightField(){
    vkCheck(vkDeviceWaitIdle(gpu.device));const size_t count=static_cast<size_t>(resolution)*resolution;
    auto staging=gpu.buffer(count*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    gpu.immediate([&](VkCommandBuffer cmd){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&b,0,nullptr,0,nullptr);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={resolution,resolution,1};vkCmdCopyImageToBuffer(cmd,field.handle,VK_IMAGE_LAYOUT_GENERAL,staging.handle,1,&copy);});
    void* mapped{};vkCheck(vkMapMemory(gpu.device,staging.memory,0,staging.size,0,&mapped));std::vector<float> values(count);for(size_t i=0;i<count;++i)values[i]=static_cast<float*>(mapped)[i*4];vkUnmapMemory(gpu.device,staging.memory);gpu.release(staging);return values;
}

std::vector<std::vector<float>> Surface::heightWindows(const std::vector<glm::ivec4>& windows){
    std::vector<std::vector<float>> out(windows.size());if(windows.empty()||!field.handle)return out;
    std::vector<VkBufferImageCopy> copies;VkDeviceSize bytes=0;
    for(auto& r:windows){if(r.x<0||r.y<0||r.z<=0||r.w<=0||uint32_t(r.x+r.z)>resolution||uint32_t(r.y+r.w)>resolution)throw std::runtime_error("height window outside the field");
        VkBufferImageCopy c{};c.bufferOffset=bytes;c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageOffset={r.x,r.y,0};c.imageExtent={uint32_t(r.z),uint32_t(r.w),1};copies.push_back(c);bytes+=VkDeviceSize(r.z)*r.w*16;}
    vkCheck(vkDeviceWaitIdle(gpu.device));auto staging=gpu.buffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    gpu.immediate([&](VkCommandBuffer cmd){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&b,0,nullptr,0,nullptr);vkCmdCopyImageToBuffer(cmd,field.handle,VK_IMAGE_LAYOUT_GENERAL,staging.handle,uint32_t(copies.size()),copies.data());});
    void* mapped{};vkCheck(vkMapMemory(gpu.device,staging.memory,0,staging.size,0,&mapped));const float* source=static_cast<const float*>(mapped);
    for(size_t i=0;i<windows.size();++i){size_t n=size_t(windows[i].z)*windows[i].w,first=size_t(copies[i].bufferOffset/16);out[i].resize(n);for(size_t k=0;k<n;++k)out[i][k]=source[(first+k)*4];}
    vkUnmapMemory(gpu.device,staging.memory);gpu.release(staging);return out;
}

void Surface::recordHeightCopy(VkCommandBuffer cmd,uint32_t frame,const TerrainParams& params,uint64_t revision,glm::ivec2 offset,uint32_t side){
    if(asyncHeights.size()!=gpu.swap.ImageCount){for(auto& h:asyncHeights)gpu.release(h.staging);asyncHeights.assign(gpu.swap.ImageCount,AsyncHeights{});}
    if(frame>=asyncHeights.size()||!field.handle)return;auto& slot=asyncHeights[frame];if(side==0||side>resolution){side=resolution;offset={0,0};}
    if(offset.x<0||offset.y<0||uint32_t(offset.x)+side>resolution||uint32_t(offset.y)+side>resolution)return;   /* a window outside the field image would make the GPU read out of bounds */const VkDeviceSize bytes=VkDeviceSize(side)*side*16;
    if(slot.resolution!=side||!slot.staging.handle){gpu.release(slot.staging);slot.staging=gpu.buffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);slot.resolution=side;}
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&b,0,nullptr,0,nullptr);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageOffset={offset.x,offset.y,0};copy.imageExtent={side,side,1};vkCmdCopyImageToBuffer(cmd,field.handle,VK_IMAGE_LAYOUT_GENERAL,slot.staging.handle,1,&copy);
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,0,0,nullptr,0,nullptr,0,nullptr); // later generation may overwrite the field
    slot.pending=true;slot.params=params;slot.revision=revision;
}
bool Surface::collectHeights(uint32_t frame,std::vector<float>& heights,TerrainParams& params,uint64_t& revision){
    if(frame>=asyncHeights.size())return false;auto& slot=asyncHeights[frame];if(!slot.pending||!slot.staging.handle)return false;
    const size_t count=size_t(slot.resolution)*slot.resolution;void* mapped{};vkCheck(vkMapMemory(gpu.device,slot.staging.memory,0,count*16,0,&mapped));
    heights.resize(count);const float* source=static_cast<const float*>(mapped);for(size_t i=0;i<count;++i)heights[i]=source[i*4];vkUnmapMemory(gpu.device,slot.staging.memory);
    params=slot.params;revision=slot.revision;slot.pending=false;return true;
}

SurfaceInspection Surface::inspect(glm::vec2 world){
    SurfaceInspection query;auto uv=glm::clamp((world-glm::vec2(lastParams.domain.y,lastParams.domain.z))/lastParams.domain.x+.5f,glm::vec2(0),glm::vec2(1));query.request={uint32_t(std::round(uv.x*(resolution-1))),uint32_t(std::round(uv.y*(resolution-1))),0,0};
    gpu.immediate([&](VkCommandBuffer cmd){vkCmdUpdateBuffer(cmd,inspectionBuffer.handle,0,sizeof(query),&query);VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,buildInspection);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,computeLayout,0,1,&fields,0,nullptr);vkCmdPushConstants(cmd,computeLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(lastParams),&lastParams);vkCmdDispatch(cmd,1,1,1);b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,nullptr,0,nullptr);});
    void* mapped{};vkCheck(vkMapMemory(gpu.device,inspectionBuffer.memory,0,sizeof(query),0,&mapped));std::memcpy(&query,mapped,sizeof(query));vkUnmapMemory(gpu.device,inspectionBuffer.memory);return query;
}

std::vector<uint32_t> Surface::generatedBiomes(){
    struct alignas(16) Census {SurfaceInspection head;uint32_t seen[32];} census{};census.head.request={0,0,0,2};
    gpu.immediate([&](VkCommandBuffer cmd){vkCmdUpdateBuffer(cmd,inspectionBuffer.handle,0,sizeof(census),&census);VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,buildInspection);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,computeLayout,0,1,&fields,0,nullptr);vkCmdPushConstants(cmd,computeLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(lastParams),&lastParams);vkCmdDispatch(cmd,(resolution+7)/8,(resolution+7)/8,1);b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,nullptr,0,nullptr);});
    void* mapped{};vkCheck(vkMapMemory(gpu.device,inspectionBuffer.memory,0,sizeof(census),0,&mapped));std::memcpy(&census,mapped,sizeof(census));vkUnmapMemory(gpu.device,inspectionBuffer.memory);
    std::vector<uint32_t> ids;for(uint32_t i=0;i<1024;++i)if((census.seen[i>>5]>>(i&31))&1u)ids.push_back(i);return ids;
}

void Surface::drawMesh(VkCommandBuffer cmd,const DrawParams& draw){
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,backfaceCull?drawPipelineCulled:drawPipeline);
    DrawParams patched=draw;patched.meta.w=patchCells;culledPatches=0;
    vkCmdPushConstants(cmd,drawLayout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(patched),&patched);
    vkCmdBindIndexBuffer(cmd,indices.handle,0,VK_INDEX_TYPE_UINT16);drawnTriangles=0;lodPatches.fill(0);
    if(!distanceLod&&!frustumCull&&!distanceCull){vkCmdDrawIndexed(cmd,indexCount,patchCount,0,0,0);drawnTriangles=uint64_t(indexCount/3)*patchCount;lodPatches[0]=patchCount;}
    else{
        uint32_t tiles=(resolution-1+patchCells-1)/patchCells;
        // Six concentric distance bands, with edge heights matched to the coarser neighbour.
        auto levelAt=[&](int x,int y){if(!distanceLod)return 0u;x=std::clamp(x,0,int(tiles)-1);y=std::clamp(y,0,int(tiles)-1);glm::vec2 q{float(x)*patchCells+patchCells*.5f,float(y)*patchCells+patchCells*.5f};auto world=(q/float(resolution-1)-.5f)*draw.view.x;float distance=std::sqrt(glm::dot(world-lodCamera,world-lodCamera)+lodAltitude*lodAltitude);if(draw.focus.w<0&&glm::length(world-glm::vec2(draw.focus))<-draw.focus.w+draw.view.x/float(tiles))return 0u;if(lodBands){for(uint32_t i=0;i<lodBands;++i)if(distance<lodBandEnd[i])return std::min(5u,lodBandLevel[i]);return std::min(5u,lodBandLevel[lodBands-1]);}return std::min(5u,uint32_t(distance/std::max(1.0f,lodBandSize)));};
        for(uint32_t tile=0;tile<patchCount;++tile){int x=int(tile%tiles),y=int(tile/tiles);uint32_t level=distanceLod?levelAt(x,y):0u;
            float a=float(x*patchCells)/float(resolution-1)*draw.view.x-draw.view.x*.5f,b=float(y*patchCells)/float(resolution-1)*draw.view.x-draw.view.x*.5f,span=float(patchCells)/float(resolution-1)*draw.view.x;glm::vec2 offset=draw.focus.w>0?glm::vec2(draw.focus):glm::vec2(0);
            bool rejected=distanceCull&&glm::length(glm::vec2(a+span*.5f,b+span*.5f)-lodCamera)>maxDrawDistance+span;
            if(frustumCull){glm::vec4 clip[8];int n=0;for(float h:{-4096.0f,std::max(4096.0f,draw.limits.x*4.0f)})for(float z:{b,b+span})for(float xx:{a,a+span})clip[n++]=draw.vp*glm::vec4(xx+offset.x,h*draw.view.z,z+offset.y,1);
                for(int plane=0;plane<6;++plane){bool outside=true;for(auto c:clip){float v=plane==0?c.x+c.w:plane==1?c.w-c.x:plane==2?c.y+c.w:plane==3?c.w-c.y:plane==4?c.z:c.w-c.z;if(v>=0)outside=false;}if(outside)rejected=true;}}
            if(rejected){++culledPatches;continue;}auto local=patched;
            local.meta.w=patchCells|((1u<<level)<<8)|65536u;
            local.meta.w|=std::max(level,levelAt(x-1,y))<<17;local.meta.w|=std::max(level,levelAt(x+1,y))<<20;
            local.meta.w|=std::max(level,levelAt(x,y-1))<<23;local.meta.w|=std::max(level,levelAt(x,y+1))<<26;
            vkCmdPushConstants(cmd,drawLayout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(local),&local);vkCmdDrawIndexed(cmd,lodCount[level],1,lodFirst[level],0,tile);drawnTriangles+=lodCount[level]/3;lodPatches[level]++;
        }
    }
}
