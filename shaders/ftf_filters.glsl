// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not
// distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// Derived work of FreeTerraForged's Erosion.java (MPL-2.0): its erosion filter re-implemented in GLSL for the GPU, on a
// preview height grid, with each FTF build's constants. Erosion.java is based on Rune Skovbo Johansen's (runevision)
// Phacelle Noise / Fast & Gorgeous Erosion Filter: https://gist.github.com/runevision/c867c458f06284ad4c0f8aa3b974bc59
// This whole file is under the MPL-2.0; the rest of the program is not. Details: LICENSES/Erosion-MPL-2.0.txt.
#include "ftf_sin.glsl"
// Tile-filter math recovered from CliffRelaxation/Erosion. Neighbors are sampled
// in world-block distances, interpolating the selected preview grid at coarse LOD.
float filterHash01(ivec2 c,uint seed){int n=int(uint(c.x)*374761393u+uint(c.y)*668265263u+seed*144677219u);n=int(uint(n^(n>>13))*1274126177u);n^=n>>16;return float(n&2147483647)/2147483648.0;}
float filterValue(vec2 p,uint seed){ivec2 c=ivec2(floor(p));precise vec2 t=p-vec2(c);t=t*t*(3.0-2.0*t);precise float a=ftfLerp(filterHash01(c,seed),filterHash01(c+ivec2(1,0),seed),t.x),b=ftfLerp(filterHash01(c+ivec2(0,1),seed),filterHash01(c+1,seed),t.x);return ftfLerp(a,b,t.y);}
float filterCentered(vec2 world,uint seed){precise float broad=filterValue(world*(1.0/28.0),seed),detail=filterValue(world*(1.0/11.0),seed+101u);return (worldDiv(broad+.35*detail,1.35)-.5)*2.0;}
// Version profile: 0 = FTF 1.0.0010 (tile filters), 1 = 1.0.0013/1.0.0014 (chunk filters, retuned erosion), 2 = stable 1.0.0 (no Phacelle erosion).
float filterSin(float angle){return graph.filterMeta2.y!=0u?uintBitsToFloat(FTF_SIN_BITS[uint(int(angle*651.8986))&4095u]):sin(angle);}
float filterCos(float angle){return graph.filterMeta2.y!=0u?filterSin(angle+1.5708):cos(angle);}
float filterEase(float t){float v=1.0-clamp(t,0.0,1.0);return 1.0-v*v;}
float filterSmoothStart(float t,float smoothing){return t>=smoothing?t-.5*smoothing:worldDiv(.5*t*t,max(smoothing,.00001));}
float filterMinmod(float back,float forward){return back*forward<=0.0?0.0:abs(back)<abs(forward)?back:forward;}
vec4 filterPhacelle(vec2 point,vec2 direction,uint seed){
    float frequency=graph.filter1.z;vec2 side=vec2(-direction.y,direction.x)*(frequency*6.2831855);float offset=graph.filter1.w*6.2831855;
    ivec2 cell=ivec2(floor(point));vec2 fraction=point-vec2(cell),phase=vec2(0);float weights=0.0;
    for(int i=-1;i<=2;++i)for(int j=-1;j<=2;++j){ivec2 c=cell+ivec2(i,j);vec2 hash=vec2(filterHash01(c,seed)-.5,filterHash01(c,seed+1013u)-.5)*.5;vec2 delta=fraction-vec2(i,j)-hash;float square=delta.x*delta.x+delta.y*delta.y,weight=max(0.0,exp(-square*2.0)-.01111);weights+=weight;float wave=delta.x*side.x+delta.y*side.y+offset;phase+=vec2(filterCos(wave),filterSin(wave))*weight;}
    phase*=worldDiv(1.0,max(weights,.00001));float magnitude=max(1.0-graph.filter2.x,length(phase));return vec4(phase/magnitude,side/(frequency*6.2831855+.00001));
}
float filterErosionDelta(vec2 world,float base,vec2 slope,float smoothSlope,float envelope,float modifier,inout float sediment){
    float scale=graph.filter0.x*graph.filter4.x,wh=graph.filter3.z,strength=.2*graph.filter0.y,gully=graph.filter0.z;
    float slopeLen=sqrt(slope.x*slope.x+slope.y*slope.y+.0000000001),heightFade=clamp((base-.5)*2.0,-1.0,1.0),ridgeMask=min(.98,filterEase(slopeLen*.01*graph.filter2.z));
    float rawT=clamp(worldDiv(slopeLen-graph.filter4.y,graph.filter4.z),0.0,1.0);
    if(rawT<=0.0){sediment=clamp(sediment+heightFade*(1.0-ridgeMask),-1.0,1.0);return 0.0;}
    float ampT=clamp(worldDiv(smoothSlope-graph.filter4.y,graph.filter4.z),0.0,1.0),gate=ampT*ampT*(3.0-2.0*ampT);if(modifier==0.0)return 0.0;
    float currentStrength=strength*scale,frequency=worldDiv(1.0,scale*graph.filter1.z),rounding=ftfLerp(graph.filter3.x,graph.filter3.y,clamp(heightFade+.5,0.0,1.0));
    float combined=filterEase(filterSmoothStart(slopeLen*graph.filter2.y,rounding*graph.filter2.y)),ridgeTarget=heightFade,delta=0.0,roundingMult=1.0,fadeTarget=0.0;vec2 gullySlope=slope;
    for(int octave=0;octave<int(graph.filter0.w);++octave){float len=length(gullySlope);vec2 direction=len>.0000000001?gullySlope/len:vec2(0);vec4 phase=filterPhacelle(world*frequency,direction,graph.filterMeta.y+uint(octave));float sloping=abs(phase.y),gradient=currentStrength*6.2831855*graph.filter1.z*frequency;
        gullySlope+=sign(phase.y)*phase.zw*gradient*gully;float faded=ftfLerp(fadeTarget,phase.x*gully,combined);delta+=faded*currentStrength;fadeTarget=faded;
        float roundOct=ftfLerp(graph.filter3.x,graph.filter3.y,clamp(phase.x+.5,0.0,1.0))*roundingMult,newMask=filterEase(filterSmoothStart(sloping*graph.filter2.y,roundOct*graph.filter2.y));combined=(1.0-pow(1.0-clamp(combined,0.0,1.0),3.0))*newMask;
        ridgeTarget=ftfLerp(ridgeTarget,phase.x,ridgeMask);ridgeMask=min(.98,ridgeMask*filterEase(sloping*graph.filter2.w));currentStrength*=graph.filter1.y;frequency*=graph.filter1.x;roundingMult*=.001;
    }
    if(isnan(delta)||isinf(delta))return 0.0;sediment=clamp(sediment+ridgeTarget*(1.0-ridgeMask),-1.0,1.0);if(gate<=0.0)return 0.0;
    float sedimentModifier=max(.2,1.0-sediment),maxDepth=graph.filter4.w*min(smoothSlope,3.0)*scale,limited=clamp(delta*gate*sedimentModifier,-maxDepth,maxDepth);
    if(limited>0.0){float room=(envelope-base)*wh+graph.filter5.x;limited=worldDiv(limited*room,limited+room);}return modifier*worldDiv(limited,wh);
}
// FTF TerrainCeiling.compress on a normalised height (shared by the final filter and, from 1.0.0013, the terrain stage before rivers).
float ftfCeilingNorm(float h,float factor,float wh){
    int available=max(1,int(wh)-int(graph.worldLevels.z)),head=min(16,max(1,available/8)),band=min(112,max(1,available/2)),tail=min(8,max(1,band/8));
    float maximum=worldDiv(wh-float(head),factor),start=worldDiv(wh-float(head+band),factor),linear=start+1.0,tailStart=worldDiv(wh-float(head+tail),factor);
    if(h>start){float sourceRange=linear-start,targetRange=tailStart-start;if(sourceRange<=0.0||targetRange<=0.0)h=min(h,maximum);else{float slope=worldDiv(targetRange,sourceRange);if(h<=linear)h=start+(h-start)*slope;else{float room=maximum-tailStart,excess=h-linear,softness=worldDiv(room,slope);h=tailStart+worldDiv(room*excess,softness+excess);}}}
    return h;
}
