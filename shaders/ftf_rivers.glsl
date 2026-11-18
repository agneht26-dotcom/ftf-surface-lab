// Original UpliftRiverCarver/Network/Wetland surface branches.
layout(std430,set=0,binding=6) readonly buffer RiverNetworkData {uint words[];} rivers;
// The cell being carved: ftfRivers loads and stores cells[cellIndex]; riverClassify carves a scratch copy (no side effects).
CellState riverCell;
precise vec4 riverVec(uint offset){return uintBitsToFloat(uvec4(rivers.words[offset],rivers.words[offset+1u],rivers.words[offset+2u],rivers.words[offset+3u]));}
uvec4 riverUInt(uint offset){return uvec4(rivers.words[offset],rivers.words[offset+1u],rivers.words[offset+2u],rivers.words[offset+3u]);}
float riverSin(float angle){return uintBitsToFloat(FTF_SIN_BITS[uint(int(angle*651.8986))&4095u]);}
float riverCos(float angle){return riverSin(angle+1.5708);}
float riverT(vec2 p,vec4 ends){precise vec2 d=ends.zw-ends.xy;precise float value=(p.x-ends.x)*d.x+(p.y-ends.y)*d.y;return worldDiv(value,d.x*d.x+d.y*d.y);}
float riverDistance2(vec2 p,float t,vec4 ends){precise vec2 nearest=t<=0.0?ends.xy:t>=1.0?ends.zw:ends.xy+t*(ends.zw-ends.xy);precise vec2 d=p-nearest;return d.x*d.x+d.y*d.y;}
float riverSize(float t,vec2 range,float fade,float inverse){if(t<0.0)return range.x;if(t>1.0)return range.y;if(range.x==range.y)return range.x;if(t>=fade)return range.y;return ftfLerp(range.x,range.y,t*inverse);}
float riverAlpha(float t,float distance,vec2 range,float fade,float inverse,float scale){precise float size=riverSize(t,range,fade,inverse)*scale;return distance>=size?0.0:1.0-worldDiv(distance,size);}
vec2 riverDomain(vec2 p,uint seed,int scale,int oct,float strength){return vec2(ftfPerlinNode(p,seed,scale,oct,2.0)-.5,ftfPerlinNode(p,seed+1u,scale,oct,2.0)-.5)*strength;}
float riverDeterministic(uint64_t seed){seed^=seed>>33;seed*=0xff51afd7ed558ccdUL;seed^=seed>>33;seed*=0xc4ceb9fe1a85ec53UL;seed^=seed>>33;return float(seed&0xffffffUL)/16777216.0;}
vec2 riverOffset(vec2 p,float t,vec4 warp,uint seed,vec2 normal,float length){precise float alpha=t<warp.x?t*worldDiv(1.0,warp.x):t>warp.y?(1.0-t)*worldDiv(1.0,1.0-warp.y):1.0;precise float distance=alpha*warp.w,noise=ftfSimplexScaled(p*warp.z,seed,99.83685);precise vec2 offset=normal*noise*distance;precise float wiggleAlpha=ftfMap(t,0.0,.075),factor=length*.0004,frequency=8.0*factor,wiggleDist=clamp(wiggleAlpha*25.0*factor,2.0,45.0),rads=noise+t*6.2831855*frequency;offset.x+=riverCos(rads)*normal.x*wiggleDist;offset.y+=riverSin(rads)*normal.y*wiggleDist;return offset;}
float riverTerrace(float progress,float terrace,float drainage,float steps){precise float intact=max(0.0,terrace-drainage*1.5),strength=clamp(intact*1.75,0.0,1.0);if(strength<=0.0)return progress;precise float scaled=progress*steps,whole=floor(scaled),fraction=scaled-whole,bias=clamp(.78-drainage*.14,.55,.88),talus=clamp(.14+drainage*.22,.04,.45),stepped;
 if(fraction<bias){precise float t=worldDiv(fraction,bias);stepped=pow(t,3.5)*talus;}else{precise float t=worldDiv(fraction-bias,1.0-bias),sharp=t*t,eased=t*t*(3.0-2.0*t);stepped=ftfLerp(talus,1.0,ftfLerp(sharp,eased,.5));}return ftfLerp(progress,worldDiv(whole+stepped,steps),strength*.9);
}
float riverLakeMultiplier(vec2 p,float t,float waterTable,float flatness,vec4 ends){int plateau=ftfWaterStepId(waterTable);precise vec4 lake=riverVec(4u),range=riverVec(8u);if(plateau< -1||t<range.x-.04||t>range.y+.04)return 1.0;
 uint64_t base=(uint64_t(floatBitsToUint(ends.x))<<32)|uint64_t(floatBitsToUint(ends.y)),index=uint64_t(int64_t(plateau));if(riverDeterministic(base^(index*25214903917UL))>=lake.x)return 1.0;
 precise float minimum=worldDiv(lake.y,100.0),maximum=worldDiv(lake.z,100.0),step=minimum+riverDeterministic(base^(index*10005465364UL))*(maximum-minimum),warp=worldSimplex(p,14878u,55,3,false),organic=step*(1.0+warp*.45),distance=1.0;
 if(t<range.x)distance=clamp(worldDiv(t-(range.x-.04),.04),0.0,1.0);else if(t>range.y)distance=clamp(worldDiv(range.y+.04-t,.04),0.0,1.0);distance=distance*distance*(3.0-2.0*distance);return 1.0+flatness*organic*distance;
}
void riverTag(uint cellIndex,float water,float bedHeight,bool lake){   /* bit 512 marks lake water (a widened river stretch); generation still treats it as river (role 10) */
    uint type=riverCell.terrain&255u;if(type==12u)return;precise float waterLine=worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w),level=max(waterLine,water);
    if(graph.filterMeta2.x>=4u){if(level>riverCell.waterHeight)riverCell.waterHeight=level;if(bedHeight<=level){riverCell.terrain=(riverCell.terrain&(7u<<10))|10u|256u|(lake?512u:0u);}else if(lake&&(riverCell.terrain&255u)==10u)riverCell.terrain|=512u;return;}
    if(level>riverCell.waterHeight){riverCell.terrain=(riverCell.terrain&(7u<<10))|10u|256u|(lake?512u:0u);riverCell.waterHeight=level;}else if(lake&&(riverCell.terrain&255u)==10u)riverCell.terrain|=512u;}
