// Additional isolated Populators graphs. These do not select/blend regions.
layout(std430,set=0,binding=3) readonly buffer GraphSeedBuffer {
    uint values[16];uvec4 regionSeeds;uvec4 mixMeta;
    uint familySeeds[144];uint borderSeeds[16];uint mixSeeds[56];
    vec4 familyScales[8];uvec4 choices[64];
    uint mountainSeeds[72];vec4 mountainScales[3];uvec4 mountainMeta;
    uint chainSeeds[24];uvec4 volcanoSeeds;uvec4 chainShapeSeeds;
    uvec4 worldSeeds0;uvec4 worldSeeds1;
    vec4 continent;vec4 continentNoise;vec4 controlPoints;vec4 worldLevels;
    vec4 island0;vec4 island1;vec4 island2;vec4 island3;uvec4 islandMeta;
    uvec4 climateMeta;uvec4 climateSeeds0;uvec4 climateSeeds1;uvec4 climateSeeds2;
    vec4 climateShape;vec4 climateEdge;vec4 climateEffects;vec4 temperature0;vec4 temperature1;vec4 moisture0;vec4 moisture1;
    vec4 filter0;vec4 filter1;vec4 filter2;vec4 filter3;uvec4 filterMeta;
    uvec4 biomeSeeds;uvec4 biomeSeeds2;vec4 biomeOptions;
    vec4 scree0;vec4 scree1;vec4 surfaceOptions;uvec4 surfaceMeta;
    vec4 filter4;vec4 filter5;uvec4 filterMeta2;
    uvec4 strataMeta;uint strata[4100];   // FTF StrataRule stacks (world.cpp buildStrata)
    uvec4 surfaceRandom;uvec4 gradientRandoms[2];uint clayBands[48];uint simplexPerms[256];uint improvedNoises[6720];   // world.cpp buildVanillaSurface
    uvec4 regionSalts;   // FTF region rendezvous salts (world.cpp)
} graph;
vec2 terrainBiome=vec2(-1.1);float terrainShapeWeirdness=-.06;

