#include "surface.hpp"
#include "world.hpp"
#include <algorithm>
#include <cstring>

namespace {
void dependency(VkCommandBuffer cmd){VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=b.srcAccessMask;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);}
}
GenerationStage Surface::updateStage(const TerrainParams& p,const GraphSeeds& g)const{
    if(!selectiveGeneration||!inputsValid||!fieldRevision)return GenerationStage::Terrain;
    auto stage=completedInputs.difference({p,g,riverEpoch});
    if(stage==GenerationStage::Climate&&!preClimateValid)return GenerationStage::Terrain;
    if(stage==GenerationStage::Filters&&!preFilterValid)return GenerationStage::Terrain;
    return stage;
}
void Surface::ensureCheckpoints(){
    // Two 48-byte/sample checkpoints are useful for ordinary grids. Preserve 8K
    // compatibility instead of reserving another six GiB at the largest grid.
    if(preClimate.handle||resolution>2048)return;
    VkDeviceSize bytes=VkDeviceSize(resolution)*resolution*96;
    if(gpu.availableDeviceMemory()<bytes+256ull*1024*1024)return;
    try{
    auto flags=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    preClimate=gpu.image(resolution,resolution,VK_FORMAT_R32G32B32A32_SFLOAT,flags,VK_IMAGE_ASPECT_COLOR_BIT);
    preFilter=gpu.image(resolution,resolution,VK_FORMAT_R32G32B32A32_SFLOAT,flags,VK_IMAGE_ASPECT_COLOR_BIT);
    auto usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    preClimateCells=gpu.buffer(VkDeviceSize(resolution)*resolution*32,usage,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    preFilterCells=gpu.buffer(VkDeviceSize(resolution)*resolution*32,usage,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    gpu.immediate([&](VkCommandBuffer cmd){VkImageMemoryBarrier b[2]{};for(int i=0;i<2;++i){b[i].sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b[i].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b[i].newLayout=VK_IMAGE_LAYOUT_GENERAL;b[i].srcQueueFamilyIndex=b[i].dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b[i].image=i?preFilter.handle:preClimate.handle;b[i].subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b[i].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT;}vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,b);});
    }catch(const std::exception&){gpu.release(preClimate);gpu.release(preFilter);gpu.release(preClimateCells);gpu.release(preFilterCells);preClimateValid=preFilterValid=false;}
}
void Surface::checkpoint(VkCommandBuffer cmd,uint32_t action){
    const bool before=action==1||action==3,restore=action>=3;auto& image=before?preClimate:preFilter;auto& buffer=before?preClimateCells:preFilterCells;
    if(!image.handle)return;
    dependency(cmd);VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={resolution,resolution,1};
    vkCmdCopyImage(cmd,restore?image.handle:rawField.handle,VK_IMAGE_LAYOUT_GENERAL,restore?rawField.handle:image.handle,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
    VkBufferCopy bc{0,0,VkDeviceSize(resolution)*resolution*32};vkCmdCopyBuffer(cmd,restore?buffer.handle:cellState.handle,restore?cellState.handle:buffer.handle,1,&bc);dependency(cmd);
    if(!restore){if(before)preClimateValid=true;else preFilterValid=true;}
}
float Surface::generationProgress()const{
    if(!workActive)return 1;
    if(workSteps.empty())return 0;
    auto& s=workSteps[std::min(workIndex,workSteps.size()-1)];float part=s.x&&s.y?float(uint64_t(workY)*s.x+workX)/float(uint64_t(s.x)*s.y):0;
    return std::clamp((float(workIndex)+part)/float(workSteps.size()),0.0f,1.0f);
}
void Surface::cancelGeneration(){if(workActive){++cancelledGenerations;workActive=false;inputsValid=preClimateValid=preFilterValid=false;workSteps.clear();}}
bool Surface::generateWork(VkCommandBuffer cmd,uint32_t frameIndex,const TerrainParams& p,const GraphSeeds& seeds,uint64_t revision){
    auto& f=frames.at(frameIndex);
    if(workActive&&workRevision!=revision)cancelGeneration();
    // Budget feedback: steps per frame scale with budget / measured GPU time of this slot's last frame (at most doubling).
    // Several cheap steps share one frame; one whole step is the smallest unit (largest: terrain, ~26 ms at 2048).
    if(workActive&&progressiveGeneration&&f.workTiles){auto t=timing(frameIndex);if(t.valid&&t.generate>.02)workTiles=std::clamp(uint32_t(std::min(float(f.workTiles)*2.0f,float(f.workTiles)*generationBudgetMs/float(t.generate))),1u,64u);}
    if(f.queries){vkCmdResetQueryPool(cmd,f.queries,0,7);vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,f.queries,0);}
    if(!workActive){
        auto stage=updateStage(p,seeds);lastGenerationStage=stage;
        if(stage==GenerationStage::None){fieldRevision=revision;f.workTiles=0;for(uint32_t i:{4u,5u,6u,1u,2u})if(f.queries)vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,f.queries,i);return false;}
        if(p.meta.z==5&&uint32_t(stage)<=uint32_t(GenerationStage::Filters))ensureCheckpoints();
        workRevision=revision;workInputs={p,seeds,riverEpoch};workActive=true;workIndex=workX=workY=0;workTiles=2;workSteps.clear();
        auto add=[&](VkPipeline pipeline,uint32_t x,uint32_t y,uint32_t tile=16){workSteps.push_back({pipeline,x,y,tile,0});};
        auto action=[&](uint32_t a){workSteps.push_back({{},0,0,0,a});};
        uint32_t grid=(resolution+7)/8;
        dependency(cmd);vkCmdUpdateBuffer(cmd,graphSeeds.handle,0,sizeof(seeds),&seeds);
        // The exact mod surface rule's seeded tables: only for biome-mod set 1 alone (surfaceMeta.w bits 1-3), rebuilt when the seed changes.
        if(!exactNoiseSpecs.empty()&&((seeds.surfaceMeta.w>>1)&7u)==1u&&(!exactNoiseValid||exactNoiseKey!=seeds.surfaceRandom)){
            auto words=exactSurfaceNoiseWords(seeds.surfaceRandom,exactNoiseSpecs,exactGradients);if(words.size()>exactNoiseCapacity)throw std::runtime_error("Exact surface noise tables exceed the buffer");
            for(size_t at=0;at<words.size();at+=16384)vkCmdUpdateBuffer(cmd,exactNoise.handle,at*4,std::min<size_t>(16384,words.size()-at)*4,words.data()+at);
            exactNoiseKey=seeds.surfaceRandom;exactNoiseValid=true;}
        dependency(cmd);
        vkCmdPushConstants(cmd,computeLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),&p);
        if(stage==GenerationStage::Terrain){
            ++stageRuns[0];inputsValid=preClimateValid=preFilterValid=false;
            auto header=useMountainCache?cacheHeaderForGeneration(p,seeds):GenerationCacheHeader{};mountainCacheSamples=header.info.z;
            vkCmdUpdateBuffer(cmd,mountainCache.handle,0,sizeof(header),&header);dependency(cmd);
            if(header.info.w){add(buildMountainCache,(header.info.z+63)/64,1,512);add(buildMountainSegments,(header.info.z+63)/64,1,512);}
            if(p.meta.z==5){add(buildWorldMap,grid,grid,64);add(buildOceanFloor,grid,grid,64);}
            uint32_t variant=p.meta.z==6?15u:p.meta.z==5?(header.info.w?13u:14u):p.meta.z==4?(header.info.w?11u:12u):p.meta.z==3?10u:p.meta.z==2?2u+p.meta.w:p.meta.z;
            add(variantPipeline(variant),grid,grid);
            if(p.meta.z==5){add(buildIslandSurface,grid,grid);if(hasRivers)add(buildRivers,grid,grid,16);if(preClimate.handle)action(1);}
        }else if(stage==GenerationStage::Climate)action(3);
        else if(stage==GenerationStage::Filters)action(4);
        if(p.meta.z==5&&stage<=GenerationStage::Climate){++stageRuns[1];if(seeds.climateMeta.x)add(buildClimate,grid,grid,32);if(preFilter.handle)action(2);}
        if(p.meta.z==5&&stage<=GenerationStage::Filters){++stageRuns[2];if(seeds.filterMeta.x)for(uint32_t phase:{8u,1u,0u,6u,2u,3u,4u,8u,1u,0u,6u,7u,5u})add(worldFilters[phase],grid,grid,phase==4?16:64);}
        // The biome + block pass is ~90% of a build (~0.95 s at 2048): progressive builds run it in bands of biomeBandGroups x 8 rows (~2-4 ms each at 2048) under the frame budget.
        if(p.meta.z==5&&seeds.climateMeta.x){if(stage<=GenerationStage::Biomes){++stageRuns[3];if(progressiveGeneration&&biomeBandGroups)for(uint32_t r=0;r<grid;r+=biomeBandGroups)workSteps.push_back({buildBiomes,grid,std::min(biomeBandGroups,grid-r),16,0,r});else add(buildBiomes,grid,grid);}else{++stageRuns[4];add(buildRecolor,grid,grid);}}
        if(stage<=GenerationStage::Filters)add(filter,grid,grid,64);
    }
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,computeLayout,0,1,&fields,0,nullptr);
    vkCmdPushConstants(cmd,computeLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),&p);
    uint32_t tiles=0,pushedRow=0;size_t started=workIndex;
    const bool profiling=profileSteps&&f.stepQueries;if(profiling){f.stepNames.clear();vkCmdResetQueryPool(cmd,f.stepQueries,0,64);vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,f.stepQueries,0);}
    auto profileStamp=[&](std::string name){if(profiling&&f.stepNames.size()<63){f.stepNames.push_back(std::move(name));vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,f.stepQueries,uint32_t(f.stepNames.size()));}};
    while(workIndex<workSteps.size()){
        auto& s=workSteps[workIndex];
        if(s.action){checkpoint(cmd,s.action);profileStamp("checkpoint copy "+std::to_string(s.action));++workIndex;continue;}
        if(progressiveGeneration&&tiles>=workTiles)break;
        dependency(cmd);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s.pipeline);
        if(s.row!=pushedRow){TerrainParams band=p;band.domain.w=float(s.row*8);vkCmdPushConstants(cmd,computeLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(band),&band);pushedRow=s.row;}
        if(progressiveGeneration){vkCmdDispatch(cmd,s.x,s.y,1);profileStamp(stepName(s.pipeline));workX=workY=0;++workIndex;++tiles;++generationDispatches;} // whole steps: sub-tile vkCmdDispatchBase made NVIDIA's vkDestroyDevice spin ~14 s
        else{vkCmdDispatch(cmd,s.x,s.y,1);profileStamp(stepName(s.pipeline));++workIndex;++generationDispatches;++tiles;}
    }
    dependency(cmd);f.workStage=uint32_t(started);f.workTiles=tiles;
    if(workIndex==workSteps.size()){workActive=false;completedInputs=workInputs;inputsValid=true;fieldRevision=revision;}
    for(uint32_t i:{4u,5u,6u,1u,2u})if(f.queries)vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,f.queries,i);
    return tiles!=0;
}

