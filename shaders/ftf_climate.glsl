// FTF ClimateModule + Climate biome-edge displacement. Values exposed as 0..1.
#include "ftf_sin.glsl"
float climateIntPower(float value,int power){precise float result=1.0;for(int i=0;i<power;++i)result*=value;return result;}
float climateSignedPower(float value,int power){precise float result=climateIntPower(value,power);if(value<0.0&&result>0.0)result=-result;else if(value>0.0&&result<0.0)result=-result;return result;}
float climateRange(float value,vec4 bounds,vec4 effects){if(effects.y!=0.0)value=pow(value,pow(2.0,-effects.y));return clamp(ftfLerp(bounds.z,bounds.w,value)+effects.x,bounds.z,bounds.w);}
float climateEdgeNoise(vec2 p,uint seed){
    int scale=int(graph.climateEdge.x),oct=int(graph.climateEdge.y);uint type=graph.climateMeta.y;
    if(type==10u)return ftfMap(uintBitsToFloat(FTF_SIN_BITS[int(p.x*651.8986)&4095]),-1.0,1.0)-.5;
    if(type==11u){ivec2 cell=ivec2(p.x>=0.0?p.x+.5:p.x-.5,p.y>=0.0?p.y+.5:p.y-.5);return abs(ftfVal(seed,cell))-.5;}
    if(type==7u||type==8u){vec2 q=p*worldDiv(1.0,float(scale));ivec2 base=ivec2(worldFloor(q.x),worldFloor(q.y)),winner=base;precise float first=3.402823466e38,second=3.402823466e38;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){ivec2 cell=base+ivec2(x,y);precise vec2 point=vec2(cell)+FTF_CELLS[ftfHash(seed,cell)&255u];precise vec2 d=point-q;precise float dist=d.x*d.x+d.y*d.y;if(dist<first){second=first;first=dist;winner=cell;}else if(dist<second)second=dist;}
        return (type==7u?ftfMap(ftfVal(seed,winner),-1.0,1.0):ftfMap(second-1.0,-1.0,1.0))-.5;
    }
    if(type==9u){p*=worldDiv(1.0,float(scale));precise float sum=ftfCubicSample(p,seed),amp=1.0,bound=.75;for(int i=1;i<oct;++i){p*=graph.climateEdge.z;amp*=graph.climateEdge.w;sum+=ftfCubicSample(p,seed+uint(i))*amp;bound+=.75*amp;}return ftfMap(sum,-bound,bound)-.5;}
    if(type==6u)return 1.0-ftfRidgeNode(p,seed,scale,oct,graph.climateEdge.z,graph.climateEdge.w)-.5;
    if(type==5u)return .5;
    if(type==4u)return ftfRidgeNode(p,seed,scale,oct,graph.climateEdge.z,graph.climateEdge.w)-.5;
    if(type==3u)return worldPerlin(p,0u,scale,oct,graph.climateEdge.z,graph.climateEdge.w)-.5;
    p*=worldDiv(1.0,float(scale));precise float sum=0.0,amp=type==2u?graph.climateEdge.w:1.0,bound=0.0;
    float signal=type==2u?(oct==1?.9:oct==2?.83:oct==3?.75:oct==4?.64:oct==5?.62:.61):(oct==1?.989:oct==2?.81:oct==3?.781:oct==4?.708:oct==5?.702:.696);
    for(int i=0;i<oct;++i){float signalValue=type==2u?ftfPerlin8(p,uint(i)):ftfSimplexScaled(p,seed+uint(i),type==1u?99.83685:79.869484);sum+=signalValue*amp;bound+=signal*amp;p*=graph.climateEdge.z;amp*=graph.climateEdge.w;}
    return ftfMap(sum,-bound,bound)-.5;
}
vec3 climateRegion(vec2 world,out vec2 position,out vec2 regionCenter){
    precise vec2 warp=vec2(worldSimplex(world,graph.climateSeeds0.y,int(graph.climateShape.y),2,false)-.5,worldSimplex(world,graph.climateSeeds0.z,int(graph.climateShape.y),2,false)-.5)*graph.climateShape.z;
    position=(world+warp)*worldDiv(1.0,graph.climateShape.x);
    ivec2 base=ivec2(worldFloor(position.x),worldFloor(position.y)),winner=base;vec2 center=position;precise float first=999999.0,second=999999.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){ivec2 cell=base+ivec2(x,y);precise vec2 point=vec2(cell)+FTF_CELLS[ftfHash(graph.climateSeeds0.x,cell)&255u];precise vec2 delta=point-position;precise float dist=delta.x*delta.x+delta.y*delta.y;if(dist<first){second=first;first=dist;winner=cell;center=point;}else if(dist<second)second=dist;}
    regionCenter=center;
    precise float edge=1.0-ftfMap(worldDiv(first,second)-1.0,-1.0,0.0);
    // Macro region variation is used only by the preview palette.
    vec2 macroPoint=center*worldDiv(1.0,graph.climateShape.w);ivec2 mbase=ivec2(worldFloor(macroPoint.x),worldFloor(macroPoint.y)),mwinner=mbase;float nearest=999999.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){ivec2 cell=mbase+ivec2(x,y);vec2 point=vec2(cell)+FTF_CELLS[ftfHash(graph.climateSeeds2.z,cell)&255u];vec2 delta=point-macroPoint;float dist=dot(delta,delta);if(dist<nearest){nearest=dist;mwinner=cell;}}
    return vec3(edge,ftfMap(ftfVal(graph.climateSeeds2.z,mwinner),-1.0,1.0),ftfMap(ftfVal(graph.climateSeeds0.x,winner),-1.0,1.0));
}
vec2 ftfClimate(vec2 world,float height,uint cellIndex,out vec2 regions){
    vec2 position,center;vec3 region=climateRegion(world,position,center);float edge=region.x;
    uint type=cells[cellIndex].terrain&255u;
    if(type<=2u||type==6u||type==8u||type==11u){vec2 worldCenter=vec2(worldFloor(worldDiv(center.x,worldDiv(1.0,graph.climateShape.x))),worldFloor(worldDiv(center.y,worldDiv(1.0,graph.climateShape.x))));float coastMarker=graph.controlPoints.z+(graph.controlPoints.w-graph.controlPoints.z)/2.0;
        if(ftfUplift(worldCenter).x<=coastMarker){type=6u;cells[cellIndex].terrain=(cells[cellIndex].terrain&~255u)|type;}}
    if(height<=worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w)&&type==6u){type=5u;cells[cellIndex].terrain=(cells[cellIndex].terrain&~255u)|type;}

    if(height>worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w)&&(edge<.4||type==2u)){
        precise float modifier=1.0-ftfMap(edge,0.0,.4),distance=graph.climateEffects.w*modifier;
        precise vec2 offset=vec2(climateEdgeNoise(world,graph.climateSeeds0.w),climateEdgeNoise(world,graph.climateSeeds1.x))*distance;
        region=climateRegion(world+offset,position,center);
    }
    regions=region.yz;
    precise float biomeFrequency=worldDiv(1.0,graph.climateShape.x);
    int moistScale=int(floor(double((graph.moisture0.x*2.5*graph.climateShape.x)*biomeFrequency)+.5));
    int tempScale=int(floor(double((graph.temperature0.x*graph.climateShape.x)*biomeFrequency)+.5));
    precise vec2 q=ftfWarpPerlin(position,graph.climateSeeds1.w,max(1,moistScale/6),2,float(moistScale)/12.0);
    q=ftfWarpPerlin(q,graph.climateSeeds1.z,max(1,moistScale/2),1,float(moistScale)/4.0);
    precise float moist=ftfMap(clamp(worldSimplex(q*vec2(.5,1),graph.climateSeeds1.y,moistScale,1,false),.125,.875),.125,.875);
    if(graph.moisture0.y>=2.0)moist=(climateSignedPower((moist-.5)*2.0,int(graph.moisture0.y))+1.0)/2.0;
    moist=climateRange(moist,graph.moisture0,graph.moisture1);
    q=ftfWarpPerlin(position,graph.climateSeeds2.y,tempScale,1,float(tempScale));q=ftfWarpPerlin(q,graph.climateSeeds2.x,tempScale*4,2,float(tempScale*4));
    precise float angle=q.y*worldDiv(1.0,float(tempScale));int index=int(angle*651.8986)&4095;precise float sine=clamp(uintBitsToFloat(FTF_SIN_BITS[index]),-1.0,1.0);
    precise float temp=(climateSignedPower(sine,int(graph.temperature0.y))+1.0)/2.0;temp=climateRange(temp,graph.temperature0,graph.temperature1);
    precise float ground=worldDiv(min(graph.worldLevels.z,graph.worldLevels.w),graph.worldLevels.w),heightNorm=height<=ground?0.0:clamp(worldDiv(float(int(height*graph.worldLevels.w))-graph.worldLevels.z,float(graph.climateMeta.z)-graph.worldLevels.z),0.0,1.0);
    moist=clamp(moist,0.0,1.0);float influence=1.0-heightNorm;if(influence>0.0)moist+=(1.0-moist)*(graph.climateEffects.z*influence);
    if(height<=ground)moist=clamp(moist,0.0,1.0);else {moist=pow(moist,pow(2.0,heightNorm*graph.climateEffects.y));temp=pow(clamp(temp,0.0,1.0),pow(2.0,heightNorm*graph.climateEffects.x));}
    return vec2(temp,moist);
}