void carveRiver(uint node,uint cellIndex,vec2 previous,float previousT,vec2 current,float t,vec4 continent,inout float height,bool classifyOnly){uint offset=rivers.words[14]+node*40u;precise vec4 ends=riverVec(offset),fade=riverVec(offset+8u),widths=riverVec(offset+12u),ranges=riverVec(offset+16u),elevation=riverVec(offset+20u),curve=riverVec(offset+36u);
 {// Exact early reject (performance): before any noise, bound the widest zone 4 this node could reach here using the ranges of
  // its noise terms (worldSimplex stays inside [-.25,1.25]). A pixel outside that bound and outside the valley mask is left
  // untouched by the full evaluation below, so skipping it changes nothing. NaN bounds compare false and never skip.
  // The valley mask needs no noise: apply it here exactly as below (the repeat below is an idempotent min).
  precise float d2=riverDistance2(current,t,ends),pd2=riverDistance2(previous,previousT,ends),vd2=graph.filterMeta2.x>=4u?d2:min(d2,pd2),valleyEarly=riverAlpha(t,vd2,ranges.xy,fade.w,fade.z,1.0);
  if(!classifyOnly&&valleyEarly>0.0){valleyEarly=pow(valleyEarly,curve.x+curve.y*valleyEarly);riverCell.riverMask=min(riverCell.riverMask,1.0-valleyEarly);}
  {
   const float scaleMax=1.4375*1.5,scaleMin=.9125*.9,widthMin=.9125,widthMax=1.4375;
   vec4 lakeInfo=riverVec(4u);float lakeMax=1.0+max(0.0,max(lakeInfo.y,lakeInfo.z)*.01)*1.5625;
   float zone1Max=sqrt(riverSize(t,widths.xy,fade.w,fade.z)*scaleMax)*lakeMax;
   if(classifyOnly){
    if(sqrt(d2)>zone1Max*1.01+2.0)return;
   }else{
    float bank=elevation.z*graph.worldLevels.w,bankMax=bank>=0.0?bank*scaleMax:bank*scaleMin;
    float waterBound=continentWaterHeight(continent.y,continent.z)+worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w);
    float shrinkBound=clamp(t*fade.z,0.0,1.0),coefficient=shrinkBound*shrinkBound+4.0+(1.0+float(int((height-waterBound)*graph.worldLevels.w))/100.0);
    float unshrunkHigh=curve.w>=0.0?curve.w*widthMax*1.95:curve.w*widthMin*.05,unshrunkLow=curve.w>=0.0?curve.w*widthMin*.05:curve.w*widthMax*1.95;
    if(graph.filterMeta2.x<4u){
     float zone4Max=zone1Max+bankMax+(coefficient>=0.0?unshrunkHigh:unshrunkLow)*coefficient;
     if(sqrt(d2)>zone4Max*1.01+1.0)return;
    }else{
     float mMaxZ1=curve.z*1.375*(1.0+max(0.0,lakeInfo.z)*.0145),mMaxZ2=worldDiv(elevation.z,worldDiv(1.0,graph.worldLevels.w))*1.89;
     float mMaxFade=curve.w*.80+max(curve.w*2.6325,max(graph.filter5.y,graph.worldLevels.w)*2.2),mRad=max(mMaxZ1+mMaxZ2+mMaxFade,sqrt(max(0.0,ranges.x)))+50.0;
     if(sqrt(d2)>mRad)return;
    }
   }
  }
 }
 precise float distance2=riverDistance2(current,t,ends),distance=sqrt(distance2),flatness=clamp(ftfWaterFlatness(continentKind()==0?continent.y:t),0.0,1.0),widthVar=worldSimplex(current,16482u,150,2,false),asymmetry=worldSimplex(current,2386u,250,1,false),pinchVar=classifyOnly?0.0:worldSimplex(current,12408u,360,2,false)*2.0;
 precise float priorDistance=riverDistance2(previous,previousT,ends),maskDist2=graph.filterMeta2.x>=4u?distance2:min(distance2,priorDistance),valley=classifyOnly?0.0:riverAlpha(t,maskDist2,ranges.xy,fade.w,fade.z,1.0);if(!classifyOnly&&valley>0.0){valley=pow(valley,curve.x+curve.y*valley);riverCell.riverMask=min(riverCell.riverMask,1.0-valley);}
 float dynamicWidth=1.0+widthVar*.35,sideBias=1.0+asymmetry*.4,pinch=clamp(1.0+pinchVar,.05,1.95),biasedScale=dynamicWidth*sideBias,zone1=sqrt(riverSize(t,widths.xy,fade.w,fade.z)*biasedScale),lake=riverLakeMultiplier(current,t,continentKind()==0?continent.y:t,flatness,ends);
 precise float unit=worldDiv(1.0,graph.worldLevels.w),bankOffset=elevation.z,zone2Width=worldDiv(bankOffset,unit)*biasedScale;zone1*=lake;precise float zone2=zone1+zone2Width;
 if(classifyOnly){
  if(distance>=zone1)return;
  precise float water=continentWaterHeight(continent.y,continent.z)+worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w);
  precise float depthVar=worldSimplex(current,7824u,100,2,false),dynamicDepth=1.0+depthVar*.25,depth=elevation.w*dynamicDepth*clamp(t,0.0,1.0);
  precise float influence=riverAlpha(t,distance2,widths.xy,fade.w,fade.z,lake*lake);influence=influence*influence*(3.0-2.0*influence);
  precise float shallow=(2.3+depthVar*.3)*unit,progressive=depth;if(lake>1.0){precise float multiplier=.35+worldDiv(riverVec(4u).w,50.0),variance=1.0+depthVar*.4;progressive*=1.0+(lake-1.0)*multiplier*variance;}
  float finalized=shallow+progressive*flatness;if(flatness>.4){precise float intensity=worldDiv(flatness-.4,.6),trench=depthVar*.5+.5;finalized+=intensity*trench*3.5*unit*t;}
  finalized=max(finalized,2.0*unit);precise float result=water-finalized*influence;riverTag(cellIndex,water,result,lake>1.0);
  return;
 }
 precise float water=continentWaterHeight(continent.y,continent.z)+worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w);
 if(graph.filterMeta2.x>=4u){
  precise vec4 lakeInfo=riverVec(4u);
  float mMaxZ1=curve.z*1.375*(1.0+max(0.0,lakeInfo.z)*.0145),mMaxZ2=worldDiv(bankOffset,unit)*1.89,mMaxFade=curve.w*.80+max(curve.w*2.6325,max(graph.filter5.y,graph.worldLevels.w)*2.2),maxZone4Radius=max(mMaxZ1+mMaxZ2+mMaxFade,sqrt(max(0.0,ranges.x)))+50.0;
  float heightDiff=max(0.0,height-water),heightDiffBlocks=worldDiv(heightDiff,unit);
  float heightBasedRun=heightDiffBlocks*2.2,erosionScore=clamp((riverCell.erosion+1.0)*.5,0.0,1.0),pinchScore=clamp(worldDiv(pinch-.85,.85),0.0,1.0),leadoutRaw=erosionScore*.45+pinchScore*.35+flatness*.20,leadoutStrength=0.0;
  if(leadoutRaw>.50){float lt=worldDiv(leadoutRaw-.50,.50);leadoutStrength=lt*lt*(3.0-2.0*lt);}
  float leadoutWidth=curve.w*.80*leadoutStrength,leadoutRadius=zone2+leadoutWidth;
  float shrink=clamp(t*fade.z,0.0,1.0),baseBankRun=curve.w*dynamicWidth*pinch*shrink,fadeWidth=max(baseBankRun,heightBasedRun),fadeRadius=min(leadoutRadius+fadeWidth,maxZone4Radius);
  if(distance>=fadeRadius)return;
  precise float valleyFloor=water+bankOffset,result=height;
  if(distance<zone1){
   precise float depthVar=worldSimplex(current,7824u,100,2,false),dynamicDepth=1.0+depthVar*.25,depth=elevation.w*dynamicDepth*clamp(t,0.0,1.0);
   precise float influence=riverAlpha(t,distance2,widths.xy,fade.w,fade.z,lake*lake);influence=influence*influence*(3.0-2.0*influence);
   precise float shallow=(2.3+depthVar*.3)*unit,progressive=depth;if(lake>1.0){precise float multiplier=.35+worldDiv(riverVec(4u).w,50.0),variance=1.0+depthVar*.4;progressive*=1.0+(lake-1.0)*multiplier*variance;}
   float finalized=shallow+progressive*flatness;if(flatness>.4){precise float intensity=worldDiv(flatness-.4,.6),trench=depthVar*.5+.5;finalized+=intensity*trench*3.5*unit*t;}
   finalized=max(finalized,2.0*unit);result=water-finalized*influence;riverTag(cellIndex,water,result,lake>1.0);
  }else if(distance<zone2){
   precise float p=clamp(worldDiv(distance-zone1,max(.001,zone2-zone1)),0.0,1.0);p=p*p*(3.0-2.0*p);result=ftfLerp(water,valleyFloor,p);
  }else if(distance<leadoutRadius){
   result=valleyFloor;
  }else{
   precise float rawP=clamp(worldDiv(distance-leadoutRadius,max(1.0,fadeRadius-leadoutRadius)),0.0,1.0),maxCarve=max(0.0,height-valleyFloor);
   if(maxCarve>0.0){
    float gullyRaw=worldSimplex(current,19752u,65,2,false),rivuletRaw=worldSimplex(current,10864u,20,2,false),isotropicSignal=(1.0-2.0*abs(gullyRaw))*.72+rivuletRaw*.28;
    float segLen=length(ends.zw-ends.xy),alongRiver=clamp(t,0.0,1.0)*segLen,meander=worldSimplex(current*.25,16482u,150,2,false)*20.0;
    vec2 sampleXZ=vec2((alongRiver+meander)*.80,distance*.25);
    float rawPerp=worldSimplex(sampleXZ,19752u,65,2,false),rawPerpRivulet=worldSimplex(sampleXZ*2.0,10864u,20,2,false);
    float perpSignal=(1.0-2.0*abs(rawPerp))*.72+rawPerpRivulet*.28,env=4.0*rawP*(1.0-rawP);
    float p=clamp(rawP+perpSignal*.054*env,0.0,1.0),u=1.0-p,uSmooth=u*u*(3.0-2.0*u),uGusset=pow(u,1.6),fBase=ftfLerp(uSmooth,uGusset,.45);
    float combined=isotropicSignal*.70+perpSignal*.30,maxRel=min(18.0*unit,maxCarve*.20),disp=combined*env*maxRel;
    float carveAmt=clamp(maxCarve*fBase-disp,0.0,maxCarve);result=max(valleyFloor,height-carveAmt);
   }
  }
  if(result<height){height=result;uint zone=(riverCell.terrain>>10)&7u;precise float submergedBelow=water-.01;
   if(distance<zone1&&result<submergedBelow)zone=1u;else if(distance<zone2){if(zone!=1u)zone=2u;}else if(distance<leadoutRadius){if(zone!=1u&&zone!=2u)zone=3u;}else{float progress=worldDiv(distance-leadoutRadius,max(1.0,fadeRadius-leadoutRadius));if(progress<.15){if(zone!=1u&&zone!=2u)zone=3u;}else if(zone!=1u&&zone!=2u&&zone!=3u)zone=4u;}
   riverCell.terrain=(riverCell.terrain&~(7u<<10))|(zone<<10);}
  return;
 }
 precise float unshrunk=curve.w*dynamicWidth*pinch,shrink=clamp(t*fade.z,0.0,1.0),zone3Width=(unshrunk*shrink)*shrink,zone3=zone2+zone3Width;
 precise float discrepancy=1.0+worldDiv(float(int((height-water)*graph.worldLevels.w)),100.0),zone4=zone3+unshrunk*(4.0+discrepancy);if(distance>=zone4)return;
 precise float depthVar=worldSimplex(current,7824u,100,2,false),terrace=worldSimplex(current,11020u,200,1,false),gullyRaw=worldSimplex(current,19752u,65,2,false),rivuletRaw=worldSimplex(current,10864u,20,2,false),gully=1.0-abs(gullyRaw),rivulet=1.0-abs(rivuletRaw);gully*=gully;rivulet=rivulet*rivulet*rivulet;precise float drainage=gully*.7+rivulet*.3,dynamicDepth=1.0+depthVar*.25,depth=elevation.w*dynamicDepth*clamp(t,0.0,1.0),valleyFloor=water+bankOffset+(terrace*.4-drainage*.6)*unit,result=height;
 if(distance<zone1){precise float influence=riverAlpha(t,distance2,widths.xy,fade.w,fade.z,lake*lake);influence=influence*influence*(3.0-2.0*influence);precise float shallow=(2.3+depthVar*.3)*unit,progressive=depth;if(lake>1.0){precise float multiplier=.35+worldDiv(riverVec(4u).w,50.0),variance=1.0+depthVar*.4;progressive*=1.0+(lake-1.0)*multiplier*variance;}float finalized=shallow+progressive*flatness;if(flatness>.4){precise float intensity=worldDiv(flatness-.4,.6),trench=depthVar*.5+.5;finalized+=intensity*trench*3.5*unit*t;}finalized=max(finalized,2.0*unit);result=water-finalized*influence;riverTag(cellIndex,water,result,lake>1.0);}
 else if(distance<zone2){precise float progress=clamp(worldDiv(distance-zone1,zone2-zone1),0.0,1.0);progress=riverTerrace(progress,terrace,drainage,3.0);precise float arc=progress*(1.0-progress)*4.0;progress=max(0.0,progress-drainage*.3*arc);progress=progress*progress*(3.0-2.0*progress);result=ftfLerp(water,valleyFloor,progress);}
 else if(distance<zone3)result=valleyFloor;
 else {precise float progress=clamp(worldDiv(distance-zone3,zone4-zone3),0.0,1.0),modified=riverTerrace(progress,terrace,drainage,5.0),slope=progress*(1.0-progress)*4.0;modified=max(0.0,modified-drainage*.25*slope);modified=modified*modified*(3.0-2.0*modified);result=ftfLerp(valleyFloor,height,modified);}
 if(result<height){height=result;   // getRiverZoneTag, sticky across nodes: 1 riverbed, 2 banks, 3 valley floor, 4 valley fade-out (terrain bits 10-12)
  uint zone=(riverCell.terrain>>10)&7u;precise float submergedBelow=water-.01;
  if(distance<zone1&&result<submergedBelow)zone=1u;else if(distance<zone2){if(zone!=1u)zone=2u;}else if(distance<zone3){if(zone!=1u&&zone!=2u)zone=3u;}else if(zone!=1u&&zone!=2u&&zone!=3u)zone=4u;
  riverCell.terrain=(riverCell.terrain&~(7u<<10))|(zone<<10);}
}
void carveWetland(uint wetIndex,uint cellIndex,vec2 warped,vec2 current,vec4 continent,inout float height){uint offset=rivers.words[15]+wetIndex*12u;precise vec4 ends=riverVec(offset),shape=riverVec(offset+4u);uint seed=rivers.words[offset+8u];precise float factor=graph.worldLevels.w,unit=worldDiv(min(graph.worldLevels.z,factor)+1.0,factor)-worldDiv(min(graph.worldLevels.z,factor),factor),waterLine=worldDiv(min(graph.worldLevels.z-1.0,factor),factor),uplift=continentWaterHeight(continent.y,continent.z),localWater=waterLine+uplift,bed=localWater-3.5*unit;if(height<bed)return;
 {// Exact early reject: the Perlin warp below moves the point by (w,w) with w in [0,8] (at most 8*sqrt(2) blocks) and the
  // distance to a segment changes by no more than the move, so a point this far away is rejected after the warp anyway.
  float tw=riverT(warped,ends),dw=sqrt(riverDistance2(warped,tw,ends));if(dw>sqrt(max(shape.y,0.0))+11.32+.5)return;}
 precise float warp=ftfPerlinNode(current,seed+3u,25,2,2.0)*8.0;precise vec2 p=warped+vec2(warp);precise float t=riverT(p,ends),distance=riverDistance2(p,t,ends);if(distance>shape.y)return;precise float dist=1.0-worldDiv(distance,shape.y),banks=height,total=ftfMap(dist,0.0,.7),rivulet=abs(ftfPerlinNode(current*.4,seed+3u,25,2,2.0)),slope=sin(total*3.1415927);if(slope>0.0)total+=rivulet*.25*slope;total=clamp(total,0.0,1.0);total=total*total*total*(total*(total*6.0-15.0)+10.0);precise float alpha=clamp(ftfMap(dist,.4,.7),0.0,1.0);alpha=alpha*alpha*alpha*(alpha*(alpha*6.0-15.0)+10.0);precise float target=ftfLerp(banks,bed,alpha);if(height>target)height=ftfLerp(banks,target,alpha);
 if(graph.filterMeta2.x>=4u){if(alpha>0.0)riverCell.waterHeight=max(riverCell.waterHeight,localWater);if(dist>=.7&&height<=localWater)riverCell.terrain=(riverCell.terrain&(7u<<10))|11u|256u;}
 else{if(alpha>0.0)riverCell.waterHeight=max(riverCell.waterHeight,uplift);if(dist>=.7)riverCell.terrain=(riverCell.terrain&(7u<<10))|11u|256u;}
 precise float moundMin=localWater+unit,moundMax=localWater+2.0*unit;if(height>=bed&&height<moundMax){precise float moundShape=ftfMap(clamp(ftfPerlinNode(current,seed+1u,10,1,2.0),.3,.6),.3,.6)*total,moundHeight=ftfMap(clamp(worldSimplex(current,seed+2u,20,1,false),0.0,.3),0.0,.3),mounds=moundMin+moundHeight*(moundMax-moundMin);height=ftfLerp(height,mounds,moundShape*.8);if(graph.filterMeta2.x>=4u&&height>localWater)riverCell.terrain&=~256u;}
 riverCell.riverMask=min(riverCell.riverMask,1.0-total);
}
void riverCarveImpl(vec2 world,uint cellIndex,inout float height,bool classifyOnly);
void ftfRivers(vec2 world,uint cellIndex,inout float height){riverCell=cells[cellIndex];riverCarveImpl(world,cellIndex,height,false);cells[cellIndex]=riverCell;}
// Terrain bits and water level a block at world would get from the river pass, starting from plain land at the given height.
uint riverClassifyWater(vec2 world,float height,out float waterHeight){riverCell=CellState(1.0,0.0,0u,0.0,0.0,0.0,0.0,0u);riverCarveImpl(world,0u,height,true);waterHeight=riverCell.waterHeight;return riverCell.terrain;}
uint riverClassify(vec2 world,float height){float unused=0.0;return riverClassifyWater(world,height,unused);}
void riverCarveImpl(vec2 world,uint cellIndex,inout float height,bool classifyOnly){ivec2 center;precise vec4 continent=ftfUpliftRivers(world,center);uvec4 map=uvec4(0);bool found=false;
 for(uint i=0u;i<rivers.words[0];++i){uvec4 candidate=riverUInt(rivers.words[12]+i*4u);if(all(equal(ivec2(candidate.xy),center))){map=candidate;found=true;break;}}if(!found||map.w==0u)return;
 uint seed=(continentKind()==1||continentKind()==2)?map.x:map.y;precise vec2 position=world+(riverDomain(world,seed+3u,95,1,25.0)+riverDomain(world,seed+4u,16,1,5.0)),lakeOffset=riverDomain(position,seed+1u,200,1,300.0)+riverDomain(position,seed+2u,50,2,50.0);precise vec2 stack[4];
 // Upper bound of carveRiver's zone-4 factor (shrink^2 + 4 + discrepancy) for this pixel; carving only lowers the height.
 // When filterMeta2.x >= 4u (mountain fades) or classifyOnly, skip box already encloses the maximum carve/channel reach.
 uint skipBase=rivers.words[15]+rivers.words[3]*12u;float waterHere=continentWaterHeight(continent.y,continent.z)+worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w),above=(height-waterHere)*graph.worldLevels.w,coefficient=(graph.filterMeta2.x>=4u||classifyOnly)?0.0:max(0.0,6.05+above*(above>0.0?.0101:.0099));
 for(uint root=map.z;root<map.z+map.w;++root){uint start=rivers.words[13]+root*8u;precise vec4 bounds=riverVec(start);if(position.x<bounds.x||position.y<bounds.y||position.x>bounds.z||position.y>bounds.w)continue;uvec4 range=riverUInt(start+4u);
  for(uint node=range.x;node<range.x+range.y;++node){uint skip=skipBase+node*8u;precise vec4 reach=riverVec(skip);float widen=coefficient==0.0?0.0:uintBitsToFloat(rivers.words[skip+5u])*coefficient;
   if(position.x<reach.x-widen||position.y<reach.y-widen||position.x>reach.z+widen||position.y>reach.w+widen){node=rivers.words[skip+4u]-1u;continue;} // whole subtree out of reach (exact, see rivers.cpp)
   uint offset=rivers.words[14]+node*40u;precise vec4 ends=riverVec(offset),vector=riverVec(offset+4u),fade=riverVec(offset+8u),warp=riverVec(offset+24u);uvec4 meta=riverUInt(offset+28u),counts=riverUInt(offset+32u);precise vec2 previous=counts.y==0u?position:stack[counts.y-1u];precise float previousT=riverT(previous,ends),t=previousT;precise vec2 current=previous;if(t>=0.0&&t<=1.0){current+=riverOffset(previous,t,warp,meta.x,fade.xy,vector.z);t=riverT(current,ends);}stack[counts.y]=current;carveRiver(node,cellIndex,previous,previousT,current,t,continent,height,classifyOnly);
   for(uint wet=0u;wet<counts.x;++wet)carveWetland(meta.w+wet,cellIndex,current+lakeOffset,current,continent,height);
  }
 }
}
