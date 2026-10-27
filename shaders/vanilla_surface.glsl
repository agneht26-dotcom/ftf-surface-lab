// Minecraft 1.21.1 surface context, bit-exact (doubles and 64-bit integers as in Java): XoroshiroRandomSource and its
// positional factory, ImprovedNoise/PerlinNoise/NormalNoise from per-seed tables (world.cpp buildVanillaSurface),
// SimplexNoise for Biome temperature, SurfaceSystem.getSurfaceDepth, vertical_gradient, getBand and FTF StrataRule.
// Every value is taken at the column's block (floor of the sample point), as the game evaluates it.

uint64_t mcRotl(uint64_t x,int k){return (x<<k)|(x>>(64-k));}
struct McRandom {uint64_t lo,hi;};
uint64_t mcNextLong(inout McRandom r){uint64_t l=r.lo,m=r.hi,n=mcRotl(l+m,17)+l;m^=l;r.lo=mcRotl(l,49)^m^(m<<21);r.hi=mcRotl(m,28);return n;}
float mcNextFloat(inout McRandom r){return float(uint(mcNextLong(r)>>40))*5.9604645E-8;}
double mcNextDouble(inout McRandom r){return double(mcNextLong(r)>>11)*1.1102230246251565E-16LF;}
// PositionalRandomFactory.at(x, y, z): Mth.getSeed, then the two-long XoroshiroRandomSource constructor.
McRandom mcRandomAt(uvec4 factory,int x,int y,int z){
    int64_t l=int64_t(x*3129871)^(int64_t(z)*116129781L)^int64_t(y);l=l*l*42317861L+l*11L;l>>=16;
    McRandom r=McRandom(uint64_t(l)^packUint2x32(factory.xy),packUint2x32(factory.zw));
    if((r.lo|r.hi)==0UL){r.lo=0x9E3779B97F4A7C15UL;r.hi=0x6A09E667F3BCC909UL;}
    return r;
}

int mcFloor(double v){int i=int(v);return v<double(i)?i-1:i;}
double mcLerp(double t,double a,double b){precise double v=a+t*(b-a);return v;}
double mcSmoothstep(double d){precise double v=d*d*d*(d*(d*6.0LF-15.0LF)+10.0LF);return v;}
const ivec3 MC_GRADIENT[16]=ivec3[16](ivec3(1,1,0),ivec3(-1,1,0),ivec3(1,-1,0),ivec3(-1,-1,0),ivec3(1,0,1),ivec3(-1,0,1),ivec3(1,0,-1),ivec3(-1,0,-1),ivec3(0,1,1),ivec3(0,-1,1),ivec3(0,1,-1),ivec3(0,-1,-1),ivec3(1,1,0),ivec3(0,-1,1),ivec3(-1,1,0),ivec3(0,-1,-1));
double mcGradDot(uint hash,double x,double y,double z){ivec3 g=MC_GRADIENT[hash&15u];precise double v=double(g.x)*x+double(g.y)*y+double(g.z)*z;return v;}

