// Heightmap's land stage: full provider, region borders and mountain chains.
// Continents/oceans, hydrology, world filters and final biomes are separate.
float ftfMixRaw(vec2 point,uint source,float vertical){
    mixingFamilies=true;terrainSeedBase=source*16u;uint profile=source==8u?6u:source;
    uvec4 s=uvec4(terrainSeed(0),terrainSeed(1),terrainSeed(2),terrainSeed(3));
    precise float scaleValue=250.0*graph.familyScales[profile].z;
    int scale=profile<=1u?int(floor(double(scaleValue)+.5)):250;
    float result=ftfTerrainRaw(point,profile,s,terrainSeed(4),scale,vertical);
    float lo=profile<=1u?-.375:-.78,hi=profile<=1u?.45:profile==5u?.45:profile==6u?.05:.55;
    terrainBiome.x=ftfLerp(lo,hi,ftfPerlinNode(point,graph.biomeSeeds.x,200,2,2.0));
    terrainBiome.y=(profile==2u||profile==5u||profile==7u)?terrainShapeWeirdness:ftfLerp(-.4,-.05-.01,ftfPerlinNode(point,graph.biomeSeeds.y,300,2,2.0));return result;
}
float ftfMixNode(vec2 point,uvec4 node,float vertical,float sea,float factor,float regionId){
    if(node.y==12u)return ftfVolcano(point,sea,factor);
    if(node.y>=9u)return ftfMountain(point,node.y-9u,regionId,vertical,sea,factor);
    if(node.z==0xffffffffu){
        uint profile=node.y==8u?6u:node.y;vec4 scales=graph.familyScales[profile];
        precise float raw=ftfMixRaw(point,node.y,vertical);
        precise float base=float(double(sea)/double(factor))*scales.x;
        precise float height=max(base+raw*scales.y,0.0);return height;
    }
    int scale=int(graph.mixMeta.z)/2;
    vec2 warped=ftfWarpPerlin(point,graph.mixSeeds[node.w*2u+1u],scale/2,2,float(scale)/2.0);
    precise float selector=ftfPerlinNode(warped,graph.mixSeeds[node.w*2u],scale,1,2.0),raw;
    if(selector<.375)raw=ftfMixRaw(point,node.y,vertical);
    else if(selector>.625)raw=ftfMixRaw(point,node.z,vertical);
    else{
        precise float a=ftfMixRaw(point,node.y,vertical);vec2 firstBiome=terrainBiome;precise float b=ftfMixRaw(point,node.z,vertical);vec2 secondBiome=terrainBiome;
        precise float alpha=float(double(selector-.375)/double(.25));raw=ftfLerp(a,b,alpha);terrainBiome=vec2(ftfLerp(firstBiome.x,secondBiome.x,alpha),selector<.5?firstBiome.y:secondBiome.y);
    }
    // Provider composites use unscaled underlying noise and levels.ground.
    precise float base=float(double(min(sea,factor))/double(factor));
    precise float height=max(base+max(raw,0.0),0.0);return height;
}
vec3 ftfLandNormalized(vec2 world,uvec4 regionSeeds,float horizontal,float vertical,float sea,float factor){
    vec4 region=ftfRegions(world,regionSeeds,float(graph.mixMeta.z));
    precise float scaledId=region.x*float(graph.mixMeta.y-1u);
    uint index=uint(scaledId+.5),choice=0u;
    for(uint i=0u;i<graph.mixMeta.x;++i){if(index<graph.choices[i].x){choice=i;break;}}
    precise float frequency=float(1.0/double(horizontal));precise vec2 point=world*frequency;
    precise float control=graph.mountainMeta.w!=0u?ftfChainControl(point):0.0;
    precise float height=0.0;
    if(control<=.8&&region.y>0.0)height=ftfMixNode(point,graph.choices[choice],vertical,sea,factor,region.x);
    if(control<=.8&&region.y<1.0){
        uvec4 s=uvec4(graph.borderSeeds[0],graph.borderSeeds[1],graph.borderSeeds[2],graph.borderSeeds[3]);
        precise float scaleValue=250.0*graph.familyScales[0].z;
        int scale=int(floor(double(scaleValue)+.5));
        precise float raw=ftfPlainsRaw(point,s,graph.borderSeeds[4],scale,vertical);
        precise float base=float(double(sea)/double(factor))*graph.familyScales[1].x;
        precise float border=max(base+raw*graph.familyScales[1].y,0.0);
        vec2 borderBiome=vec2(ftfLerp(-.375,.45,ftfPerlinNode(point,graph.biomeSeeds.z,200,2,2.0)),ftfLerp(-.4,-.05-.01,ftfPerlinNode(point,graph.biomeSeeds.w,300,2,2.0)));
        terrainBiome=region.y==0.0?borderBiome:vec2(ftfLerp(borderBiome.x,terrainBiome.x,region.y),ftfLerp(borderBiome.y,terrainBiome.y,region.y));
        height=region.y==0.0?border:ftfLerp(border,height,region.y);
    }
    if(control>=.3){
        vec2 lowerBiome=terrainBiome;precise float chain=ftfMountainChain(point,region.x,region.y,vertical,sea,factor);
        if(control>.8)height=chain;
        else{precise float alpha=float(double(control-.3)/double(.8-.3));height=ftfLerp(height,chain,alpha);terrainBiome=vec2(ftfLerp(lowerBiome.x,terrainBiome.x,alpha),ftfLerp(lowerBiome.y,terrainBiome.y,alpha));}
        precise float midpoint=.3+(.8-.3)*.575;
        if(control>=midpoint)choice=64u;
    }
    return vec3(height,region.y,float(choice));
}
vec3 ftfLandMix(vec2 world,uvec4 regionSeeds,float horizontal,float vertical,float sea,float factor){vec3 value=ftfLandNormalized(world,regionSeeds,horizontal,vertical,sea,factor);value.x*=factor;return value;}
