// Flat block colours over a height mesh: each sample takes the block column it lies in (floor of the point) and evaluates
// it as the game does, at the set resolution. Inherited vanilla SurfaceRules with Minecraft's seeded noises and integer
// column context (vanilla_surface.glsl), FTF's StrataRule below them, then FTF's scree/erosion decorator.
vec2 matWorld;float matHeight,matWater,matGradient,matTemperature,matDepth,matBlockSlope;uint matBiome;bool matSteep;
ivec2 matBlock;int matY,matStone,matSurfaceDepth,matWaterHeight,matTopY,matColumnHeight,matMinSurface,matPreScree;double matSecondary;float matCellHeight;bool matVolcano;uint matFinal,matScreeTop;int matUnfilledTop;
vec2 matPoint,matSample;float matStep;
float materialWhite(vec2 point,uint offset){precise vec2 shifted=point+mix(vec2(-.5),vec2(.5),greaterThanEqual(point,vec2(0)));return abs(ftfVal(graph.surfaceMeta.y+offset,ivec2(shifted)));}
layout(std430,set=0,binding=16) readonly buffer ExactSurfaceNoise {uint words[];} exactNoise;   // Surface: seeded tables of the exact mod surface rule
#include "material_rules_generated.glsl"
uint materialTableBase(){return table.words[5]+256u;}
uvec4 materialData(uint id){uint base=materialTableBase(),offset=base+table.words[base+3u]+id*4u;return uvec4(table.words[offset],table.words[offset+1u],table.words[offset+2u],table.words[offset+3u]);}
uint materialTags(uint id){return materialData(id).w;}   // bits: freeterraforged:erodible, freeterraforged:rock, minecraft:dirt, minecraft:base_stone_overworld

// Exact mod surface rule (tools/prepare_exact_mod_rules.py, header word 7 of the material table): the world's whole surface rule
// for biome-mod set 1 alone, evaluated node by node as SurfaceRules does (a sequence returns its first result; a condition
// that fails skips its subtree). Conditions use the same column context as the inherited vanilla rule; noise_threshold reads
// Minecraft's NormalNoise for the world seed at (x, 0, z), computed once per column when first needed.
uint exactProgram;bool exactMode;uint matRawBiome;double exactNoiseCache[96];uint exactNoiseValid[3];
double exactDouble(uint at){return packDouble2x32(uvec2(table.words[at],table.words[at+1u]));}
double exactPerlin(uint m,int part,dvec3 p){
    int count=int(table.words[m+1u]);uint amps=exactProgram+table.words[m+2u],base=table.words[m+3u],nonzero=0u;
    for(int o=0;o<count;++o)nonzero+=exactDouble(amps+2u*uint(o))!=0.0LF?1u:0u;
    precise double sum=0.0LF;double e=ldexp(1.0LF,int(table.words[m])),f=exactDouble(m+6u);uint t=4096u+base+uint(part)*nonzero;
    for(int o=0;o<count;++o){double a=exactDouble(amps+2u*uint(o));
        if(a!=0.0LF){precise dvec3 q=p*e;double g=mcImproved(t,dvec3(mcWrap(q.x),mcWrap(q.y),mcWrap(q.z)));precise double v=a*g;sum+=v*f;++t;}
        e*=2.0LF;f/=2.0LF;}
    return sum;
}
double exactNoiseAt(uint i){uint g=i/32u,bit=1u<<(i%32u);
    if((exactNoiseValid[g]&bit)==0u){uint m=exactProgram+table.words[exactProgram+4u]+i*8u;dvec3 p=dvec3(matBlock.x,0,matBlock.y);precise dvec3 q=p*1.0181268882175227LF;precise double v=(exactPerlin(m,0,p)+exactPerlin(m,1,q))*exactDouble(m+4u);exactNoiseCache[i]=v;exactNoiseValid[g]|=bit;}
    return exactNoiseCache[i];}
