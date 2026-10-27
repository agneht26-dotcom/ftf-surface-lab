// Recovered Populators mountain height graphs and the optional two-octave
// cellular erosion. No continent/mountain-chain/world-filter stages here.
#include "ftf_sin.glsl"
#ifdef FTF_CACHE_BUILD
layout(std430,set=0,binding=4) buffer MountainCacheBuffer {ivec4 bounds[2];uvec4 info;float heights[];} mountainCache;
#else
layout(std430,set=0,binding=4) readonly buffer MountainCacheBuffer {ivec4 bounds[2];uvec4 info;float heights[];} mountainCache;
#endif
layout(constant_id=2) const bool USE_MOUNTAIN_CACHE=true;
int mountainFloor(float x){return int(x)-(x<0.0?1:0);}
uint mountainType(uint instance){return instance>=9u?0u:instance%3u;}
uint mountainSettings(uint instance){return instance>=9u?instance-9u:instance/3u;}
uint mountainSeed(uint instance,uint i){return instance>=9u?graph.chainSeeds[(instance-9u)*8u+i]:graph.mountainSeeds[instance*8u+i];}
int mountainScale(uint instance){
    uint type=mountainType(instance);bool legacy=graph.mountainMeta.y!=0u;
    precise float scaleValue=(type==0u?(legacy?410.0:610.0):type==1u?360.0:600.0)*graph.mountainScales[mountainSettings(instance)].z;
    return type==1u&&legacy?360:type==2u&&legacy?400:int(floor(double(scaleValue)+.5));
}
float mountainSin(float angle){return uintBitsToFloat(FTF_SIN_BITS[uint(int(angle*651.8986))&4095u]);}
float mountainWorley(vec2 p,uint seed,int scale){
    precise float frequency=float(1.0/double(scale));p*=frequency;
    ivec2 c=ivec2(mountainFloor(p.x),mountainFloor(p.y));
    precise float a=3.402823466e38,b=3.402823466e38;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        ivec2 cell=c+ivec2(x,y);precise vec2 d=vec2(cell)+FTF_CELLS[ftfHash(seed,cell)&255u]-p;
        precise float dist=d.x*d.x+d.y*d.y;
        if(dist<a){b=a;a=dist;}else if(dist<b)b=dist;
    }
    precise float value=b-1.0;return ftfMap(value,-1.0,1.0);
}
float mountainBase(vec2 p,uint instance,int scale){
    uint type=mountainType(instance);
    if(type==0u){
        precise vec2 q=ftfWarpPerlin(p,mountainSeed(instance,2),350,1,150.0);
        precise float height=ftfRidgeNode(q,mountainSeed(instance,0),scale,4,2.35,1.15);
        precise float scaler=ftfPerlinNode(q,mountainSeed(instance,1),24,4,2.0)*.075+(1.0-.075);
        return height*scaler;
    }
    precise vec2 q=ftfWarpPerlin(p,mountainSeed(instance,1),200,2,100.0);
    precise float cell=clamp(mountainWorley(q,mountainSeed(instance,0),scale)*1.2,0.0,1.0);
    precise float blur=ftfPerlinNode(p,mountainSeed(instance,2),10,1,2.0)*.025+(1.0-.025);
    precise float surface=ftfRidgeNode(p,mountainSeed(instance,3),125,4,2.0,.975)*.37+(1.0-.37);
    precise float height=cell*blur; height*=surface;height=pow(height,1.1);
    if(type==2u&&height>.2){
        precise float mask=ftfMap(ftfPerlinNode(p,mountainSeed(instance,5),100,1,2.0),.5,.95);
        if(mask!=0.0){
            precise float modulation=ftfPerlinNode(p,mountainSeed(instance,4),50,1,2.0)*.5;
            precise float result=float(double(float(int(height*24.0+.5)))/24.0);
            result+=(height-result)*.45;result=float(double(result+modulation)/1.5);
            precise float alpha=height>.45?1.0:float(double(height-.2)/double(.45-.2));alpha*=mask;
            height=ftfLerp(height,result,alpha);
        }
    }
    return height;
}
float mountainSegment(vec2 p,vec2 a,vec2 b){
    precise vec2 pa=p-a,ba=b-a;
    precise float paba=pa.x*ba.x+pa.y*ba.y,baba=ba.x*ba.x+ba.y*ba.y;
    precise float h=paba==0.0&&baba==0.0?0.0:clamp(float(double(paba)/double(baba)),0.0,1.0);
    precise vec2 d=ba*h-pa;return d.x*d.x+d.y*d.y;
}
float mountainErosion(vec2 p,uint instance,int scale,uint seed,float gridSize,uint octave){
    precise vec2 divided=vec2(dvec2(p)/double(gridSize));ivec2 c=ivec2(mountainFloor(divided.x),mountainFloor(divided.y));
    if(USE_MOUNTAIN_CACHE){
        precise float distance=3.402823466e38;ivec4 bounds=mountainCache.bounds[octave];
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
            ivec2 rel=c+ivec2(x,y)-bounds.xy;
            uint index=instance*mountainCache.info.y+(octave==1u?mountainCache.info.x:0u)+uint(rel.y*bounds.z+rel.x);
            uint start=mountainCache.info.z+index*4u;
            vec2 a=vec2(mountainCache.heights[start],mountainCache.heights[start+1u]),b=vec2(mountainCache.heights[start+2u],mountainCache.heights[start+3u]);
            distance=min(distance,mountainSegment(p,a,b));
        }
        return clamp(float(double(sqrt(distance))/double(gridSize)),0.0,1.0);
    }
    // The Java cache lazily samples this same 5x5 union. Precomputing it once
    // preserves the sample locations and avoids 81 expensive height evaluations.
    float heights[25];vec2 anchors[25];
    for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){
        ivec2 cell=c+ivec2(x,y);precise vec2 point=(vec2(cell)+FTF_CELLS[ftfHash(seed,cell)&255u])*gridSize;
        int i=(y+2)*5+x+2;anchors[i]=point;
        heights[i]=mountainBase(point,instance,scale);
    }
    precise float distance=3.402823466e38;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        int a=(y+2)*5+x+2,b=a;precise float lowest=3.402823466e38;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int i=a+dy*5+dx;if(heights[i]<lowest){lowest=heights[i];b=i;}}
        distance=min(distance,mountainSegment(p,anchors[a],anchors[b]));
    }
    return clamp(float(double(sqrt(distance))/double(gridSize)),0.0,1.0);
}
uint mountainVariant(float id){
    uint bits=floatBitsToUint(id);bits=((bits>>16u)^bits)*73244475u;bits=((bits>>16u)^bits)*73244475u;bits=(bits>>16u)^bits;
    return (bits&0x7fffffffu)%3u;
}
float ftfMountainInstance(vec2 p,uint instance,float vertical,float sea,float factor){
    vec2 originalPoint=p;uint variant=mountainSettings(instance),type=mountainType(instance);
    vec4 settings=graph.mountainScales[variant];bool legacy=graph.mountainMeta.y!=0u;
    // Heightmap passes family verticalScale into the chain graph in addition
    // to TerrainPopulator's own verticalScale. Preserve this double scaling.
    if(instance>=9u&&!legacy)vertical*=settings.y;
    int scale=mountainScale(instance);
    if(graph.mountainMeta.x!=0u){
        uint first=type==0u?3u:type==1u?4u:6u;
        precise float angle=ftfPerlinNode(p,mountainSeed(instance,first),10,1,2.0)*6.2831855;
        precise vec2 offset=vec2(mountainSin(angle),mountainSin(angle+1.5708))*2.0;p+=offset;
    }
    precise float height=mountainBase(p,instance,scale);
    if(graph.mountainMeta.x!=0u){
        uint first=type==0u?3u:type==1u?4u:6u,seed=mountainSeed(instance,first+1u);
        precise float sum=mountainErosion(p,instance,scale,seed,128.0,0u);
        precise vec2 p2=p*3.1;precise float grid2=128.0*.8;
        sum+=mountainErosion(p2,instance,scale,seed,grid2,1u)*.15;
        precise float erosion=float(double(sum)/double(1.0+.15));
        height=ftfLerp(erosion,height,1.0-.65);
    }
    uint erosionSeed=variant==1u?graph.biomeSeeds.x:instance>=9u?graph.biomeSeeds2.y:graph.biomeSeeds2.x;
    float variation=variant==1u?0.0:(variant==0u?.2:-.25)*graph.biomeOptions.y;
    terrainBiome=vec2(clamp(ftfLerp(-1.0,.05,ftfPerlinNode(originalPoint,erosionSeed,200,2,2.0))+variation,-1.0,1.0),min(-height,-.08));
    precise float multiplier=type==0u?(legacy?.7:1.3):type==1u?.645:(legacy?.645:1.185);
    height*=multiplier*vertical;precise float base=float(double(sea)/double(factor))*settings.x;
    return max(base+height*settings.y,0.0);
}
float ftfMountain(vec2 p,uint type,float regionId,float vertical,float sea,float factor){
    uint variant=graph.mountainMeta.z!=0u?mountainVariant(regionId):1u;
    return ftfMountainInstance(p,variant*3u+type,vertical,sea,factor);
}
float ftfMountainChain(vec2 p,float id,float edge,float vertical,float sea,float factor){
    uint variant=graph.mountainMeta.z!=0u?mountainVariant(id):1u;
    if(graph.mountainMeta.z==0u||edge>=1.0||variant==1u)return ftfMountainInstance(p,9u+variant,vertical,sea,factor);
    precise float center=ftfMountainInstance(p,10u,vertical,sea,factor);vec2 centerBiome=terrainBiome;
    if(edge<=0.0)return center;
    precise float selected=ftfMountainInstance(p,9u+variant,vertical,sea,factor);
    terrainBiome=vec2(ftfLerp(centerBiome.x,terrainBiome.x,edge),ftfLerp(centerBiome.y,terrainBiome.y,edge));return ftfLerp(center,selected,edge);
}
