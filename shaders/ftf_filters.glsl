#include "ftf_sin.glsl"
// Tile-filter math recovered from FTF (MIT): CliffRelaxation noise, sine table, TerrainCeiling; the erosion filter
// (MPL-2.0) is in ftf_erosion.glsl. Neighbors are sampled
// in world-block distances, interpolating the selected preview grid at coarse LOD.
float filterHash01(ivec2 c,uint seed){int n=int(uint(c.x)*374761393u+uint(c.y)*668265263u+seed*144677219u);n=int(uint(n^(n>>13))*1274126177u);n^=n>>16;return float(n&2147483647)/2147483648.0;}
float filterValue(vec2 p,uint seed){ivec2 c=ivec2(floor(p));precise vec2 t=p-vec2(c);t=t*t*(3.0-2.0*t);precise float a=ftfLerp(filterHash01(c,seed),filterHash01(c+ivec2(1,0),seed),t.x),b=ftfLerp(filterHash01(c+ivec2(0,1),seed),filterHash01(c+1,seed),t.x);return ftfLerp(a,b,t.y);}
float filterCentered(vec2 world,uint seed){precise float broad=filterValue(world*(1.0/28.0),seed),detail=filterValue(world*(1.0/11.0),seed+101u);return (worldDiv(broad+.35*detail,1.35)-.5)*2.0;}
// Version profile: 0 = FTF 1.0.0010 (tile filters), 1 = 1.0.0013/1.0.0014 (chunk filters, retuned erosion), 2 = stable 1.0.0 (no Phacelle erosion).
float filterSin(float angle){return graph.filterMeta2.y!=0u?uintBitsToFloat(FTF_SIN_BITS[uint(int(angle*651.8986))&4095u]):sin(angle);}
float filterCos(float angle){return graph.filterMeta2.y!=0u?filterSin(angle+1.5708):cos(angle);}
#include "ftf_erosion.glsl"   // MPL-2.0 erosion filter, kept in its own file (the rest of this file is MIT, from FTF)
// FTF TerrainCeiling.compress on a normalised height (shared by the final filter and, from 1.0.0013, the terrain stage before rivers).
float ftfCeilingNorm(float h,float factor,float wh){
    int available=max(1,int(wh)-int(graph.worldLevels.z)),head=min(16,max(1,available/8)),band=min(112,max(1,available/2)),tail=min(8,max(1,band/8));
    float maximum=worldDiv(wh-float(head),factor),start=worldDiv(wh-float(head+band),factor),linear=start+1.0,tailStart=worldDiv(wh-float(head+tail),factor);
    if(h>start){float sourceRange=linear-start,targetRange=tailStart-start;if(sourceRange<=0.0||targetRange<=0.0)h=min(h,maximum);else{float slope=worldDiv(targetRange,sourceRange);if(h<=linear)h=start+(h-start)*slope;else{float room=maximum-tailStart,excess=h-linear,softness=worldDiv(room,slope);h=tailStart+worldDiv(room*excess,softness+excess);}}}
    return h;
}