int exactAnchor(uint kind,int v){return kind==0u?v:kind==1u?-int(graph.worldLevels.y)+v:int(graph.surfaceOptions.y)-1-v;}
bool exactGradient(uint k,int low,int high){
    if(matY<=low)return true;if(matY>=high)return false;
    precise double t=(double(matY)-double(low))/(double(high)-double(low));precise double d=1.0LF+t*(0.0LF-1.0LF);
    uvec4 factory=uvec4(exactNoise.words[k*4u],exactNoise.words[k*4u+1u],exactNoise.words[k*4u+2u],exactNoise.words[k*4u+3u]);
    McRandom r=mcRandomAt(factory,matBlock.x,matY,matBlock.y);return double(mcNextFloat(r))<d;
}
bool exactLeaf(uint at){uint t=table.words[at];int a=int(table.words[at+1u]),b=int(table.words[at+2u]),c=int(table.words[at+3u]),d=int(table.words[at+4u]);
    if(t==1u)return matRawBiome<32u*table.words[exactProgram+7u]&&(table.words[exactProgram+uint(a)+matRawBiome/32u]&(1u<<(matRawBiome%32u)))!=0u;
    if(t==3u)return matY>=matMinSurface;   // Context.getMinSurfaceLevel
    if(t==4u){if((b&2)!=0)return false;   // ceiling: solid columns reach the world bottom (caves are out of scope)
        int extra=((b&1)!=0?matSurfaceDepth:0)+(c!=0?int((matSecondary+1.0LF)/2.0LF*double(c)):0);return matStone<=1+a+extra;}
    if(t==5u)return matY+(d!=0?matStone:0)>=exactAnchor(uint(a),b)+matSurfaceDepth*c;
    if(t==6u)return matWaterHeight==-2147483648||matY+(d!=0?matStone:0)>=matWaterHeight+a+matSurfaceDepth*c;
    if(t==7u){double v=exactNoiseAt(uint(a));return v>=exactDouble(at+2u)&&v<=exactDouble(at+4u);}
    if(t==8u)return materialCold(matY);
    if(t==9u)return matSteep;
    if(t==10u)return matSurfaceDepth<=0;
    if(t==11u)return exactGradient(uint(a),exactAnchor(uint(b),c),exactAnchor(uint(d),int(table.words[at+5u])));
    return false;
}
bool exactCondition(uint c){uint conds=exactProgram+table.words[exactProgram+3u],at=conds+c*8u;
    if(table.words[at]==2u)return !exactLeaf(conds+table.words[at+1u]*8u);
    return exactLeaf(at);}
uint exactSurface(int y,int stone){matY=y;matStone=stone;uint count=table.words[exactProgram+1u],nodes=exactProgram+table.words[exactProgram+2u];
    for(uint i=0u;i<count;){uint a=table.words[nodes+2u*i],b=table.words[nodes+2u*i+1u];
        if((a&0x80000000u)!=0u){i+=exactCondition(a&0x7fffffffu)?1u:1u+b;continue;}
        return b==0xfffffffeu?materialBandlands():b;}
    return MATERIAL_NONE;
}
uint minecraftSurface(int y,int stone){return exactMode?exactSurface(y,stone):inheritedMinecraftSurface(y,stone);}

// FTF StrataRule (freeterraforged:overworld_strata): the selector picks one of the seed's 100 stacks, the layer bottoms come
// from the depth noise scaled to the column height (WORLD_SURFACE_WG), exactly as StrataRule.Source.initBuffer/tryApply.
uint strataStack;float strataNoise;
float strataWorley(vec2 p,uint seed,float frequency){
    precise vec2 s=p*frequency;ivec2 i=ivec2(s)-ivec2(s.x<0.0?1:0,s.y<0.0?1:0);float nearest=3.4028235e38;ivec2 best=i;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){ivec2 c=i+ivec2(dx,dy);vec2 v=FTF_CELLS[ftfHash(seed,c)&255u];precise float ax=float(c.x)+v.x-s.x,az=float(c.y)+v.y-s.y;precise float d=ax*ax+az*az;if(d<nearest){nearest=d;best=c;}}
    precise float dif=clamp(ftfVal(seed,best),-1.0,1.0)+1.0;return dif>=2.0?1.0:dif/2.0;   // CELL_VALUE mapped from [-1, 1]
}
void strataSetup(){
    vec2 p=vec2(matBlock);uint seed=graph.surfaceMeta.z;   // FTFRandomState.seed shifts the selector by Seed.toInt(world seed); Perlin nodes keep their own seeds
    // PresetStrataNoise: worley(1234153, size), warpPerlin(1234154, size/4, 2, size/2), warpPerlin(1234155, 15, 2, 30); depth perlin(1234156, size, 3).
    int size=int(graph.strataMeta.y);
    precise vec2 a=p+(vec2(ftfPerlinNode(p,1234155u,15,2,2.0),ftfPerlinNode(p,1234156u,15,2,2.0))-.5)*30.0;
    precise vec2 b=a+(vec2(ftfPerlinNode(a,1234154u,size/4,2,2.0),ftfPerlinNode(a,1234155u,size/4,2,2.0))-.5)*(float(size)/2.0);
    float selector=strataWorley(b,seed+1234153u,uintBitsToFloat(graph.strataMeta.z));uint count=graph.strataMeta.x;
    strataStack=uint(min(int(count)-1,int(selector*float(count))));strataNoise=ftfPerlinNode(p,1234156u,size,3,2.0);
}
int javaRound(float a){if(isnan(a))return 0;float f=floor(a);return int(f)+(a-f>=.5?1:0);}
uint strataBlock(uint m){return m==0u?BLOCK_DIRT:m==1u?BLOCK_COARSE_DIRT:m==2u?BLOCK_SAND:m==3u?BLOCK_GRAVEL:m==4u?BLOCK_CLAY:m==5u?BLOCK_GRANITE:m==6u?BLOCK_ANDESITE:m==7u?BLOCK_STONE:BLOCK_DIORITE;}
uint strataAt(int y){
    uint base=strataStack*41u,count=graph.strata[base],material=0u;if(count==0u)return BLOCK_STONE;precise float sum=0.0;
    for(uint i=0u;i<count;++i){precise float depth=strataNoise*uintBitsToFloat(graph.strata[base+1u+i]);sum+=depth;}
    int bottom=matColumnHeight;
    for(uint i=0u;i<count;++i){material=(graph.strata[base+36u+i/8u]>>(4u*(i%8u)))&15u;precise float depth=strataNoise*uintBitsToFloat(graph.strata[base+1u+i]);precise float share=worldDiv(depth,sum)*float(matColumnHeight);bottom-=javaRound(share);if(y>bottom)break;}
    return strataBlock(material);
}
// The block dy below the top after surface building: the inherited rule where it applies, else the strata (or the default stone).
uint columnBlock(int dy){uint b=minecraftSurface(matTopY-dy,dy+1);if(b!=MATERIAL_NONE)return b;return (graph.surfaceMeta.x&2u)!=0u?strataAt(matTopY-dy):BLOCK_STONE;}

