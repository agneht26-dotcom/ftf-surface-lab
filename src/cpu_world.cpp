#include "cpu_world.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
namespace ftf {
#include "cpu_tables.inc"
uint32_t seedInt(uint64_t seed){if(seed<=0x7fffffffull||seed>=0xffffffff80000000ull)return static_cast<uint32_t>(seed);seed=(seed^(seed>>33))*static_cast<uint64_t>(-49064778989728563ll);seed=(seed^(seed>>33))*static_cast<uint64_t>(-4265267296055464877ll);seed^=seed>>33;return static_cast<uint32_t>(seed^(seed>>32));}
int floor(float value){return static_cast<int>(value)-(value<0?1:0);}
int round(float value){return value>=0?static_cast<int>(value+.5f):static_cast<int>(value-.5f);}
static int javaRound(float value){double rounded=std::floor(static_cast<double>(value)+.5);return static_cast<int>(std::clamp(rounded,-2147483648.0,2147483647.0));}
float map(float value,float lo,float hi){float numerator=std::clamp(value,lo,hi)-lo,span=hi-lo;return numerator>=span?1:numerator/span;}
static float lerp(float a,float b,float t){return a+t*(b-a);}
static uint32_t hash(uint32_t seed,int x,int z){uint32_t h=seed^(1619u*static_cast<uint32_t>(x))^(31337u*static_cast<uint32_t>(z));h=h*h*h*60493u;return h^static_cast<uint32_t>(static_cast<int32_t>(h)>>13);}
static float value(uint32_t seed,glm::ivec2 p){uint32_t h=seed^(1619u*static_cast<uint32_t>(p.x))^(31337u*static_cast<uint32_t>(p.y));h=h*h*h*60493u;return .5f+static_cast<float>(static_cast<int32_t>(h))/2147483648.0f*.5f;}
static float gradient(uint32_t seed,int x,int z,float dx,float dz,bool eight=false){
    static const glm::vec2 grad8[]={{-1,-1},{1,-1},{-1,1},{1,1},{0,-1},{-1,0},{0,1},{1,0}};
    uint32_t h=hash(seed,x,z),index=eight?(h&7u):(static_cast<uint32_t>(static_cast<float>(h&0x3fffffu)*1.3333334f)&31u);auto p=eight?grad8[index]:cpuGrad24[index];return dx*p.x+dz*p.y;
}
static float perlin(float x,float z,uint32_t seed,bool eight=false){int ix=floor(x),iz=floor(z);float dx=x-static_cast<float>(ix),dz=z-static_cast<float>(iz),tx=dx*dx*(3-2*dx),tz=dz*dz*(3-2*dz);float a=lerp(gradient(seed,ix,iz,dx,dz,eight),gradient(seed,ix+1,iz,dx-1,dz,eight),tx),b=lerp(gradient(seed,ix,iz+1,dx,dz-1,eight),gradient(seed,ix+1,iz+1,dx-1,dz-1,eight),tx);return lerp(a,b,tz);}
float sine(float angle){return std::bit_cast<float>(cpuSinBits[static_cast<uint32_t>(static_cast<int>(angle*651.8986f))&4095u]);}
float cosine(float angle){return sine(angle+1.5708f);}
static float simplexCorner(float x,float z,int ix,int iz,uint32_t seed){float t=.5f-x*x-z*z;if(t<0)return 0;t*=t;return t*t*gradient(seed,ix,iz,x,z);}
float simplex(float x,float z,uint32_t seed,bool second){float t=(x+z)*.36602542f;int ix=floor(x+t),iz=floor(z+t);t=static_cast<float>(ix+iz)*.21132487f;float dx=x-(static_cast<float>(ix)-t),dz=z-(static_cast<float>(iz)-t);int sx=dx>dz?1:0,sz=1-sx;float dx1=dx-static_cast<float>(sx)+.21132487f,dz1=dz-static_cast<float>(sz)+.21132487f,dx2=dx-1+.42264974f,dz2=dz-1+.42264974f;return (second?99.83685f:79.869484f)*(simplexCorner(dx,dz,ix,iz,seed)+simplexCorner(dx1,dz1,ix+sx,iz+sz,seed)+simplexCorner(dx2,dz2,ix+1,iz+1,seed));}
static float simplexNode(float x,float z,uint32_t seed,int scale,int octaves,bool second){float frequency=1.0f/static_cast<float>(scale);x*=frequency;z*=frequency;float sum=0,amp=1,bound=0,signal=octaves==1?.989f:octaves==2?.81f:octaves==3?.781f:octaves==4?.708f:octaves==5?.702f:.696f;for(int i=0;i<octaves;++i){sum+=simplex(x,z,seed+static_cast<uint32_t>(i),second)*amp;bound+=amp*signal;x*=2;z*=2;amp*=.5f;}return map(sum,-bound,bound);}
float perlinNode(float x,float z,uint32_t seed,int scale,int octaves){float frequency=1.0f/static_cast<float>(scale);x*=frequency;z*=frequency;float sum=0,amp=.5f,bound=0,signal=octaves==1?.9f:octaves==2?.83f:octaves==3?.75f:octaves==4?.64f:octaves==5?.62f:.61f;for(int i=0;i<octaves;++i){sum+=perlin(x,z,seed+static_cast<uint32_t>(i),true)*amp;bound+=amp*signal;x*=2;z*=2;amp*=.5f;}return map(sum,-bound,bound);}
float WorldSampler::warpNoise(float x,float z,uint32_t seed,int scale)const{float frequency=1.0f/static_cast<float>(scale);x*=frequency;z*=frequency;int octaves=static_cast<int>(g.continentNoise.x);float sum=0,amp=g.continentNoise.y,bound=0,signal=octaves==1?.9f:octaves==2?.83f:octaves==3?.75f:octaves==4?.64f:octaves==5?.62f:.61f;for(int i=0;i<octaves;++i){sum+=perlin(x,z,seed+static_cast<uint32_t>(i))*amp;bound+=signal*amp;x*=g.continentNoise.z;z*=g.continentNoise.z;amp*=g.continentNoise.y;}return map(sum,-bound,bound);}
glm::vec2 WorldSampler::point(glm::ivec2 cell)const{auto offset=cpuCells[hash(g.worldSeeds0.x,cell.x,cell.y)&255u];return {static_cast<float>(cell.x)+offset.x*g.continent.y,static_cast<float>(cell.y)+offset.y*g.continent.y};}
glm::vec2 WorldSampler::closest(glm::vec2 p,glm::ivec2& winner)const{glm::ivec2 base{floor(p.x),floor(p.y)};winner=base;glm::vec2 center=p;float nearest=std::numeric_limits<float>::max();for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x){auto cell=base+glm::ivec2(x,z);auto candidate=point(cell);float dx=p.x-candidate.x,dz=p.y-candidate.y,dist=dx*dx+dz*dz;if(dist<nearest){nearest=dist;winner=cell;center=candidate;}}return center;}
float WorldSampler::coast(float x,float z,float edge,float frequency)const{if(edge>g.controlPoints.y&&edge<g.controlPoints.w){float alpha=edge/g.controlPoints.w,cliff=map(simplexNode(x*frequency,z*frequency,g.worldSeeds1.y,static_cast<int>(g.continent.x)/2,2,true),.1f,.25f);edge=lerp(edge*cliff,edge,alpha);if(edge<g.controlPoints.y)edge=g.controlPoints.y*(simplexNode(x*frequency,z*frequency,g.worldSeeds1.z,100,1,false)*.1f+.9f);}return edge;}
static float lineDistance(glm::vec2 p,glm::vec2 a,glm::vec2 b){float mx=(a.x+b.x)*.5f,mz=(a.y+b.y)*.5f,dx=b.x-a.x,dz=b.y-a.y,ex=mx-dz,ez=mz+dx;dx=ex-mx;dz=ez-mz;float v=(p.x-mx)*dx+(p.y-mz)*dz;v/=dx*dx+dz*dz;float ox=mx+dx*v,oz=mz+dz*v;dx=p.x-ox;dz=p.y-oz;return dx*dx+dz*dz;}
glm::ivec2 WorldSampler::centerForCell(glm::ivec2 cell)const{
 int kind=static_cast<int>(g.continentNoise.w)&3;float frequency=1.0f/(g.continent.x*4);auto center=point(cell);
 if(kind==3){glm::vec2 sum(0);for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x)if(x||z)sum+=point(cell+glm::ivec2(x,z));center={lerp(center.x,sum.x/8,.35f),lerp(center.y,sum.y/8,.35f)};}
 if(kind!=0)return {static_cast<int>(center.x/frequency),static_cast<int>(center.y/frequency)};
 return {javaRound(center.x/frequency),javaRound(center.y/frequency)};
}
WorldPoint WorldSampler::sample(float rawX,float rawZ,bool restrictSingle)const{
 int kind=static_cast<int>(g.continentNoise.w)&3;
 if(kind==1||kind==2){int scale=static_cast<int>(g.continent.x),small=scale/2;float frequency=1.0f/(scale*4);
 float ax=rawX+(perlinNode(rawX,rawZ,g.worldSeeds0.y,20,2)-.5f)*20,az=rawZ+(perlinNode(rawX,rawZ,g.worldSeeds0.y+1u,20,2)-.5f)*20;
 glm::vec2 p{(rawX+(simplexNode(ax,az,g.worldSeeds0.z,small,3,false)-.5f)*small)*frequency,(rawZ+(simplexNode(ax,az,g.worldSeeds0.z+1u,small,3,false)-.5f)*small)*frequency};
 glm::ivec2 base{floor(p.x),floor(p.y)};float first=999999,second=999999;WorldPoint result;result.cell=base;
 int metric=(static_cast<int>(g.continentNoise.w)>>4)&3;
 for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x){auto cell=base+glm::ivec2(x,z);auto center=point(cell);float dx=center.x-p.x,dz=center.y-p.y,distance=dx*dx+dz*dz;if(metric==1)distance=std::abs(dx)+std::abs(dz);if(metric==2)distance=std::abs(dx)+std::abs(dz)+distance;if(distance<first){second=first;first=distance;result.cell=cell;}else if(distance<second)second=distance;}
 result.center=centerForCell(result.cell);float edge=1-first/second;edge=edge<=.2f?0:edge>=1?1:(edge-.2f)/.8f;
 if(edge<g.controlPoints.w)edge*=std::clamp(simplexNode(rawX,rawZ,g.worldSeeds0.w,scale*2,1,false)+.65f,0.0f,1.0f)*(edge/g.controlPoints.w);result.edge=edge;
 if(kind==2&&restrictSingle&&result.center!=sample(0,0,false).center){result.edge=0;result.center={0,0};}return result;
 }

    int tectonic=static_cast<int>(g.continent.x)*4,scale=round(static_cast<float>(tectonic)*.225f);float strength=static_cast<float>(round(static_cast<float>(tectonic)*.33f)),frequency=1.0f/static_cast<float>(tectonic);
    float wx=rawX+(warpNoise(rawX,rawZ,g.worldSeeds0.w,scale)-.5f)*strength,wz=rawZ+(warpNoise(rawX,rawZ,g.worldSeeds1.x,scale)-.5f)*strength;glm::vec2 p{wx*frequency,wz*frequency};WorldPoint result;auto center=closest(p,result.cell);result.center=centerForCell(result.cell);bool isDefault=result.cell==glm::ivec2(0);
    if(g.continent.z>0&&!isDefault&&value(g.worldSeeds0.y,result.cell)<g.continent.z)return result;
    float nearest=std::numeric_limits<float>::max();for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x){if(x==0&&z==0)continue;auto other=point(result.cell+glm::ivec2(x,z));nearest=std::min(nearest,lineDistance(p,center,other));}
    if(g.continent.w>0&&!isDefault){result.size=map(value(g.worldSeeds0.z,result.cell),0,g.continent.w);nearest*=result.size;if(kind==3)result.size=1;}
    float edge=map(std::sqrt(nearest),.05f,.25f);edge=coast(p.x,p.y,edge,1.0f/frequency);if(edge<g.controlPoints.w&&edge>=g.controlPoints.y)edge=coast(p.x,p.y,edge,1.0f/frequency);result.edge=edge;return result;
}
float WorldSampler::distanceToOcean(int cx,int cz,float dx,float dz)const{int kind=static_cast<int>(g.continentNoise.w)&3;float distance=g.continent.x*((kind==1||kind==2)?2.0f:4.0f),high=distance;bool found=false;glm::ivec2 current{cx,cz};for(int i=0;i<10;++i){auto nearest=sample(static_cast<float>(cx)+dx*distance,static_cast<float>(cz)+dz*distance,false).center;distance+=distance;if(nearest==current)continue;float low=0;high=distance;for(int j=0;j<50;++j){float mid=(low+high)/2;nearest=sample(static_cast<float>(cx)+dx*mid,static_cast<float>(cz)+dz*mid,false).center;if(nearest==current)low=mid;else high=mid;if(high-low<50)break;}found=true;break;}if(!found)high=distance;float low=0;for(int i=0;i<50;++i){float mid=(low+high)/2,edge=sample(static_cast<float>(cx)+dx*mid,static_cast<float>(cz)+dz*mid,false).edge;if(edge>g.controlPoints.y)low=mid;else high=mid;if(high-low<10)break;}return high;}
}