// ImprovedNoise table t: xo, yo, zo as doubles, then the 256-byte permutation.
// Tables from 4096 on are the exact mod surface rule's (binding 16, after its 32 gradient words).
uint mcPerm(uint t,int i){uint k=uint(i)&255u;if(t>=4096u)return (exactNoise.words[32u+(t-4096u)*70u+6u+k/4u]>>(8u*(k&3u)))&255u;return (graph.improvedNoises[t*70u+6u+k/4u]>>(8u*(k&3u)))&255u;}
double mcOffset(uint t,uint k){if(t>=4096u){uint e=32u+(t-4096u)*70u+k*2u;return packDouble2x32(uvec2(exactNoise.words[e],exactNoise.words[e+1u]));}return packDouble2x32(uvec2(graph.improvedNoises[t*70u+k*2u],graph.improvedNoises[t*70u+k*2u+1u]));}
double mcImproved(uint t,dvec3 p){
    precise double x=p.x+mcOffset(t,0u),y=p.y+mcOffset(t,1u),z=p.z+mcOffset(t,2u);
    int i=mcFloor(x),j=mcFloor(y),k=mcFloor(z);
    precise double g=x-double(i),h=y-double(j),l=z-double(k);
    int a=int(mcPerm(t,i)),b=int(mcPerm(t,i+1)),c=int(mcPerm(t,a+j)),e=int(mcPerm(t,a+j+1)),m=int(mcPerm(t,b+j)),n=int(mcPerm(t,b+j+1));
    precise double g1=g-1.0LF,h1=h-1.0LF,l1=l-1.0LF;
    double v000=mcGradDot(mcPerm(t,c+k),g,h,l),v100=mcGradDot(mcPerm(t,m+k),g1,h,l),v010=mcGradDot(mcPerm(t,e+k),g,h1,l),v110=mcGradDot(mcPerm(t,n+k),g1,h1,l);
    double v001=mcGradDot(mcPerm(t,c+k+1),g,h,l1),v101=mcGradDot(mcPerm(t,m+k+1),g1,h,l1),v011=mcGradDot(mcPerm(t,e+k+1),g,h1,l1),v111=mcGradDot(mcPerm(t,n+k+1),g1,h1,l1);
    double s=mcSmoothstep(g),u=mcSmoothstep(h),w=mcSmoothstep(l);
    return mcLerp(w,mcLerp(u,mcLerp(s,v000,v100),mcLerp(s,v010,v110)),mcLerp(u,mcLerp(s,v001,v101),mcLerp(s,v011,v111)));
}
double mcWrap(double v){precise double q=v/3.3554432E7LF+0.5LF;int64_t l=int64_t(q);if(q<double(l))l-=1L;precise double r=v-double(l)*3.3554432E7LF;return r;}
double mcPerlin(int id,int part,dvec3 p){
    int count=NOISE_OCTAVES[id],offset=NOISE_AMPLITUDE_OFFSET[id];
    precise double sum=0.0LF;double e=ldexp(1.0LF,NOISE_FIRST_OCTAVE[id]),f=NOISE_LOWEST_VALUE_FACTOR[id];
    for(int o=0;o<count;++o){
        int t=NOISE_TABLE[2*offset+part*count+o];
        if(t>=0){precise dvec3 q=p*e;double g=mcImproved(uint(t),dvec3(mcWrap(q.x),mcWrap(q.y),mcWrap(q.z)));precise double a=NOISE_AMPLITUDES[offset+o]*g;sum+=a*f;}
        e*=2.0LF;f/=2.0LF;
    }
    return sum;
}
double mcNormalNoise(int id,dvec3 p){precise dvec3 q=p*1.0181268882175227LF;precise double v=(mcPerlin(id,0,p)+mcPerlin(id,1,q))*NOISE_VALUE_FACTOR[id];return v;}

// SimplexNoise.getValue(x, y) with permutation k of graph.simplexPerms (TEMPERATURE_NOISE, FROZEN_TEMPERATURE_NOISE levels).
uint mcSimplexPerm(uint k,int i){uint j=uint(i)&255u;return (graph.simplexPerms[k*64u+j/4u]>>(8u*(j&3u)))&255u;}
double mcSimplexCorner(uint gradient,dvec2 p){precise double t=.5LF-p.x*p.x-p.y*p.y;if(t<0.0LF)return 0.0LF;t*=t;ivec3 g=MC_GRADIENT[gradient];precise double v=double(g.x)*p.x+double(g.y)*p.y;return t*t*v;}
double mcSimplex(uint k,dvec2 p){
    precise double s=(p.x+p.y)*.3660254037844386LF;int i=mcFloor(p.x+s),j=mcFloor(p.y+s);precise double t=double(i+j)*.21132486540518713LF;
    precise dvec2 a=p-(dvec2(i,j)-dvec2(t));ivec2 step=a.x>a.y?ivec2(1,0):ivec2(0,1);precise dvec2 b=a-dvec2(step)+.21132486540518713LF,c=a-1.0LF+2.0LF*.21132486540518713LF;
    int r=i&255,q=j&255;
    uint ga=mcSimplexPerm(k,r+int(mcSimplexPerm(k,q)))%12u,gb=mcSimplexPerm(k,r+step.x+int(mcSimplexPerm(k,q+step.y)))%12u,gc=mcSimplexPerm(k,r+1+int(mcSimplexPerm(k,q+1)))%12u;
    return 70.0LF*(mcSimplexCorner(ga,a)+mcSimplexCorner(gb,b)+mcSimplexCorner(gc,c));
}