float materialPerlin(vec2 point,uint seed,int scale){return ftfPerlinNode(point,seed,scale,1,2.0);}
uint materialWeighted(float value,bool dirt,bool omitMoss){value=clamp(value,0.0,1.0);float target=value*(dirt?(omitMoss?3.0:5.0):(omitMoss?6.0:7.0));if(dirt){if(target<=2.0)return BLOCK_COARSE_DIRT;if(!omitMoss&&target<=4.0)return BLOCK_MOSS_BLOCK;return BLOCK_GRAVEL;}if(target<=1.0)return BLOCK_GRAVEL;if(target<=2.0)return BLOCK_COARSE_DIRT;if(target<=4.0)return BLOCK_ANDESITE;if(target<=6.0)return BLOCK_TUFF;return BLOCK_MOSS_BLOCK;}
uint materialCluster(vec2 world,uint offset,bool dirt){uint seed=graph.surfaceMeta.y;precise float oldSample=clamp((materialPerlin(world*.05,seed+7777u,2)+1.0)*.5,0.0,1.0);precise vec2 warp=vec2(materialPerlin(world*.04,seed+1001u,2),materialPerlin(world*.04,seed+2002u,2))*22.0;precise float warped=clamp((materialPerlin((world+warp)*.05,seed+7777u,2)+1.0)*.5+(materialWhite(world,offset)-.5)*.12,0.0,1.0);uint result=materialWeighted(warped,dirt,false);if(result==BLOCK_MOSS_BLOCK)return result;result=materialWeighted(oldSample,dirt,false);return result==BLOCK_MOSS_BLOCK?materialWeighted(oldSample,dirt,true):result;}
uint materialGround(uint top){if(top==BLOCK_GRASS_BLOCK||top==BLOCK_MYCELIUM)return materialCluster(matWorld,4u,true);uint tags=materialTags(top);if((tags&8u)!=0u)return BLOCK_MOSS_BLOCK;if((tags&4u)!=0u)return top;if(top==BLOCK_SAND)return BLOCK_SMOOTH_SANDSTONE;if(top==BLOCK_RED_SAND)return BLOCK_SMOOTH_RED_SANDSTONE;return materialCluster(matWorld,4u,true);}
// ScreeFeature.erodeRock: the first freeterraforged:rock block 3 to 31 blocks down is pulled up to the surface (gravel if none).
uint materialRock(){for(int dy=3;dy<32;++dy){uint b=columnBlock(dy);if((materialTags(b)&2u)!=0u)return b;}return BLOCK_GRAVEL;}
uint materialScree(uint top,uint index){if((graph.surfaceMeta.x&1u)==0u)return top;uint terrain=cells[index].terrain&255u;
    // Desert erosion is a distinct source branch, before the erodible-tag test.
    if(matBiome==MATERIAL_BIOME_DESERT){float h=worldDiv(matHeight,graph.worldLevels.w),ground=worldDiv(min(graph.worldLevels.z,graph.worldLevels.w),graph.worldLevels.w);if(matGradient<.15||h<ground+worldDiv(10.0,graph.worldLevels.w))return top;float value=h+materialPerlin(matWorld,435u,8)*worldDiv(16.0,graph.worldLevels.w);if(matGradient>.3||value>ground+worldDiv(40.0,graph.worldLevels.w)){uint result=BLOCK_SMOOTH_SANDSTONE;if(value>ground+worldDiv(40.0,graph.worldLevels.w)){if(matGradient>.975)result=BLOCK_TERRACOTTA;else if(matGradient>.85)result=BLOCK_BROWN_TERRACOTTA;else if(matGradient>.75)result=BLOCK_ORANGE_TERRACOTTA;else if(matGradient>.65)result=BLOCK_TERRACOTTA;}return result;}return top;}
    // surfaceY (WORLD_SURFACE_WG, water included) above the terrain or below sea level - 1 is skipped.
    if(terrain==10u||terrain==11u||matColumnHeight>matTopY||matColumnHeight<int(graph.worldLevels.z)-1||matBiome==MATERIAL_BIOME_WOODED_BADLANDS||matBiome==MATERIAL_BIOME_BADLANDS||(materialTags(top)&1u)==0u)return top;
    precise float height=worldDiv(matHeight,graph.worldLevels.w)+materialWhite(matWorld,0u)*graph.scree1.w,steepness=matGradient+materialWhite(matWorld,1u)*.011764706;
    vec4 continent=ftfUplift(matWorld);float baseline=worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w)+continentWaterHeight(continent.y,continent.z),variance=materialPerlin(matWorld,0u,100);
    precise float rockThreshold=variance*worldDiv(graph.scree0.x,255.0)+baseline+worldDiv(graph.scree0.y,255.0),dirtThreshold=variance*worldDiv(graph.scree0.z,255.0)+baseline+worldDiv(graph.scree0.w,255.0);
    // getMaterial: rock(defaultBlock) is stone; any changed rock-tag result except tuff runs erodeRock and skips the scree.
    uint material=top,result=top;
    if(steepness>graph.scree1.x||height>rockThreshold)material=BLOCK_STONE;else if(steepness>graph.scree1.z||height>rockThreshold)material=BLOCK_TUFF;else if(steepness>graph.scree1.y&&height>dirtThreshold)material=materialGround(top);
    if(material!=top){if((materialTags(material)&2u)!=0u&&material!=BLOCK_TUFF)return materialRock();result=material;}
    if(steepness>=graph.scree1.z&&(cells[index].sediment+materialWhite(matWorld,2u))*.011764706>.55)result=materialCluster(matWorld,3u,false);
    return result;
}
float materialHeightAt(vec2 point){float n=float(p.meta.x);point=clamp(point,vec2(0),vec2(n-1));ivec2 lo=ivec2(floor(point)),hi=min(lo+1,ivec2(int(p.meta.x)-1));vec2 f=point-vec2(lo);return mix(mix(imageLoad(rawField,lo).x,imageLoad(rawField,ivec2(hi.x,lo.y)).x,f.x),mix(imageLoad(rawField,ivec2(lo.x,hi.y)).x,imageLoad(rawField,hi).x,f.x),f.y);}
// FTF placeRiverWater (MixinSurfaceSystem, start of buildSurface, so the surface rules run on its result). A water cell is
// wetland terrain, or river/lake terrain with riverWaterLevel > 0; a column's water Y is Levels.scale(getComplexWaterHeight(its
// own water table) + levels.water). Cell terrain comes from the nearest grid sample (resolution); water tables are evaluated at
// the block itself.
bool materialWaterCell(uint i){uint t=cells[i].terrain&255u;return t==11u||((t==10u||t==12u)&&cells[i].waterHeight>0.0);}
float materialWaterLine(){return worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w);}
int materialOceanY(){precise float scaled=materialWaterLine()*graph.worldLevels.w;return int(scaled);}
int materialWaterY(vec2 world){vec4 continent=ftfUplift(world);precise float level=continentWaterHeight(continent.y,continent.z)+materialWaterLine(),scaled=level*graph.worldLevels.w;return int(scaled);}
uint materialGridCell(vec2 grid){uint n=p.meta.x;ivec2 c=clamp(ivec2(grid+.5),ivec2(0),ivec2(int(n)-1));return uint(c.y)*n+uint(c.x);}
// Water top of block column b (grid point g) whose solid top is top: a water cell fills to its water Y (at least the ocean Y)
// when that is above the top; any other column holds sea water up to seaLevel - 1 (the global fluid picker).
int materialWaterTop(vec2 grid,vec2 world,int top){uint i=materialGridCell(grid);
    if(materialWaterCell(i)){int y=max(materialWaterY(world),materialOceanY());if(y>top)return y;}
    return int(graph.worldLevels.z)-1;}