void Surface::exchangeTerrain(Surface& o){
    // Called only after submission, outside render-pass/command recording. One
    // promotion wait protects descriptor updates; ordinary refinement doesn't wait.
    vkCheck(vkDeviceWaitIdle(gpu.device));using std::swap;
    swap(resolution,o.resolution);swap(indexCount,o.indexCount);swap(patchCells,o.patchCells);swap(patchCount,o.patchCount);swap(lodFirst,o.lodFirst);swap(lodCount,o.lodCount);swap(fieldBytes,o.fieldBytes);swap(fieldRevision,o.fieldRevision);swap(lastParams,o.lastParams);swap(lastGenerationStage,o.lastGenerationStage);swap(mountainCacheSamples,o.mountainCacheSamples);
    swap(indices,o.indices);swap(graphSeeds,o.graphSeeds);swap(mountainCache,o.mountainCache);swap(cellState,o.cellState);swap(riverNetworks,o.riverNetworks);swap(riverWords,o.riverWords);swap(exactNoise,o.exactNoise);swap(exactNoiseKey,o.exactNoiseKey);swap(exactNoiseValid,o.exactNoiseValid);swap(riverEpoch,o.riverEpoch);swap(hasRivers,o.hasRivers);swap(biomeTable,o.biomeTable);swap(effectsBuffer,o.effectsBuffer);swap(inspectionBuffer,o.inspectionBuffer);swap(rawField,o.rawField);swap(field,o.field);swap(colors,o.colors);swap(fields,o.fields);
    swap(preClimate,o.preClimate);swap(preFilter,o.preFilter);swap(preClimateCells,o.preClimateCells);swap(preFilterCells,o.preFilterCells);swap(inputsValid,o.inputsValid);swap(preClimateValid,o.preClimateValid);swap(preFilterValid,o.preFilterValid);swap(completedInputs,o.completedInputs);
    workActive=o.workActive=false;workSteps.clear();o.workSteps.clear();treeKeyValid=false;
    if(treeInstances.handle){VkDescriptorBufferInfo infos[4]{{treeInstances.handle,0,treeInstances.size},{treeArgs.handle,0,treeArgs.size},{treeMesh.handle,0,treeMesh.size},{treeTable.handle,0,treeTable.size}};VkWriteDescriptorSet w[4]{};for(uint32_t i=0;i<4;++i){w[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w[i].dstSet=fields;w[i].dstBinding=11+i;w[i].descriptorCount=1;w[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[i].pBufferInfo=&infos[i];}vkUpdateDescriptorSets(gpu.device,4,w,0,nullptr);}
}
std::string Surface::stepName(VkPipeline p)const{
    const Surface& o=pipelineOwner?*pipelineOwner:*this;
    for(size_t i=0;i<o.generate.size();++i)if(p==o.generate[i])return "terrain variant "+std::to_string(i);
    for(size_t i=0;i<worldFilters.size();++i)if(p==worldFilters[i])return "world filter "+std::to_string(i);
    const std::pair<VkPipeline,const char*> named[]={{filter,"final filter"},{buildMountainCache,"mountain cache"},{buildMountainSegments,"mountain segments"},{buildWorldMap,"world map"},{buildOceanFloor,"ocean floor"},{buildIslandSurface,"island surface"},{buildClimate,"climate"},{buildRivers,"rivers"},{buildBiomes,"biomes"},{buildRecolor,"recolour"},{buildInspection,"inspection"}};
    for(auto& n:named)if(p==n.first)return n.second;
    return "other";
}
