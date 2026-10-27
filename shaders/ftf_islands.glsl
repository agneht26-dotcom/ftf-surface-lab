// ArchipelagoPopulator height/type branch, after pre-river continent blending.
int islandScale(float v){return int(clamp(floor(double(v)+.5),1.0LF,2147483647.0LF));}
float islandSmooth(float lo,float hi,float v){if(hi<=lo)return v>=hi?1.0:0.0;precise float a=clamp(worldDiv(v-lo,hi-lo),0.0,1.0);return a*a*(3.0-2.0*a);}
float islandNoise(vec2 p,uint offset,int scale,int oct){return worldSimplex(p,graph.islandMeta.y+offset,scale,oct,false);}
vec2 islandWarp(vec2 p,uint offset,int scale,int oct,float strength){return ftfWarpPerlin(p,graph.islandMeta.y+offset,scale,oct,strength);}
float islandDensity(vec2 p){
    precise vec2 q=islandWarp(p,9735u,2000,2,1000.0);precise float density=clamp(islandNoise(q,9735u,4000,3),0.0,1.0);
    q=islandWarp(p,1492u,4000,2,2000.0);precise float region=clamp(islandNoise(q,1492u,12000,2),0.0,1.0);
    precise float threshold=ftfLerp(1.05,-.25,clamp(graph.island3.w,0.0,1.0));
    precise float regionAlpha=islandSmooth(threshold,threshold+.25,region);
    precise float densityThreshold=clamp(1.0-graph.island0.x*.8,.05,.98),fade=clamp((1.0-densityThreshold)*.5,.12,.3);
    return islandSmooth(densityThreshold,densityThreshold+fade,density)*regionAlpha;
}
float islandShape(vec2 p,float size,float falloff){
    precise float mask=islandDensity(p);if(mask<=.001)return 0.0;
    precise vec2 q=islandWarp(p,8932u,islandScale(size*.08),2,size*.15);
    q=islandWarp(q,4830u,islandScale(size*.5),1,size*.3);q=islandWarp(q,1273u,islandScale(size*2.0),2,size*.5);
    precise float value=clamp(islandNoise(q,1273u,islandScale(size*3.5),3),0.0,1.0);
    precise float alpha=islandSmooth(falloff,1.0,value),h=max(.1,graph.island1.y),mh=max(.1,graph.island2.w);
    precise float drift=islandNoise(p,3391u,islandScale(size*1.2*mh*h),1)*.35;
    return clamp(alpha+drift*alpha,0.0,1.0)*mask;
}
vec3 ftfIslands(vec2 p,vec3 base){
    precise float size=float(int(floor(double(graph.island0.y)+.5))),depth=graph.worldLevels.x,factor=graph.worldLevels.w;
    precise float depthScale=max(1.0,worldDiv(depth,30.0)),sizeScale=max(.5,worldDiv(size,185.0));
    precise float falloff=clamp(.5-1.05*sizeScale*depthScale,.01,.4);
    precise float shape=islandShape(p,size,falloff),coast=uintBitsToFloat(graph.islandMeta.z);
    precise float fade=1.0-islandSmooth(coast,graph.controlPoints.x,base.y),alpha=shape*fade;
    if(alpha<=.001)return base;
    precise float gx=worldDiv(islandShape(p+vec2(5,0),size,falloff)-shape,5.0),gz=worldDiv(islandShape(p+vec2(0,5),size,falloff)-shape,5.0);
    precise float magnitude=gx*gx+gz*gz,angle=magnitude>.00000001?atan(gz,gx):0.0;
    precise float ca=cos(angle),sa=sin(angle),h=max(.1,graph.island1.y),mh=max(.1,graph.island2.w),vh=max(.1,graph.island2.z);
    precise float modulation=islandNoise(p*.005,4109u,islandScale(size*1.5),2),modFactor=.5+.5*modulation;
    precise vec2 angularPoint=vec2(ca*2.5+p.x*.008,sa*2.5+p.y*.008);
    precise float angular=islandNoise(angularPoint,8821u,islandScale(size*.25*h),2),detail=islandNoise(p*.02,6317u,islandScale(size*.12*h),3);
    precise float coastal=(angular*.65+detail*.35)*(.2+.8*modFactor),noiseFade=islandSmooth(0.0,.12,alpha);
    precise float perturbed=clamp(alpha+coastal*.18*(1.0-alpha*.8)*noiseFade,0.0,1.0),beachWidth=clamp(graph.island3.y,.05,.5),coverage=clamp(graph.island3.z,0.0,1.0);
    precise float widthMultiplier=3.0*sizeScale*depthScale*(.7+.6*modFactor),shelfEnd=clamp(beachWidth*.65*widthMultiplier,.12,.6);
    precise float variance=islandNoise(p,5541u,islandScale(size*.21*h),2)+coastal*.5,cliff=islandSmooth(-.2,.6,variance);
    precise float activeWidth=ftfLerp(beachWidth*widthMultiplier,.02,cliff),coastEnd=clamp(shelfEnd+activeWidth*.5,shelfEnd+.01,.85);
    precise float beachEnd=coastEnd+activeWidth*coverage*1.5,bVariance=variance*.15*(1.0-cliff)-.05*(1.0-cliff),dynamicEnd=clamp(beachEnd+bVariance,coastEnd+.01,.9);
    precise float waterY=min(graph.worldLevels.z-1.0,factor),water=worldDiv(waterY,factor),ground=worldDiv(min(graph.worldLevels.z,factor),factor);
    precise float oceanFloor=worldDiv(waterY-depth,factor),clampedFloor=min(base.x,oceanFloor),floorAlpha=islandDensity(p)*islandSmooth(0.0,shelfEnd,perturbed);
    precise float floorHeight=ftfLerp(base.x,clampedFloor,floorAlpha);
    int shelfDepth=max(2,int(floor(double(3.0+graph.island3.x*8.0)+.5)));
    precise float shelfTarget=max(floorHeight,worldDiv(waterY-float(shelfDepth),factor)),shelfAlpha=islandSmooth(0.0,shelfEnd,perturbed);
    precise float shelfHeight=ftfLerp(floorHeight,shelfTarget,pow(shelfAlpha,1.2));
    precise float regionVariance=.5+.5*islandNoise(p,3141u,islandScale(size*2.2),2);
    precise float ridge=clamp(ftfRidgeNode(p,graph.islandMeta.y+2718u,islandScale(size*.1*h),3,2.0,.85),0.0,1.0);
    precise float onset=islandSmooth(0.0,shelfEnd*.3,perturbed),shoreWidth=shelfEnd*.2,shoreStart=max(0.0,shelfEnd-shoreWidth),shore=1.0-islandSmooth(shoreStart,shelfEnd,perturbed);
    precise float detailBlocks=ridge*ftfLerp(1.5,6.0,regionVariance)*(onset*shore),submerged=min(shelfHeight,water),headroom=max(0.0,(water-submerged)*factor);
    shelfHeight=submerged+worldDiv(min(max(0.0,detailBlocks),headroom),factor);
    precise float coastAlpha=islandSmooth(shelfEnd,coastEnd,perturbed),beachHeight=ftfLerp(shelfHeight,ground,coastAlpha);
    precise float landEnd=clamp(ftfLerp(1.0,dynamicEnd+.32,cliff),dynamicEnd+.05,1.0),landAlpha=islandSmooth(dynamicEnd,landEnd,perturbed);
    precise float inlandBase=landAlpha*graph.island0.z*(.035+graph.island0.w*.1),mScale=clamp(graph.island1.w,0.0,1.0),mChance=clamp(graph.island1.z,0.0,1.0);
    precise float dome=ftfLerp(shape*.4,pow(shape,ftfLerp(1.3,3.6,mScale)),shape),domeContribution=dome*graph.island0.z*graph.island1.x*.95;
    precise float summitInfluence=islandSmooth(.6,.95,shape),summitStrength=.42*(.4+mScale*.8)*mChance;
    precise float summit=islandNoise(p,5107u,islandScale(size*.4*mh*h),2)*summitInfluence*summitStrength;domeContribution+=summit*graph.island0.z*graph.island1.x;
    precise float vScale=clamp(graph.island2.y,0.0,1.0),vChance=clamp(graph.island2.x,0.0,1.0),spire=ftfRidgeNode(p,graph.islandMeta.y+7213u,islandScale(size*.07*vh*h),3,2.2,.85);
    precise float mask=.5+.5*islandNoise(p,3391u,islandScale(size*.35*vh*h),2),presence=islandSmooth(1.0-vChance,1.0,mask),location=islandSmooth(.25,.8,shape)*landAlpha;
    precise float relief=pow(spire,2.8)*presence*location*vScale*graph.island0.z*graph.island1.x*.5,target=ground+inlandBase+(max(0.0,domeContribution)+relief);
    precise float height=ftfLerp(beachHeight,target,landAlpha),edge;
    if(perturbed<shelfEnd)edge=ftfLerp(base.y,coast,islandSmooth(0.0,shelfEnd,perturbed));else edge=ftfLerp(coast,1.0,islandSmooth(shelfEnd,1.0,perturbed));
    float assignment=base.z;if(height<=water){if(perturbed>=.01)assignment=66.0;}
    else assignment=perturbed<dynamicEnd?69.0:(shape>.5&&landAlpha>.5&&(graph.island1.z>.05||vScale>.2)?70.0:68.0);
    return vec3(height,max(base.y,edge),assignment);
}
