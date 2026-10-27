#pragma once
#include "surface.hpp"
#include <string>

// One complete map stays drawable while the other field is refined. This is A's
// edit/refine scheduler, not the camera streaming/tile cache proposed as B.
class EditPreview {
public:
    struct Snapshot {TerrainParams params{};GraphSeeds seeds{};std::vector<uint32_t> rivers;uint64_t revision=0,target=0;};
    Surface& visible;Surface worker;
    // hold: the final (full-resolution) refinement waits, e.g. while the finer patch builds.
    bool enabled=true,initialized=false,job=false,refining=false,hold=false;uint32_t cap=256,directLimit=1024;
    float budgetMs=8,settleSeconds=.30f;uint64_t promotions=0,coarseUpdates=0;
    Snapshot display,latest,work;double changedAt=0,previewAt=-10;std::string error;
    explicit EditPreview(Surface& s):visible(s),worker(s.gpu){}
    void plan(const TerrainParams& p,const GraphSeeds& g,const std::vector<uint32_t>& rivers,uint64_t revision,double now,bool editing){
        const bool changed=!latest.revision||latest.target!=revision;
        if(changed){latest={p,g,rivers,revision,revision};changedAt=now;error.clear();if(job&&work.target!=revision){worker.cancelGeneration();job=false;}}
        if(!enabled&&latest.params.meta.x<=directLimit){if(job)worker.cancelGeneration();job=false;display=latest;refining=false;return;}
        if(!enabled){
            // Direct updates above directLimit would be one multi-second GPU submission (2048: ~0.6 s, 4096: ~2.6 s, past
            // Windows' 2 s GPU watchdog). Keep the last map drawn and refine the new one in small background jobs instead.
            if(!display.revision){display=latest;display.params.meta.x=std::min(latest.params.meta.x,directLimit);display.revision=++serial;}
            const bool dirty=display.target!=latest.target||display.params.meta.x!=latest.params.meta.x;
            if(!job&&!hold&&dirty&&error.empty()&&(!editing||now-changedAt>=settleSeconds))start(latest.params.meta.x,true);
            refining=job||dirty;return;
        }
        if(!display.revision){display=latest;display.params.meta.x=std::min(p.meta.x,cap);display.revision=++serial;previewAt=now;}
        uint32_t low=std::min(latest.params.meta.x,cap);
        bool dirty=display.target!=latest.target;
        if(dirty&&display.params.meta.x<=low&&(now-previewAt>=1.0/12.0||!editing&&now-changedAt>=settleSeconds)){
            display=latest;display.params.meta.x=low;display.revision=++serial;previewAt=now;++coarseUpdates;
        }
        // Drop a completed large field to a cached low-resolution field once. The
        // old large field then becomes the worker, reusable by selective updates.
        if(!job&&dirty&&display.params.meta.x>low&&error.empty())start(low,false);
        if(!job&&!hold&&error.empty()&&now-changedAt>=settleSeconds&&!editing&&
            (display.params.meta.x!=latest.params.meta.x||display.target!=latest.target))start(latest.params.meta.x,true);
        refining=job||display.params.meta.x!=latest.params.meta.x||display.target!=latest.target;
    }
    void record(VkCommandBuffer cmd,uint32_t frame,const DrawParams& draw){
        if(!job||(hold&&work.params.meta.x>cap)){if(initialized&&frame<worker.frames.size())worker.frames[frame].recorded=false;return;}   // a held final refinement pauses
        worker.progressiveGeneration=work.params.meta.x>(enabled?cap:directLimit);worker.generationBudgetMs=budgetMs;
        try{if(worker.frames.size()!=worker.gpu.swap.ImageCount)worker.ensure(work.params.meta.x,1,1);worker.record(cmd,frame,work.params,work.seeds,draw,work.revision,nullptr,nullptr,true);}catch(const std::exception& e){error=e.what();worker.cancelGeneration();job=false;}
    }
    bool promote(){
        if(!job||worker.generationPending()||worker.fieldRevision!=work.revision||work.target!=latest.target)return false;
        visible.exchangeTerrain(worker);display=work;job=false;++promotions;previewAt=changedAt;
        refining=display.params.meta.x!=latest.params.meta.x||display.target!=latest.target;return true;
    }
    bool busy()const{return refining||job;}
    float progress()const{return job?worker.generationProgress():1;}
    std::string status()const{
        if(!error.empty())return "Refinement paused: "+error;
        if(!enabled&&!job)return "Direct updates (selected resolution)";
        if(job)return std::string(work.params.meta.x<=cap?"Updating preview ":"Refining ")+std::to_string(work.params.meta.x)+" x "+std::to_string(work.params.meta.x)+" | "+std::to_string(int(progress()*100))+"%";
        if(busy())return "Editing preview "+std::to_string(display.params.meta.x)+" x "+std::to_string(display.params.meta.x);
        return "Selected resolution ready";
    }
    void destroy(){if(initialized)worker.destroy();}
private:
    uint64_t serial=1000000;
    void start(uint32_t grid,bool final){
        try{if(!initialized){worker.initialize(&visible);worker.useMountainCache=visible.useMountainCache;initialized=true;}
            worker.ensure(grid,1,1);work=latest;work.params.meta.x=grid;work.revision=++serial;
            worker.updateRivers(work.rivers);job=true;refining=final;
        }catch(const std::exception& e){error=e.what();job=false;}
    }
};
// Property/project state keeps its selected resolution. Only the rendering scope
// sees a preview snapshot; saving while dragging must never save the coarse grid.
struct RenderSnapshotScope {
    TerrainParams& p;GraphSeeds& g;uint64_t& r;TerrainParams savedP;GraphSeeds savedG;uint64_t savedR;bool active=true;
    RenderSnapshotScope(TerrainParams& p_,GraphSeeds& g_,uint64_t& r_,const EditPreview::Snapshot& s):p(p_),g(g_),r(r_),savedP(p_),savedG(g_),savedR(r_){p=s.params;g=s.seeds;r=s.revision;}
    void restore(){if(active){p=savedP;g=savedG;r=savedR;active=false;}}
    ~RenderSnapshotScope(){restore();}
};
