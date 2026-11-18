// UpliftContinentGenerator + PlateUpliftBridge + pre-river Heightmap ocean/land interpolation.
#extension GL_EXT_control_flow_attributes : enable
#include "ftf_water_steps.glsl"
layout(std430,set=0,binding=19) readonly buffer VoronoiData {uint words[];} voronoi;
bool voronoiCellBase(ivec2 c,out uint cb){
    uint gw=voronoi.words[0],gh=voronoi.words[1];
    int ix=c.x-int(voronoi.words[2]),iz=c.y-int(voronoi.words[3]);
    if(gw==0u||ix<0||iz<0||uint(ix)>=gw||uint(iz)>=gh){cb=0u;return false;}
    cb=8u+(uint(iz)*gw+uint(ix))*voronoi.words[4];
    return true;
}
float worldDiv(float a,float b){return float(double(a)/double(b));}
int worldFloor(float x){return int(x)-(x<0.0?1:0);}
float worldPerlinSample(vec2 p,uint seed){
    ivec2 c=ivec2(worldFloor(p.x),worldFloor(p.y));precise vec2 d=p-vec2(c),t=d*d*(3.0-2.0*d);
    precise float a=ftfLerp(ftfGrad(seed,c,d),ftfGrad(seed,c+ivec2(1,0),d-vec2(1,0)),t.x);
    precise float b=ftfLerp(ftfGrad(seed,c+ivec2(0,1),d-vec2(0,1)),ftfGrad(seed,c+1,d-1.0),t.x);
    return ftfLerp(a,b,t.y);
}
float worldPerlin(vec2 p,uint seed,int scale,int octaves,float lacunarity,float gain){
    precise float frequency=worldDiv(1.0,float(scale));p*=frequency;
    precise float sum=0.0,amp=gain,bound=0.0;
    float signal=octaves==1?.9:octaves==2?.83:octaves==3?.75:octaves==4?.64:octaves==5?.62:.61;
    for(int i=0;i<octaves;++i){sum+=worldPerlinSample(p,seed+uint(i))*amp;bound+=signal*amp;p*=lacunarity;amp*=gain;}
    return ftfMap(sum,-bound,bound);
}
float worldSimplex2(vec2 p,uint seed,int scale,int octaves,float lacunarity,float gain,bool second){
    precise float frequency=worldDiv(1.0,float(scale));p*=frequency;
    precise float sum=0.0,amp=1.0,bound=0.0;
    float signal=octaves==1?.989:octaves==2?.81:octaves==3?.781:octaves==4?.708:octaves==5?.702:.696;
    for(int i=0;i<octaves;++i){sum+=ftfSimplexScaled(p,seed+uint(i),second?99.83685:79.869484)*amp;bound+=amp*signal;p*=lacunarity;amp*=gain;}
    return ftfMap(sum,-bound,bound);
}
float worldSimplex(vec2 p,uint seed,int scale,int octaves,bool second){
    return worldSimplex2(p,seed,scale,octaves,2.0,.5,second);
}
float continentCellValue(uint seed,ivec2 cell){precise float v=.5+ftfVal(seed,cell)*.5;return v;}
vec2 continentPoint(ivec2 cell){precise vec2 p=vec2(cell)+FTF_CELLS[ftfHash(graph.worldSeeds0.x,cell)&255u]*graph.continent.y;return p;}
float continentSize(ivec2 cell){
    if(graph.continent.w>0.0&&any(notEqual(cell,ivec2(0))))return ftfMap(continentCellValue(graph.worldSeeds0.z,cell),0.0,graph.continent.w);
    return 1.0;
}
vec2 continentClosest(vec2 p,out ivec2 winner){
    ivec2 base=ivec2(worldFloor(p.x),worldFloor(p.y));winner=base;vec2 point=p;precise float nearest=3.402823466e38;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){ivec2 cell=base+ivec2(x,y);vec2 candidate=continentPoint(cell);precise vec2 d=p-candidate;precise float distance=d.x*d.x+d.y*d.y;
        if(distance<nearest){nearest=distance;winner=cell;point=candidate;}}
    return point;
}
float continentEdgeRoll(uint baseSeed,ivec2 a,ivec2 b){
    if(a.x>b.x||(a.x==b.x&&a.y>b.y)){ivec2 t=a;a=b;b=t;}
    uint s=baseSeed^(uint(b.x)*73856093u)^(uint(b.y)*19349663u);
    return continentCellValue(s,a);
}
bool continentShouldSkip(ivec2 cell){
    if(graph.continent.z<=0.0||all(equal(cell,ivec2(0))))return false;
    float t=graph.continent.z;
    if(graph.filterMeta2.x<2u||((int(graph.continentNoise.w)&3)!=0)||graph.upliftExtra.z<=0.0)
        return continentCellValue(graph.worldSeeds0.y,cell)<t;
    uint cb;
    if(voronoiCellBase(cell,cb))return (voronoi.words[cb+3u]&1u)!=0u;
    return continentCellValue(graph.worldSeeds0.y,cell)<t;
}
vec2 continentCentroidAndBridge(ivec2 cell,out bool hasAnyBridge){
    uint cb;
    if(!voronoiCellBase(cell,cb)){hasAnyBridge=false;return continentPoint(cell);}
    hasAnyBridge=(voronoi.words[cb+3u]&2u)!=0u;
    return vec2(uintBitsToFloat(voronoi.words[cb+0u]),uintBitsToFloat(voronoi.words[cb+1u]));
}
float continentDistToSegSq(vec2 p,vec2 a,vec2 b){
    vec2 d=b-a;float l2=dot(d,d),t=l2<1.0e-12?0.0:clamp(worldDiv(dot(p-a,d),l2),0.0,1.0);
    vec2 off=p-(a+t*d);return dot(off,off);
}
float continentCellClosedDistSq(vec2 p,ivec2 cell,ivec2 refCell,bool deduplicate){
    uint cb;if(!voronoiCellBase(cell,cb))return 3.402823466e38;
    uint ec=min(voronoi.words[cb+2u],24u);
    float bestSq=3.402823466e38;
    [[dont_unroll]] for(uint k=0u;k<ec;++k){
        uint eb=cb+4u+k*12u,ef=voronoi.words[eb+11u];
        if((ef&1u)!=0u)continue;
        if(deduplicate){
            bool ncSkip=(ef&4u)!=0u;
            ivec2 nc=ivec2(int(voronoi.words[eb+9u]),int(voronoi.words[eb+10u]));
            if(!ncSkip&&(nc.x<cell.x||(nc.x==cell.x&&nc.y<cell.y))&&abs(nc.x-refCell.x)<=2&&abs(nc.y-refCell.y)<=2)continue;
        }
        vec2 m=vec2(uintBitsToFloat(voronoi.words[eb+0u]),uintBitsToFloat(voronoi.words[eb+1u]));
        vec2 t=vec2(uintBitsToFloat(voronoi.words[eb+2u]),uintBitsToFloat(voronoi.words[eb+3u]));
        float lo=uintBitsToFloat(voronoi.words[eb+4u]),hi=uintBitsToFloat(voronoi.words[eb+5u]);
        bestSq=min(bestSq,continentDistToSegSq(p,m+lo*t,m+hi*t));
    }
    return bestSq;
}
float continentBoundaryDistSq(vec2 p,ivec2 cell,bool hasAnyBridge){
    if(!hasAnyBridge){
        float d=continentCellClosedDistSq(p,cell,cell,false);
        return d>1.0e37?4.0:d;
    }
    float bestSq=3.402823466e38;
    [[dont_unroll]] for(int idx=0;idx<25;++idx){
        ivec2 c=cell+ivec2((idx%5)-2,(idx/5)-2);if(continentShouldSkip(c))continue;
        bestSq=min(bestSq,continentCellClosedDistSq(p,c,cell,true));
    }
    return bestSq>1.0e37?4.0:bestSq;
}
float continentSmoothGradient(vec2 raw,float frequency){
    vec2 p=raw*frequency;ivec2 cell;continentClosest(p,cell);
    uint cb;if(!voronoiCellBase(cell,cb))return 1.0;
    vec2 centroidPt=vec2(uintBitsToFloat(voronoi.words[cb+0u]),uintBitsToFloat(voronoi.words[cb+1u]));
    uint ec=min(voronoi.words[cb+2u],24u);
    float minGrad=3.402823466e38,wLen=0.0,wTMid=0.0,wHalfLen=0.0,wMTan=0.0;vec2 wTan=vec2(0);bool wActive=false;
    [[dont_unroll]] for(uint k=0u;k<ec;++k){
        uint eb=cb+4u+k*12u;
        vec2 m=vec2(uintBitsToFloat(voronoi.words[eb+0u]),uintBitsToFloat(voronoi.words[eb+1u]));
        vec2 t=vec2(uintBitsToFloat(voronoi.words[eb+2u]),uintBitsToFloat(voronoi.words[eb+3u]));
        float len=uintBitsToFloat(voronoi.words[eb+6u]),hc=uintBitsToFloat(voronoi.words[eb+7u]);
        vec2 diff=vec2(len*t.y,-len*t.x);
        float hx=dot(m-p,diff),pv=worldDiv(hx,hc);
        if(pv<minGrad){
            float lo=uintBitsToFloat(voronoi.words[eb+4u]),hi=uintBitsToFloat(voronoi.words[eb+5u]);
            minGrad=pv;wLen=len;wTMid=(lo+hi)*.5;wHalfLen=(hi-lo)*.5;wTan=t;
            wMTan=uintBitsToFloat(voronoi.words[eb+8u]);
            wActive=(voronoi.words[eb+11u]&2u)!=0u;
        }
    }
    if(minGrad>1.0e37)return 1.0;
    float clampedMin=clamp(minGrad,0.0,1.0);
    if(!wActive)return clampedMin;
    float r=1.0-clampedMin;if(r<=.01)return clampedMin;
    float dTan=dot(p-centroidPt,wTan),tEdge=worldDiv(dTan,r)-wMTan,halfLen=wHalfLen;
    if(halfLen<=1.0e-6)return clampedMin;
    float uEdge=worldDiv(tEdge-wTMid,halfLen);if(uEdge<=-1.0||uEdge>=1.0)return clampedMin;
    float rounding=graph.upliftExtra2.x,fraction;
    if(rounding<=0.0||abs(wTMid)<1.0e-4){fraction=abs(uEdge);}else{
        float halfDel=.5*wLen,h2=halfDel*halfDel,tMid=wTMid,cy=.5*(tMid-worldDiv(h2,tMid)),M=abs(cy);
        float radicand=M*M+r*(2.0-r)*h2,denom=sqrt(radicand)+M,sgn=tMid>=0.0?1.0:-1.0;
        float tArcDivR=sgn*worldDiv((2.0-r)*h2,denom),uCrest=clamp(rounding*worldDiv(tArcDivR-tMid,halfLen),-.80,.80);
        fraction=(uEdge<uCrest)?worldDiv(uCrest-uEdge,uCrest+1.0):worldDiv(uEdge-uCrest,1.0-uCrest);
    }
    if(fraction>=1.0)return clampedMin;
    float lateralFade=1.0-(3.0*fraction*fraction-2.0*fraction*fraction*fraction);
    float saddleHeight=graph.upliftExtra2.y>0.0?graph.upliftExtra2.y:.75,sh=clamp(saddleHeight,.01,1.0);
    float edgeHeight=saddleHeight*lateralFade,kSlope=worldDiv(1.0-sh,sh),gR=r*(1.0+kSlope*(r-1.0));
    return clamp(clampedMin+gR*edgeHeight,0.0,1.0);
}
vec2 continentWarp(vec2 raw,float tectonic,bool includeMacro){
    int scale=int(floor(double(tectonic*.225)+.5));float strength=float(floor(double(tectonic*.33)+.5));
    precise vec2 offset=vec2(worldPerlin(raw,graph.worldSeeds0.w,scale,int(graph.continentNoise.x),graph.continentNoise.z,graph.continentNoise.y)-.5,
        worldPerlin(raw,graph.worldSeeds1.x,scale,int(graph.continentNoise.x),graph.continentNoise.z,graph.continentNoise.y)-.5)*strength;
    if(includeMacro&&graph.upliftExtra.x>0.0){
        int mScale=int(floor(double(tectonic*.75)+.5));float mStrength=tectonic*.40*graph.upliftExtra.x;
        offset+=vec2(worldPerlin(raw,graph.worldSeeds0.w+7919u,mScale,3,2.2,.35)-.5,worldPerlin(raw,graph.worldSeeds1.x+104729u,mScale,3,2.2,.35)-.5)*mStrength;
    }
    return raw+offset;
}
float continentLineDistance(vec2 p,vec2 a,vec2 b){
    precise vec2 mid=(a+b)*.5,d=b-a,n=vec2(-d.y,d.x),end=mid+n;
    // Keep getDistance2Line's endpoint subtraction; replacing it with n can
    // change float rounding at distant coordinates.
    precise vec2 delta=end-mid;precise float v=(p.x-mid.x)*delta.x+(p.y-mid.y)*delta.y;
    v=worldDiv(v,delta.x*delta.x+delta.y*delta.y);
    precise vec2 point=mid+delta*v,offset=p-point;return offset.x*offset.x+offset.y*offset.y;
}
float continentCoastal(vec2 p,float distance,float tectonic){
    bool bridged=graph.filterMeta2.x>=2u&&(int(graph.continentNoise.w)&3)==0;
    float complexity=bridged?graph.upliftExtra.y:1.0;
    if(complexity<=0.0)return distance;
    if(distance>graph.controlPoints.y&&distance<graph.controlPoints.w){
        precise float alpha=worldDiv(distance,graph.controlPoints.w);
        precise float rawCliff=bridged?ftfMap(worldSimplex2(p*tectonic,graph.worldSeeds1.y,int(graph.continent.x)/2,3,2.2,.35,true),.1,.25)
                                      :ftfMap(worldSimplex(p*tectonic,graph.worldSeeds1.y,int(graph.continent.x)/2,2,true),.1,.25);
        precise float cliff=ftfLerp(1.0,rawCliff,complexity);
        distance=ftfLerp(distance*cliff,distance,alpha);
        if(distance<graph.controlPoints.y){
            precise float rawBay=bridged?(worldSimplex2(p*tectonic,graph.worldSeeds1.z,100,2,2.5,.4,false)*.1+.9)
                                        :(worldSimplex(p*tectonic,graph.worldSeeds1.z,100,1,false)*.1+.9);
            precise float bay=ftfLerp(1.0,rawBay,complexity);
            distance=graph.controlPoints.y*bay;
        }
    }
    return distance;
}
float continentShift(float value,float threshold){if(value<=threshold)return 0.0;return clamp(worldDiv(value-threshold,1.0-threshold),0.0,1.0);}
float continentGradient(vec2 raw,float frequency){
    precise vec2 p=raw*frequency;ivec2 winner;vec2 center=continentClosest(p,winner);
    precise float square=center.x*center.x+center.y*center.y,gradient=1.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        if(x==0&&y==0)continue;
        vec2 neighbor=continentPoint(winner+ivec2(x,y));precise vec2 d=neighbor-center;precise float len=d.x*d.x+d.y*d.y;
        if(len<=.00001)continue;
        precise float other=neighbor.x*neighbor.x+neighbor.y*neighbor.y,base=.5*(other-square);
        precise float hx=base-(p.x*d.x+p.y*d.y),hc=base-(center.x*d.x+center.y*d.y);
        if(hc>.00001){precise float plane=worldDiv(hx,hc);if(plane<gradient)gradient=plane;}
    }
    return clamp(gradient,0.0,1.0);
}
int continentKind(){return int(graph.continentNoise.w)&3;}
float continentMetric(vec2 d){int kind=(int(graph.continentNoise.w)>>4)&3;precise float sq=d.x*d.x+d.y*d.y;return kind==1?abs(d.x)+abs(d.y):kind==2?abs(d.x)+abs(d.y)+sq:sq;}
vec4 ftfSimpleContinent(vec2 raw,out ivec2 worldCenter,bool restrictSingle){
    int scale=int(graph.continent.x),small=scale/2;precise float frequency=worldDiv(1.0,float(scale*4));
    precise vec2 warped=ftfWarpPerlin(raw,graph.worldSeeds0.y,20,2,20.0);
    precise vec2 offset=vec2(worldSimplex(warped,graph.worldSeeds0.z,small,3,false)-.5,worldSimplex(warped,graph.worldSeeds0.z+1u,small,3,false)-.5)*float(small);
    precise vec2 p=(raw+offset)*frequency;ivec2 base=ivec2(worldFloor(p.x),worldFloor(p.y)),winner=base;vec2 center=p;precise float first=999999.0,second=999999.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){ivec2 cell=base+ivec2(x,y);vec2 c=continentPoint(cell);precise float d=continentMetric(c-p);if(d<first){second=first;first=d;center=c;winner=cell;}else if(d<second)second=d;}
    worldCenter=ivec2(vec2(worldDiv(center.x,frequency),worldDiv(center.y,frequency)));
    precise float edge=1.0-worldDiv(first,second);edge=edge<=.2?0.0:edge>=1.0?1.0:worldDiv(edge-.2,.8);
    if(edge<graph.controlPoints.w)edge*=clamp(worldSimplex(raw,graph.worldSeeds0.w,scale*2,1,false)+.65,0.0,1.0)*worldDiv(edge,graph.controlPoints.w);
    return vec4(edge,0,1,ftfMap(ftfVal(graph.worldSeeds0.x,winner),-1.0,1.0));
}
vec4 ftfUpliftWithCenter(vec2 raw,out ivec2 worldCenter){
    int kind=continentKind();
    if(kind==1||kind==2){vec4 result=ftfSimpleContinent(raw,worldCenter,false);if(kind==2){ivec2 origin;ftfSimpleContinent(vec2(0),origin,false);if(any(notEqual(origin,worldCenter))){worldCenter=ivec2(0);return vec4(0,0,1,0);}}return result;}

    bool bridged=(kind==0)&&(graph.filterMeta2.x>=2u);
    int tectonicInt=int(graph.continent.x)*4;float tectonic=float(tectonicInt);
    precise float frequency=worldDiv(1.0,tectonic);
    precise vec2 p=continentWarp(raw,tectonic,bridged)*frequency;ivec2 winner;vec2 center=continentClosest(p,winner);
    bool hasAnyBridge=false;
    if(bridged){
        vec2 centroidPt=continentCentroidAndBridge(winner,hasAnyBridge);
        precise vec2 realCenter=vec2(worldDiv(centroidPt.x,frequency),worldDiv(centroidPt.y,frequency));
        worldCenter=ivec2(floor(dvec2(realCenter)+.5LF));
    }else if(kind==3){
        precise vec2 sum=vec2(0);for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)if(x!=0||y!=0)sum+=continentPoint(winner+ivec2(x,y));
        vec2 corrected=vec2(ftfLerp(center.x,sum.x/8.0,.35),ftfLerp(center.y,sum.y/8.0,.35));worldCenter=ivec2(vec2(worldDiv(corrected.x,frequency),worldDiv(corrected.y,frequency)));
    }else{
        precise vec2 realCenter=vec2(worldDiv(center.x,frequency),worldDiv(center.y,frequency));worldCenter=ivec2(floor(dvec2(realCenter)+.5LF));
    }
    if(continentShouldSkip(winner))return vec4(0,0,1,0);
    precise float nearest=3.402823466e38;
    if(bridged){nearest=continentBoundaryDistSq(p,winner,hasAnyBridge);}
    else{for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){if(x==0&&y==0)continue;precise float distance=continentLineDistance(p,center,continentPoint(winner+ivec2(x,y)));if(distance<nearest)nearest=distance;}}
    precise float size=continentSize(winner),varied=nearest;
    if(graph.continent.w>0.0&&any(notEqual(winner,ivec2(0))))varied*=size;
    precise float edge=ftfMap(sqrt(varied),.05,.25);
    // Source Frequency wrapper uses 1/frequency, not the original int scale.
    precise float externalFrequency=worldDiv(1.0,frequency);
    edge=continentCoastal(p,edge,externalFrequency);
    if(!bridged&&edge<graph.controlPoints.w&&edge>=graph.controlPoints.y)edge=continentCoastal(p,edge,externalFrequency);
    if(kind==3)return vec4(edge,0,1,continentCellValue(graph.worldSeeds0.x,winner));
    precise float water=worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w);
    precise float gradient=continentShift(bridged?continentSmoothGradient(raw,frequency):continentGradient(raw,frequency),water);
    gradient=continentShift(gradient,.15);
    if(size!=1.0)gradient=continentShift(gradient,1.0-size);
    precise float peak=continentShift(edge,water);if(peak<.05&&peak<gradient)gradient=peak;
    return vec4(edge,gradient,size,continentCellValue(graph.worldSeeds0.x,winner));
}
// Fast river-only variant: resolves exact centroid worldCenter, skip, size, and waterTable.
// Evaluates bridged coastal edge only in the narrow rawGradient in (0, 0.05) band where customPeak can clamp waterTable.
vec4 ftfUpliftRivers(vec2 raw,out ivec2 worldCenter){
    int kind=continentKind();
    if(kind==1||kind==2){vec4 result=ftfSimpleContinent(raw,worldCenter,false);if(kind==2){ivec2 origin;ftfSimpleContinent(vec2(0),origin,false);if(any(notEqual(origin,worldCenter))){worldCenter=ivec2(0);return vec4(0,0,1,0);}}return result;}
    bool bridged=(kind==0)&&(graph.filterMeta2.x>=2u);
    int tectonicInt=int(graph.continent.x)*4;float tectonic=float(tectonicInt);
    precise float frequency=worldDiv(1.0,tectonic);
    precise vec2 p=continentWarp(raw,tectonic,bridged)*frequency;ivec2 winner;vec2 center=continentClosest(p,winner);
    bool hasAnyBridge=false;
    if(bridged){
        vec2 centroidPt=continentCentroidAndBridge(winner,hasAnyBridge);
        precise vec2 realCenter=vec2(worldDiv(centroidPt.x,frequency),worldDiv(centroidPt.y,frequency));
        worldCenter=ivec2(floor(dvec2(realCenter)+.5LF));
    }else if(kind==3){
        precise vec2 sum=vec2(0);for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)if(x!=0||y!=0)sum+=continentPoint(winner+ivec2(x,y));
        vec2 corrected=vec2(ftfLerp(center.x,sum.x/8.0,.35),ftfLerp(center.y,sum.y/8.0,.35));worldCenter=ivec2(vec2(worldDiv(corrected.x,frequency),worldDiv(corrected.y,frequency)));
    }else{
        precise vec2 realCenter=vec2(worldDiv(center.x,frequency),worldDiv(center.y,frequency));worldCenter=ivec2(floor(dvec2(realCenter)+.5LF));
    }
    if(continentShouldSkip(winner))return vec4(0,0,1,0);
    if(kind==3)return vec4(1.0,0,1,continentCellValue(graph.worldSeeds0.x,winner));
    precise float size=continentSize(winner);
    precise float water=worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w);
    precise float gradient=continentShift(bridged?continentSmoothGradient(raw,frequency):continentGradient(raw,frequency),water);
    gradient=continentShift(gradient,.15);
    if(size!=1.0)gradient=continentShift(gradient,1.0-size);
    if(gradient>0.0){
        precise float nearest=3.402823466e38;
        if(bridged){nearest=continentBoundaryDistSq(p,winner,hasAnyBridge);}
        else{for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){if(x==0&&y==0)continue;precise float distance=continentLineDistance(p,center,continentPoint(winner+ivec2(x,y)));if(distance<nearest)nearest=distance;}}
        precise float varied=nearest;if(graph.continent.w>0.0&&any(notEqual(winner,ivec2(0))))varied*=size;
        precise float edge=ftfMap(sqrt(varied),.05,.25),externalFrequency=worldDiv(1.0,frequency);
        edge=continentCoastal(p,edge,externalFrequency);
        if(!bridged&&edge<graph.controlPoints.w&&edge>=graph.controlPoints.y)edge=continentCoastal(p,edge,externalFrequency);
        precise float peak=continentShift(edge,water);if(peak<.05&&peak<gradient)gradient=peak;
    }
    return vec4(0.0,gradient,size,continentCellValue(graph.worldSeeds0.x,winner));
}
vec4 ftfUplift(vec2 raw){ivec2 center;return ftfUpliftWithCenter(raw,center);}
float continentWaterHeight(float gradient,float size){
    float stepped=ftfSteppedWater(gradient);
    precise float height=stepped*worldDiv(graph.continent.x,4000.0);height*=size;height*=.5;return height;
}
// Lightweight hydrology helper for biomes.comp/materials.glsl: customPeak (<0.05) maps to 0.0 in ftfSteppedWater,
// so omitting the 5x5 bridged coastal-distance pass gives identical water heights without SPIR-V bloat.
float ftfContinentWaterHeight(vec2 raw){
    if(continentKind()!=0)return 0.0;
    bool bridged=graph.filterMeta2.x>=2u;
    float tectonic=float(int(graph.continent.x)*4);
    precise float frequency=worldDiv(1.0,tectonic);
    precise vec2 p=continentWarp(raw,tectonic,bridged)*frequency;
    ivec2 winner;continentClosest(p,winner);
    if(continentShouldSkip(winner))return 0.0;
    precise float size=continentSize(winner);
    precise float water=worldDiv(min(graph.worldLevels.z-1.0,graph.worldLevels.w),graph.worldLevels.w);
    precise float gradient=continentShift(bridged?continentSmoothGradient(raw,frequency):continentGradient(raw,frequency),water);
    gradient=continentShift(gradient,.15);
    if(size!=1.0)gradient=continentShift(gradient,1.0-size);
    return continentWaterHeight(gradient,size);
}
float continentDeepOcean(vec2 p,float lower,float upper,float canyonUpper,float depth){
    precise float depthScale=worldDiv(depth,63.0);
    int scale=max(1,int(floor(double(150.0*depthScale)+.5))),selectorScale=max(1,int(floor(double(500.0*depthScale)+.5))),warpScale=max(1,int(floor(double(50.0*depthScale)+.5)));
    precise vec2 q=ftfWarpPerlin(p,graph.worldSeeds1.w+4u,warpScale,2,50.0*depthScale);
    precise float selector=ftfPerlinNode(q,graph.worldSeeds1.w+3u,selectorScale,1,2.0);
    precise float lo=max(0.0,.6-.65/2.0),hi=min(1.0,.6+.65/2.0),a=0.0,b=0.0;
    if(selector<=hi)a=ftfLerp(lower,upper,ftfPerlinNode(q,graph.worldSeeds1.w+1u,scale,3,2.0));
    if(selector>=lo){
        precise float value=ftfPerlinNode(q,graph.worldSeeds1.w+2u,scale,4,2.0),part=abs(value-.5);
        value=value>=.5?.5+pow(part,.2):.5-pow(part,.2);
        precise float bound=pow(.5,.2),minimum=.5-bound,maximum=.5+bound;
        value=1.0-ftfMap(value,minimum,maximum);b=ftfLerp(lower,canyonUpper,value);
    }
    precise float result=selector<lo?a:selector>hi?b:ftfLerp(a,b,worldDiv(selector-lo,hi-lo));
    return clamp(result,lower,upper);
}
float continentOcean(vec2 p,float edge){
    float sea=graph.worldLevels.z,factor=graph.worldLevels.w,depth=graph.worldLevels.x;
    precise float waterY=min(sea-1.0,factor),minimum=worldDiv(-graph.worldLevels.y,factor),water=worldDiv(waterY,factor);
    int depthInt=int(depth),minDepth=max(8,depthInt/3),canyonDepth=minDepth+max(1,(depthInt-minDepth)/2),shallowDepth=max(7,depthInt/9);
    precise float lower=max(worldDiv(waterY-depth,factor),minimum),upper=max(worldDiv(waterY-float(minDepth),factor),lower),canyon=max(worldDiv(waterY-float(canyonDepth),factor),lower);
    precise float shallow=max(worldDiv(waterY-float(shallowDepth),factor),minimum),height;
    if(edge<graph.controlPoints.x)height=continentDeepOcean(p,lower,upper,canyon,depth);
    else if(edge>graph.controlPoints.z)height=max(water,0.0);
    else if(edge<graph.controlPoints.y){
        precise float alpha=worldDiv(edge-graph.controlPoints.x,graph.controlPoints.y-graph.controlPoints.x);alpha=alpha*alpha*(3.0-2.0*alpha);
        height=ftfLerp(continentDeepOcean(p,lower,upper,canyon,depth),shallow,alpha);
    }else{
        precise float alpha=worldDiv(edge-graph.controlPoints.y,graph.controlPoints.z-graph.controlPoints.y);alpha=alpha*alpha*(3.0-2.0*alpha);
        height=ftfLerp(shallow,max(water,0.0),alpha);
    }
    return height;
}
#ifndef FTF_MAP_PASS
vec3 ftfUpliftSurface(vec2 world,vec4 continent,float ocean,float horizontal,float vertical){
    float sea=graph.worldLevels.z,factor=graph.worldLevels.w;
    precise float frequency=worldDiv(1.0,horizontal);precise vec2 p=world*frequency;
    terrainBiome=vec2(-1.1);precise float edge=continent.x,land=0.0;float choice=edge<graph.controlPoints.x?65.0:edge<graph.controlPoints.y?66.0:67.0;
    if(edge>=graph.controlPoints.y){vec3 terrain=ftfLandNormalized(world,graph.regionSeeds,horizontal,vertical,sea,factor);land=terrain.x+continent.y;choice=terrain.z;}
    precise float height=edge<graph.controlPoints.y?ocean:edge>graph.controlPoints.w?land:ftfLerp(ocean,land,worldDiv(edge-graph.controlPoints.y,graph.controlPoints.w-graph.controlPoints.y));
    vec3 surface=vec3(height,edge,choice);
    surface.x*=factor;return surface;
}
#endif
