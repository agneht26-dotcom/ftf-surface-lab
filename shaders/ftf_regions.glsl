#include "ftf_cells.glsl"
float ftfSimplexCorner(vec2 d,ivec2 c,uint seed){
    precise float t=.5-d.x*d.x-d.y*d.y;if(t<0.0)return 0.0;t*=t;
    precise float v=t*t*ftfGrad(seed,c,d);return v;
}
float ftfSimplexScaled(vec2 p,uint seed,float scaler){
    precise float t=(p.x+p.y)*.36602542;
    vec2 skewed=p+t;ivec2 cell=ivec2(skewed)-ivec2(skewed.x<0.0?1:0,skewed.y<0.0?1:0);
    t=float(cell.x+cell.y)*.21132487;
    precise vec2 d=p-(vec2(cell)-t);ivec2 stepCell=d.x>d.y?ivec2(1,0):ivec2(0,1);
    precise vec2 d1=d-vec2(stepCell)+.21132487,d2=d-1.0+.42264974;
    precise float v=scaler*(ftfSimplexCorner(d,cell,seed)+ftfSimplexCorner(d1,cell+stepCell,seed)+ftfSimplexCorner(d2,cell+1,seed));
    return v;
}
float ftfSimplex(vec2 p,uint seed){return ftfSimplexScaled(p,seed,79.869484);}
vec2 ftfRegionWarp(vec2 world,uvec4 seeds){
    precise float warpFrequency=float(1.0/double(400));
    precise vec2 wp=world*warpFrequency;
    precise vec2 offset=vec2(ftfMap(ftfSimplex(wp,seeds.y),-.989,.989)-.5,ftfMap(ftfSimplex(wp,seeds.z),-.989,.989)-.5)*200.0;
    precise vec2 warped=world+offset;return warped;
}
vec4 ftfRegions(vec2 world,uvec4 seeds,float regionScale){
    precise float frequency=float(1.0/double(regionScale));
    precise vec2 p=ftfRegionWarp(world,seeds)*frequency;
    ivec2 c=ivec2(p)-ivec2(p.x<0.0?1:0,p.y<0.0?1:0),winner=ivec2(0);
    precise float first=3.402823466e38,second=3.402823466e38;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        ivec2 candidate=c+ivec2(x,y);precise vec2 point=vec2(candidate)+FTF_CELLS[ftfHash(seeds.x,candidate)&255u]*.7;
        precise vec2 d=point-p;precise float dist=abs(d.x)+abs(d.y)+(d.x*d.x+d.y*d.y);
        if(dist<first){second=first;first=dist;winner=candidate;}else if(dist<second)second=dist;
    }
    precise float identity=ftfMap(ftfVal(seeds.x,winner),-1.0,1.0);
    precise float ratio=float(double(first)/double(second))-1.0;
    precise float edge=1.0-ftfMap(ratio,-1.0,0.0);edge=pow(edge,1.5);edge=edge>.5?1.0:edge/.5;
    precise vec2 point=vec2(winner)+FTF_CELLS[ftfHash(seeds.x,winner)&255u]*.7;
    precise vec2 center=vec2(dvec2(point)/double(frequency));
    return vec4(identity,edge,center);
}
