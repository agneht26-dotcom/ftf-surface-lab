// Euclidean Worley support for VolcanoPopulator and Heightmap's chain control.
// Centers remain in cellular coordinates for NOISE_LOOKUP, as in CellFunction.
vec4 ftfLandCell(vec2 p,uint seed,int scale){
    precise float frequency=float(1.0/double(scale));p*=frequency;
    ivec2 c=ivec2(mountainFloor(p.x),mountainFloor(p.y));
    precise float first=3.402823466e38,second=3.402823466e38;vec2 center=vec2(0);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        ivec2 candidate=c+ivec2(x,y);precise vec2 point=vec2(candidate)+FTF_CELLS[ftfHash(seed,candidate)&255u];
        precise vec2 d=point-p;precise float distance=d.x*d.x+d.y*d.y;
        if(distance<first){second=first;first=distance;center=point;}else if(distance<second)second=distance;
    }
    return vec4(max(first,.000001),second,center);
}
float ftfVolcanoHeight(vec4 cell){
    // Perlin stores its construction seed and ignores Worley's lookup seed.
    precise float value=ftfPerlinNode(cell.zw,graph.volcanoSeeds.x,2,1,2.0);
    precise float height=value*(.65-.45)+.45;return height;
}
float ftfVolcano(vec2 p,float sea,float factor){
    int scale=int(graph.mixMeta.z);uint seed=graph.regionSeeds.x-7u;
    vec4 cell=ftfLandCell(ftfRegionWarp(p,graph.regionSeeds),seed,scale);
    precise float limit=ftfVolcanoHeight(cell);
    precise vec2 q=ftfWarpPerlin(p,graph.volcanoSeeds.y,15,2,10.0);
    vec4 coneCell=ftfLandCell(ftfRegionWarp(q,graph.regionSeeds),seed,scale);
    precise float ratio=float(double(coneCell.x)/double(coneCell.y))-1.0;
    precise float cone=1.0-ftfMap(ratio,-1.0,0.0);
    precise float part=abs(cone-.5);
    cone=cone>=.5?.5+pow(part,11.0):.5-pow(part,11.0);
    cone=ftfMap(cone,.5-.00048828125,.5+.00048828125);
    cone=ftfMap(cone,.475,1.0);
    if(cone<=.5){precise float alpha=1.0-float(double(cone)/.5);cone=pow(cone,1.0-.5*alpha);}
    precise float value=cone*limit,maxHeight=limit*.94;
    precise float lower=.3-.3/2.0,upper=lower+.3,range=upper-lower;
    float influence=value>maxHeight?1.0:clamp((value-lower)/range,0.0,1.0);if(value<=maxHeight)influence=influence*influence*(3.0-2.0*influence);
    terrainBiome=vec2(ftfLerp(ftfLerp(-.375,.45,ftfPerlinNode(p,graph.biomeSeeds.x,200,2,2.0)),.25,influence),ftfLerp(ftfLerp(-.4,-.05-.01,ftfPerlinNode(p,graph.biomeSeeds.y,300,2,2.0)),(-.26666668-.05)*.5,influence));
    if(value>maxHeight){
        precise float delta=value-maxHeight,span=limit-maxHeight,alpha=float(double(delta)/double(span));
        value=maxHeight-float(double(maxHeight)/5.0)*alpha;
    }else if(value<upper){
        precise vec2 lowPoint=ftfWarpPerlin(p,graph.volcanoSeeds.w,30,1,30.0);
        precise float low=ftfRidgeNode(lowPoint,graph.volcanoSeeds.z,150,3,2.0,.975)*.1;
        if(value<lower)value+=low;
        else{precise float alpha=1.0-float(double(value-lower)/double(range));value+=low*alpha;}
    }
    precise float bias=float(double(min(sea,factor))/double(factor));return bias+value;
}
float ftfChainControl(vec2 p){
    bool legacy=graph.mountainMeta.y!=0u;
    precise float scaleValue=1000.0*graph.mountainScales[1].z;scaleValue*=2.25;
    int scale=legacy?1000:int(floor(double(scaleValue)+.5));
    precise vec2 q=ftfWarpPerlin(p,graph.chainShapeSeeds.y,333,2,250.0);
    vec4 cell=ftfLandCell(q,graph.chainShapeSeeds.x,scale);
    precise float shape=ftfMap(cell.y+cell.x-1.0,-1.0,1.6);
    shape=shape*shape*(3.0-2.0*shape);
    return ftfMap(shape,0.0,.9);
}