// Solid top of block column b (grid point g) after placeRiverWater's stone fills on a column that is not a water cell and not
// below the ocean Y: up to its water table in a bank / valley floor / valley fade-out zone (when above the ocean Y), and up to
// the highest water Y of the four neighbouring river/lake blocks, classified by the river pass at their own block positions
// (riverClassify, starting from the height field there). Transition fills under water columns are not visible and not applied.
int materialRiverFill(ivec2 b,vec2 g,int top){uint i=materialGridCell(g);if(materialWaterCell(i))return top;int ocean=materialOceanY();if(top<ocean)return top;
    int raised=top;uint zone=(cells[i].terrain>>10)&7u;
    if(zone>=2u&&top>ocean){int table=materialWaterY(vec2(b));if(top<table)raised=table;}
    if(zone!=0u||cells[i].riverMask<1.0){const ivec2 offsets[4]=ivec2[4](ivec2(0,-1),ivec2(0,1),ivec2(1,0),ivec2(-1,0));
        for(int k=0;k<4;++k){ivec2 nb=b+offsets[k];vec2 ng=matPoint+(vec2(nb)-matSample)*matStep;uint t=riverClassify(vec2(nb),worldDiv(materialHeightAt(ng),graph.worldLevels.w))&255u;
            if(t==10u||t==12u)raised=max(raised,materialWaterY(vec2(nb)));}}
    return raised;}