// Biome.coldEnoughToSnow at block (x, y, z): temperature modifier, then the height adjustment above y 80.
float matBaseTemperature;bool matFrozenModifier;
float materialTemperature(int y){
    float temperature=matBaseTemperature;
    if(matFrozenModifier){
        // FROZEN_TEMPERATURE_NOISE: octaves 0, -1, -2 at input factors 1, 1/2, 1/4 and value factors 1/7, 2/7, 4/7.
        dvec2 b=dvec2(matBlock),q=b*.05LF;double f0=1.0LF/(8.0LF-1.0LF);precise double d=0.0LF;d+=mcSimplex(1u,q)*f0;d+=mcSimplex(2u,q*.5LF)*(f0*2.0LF);d+=mcSimplex(3u,q*.25LF)*(f0*4.0LF);d*=7.0LF;
        precise double f=d+double(biomeInfoNoise(b*.2LF));
        if(f<.3LF&&biomeInfoNoise(b*.09LF)<.8LF)temperature=.2;
    }
    if(y>80){precise float g=float(mcSimplex(0u,dvec2(float(matBlock.x)/8.0,float(matBlock.y)/8.0))*8.0LF);precise float scaled=(g+float(y)-80.0)*.05;precise float v=temperature-worldDiv(scaled,40.0);return v;}
    return temperature;
}
bool materialCold(int y){return !(materialTemperature(y)>=.15);}

// vertical_gradient: RandomState.getOrCreateRandomFactory(random_name).at(x, y, z).nextFloat() < Mth.map(y, low, high, 1, 0).
bool materialGradient(uint k,int low,int high){
    if(matY<=low)return true;if(matY>=high)return false;
    precise double t=(double(matY)-double(low))/(double(high)-double(low));precise double d=1.0LF+t*(0.0LF-1.0LF);
    McRandom r=mcRandomAt(graph.gradientRandoms[k],matBlock.x,matY,matBlock.y);return double(mcNextFloat(r))<d;
}
// SurfaceSystem.getSurfaceDepth.
int materialSurfaceDepth(){McRandom r=mcRandomAt(graph.surfaceRandom,matBlock.x,0,matBlock.y);precise double v=matNoise[0]*2.75LF+3.0LF+mcNextDouble(r)*.25LF;return int(v);}
// SurfaceSystem.getBand with FTF's safe-Y remap (MixinSurfaceSystem): clayBands[(y' + round(offset * 4) + 192) % 192].
uint materialBandlands(){
    int y=((matY%192)+192)%192+3840;double v=matNoise[2]*4.0LF,fl=floor(v);int64_t r=int64_t(fl)+(v-fl>=.5LF?1L:0L);   // Math.round: nearest, ties up
    int i=int((int64_t(y)+r+192L)%192L);if(i<0)i+=192;
    uint code=(graph.clayBands[uint(i)/4u]>>(8u*(uint(i)&3u)))&255u;
    return code==1u?BLOCK_ORANGE_TERRACOTTA:code==2u?BLOCK_YELLOW_TERRACOTTA:code==3u?BLOCK_BROWN_TERRACOTTA:code==4u?BLOCK_RED_TERRACOTTA:code==5u?BLOCK_WHITE_TERRACOTTA:code==6u?BLOCK_LIGHT_GRAY_TERRACOTTA:BLOCK_TERRACOTTA;
}
