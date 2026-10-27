// GPU port of NoiseUtil's 24-direction Perlin primitive from the supplied FTF
// build. The terrain composition using this primitive is a validation scene,
// not the complete FTF heightmap/continent/hydrology pipeline.
struct Sample { vec4 field; vec4 color; };
const vec2 GRAD24[32] = vec2[32](
 vec2(.13052619,.9914449),vec2(.38268343,.9238795),vec2(.6087614,.7933533),vec2(.6087614,.7933533),
 vec2(.7933533,.6087614),vec2(.9238795,.38268343),vec2(.9914449,.13052619),vec2(.9914449,.13052619),
 vec2(.9914449,-.13052619),vec2(.9238795,-.38268343),vec2(.7933533,-.6087614),vec2(.7933533,-.6087614),
 vec2(.6087614,-.7933533),vec2(.38268343,-.9238795),vec2(.13052619,-.9914449),vec2(.13052619,-.9914449),
 vec2(-.13052619,-.9914449),vec2(-.38268343,-.9238795),vec2(-.6087614,-.7933533),vec2(-.6087614,-.7933533),
 vec2(-.7933533,-.6087614),vec2(-.9238795,-.38268343),vec2(-.9914449,-.13052619),vec2(-.9914449,-.13052619),
 vec2(-.9914449,.13052619),vec2(-.9238795,.38268343),vec2(-.7933533,.6087614),vec2(-.7933533,.6087614),
 vec2(-.6087614,.7933533),vec2(-.38268343,.9238795),vec2(-.13052619,.9914449),vec2(-.13052619,.9914449));

uint ftfHash(uint seed, ivec2 c) {
    uint h = seed ^ (1619u * uint(c.x)) ^ (31337u * uint(c.y));
    h = h * h * h * 60493u;
    return h ^ uint(int(h) >> 13);
}
float ftfGrad(uint seed, ivec2 c, vec2 d) {
    uint selector = uint(float(ftfHash(seed,c) & 0x3fffffu) * 1.3333334) & 31u;
    precise float value = d.x * GRAD24[selector].x + d.y * GRAD24[selector].y;
    return value;
}
float ftfLerp(float a,float b,float t) { precise float v = a+t*(b-a); return v; }
float ftfPerlin(vec2 p,uint seed) {
    // Preserve FTF's floor semantics, including negative integral coordinates.
    ivec2 c=ivec2(p)-ivec2(p.x<0.0?1:0,p.y<0.0?1:0);
    precise vec2 d=p-vec2(c);
    precise vec2 t=d*d*d*(d*(d*6.0-15.0)+10.0);
    precise float a=ftfLerp(ftfGrad(seed,c,d),ftfGrad(seed,c+ivec2(1,0),d-vec2(1,0)),t.x);
    precise float b=ftfLerp(ftfGrad(seed,c+ivec2(0,1),d-vec2(0,1)),ftfGrad(seed,c+ivec2(1,1),d-vec2(1,1)),t.x);
    return ftfLerp(a,b,t.y);
}
float fbm(vec2 p,uint seed,int octaves,float gain) {
    float a=.5,sum=0.0,weight=0.0;
    for(int i=0;i<octaves;++i) { sum+=a*ftfPerlin(p,seed+uint(i)); weight+=a; p*=2.0; a*=gain; }
    return sum/max(weight,.0001);
}
const vec2 GRAD8[8]=vec2[8](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1),vec2(0,-1),vec2(-1,0),vec2(0,1),vec2(1,0));
float ftfGrad8(uint seed,ivec2 c,vec2 d){vec2 g=GRAD8[ftfHash(seed,c)&7u];precise float v=d.x*g.x+d.y*g.y;return v;}
float ftfPerlin8(vec2 p,uint seed){
    ivec2 c=ivec2(p)-ivec2(p.x<0.0?1:0,p.y<0.0?1:0);
    precise vec2 d=p-vec2(c);precise vec2 t=d*d*(3.0-2.0*d);
    precise float a=ftfLerp(ftfGrad8(seed,c,d),ftfGrad8(seed,c+ivec2(1,0),d-vec2(1,0)),t.x);
    precise float b=ftfLerp(ftfGrad8(seed,c+ivec2(0,1),d-vec2(0,1)),ftfGrad8(seed,c+ivec2(1,1),d-vec2(1,1)),t.x);
    return ftfLerp(a,b,t.y);
}
// Noises.perlin's CURVE3 mapping, normalization and first-octave amplitude.
float ftfPerlinNode(vec2 world,uint seed,int scale,int octaves,float lacunarity){
    precise float frequency=float(1.0/double(scale));
    precise vec2 p=world*frequency;
    precise float sum=0.0;precise float amplitude=.5;precise float bound=0.0;
    float signal=octaves==1?.9:(octaves==2?.83:(octaves==3?.75:(octaves==4?.64:(octaves==5?.62:.61))));
    for(int i=0;i<octaves;++i){sum+=ftfPerlin8(p,seed+uint(i))*amplitude;bound+=signal*amplitude;p*=lacunarity;amplitude*=.5;}
    precise float numerator=clamp(sum,-bound,bound)+bound;
    precise float denominator=bound-(-bound);
    precise float value=float(double(numerator)/double(denominator));
    return min(value,1.0);
}
// Populators.makePlains isolated before continent/region blending, hydrology,
// tile filters and biome selection. Scales come from the loaded FTF preset.
float ftfPlainsRaw(vec2 world,uvec4 seed0,uint outerSeed,int scale,float verticalScale){
    precise vec2 outerOffset=vec2(ftfPerlinNode(world,outerSeed,256,1,2.0)-.5,
        ftfPerlinNode(world,outerSeed+1u,256,1,2.0)-.5)*256.0;
    precise vec2 outer=world+outerOffset;
    precise vec2 innerOffset=vec2(ftfPerlinNode(outer,seed0.y,scale/4,3,3.5)-.5,
        ftfPerlinNode(outer,seed0.z,scale/4,3,3.5)-.5)*(float(scale)/4.0);
    precise vec2 inner=outer+innerOffset;
    precise float erosion=ftfPerlinNode(inner,seed0.x,scale*2,3,3.75)*.45+(1.0-.45);
    precise float height=ftfPerlinNode(inner,seed0.w,scale,1,2.0)*erosion;
    height=height*(.15*verticalScale)+(-.02);
    return height;
}