// WORLD_SURFACE_WG height (top non-air block, water included) of block column b, from the height field at that block.
int materialColumnHeight(ivec2 b){vec2 g=matPoint+(vec2(b)-matSample)*matStep;int top=int(materialHeightAt(g));int water=materialWaterTop(g,vec2(b),top);return max(materialRiverFill(b,g,top),water);}
// NoiseChunk.computePreliminarySurfaceLevel at a surface-cell corner column: the highest y (from the world top in 8-block
// cell steps) where FTF's initial_density_without_jaggedness exceeds 0.390625. FTF's depth is yClampedGradient plus
// offset = -1.00375 + 2 * clampToNearestUnit(height); factor is Minecraft's overworld/factor spline over FTF's cell fields.
int materialPreliminary(ivec2 b){
    uint n=p.meta.x;ivec2 c=clamp(ivec2(round(matPoint+(vec2(b)-matSample)*matStep)),ivec2(0),ivec2(int(n)-1));uint i=uint(c.y)*n+uint(c.x);
    vec4 field=imageLoad(rawField,c);vec2 world=(vec2(dvec2(c)/double(n-1))-.5)*p.domain.x+p.domain.yz;
    double factor=mcFactor(biomeContinentalness(world,field,i),cells[i].erosion,cells[i].weirdness);
    double R=double(graph.worldLevels.w),scaled=double(worldDiv(field.x,graph.worldLevels.w))*R,truncated=scaled<0.0LF?ceil(scaled):floor(scaled);precise double unit=(truncated+1.0LF)/R;
    precise double offset=-1.0037500858306885LF+2.0LF*unit;
    int depth=int(graph.worldLevels.y),top=int(graph.surfaceOptions.y);double from=double(1.0+float(depth)/128.0),to=double(1.0-float(top)/128.0);
    // Down to -depth + 24 the slide factor is 1 and, with factor >= 0, every step below is non-increasing in y (each rounded
    // operation is monotone), so the first y from the top above the threshold is found by bisection: the same y as the scan.
    int first=top;
    if(factor>=0.0LF&&top>=-depth+24){int lo=0,hi=(top-(-depth+24))/8+1;
        while(lo<hi){int k=(lo+hi)/2,y=top-8*k;
            precise double t=(double(y)-double(-depth))/(double(top)-double(-depth));precise double gradient=t<0.0LF?from:t>1.0LF?to:from+t*(to-from);
            precise double d=gradient+offset;precise double m=d*factor;m=m>0.0LF?m:m*.25LF;precise double v=4.0LF*m;v=v+-0.703125LF;v=clamp(v,-64.0LF,64.0LF);
            precise double slid=1.0LF*(v+-0.1171875LF);slid=slid+0.1171875LF;
            if(slid>0.390625LF)hi=k;else lo=k+1;}
        first=top-8*lo;}
    for(int y=first;y>=-depth;y-=8){
        precise double t=(double(y)-double(-depth))/(double(top)-double(-depth));precise double gradient=t<0.0LF?from:t>1.0LF?to:from+t*(to-from);
        precise double d=gradient+offset;precise double m=d*factor;m=m>0.0LF?m:m*.25LF;precise double v=4.0LF*m;v=v+-0.703125LF;v=clamp(v,-64.0LF,64.0LF);
        precise double s=(double(y)-double(-depth))/(double(-depth+24)-double(-depth));s=s<0.0LF?0.0LF:s>1.0LF?1.0LF:s;precise double slid=s*(v+-0.1171875LF);slid=slid+0.1171875LF;
        if(slid>0.390625LF)return y;
    }
    return 2147483647;
}
#ifdef MATERIAL_CORNERS
// Corner pre-pass (biomes.comp CORNERS, src/surface_generation.cpp dispatches it under the same condition): in the block view
// at sample spacing <= 4 blocks, materialPreliminary runs once per 16-block corner instead of 4 times per column. Inspection
// computes directly.
int cornerBound(){return int(p.domain.x/16.0)+5;}
bool cornerUse(){return graph.biomeSeeds2.w==3u&&p.domain.x<=4.0*float(p.meta.x-1u)&&(cornerBound()+7)/8*8<=1024;}
ivec2 cornerOrigin(){return (ivec2(floor(p.domain.yz-p.domain.x*.5))>>4)-1;}
int cornerLevel(ivec2 corner){
    if(!INSPECTION&&cornerUse()){ivec2 k=(corner>>4)-cornerOrigin();int b=cornerBound();if(all(greaterThanEqual(k,ivec2(0)))&&all(lessThan(k,ivec2(b))))return corners.levels[k.y*b+k.x];}
    return materialPreliminary(corner);}