uint terrainSeedBase=0u;
bool mixingFamilies=false;
uint terrainSeed(uint i){return mixingFamilies?graph.familySeeds[terrainSeedBase+i]:graph.values[i];}
float ftfMap(float v,float lo,float hi){
    precise float a=clamp(v,lo,hi)-lo;
    precise float span=hi-lo;
    return a>=span?1.0:float(double(a)/double(span));
}
vec2 ftfWarpPerlin(vec2 p,uint seed,int scale,int octaves,float strength){
    precise vec2 offset=vec2(ftfPerlinNode(p,seed,scale,octaves,2.0)-.5,ftfPerlinNode(p,seed+1u,scale,octaves,2.0)-.5)*strength;
    precise vec2 q=p+offset;return q;
}
float ftfRidgeNode(vec2 p,uint seed,int scale,int octaves,float lacunarity,float gain){
    precise float frequency=float(1.0/double(scale));p*=frequency;
    precise float amp=2.0,value=0.0,weight=1.0,spectral=1.0,bound=0.0,spectralFrequency=1.0,boundWeight=1.0;
    for(int i=0;i<octaves;++i){
        precise float signal=1.0-abs(ftfPerlin8(p,seed+uint(i)));
        signal*=signal;signal*=weight;weight=clamp(signal*amp,0.0,1.0);
        spectral=float(1.0/double(spectralFrequency));value+=signal*spectral;bound+=boundWeight*spectral;boundWeight=clamp(boundWeight*amp,0.0,1.0);
        p*=lacunarity; spectralFrequency*=lacunarity;amp*=gain;
    }
    return ftfMap(value,0.0,bound);
}
float ftfVal(uint seed,ivec2 c){uint n=seed^(1619u*uint(c.x))^(31337u*uint(c.y));n=n*n*n*60493u;return float(int(n))/2147483648.0;}
float ftfCubicLerp(float a,float b,float c,float d,float t){precise float p=d-c-(a-b);precise float v=t*t*t*p+t*t*(a-b-p)+t*(c-a)+b;return v;}
float ftfCubicSample(vec2 p,uint seed){
    ivec2 c=ivec2(p)-ivec2(p.x<0.0?1:0,p.y<0.0?1:0);precise vec2 t=p-vec2(c);float rows[4];
    for(int y=-1;y<=2;++y)rows[y+1]=ftfCubicLerp(ftfVal(seed,c+ivec2(-1,y)),ftfVal(seed,c+ivec2(0,y)),ftfVal(seed,c+ivec2(1,y)),ftfVal(seed,c+ivec2(2,y)),t.x);
    precise float v=ftfCubicLerp(rows[0],rows[1],rows[2],rows[3],t.y)*.44444445;return v;
}
float ftfCubicNode(vec2 p,uint seed,int scale,int octaves){
    precise float frequency=float(1.0/double(scale));p*=frequency;
    precise float sum=ftfCubicSample(p,seed),amp=1.0,bound=.75;
    for(int i=1;i<octaves;++i){p*=2.0;amp*=.5;sum+=ftfCubicSample(p,seed+uint(i))*amp;bound+=.75*amp;}
    return ftfMap(sum,-bound,bound);
}
float ftfDales(vec2 p,uvec4 s){
    precise vec2 q=ftfWarpPerlin(p,s.w,300,1,100.0);
    precise float a=1.0-ftfRidgeNode(q,s.x,300,4,4.0,.8);
    precise float part=abs(a-.5);
    precise float transformed=a>=.5?.5+sqrt(part):.5-sqrt(part);
    precise float curveMin=.5-sqrt(.5),curveMax=.5+sqrt(.5);
    a=ftfMap(transformed,curveMin,curveMax)*.75;
    precise float b=pow(1.0-ftfRidgeNode(q,s.y,350,3,4.0,.8),1.25);
    precise float selector=ftfPerlinNode(q,s.z,400,1,2.0);
    selector=clamp(selector,.3,.6);selector=float(double(selector-.3)/double(.6-.3));
    precise float lower=max(0.0,.4-.75/2.0),upper=min(1.0,.4+.75/2.0);
    precise float alpha=float(double(selector-lower)/double(upper-lower));
    precise float h=selector<lower?a:(selector>upper?b:ftfLerp(a,b,alpha));
    h=pow(h,1.125);terrainShapeWeirdness=min(-h,-.06);return h*.4;
}
float ftfHills(vec2 p,uvec4 s,uint fifth,bool second,float vertical){
    precise vec2 q=ftfWarpPerlin(p,s.w,400,3,200.0);
    q=ftfWarpPerlin(q,s.z,30,3,20.0);
    precise float height;
    if(second){
        height=ftfCubicNode(q,s.x,128,2);
        precise float a=ftfPerlinNode(q,s.y,32,4,2.0)*.075+(1.0-.075);
        height*=a;
        precise float ridge=ftfRidgeNode(p,fifth,512,2,2.0,.975);
        height*=ridge*.8+(1.0-.8);height*=.55*vertical;
    }else{
        height=ftfPerlinNode(q,s.x,200,3,2.0);
        precise float a=(1.0-ftfRidgeNode(q,s.y,400,3,2.0,.5))*.5+(1.0-.5);
        height*=a;height*=.6*vertical;
    }
    return height;
}
float ftfSteps(float v,int count,float lo,float hi,bool hermite){
    precise float noise=1.0-v;
    precise float stepped=float(double(float(int(noise*float(count))))/double(count));
    precise float alpha=ftfMap((noise-stepped)*float(count),lo,hi);
    if(hermite)alpha=alpha*alpha*(3.0-2.0*alpha);
    return 1.0-ftfLerp(stepped,noise,alpha);
}
float ftfPlateau(vec2 p,float vertical){
    precise vec2 q=ftfWarpPerlin(p,terrainSeed(2),20,1,15.0);q=ftfWarpPerlin(q,terrainSeed(1),100,1,150.0);
    precise float valley=1.0-ftfRidgeNode(q,terrainSeed(0),500,1,2.0,.975);
    q=ftfWarpPerlin(p,terrainSeed(5),40,2,20.0);q=ftfWarpPerlin(q,terrainSeed(4),300,1,150.0);
    precise float top=ftfRidgeNode(q,terrainSeed(3),150,3,2.45,.975)*.15;
    precise float valleyScaler=float(double(clamp(valley,.02,.1)-.02)/double(.1-.02));top*=valleyScaler;
    q=ftfWarpPerlin(p,terrainSeed(7),40,2,20.0);precise float surface=ftfPerlinNode(q,terrainSeed(6),20,3,2.0)*.05;
    precise float cubic=ftfCubicNode(p,terrainSeed(8),500,1)*.6+.3;
    precise float inputHeight=clamp(valley*cubic+top,0.0,.999999);
    terrainShapeWeirdness=ftfLerp((-.26666668+(-.05))*.5,-.42,ftfMap(inputHeight,0.0,.415));
    // Terrace's step spacing uses its input graph's declared maximum, not 1.
    precise float maximum=.6;maximum+=.3;maximum+=.15;
    precise float spacing=float(double(maximum)/3.0),bound=(spacing-spacing*.4)/2.0;
    int index=int(inputHeight*4.0);precise float value=float(index)*spacing,next=float(index+1)*spacing;
    precise float lower=value-bound,upper=value+bound;
    if(index<3&&inputHeight>=lower){
        if(inputHeight>upper)value=next;
        else{
            precise float ramp=1.0-.9*.5,cliff=1.0-.15*.5;
            precise float alpha=float(double(inputHeight-lower)/double(upper-lower));
            if(alpha>ramp){precise float rampAlpha=float(double(alpha-ramp)/double(1.0-ramp));value+=(next-value)*rampAlpha*.35;}
            if(alpha>cliff){precise float cliffAlpha=float(double(alpha-cliff)/double(1.0-cliff));value=ftfLerp(value,next,cliffAlpha);}
        }
    }
    precise float h=(value+surface)*(.475*vertical);return h;
}
float ftfBadlandsHills(vec2 p){
    precise float mask=float(double(clamp(ftfPerlinNode(p,terrainSeed(0),270,3,2.0),.35,.65)-.35)/double(.65-.35));
    precise vec2 q=ftfWarpPerlin(p,terrainSeed(3),18,1,20.0);q=ftfWarpPerlin(q,terrainSeed(2),400,2,100.0);
    precise float hills=ftfRidgeNode(q,terrainSeed(1),275,4,2.0,.975)*mask;return hills;
}
float ftfBadlands(vec2 p){
    precise float hills=ftfBadlandsHills(p);
    precise float modulation=.4,alpha=1.0-modulation;
    precise float mod1=ftfBadlandsHills(ftfWarpPerlin(p,terrainSeed(4),100,1,50.0))*modulation;
    precise float low=ftfSteps(hills,4,.6,.7,false)*alpha+mod1,high=ftfSteps(hills,10,.6,.7,false)*alpha+mod1;
    precise float detail=(low+high)*.5+(1.0-.5);
    precise float scaler=ftfPerlinNode(p,terrainSeed(5),200,3,2.0)*modulation;
    precise float shape=ftfSteps(hills,4,.65,.75,true)*alpha+hills*scaler;shape*=alpha;
    precise float h=shape*detail;h*=.55;h+=.025;return h;
}
float ftfTorridonian(vec2 p){
    precise vec2 q=ftfWarpPerlin(p,terrainSeed(2),20,1,40.0);q=ftfWarpPerlin(q,terrainSeed(1),300,1,150.0);
    precise float plains=ftfPerlinNode(q,terrainSeed(0),100,3,2.0)*.15;
    q=ftfWarpPerlin(p,terrainSeed(5),20,2,20.0);q=ftfWarpPerlin(q,terrainSeed(4),300,1,200.0);
    precise float hills=ftfPerlinNode(q,terrainSeed(3),150,4,2.0);hills=pow(hills,1.0-hills);
    precise float selector=ftfPerlinNode(p,terrainSeed(6),200,3,2.0);
    precise float lower=.6-.6/2.0,upper=.6+.6/2.0,blendAlpha=float(double(selector-lower)/double(upper-lower));
    precise float value=selector<lower?plains:(selector>upper?hills:ftfLerp(plains,hills,blendAlpha));
    if(value>0.0){
        precise float modulation=ftfPerlinNode(p,terrainSeed(7),120,1,2.0)*.25;
        precise float mask=ftfPerlinNode(p,terrainSeed(8),200,1,2.0)*.5+.5;
        precise float result=float(double(float(int(value*6.0+.5)))/6.0);result+=(value-result)*.5;result=float(double(result+modulation)/double(1.0+.25));
        precise float alpha=value>.3?1.0:float(double(value)/double(.3));alpha*=mask;
        value=ftfLerp(value,result,alpha);
    }
    terrainShapeWeirdness=min(-value,-.05-.01);precise float h=pow(value,1.0-value)*.5;return h;
}
float ftfTerrainRaw(vec2 p,uint profile,uvec4 seeds,uint fifth,int scale,float vertical){
    if(profile==0u)return ftfPlainsRaw(p,seeds,fifth,scale,vertical);
    precise float height;
    if(profile==1u){
        precise vec2 q=ftfWarpPerlin(p,fifth,256,1,200.0);
        precise vec2 innerOffset=vec2(ftfPerlinNode(q,seeds.y,scale/4,3,3.0)-.5,ftfPerlinNode(q,seeds.z,scale/4,3,3.0)-.5)*(float(scale)/4.0);
        q+=innerOffset;
        precise float erosion=ftfPerlinNode(q,seeds.x,scale*2,3,3.75)*.45+(1.0-.45);
        height=ftfPerlinNode(q,seeds.w,scale,1,2.0)*erosion;height=height*.08+(-.02);
    }else if(profile==2u)height=ftfDales(p,seeds);
    else if(profile<=4u)height=ftfHills(p,seeds,fifth,profile==4u,vertical);
    else if(profile==5u)height=ftfPlateau(p,vertical);
    else if(profile==6u)height=ftfBadlands(p);
    else height=ftfTorridonian(p);
    return height;
}
float ftfIsolatedTerrain(vec2 p,uint profile,uvec4 seeds,uint fifth,int scale,float vertical,float sea,float baseScale,float heightScale,float factor){
    precise float height=ftfTerrainRaw(p,profile,seeds,fifth,scale,vertical);
    precise float base=float(double(sea)/double(factor))*baseScale;
    precise float result=max(base+height*heightScale,0.0)*factor;return result;
}