#else
int cornerLevel(ivec2 corner){return materialPreliminary(corner);}
#endif
// Biome features of the exact mod set that rewrite the top block, applied after scree (prepare_exact_mod_rules.py
// decorations): recolour (replace_blobs) and scatter (simple_block over environment_scan: the top is a target and the blocks
// two above it at +x and +-z are air, i.e. those columns are at most one block higher).
uint exactDecorate(uint top){
    uint count=table.words[exactProgram+9u],at=exactProgram+table.words[exactProgram+8u];
    for(uint d=0u;d<count;++d,at+=8u){uint kind=table.words[at],biome=table.words[at+1u];if(biome!=matRawBiome)continue;
        if(kind==1u){if(top==table.words[at+2u])top=table.words[at+3u];continue;}
        if(kind==2u){bool target=false;uint n=table.words[at+3u],list=exactProgram+table.words[at+4u];for(uint t=0u;t<n;++t)target=target||top==table.words[list+t];
            if(target&&materialColumnHeight(matBlock+ivec2(1,0))<=matTopY+1&&materialColumnHeight(matBlock+ivec2(0,1))<=matTopY+1&&materialColumnHeight(matBlock+ivec2(0,-1))<=matTopY+1)top=table.words[at+2u];}
    }
    return top;
}
// SurfaceRules.Context.getMinSurfaceLevel: floor(lerp2 of the four corner levels) + surfaceDepth - 8.
int materialMinSurface(){
    ivec2 cell=matBlock>>4,corner=cell<<4;double c0=double(cornerLevel(corner)),c1=double(cornerLevel(corner+ivec2(16,0))),c2=double(cornerLevel(corner+ivec2(0,16))),c3=double(cornerLevel(corner+ivec2(16,16)));
    double fx=double(float(matBlock.x&15)/16.0),fz=double(float(matBlock.y&15)/16.0);precise double a=c0+fx*(c1-c0),b=c2+fx*(c3-c2);precise double v=a+fz*(b-a);
    return int(min(floor(v),2147483647.0LF))+matSurfaceDepth-8;
}
#include "mod_material_rules_generated.glsl"
uint materialAt(vec2 world,vec4 field,uint index,uint biome,float water){
    matSample=world;matBlock=ivec2(floor(world));matWorld=vec2(matBlock);matStep=float(p.meta.x-1u)/p.domain.x;matPoint=vec2(index%p.meta.x,index/p.meta.x);
    vec2 own=matPoint+(matWorld-matSample)*matStep;matHeight=materialHeightAt(own);matTopY=int(matHeight);int waterTop=materialWaterTop(own,vec2(matBlock),matTopY);matUnfilledTop=matTopY;matTopY=materialRiverFill(matBlock,own,matTopY);matWater=float(waterTop+1);
    matColumnHeight=max(matTopY,waterTop);matWaterHeight=matTopY<waterTop?waterTop+1:-2147483648;
    matBiome=canonicalBiome(biome);matRawBiome=biome;matGradient=cells[index].gateMin;
    exactProgram=table.words[materialTableBase()+7u];exactMode=exactProgram!=0u&&((graph.surfaceMeta.w>>1u)&7u)==1u;exactNoiseValid=uint[3](0u,0u,0u);
    if(graph.filterMeta.x==0u){float sum=0.0;for(int y=-1;y<=2;++y)for(int x=-1;x<=2;++x){if(x==0&&y==0)continue;sum+=abs(field.x-max(materialHeightAt(matPoint+vec2(x,y)*matStep),graph.worldLevels.z-1.0));}matGradient=worldDiv(sum*10.0,graph.worldLevels.w);}
    // SteepMaterialCondition: heights one block north/south, then west/east, clamped to the chunk.
    ivec2 local=matBlock&15,chunk=matBlock-local;
    int north=materialColumnHeight(chunk+ivec2(local.x,max(local.y-1,0))),south=materialColumnHeight(chunk+ivec2(local.x,min(local.y+1,15)));
    matSteep=south>=north+4;if(!matSteep)matSteep=materialColumnHeight(chunk+ivec2(max(local.x-1,0),local.y))>=materialColumnHeight(chunk+ivec2(min(local.x+1,15),local.y))+4;
    matBlockSlope=max(abs(float(south-north)),0.0);
    uint base=materialTableBase(),info=base+table.words[base+4u]+biome*4u;matBaseTemperature=uintBitsToFloat(table.words[info]);matFrozenModifier=matBiome==MATERIAL_BIOME_FROZEN_OCEAN||matBiome==MATERIAL_BIOME_DEEP_FROZEN_OCEAN;
    matTemperature=materialTemperature(matTopY);
    materialNoises();matSurfaceDepth=materialSurfaceDepth();matSecondary=matNoise[1];matDepth=float(matSurfaceDepth);matMinSurface=materialMinSurface();
    if((graph.surfaceMeta.x&2u)!=0u)strataSetup();
    uint top=minecraftSurface(matTopY,1);
    if(top==MATERIAL_NONE)top=(graph.surfaceMeta.x&2u)!=0u?strataAt(matTopY):BLOCK_STONE;
    if(!exactMode)top=moddedSurface(biome,top);   // the exact rule already is the whole mod surface
    matPreScree=int(top);
    top=materialScree(top,index);if(exactMode&&matWaterHeight==-2147483648)top=exactDecorate(top);matScreeTop=top;
    // Snow, TOP_LAYER_MODIFICATION. Minecraft's freeze_top_layer: a layer one block above the top where that block is cold and
    // SnowLayerBlock.canSurvive (not on ice, packed ice or powder snow). Then FTF's DecorateSnowFeature, only on columns whose
    // cell height scales to the top block at or above sea level: erode removes the layer where Biome.getTemperature(top) <= 0.25
    // and the snow erosion test holds; smooth sets the layer count from the cell height's fraction of a block (8 = snow block).
    matCellHeight=worldDiv(matHeight,graph.worldLevels.w);matVolcano=(cells[index].terrain&255u)==3u;
    if(matWaterHeight==-2147483648&&top!=BLOCK_ICE&&top!=BLOCK_PACKED_ICE&&top!=BLOCK_POWDER_SNOW&&materialCold(matTopY+1)){
        bool snow=true;precise float h=matCellHeight;
        if(int(h*graph.worldLevels.w)==matTopY&&matTopY>=int(graph.worldLevels.z)){
            if((graph.surfaceMeta.x&4u)!=0u&&materialTemperature(matTopY)<=.25){
                precise float variation=-(materialPerlin(matWorld,0u,100)*worldDiv(16.0,255.0)+0.0),v=matVolcano?.15:0.0;
                precise float steepness=matGradient+variation+materialWhite(matWorld,5u)*.011764706+v,height=h+variation+materialWhite(matWorld,4u)*graph.scree1.w+v;
                precise float dirt=materialPerlin(matWorld,0u,100)*worldDiv(graph.scree0.z,255.0)+worldDiv(graph.scree0.w,255.0);
                snow=!(steepness>graph.scree1.x||(steepness>.45&&height>.37254903)||(steepness>graph.scree1.y&&height>dirt));
            }
            if(snow&&(graph.surfaceMeta.x&8u)!=0u){precise float layers=worldDiv(h,worldDiv(1.0,graph.worldLevels.w));float depth=layers-float(int(layers));
                if(depth>0.0&&int(depth*8.0+.5)>=8){matFinal=BLOCK_SNOW_BLOCK;return matFinal;}}
        }
        if(snow)top=BLOCK_SNOW;
    }
    // Submerged column: the water's top block, frozen where Biome.shouldFreeze (temperature at the water surface).
    if(matWaterHeight!=-2147483648&&(graph.surfaceMeta.w&1u)==0u)top=materialCold(waterTop)?BLOCK_ICE:BLOCK_WATER;
    matFinal=top;return top;
}
// Verification channel (--material-debug k): one 24-bit slice of the column context per run, read back as the block id.
uint materialDebug(int k){
    uvec2 bits;
    if(k>=100&&k<388){if(!exactMode)return 0u;int n=(k-100)/3,part=(k-100)%3;if(uint(n)>=table.words[exactProgram+5u])return 0u;bits=unpackDouble2x32(exactNoiseAt(uint(n)));return part==0?bits.x&0xffffffu:part==1?((bits.x>>24)|(bits.y<<8))&0xffffffu:bits.y>>16;}   // exact mod rule noises
    if(k==83)return matRawBiome;
    if(k>=84&&k<=86){int dy=k==84?3:k==85?10:25;uint b=minecraftSurface(matTopY-dy,dy+1);return b==MATERIAL_NONE?0xffffffu:b;}   // the surface rule below the top (scree reads 3..31)
    if(k>=10&&k<55){int n=(k-10)/3,part=(k-10)%3;bits=unpackDouble2x32(matNoise[n]);return part==0?bits.x&0xffffffu:part==1?((bits.x>>24)|(bits.y<<8))&0xffffffu:bits.y>>16;}
    if(k>=59&&k<=67){   // preliminary surface of this sample's own grid column and its inputs (continentalness, erosion, ridges, height)
        if(k==59)return uint(materialPreliminary(ivec2(round(matSample))))&0xffffffu;
        uint i=uint(matPoint.y)*p.meta.x+uint(matPoint.x);vec4 field=imageLoad(rawField,ivec2(matPoint));vec2 world=(vec2(dvec2(matPoint)/double(p.meta.x-1u))-.5)*p.domain.x+p.domain.yz;
        int j=(k-60)/2;float f=j==0?biomeContinentalness(world,field,i):j==1?cells[i].erosion:j==2?cells[i].weirdness:field.x;uint b=floatBitsToUint(f);return (k-60)%2==0?b&0xffffffu:b>>24;
    }
    int v=k==0?matTopY:k==1?matColumnHeight:k==2?matSurfaceDepth:k==3?matMinSurface:k==4?int(strataStack):k==5?int(floatBitsToUint(strataNoise)&0xffffffu):k==6?int(floatBitsToUint(strataNoise)>>24):k==7?int(matBiome):k==8?matPreScree:k==9?(matSteep?1:0)|(matWaterHeight==-2147483648?0:2):
        k>=68&&k<=71?int(strataAt(matTopY-(k==68?0:k==69?5:k==70?20:60))):k==72?int(matFinal)|(matVolcano?0x800000:0):
        k==73?int(floatBitsToUint(matGradient)&0xffffffu):k==74?int(floatBitsToUint(matGradient)>>24):k==75?int(floatBitsToUint(matCellHeight)&0xffffffu):k==76?int(floatBitsToUint(matCellHeight)>>24):k==77?int(graph.surfaceMeta.y&0xffffffu):k==78?int(graph.surfaceMeta.y>>24):k==79?int(matScreeTop):k==80?matUnfilledTop:k==81?int((cells[materialGridCell(matPoint)].terrain>>10)&7u):k==82?int(riverClassify(vec2(matBlock),worldDiv(matHeight,graph.worldLevels.w))&255u):
        k==55?matBlock.x:k==56?matBlock.y:k==57?int(floatBitsToUint(matTemperature)&0xffffffu):k==58?int(floatBitsToUint(matTemperature)>>24):0;
    return uint(v)&0xffffffu;
}
vec3 materialColor(uint block,uint biome,vec2 world){uvec4 data=materialData(block);vec3 color=unpackRgb(data.x);uint offset=table.words[4]+biome*4u;
    if(data.y==1u){uint rgb=table.words[offset+1u];if(swampBiome(biome))rgb=biomeInfoNoise(dvec2(floor(world))*.0225LF)<-.1LF?5011004u:6975545u;color*=unpackRgb(rgb);}else if(data.y==2u)color*=unpackRgb(table.words[offset+2u]);else if(data.y==3u)color*=unpackRgb(table.words[offset+3u]);else if(data.y==4u)color*=unpackRgb(0x80a755u);else if(data.y==5u)color*=unpackRgb(0x619961u);
    return color;
}
