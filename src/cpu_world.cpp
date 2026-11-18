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
static float simplexNode(float x,float z,uint32_t seed,int scale,int octaves,float lacunarity,float gain,bool second){float frequency=1.0f/static_cast<float>(scale);x*=frequency;z*=frequency;float sum=0,amp=1,bound=0,signal=octaves==1?.989f:octaves==2?.81f:octaves==3?.781f:octaves==4?.708f:octaves==5?.702f:.696f;for(int i=0;i<octaves;++i){sum+=simplex(x,z,seed+static_cast<uint32_t>(i),second)*amp;bound+=amp*signal;x*=lacunarity;z*=lacunarity;amp*=gain;}return map(sum,-bound,bound);}
float perlinNode(float x,float z,uint32_t seed,int scale,int octaves){float frequency=1.0f/static_cast<float>(scale);x*=frequency;z*=frequency;float sum=0,amp=.5f,bound=0,signal=octaves==1?.9f:octaves==2?.83f:octaves==3?.75f:octaves==4?.64f:octaves==5?.62f:.61f;for(int i=0;i<octaves;++i){sum+=perlin(x,z,seed+static_cast<uint32_t>(i),true)*amp;bound+=amp*signal;x*=2;z*=2;amp*=.5f;}return map(sum,-bound,bound);}
bool WorldSampler::upliftBridged()const{return (static_cast<int>(g.continentNoise.w)&3)==0&&g.filterMeta2.x>=2u;}
float WorldSampler::warpNoise(float x,float z,uint32_t seed,int scale,int octaves,float lacunarity,float gain)const{float frequency=1.0f/static_cast<float>(scale);x*=frequency;z*=frequency;float sum=0,amp=gain,bound=0,signal=octaves==1?.9f:octaves==2?.83f:octaves==3?.75f:octaves==4?.64f:octaves==5?.62f:.61f;for(int i=0;i<octaves;++i){sum+=perlin(x,z,seed+static_cast<uint32_t>(i))*amp;bound+=signal*amp;x*=lacunarity;z*=lacunarity;amp*=gain;}return map(sum,-bound,bound);}
glm::vec2 WorldSampler::warpUplift(float rawX,float rawZ,bool includeMacro)const{
    int tectonic=static_cast<int>(g.continent.x)*4;
    int mesoScale=round(static_cast<float>(tectonic)*.225f);float mesoStrength=static_cast<float>(round(static_cast<float>(tectonic)*.33f));
    int octaves=static_cast<int>(g.continentNoise.x);float lac=g.continentNoise.z,gain=g.continentNoise.y;
    float ox=(warpNoise(rawX,rawZ,g.worldSeeds0.w,mesoScale,octaves,lac,gain)-.5f)*mesoStrength;
    float oz=(warpNoise(rawX,rawZ,g.worldSeeds1.x,mesoScale,octaves,lac,gain)-.5f)*mesoStrength;
    if(includeMacro&&g.upliftExtra.x>0.0f){
        int macroScale=round(static_cast<float>(tectonic)*.75f);
        float macroStrength=static_cast<float>(tectonic)*.40f*g.upliftExtra.x;
        ox+=(warpNoise(rawX,rawZ,g.worldSeeds0.w+7919u,macroScale,3,2.2f,.35f)-.5f)*macroStrength;
        oz+=(warpNoise(rawX,rawZ,g.worldSeeds1.x+104729u,macroScale,3,2.2f,.35f)-.5f)*macroStrength;
    }
    return {rawX+ox,rawZ+oz};
}
glm::vec2 WorldSampler::point(glm::ivec2 cell)const{auto offset=cpuCells[hash(g.worldSeeds0.x,cell.x,cell.y)&255u];return {static_cast<float>(cell.x)+offset.x*g.continent.y,static_cast<float>(cell.y)+offset.y*g.continent.y};}
glm::vec2 WorldSampler::closest(glm::vec2 p,glm::ivec2& winner)const{glm::ivec2 base{floor(p.x),floor(p.y)};winner=base;glm::vec2 center=p;float nearest=std::numeric_limits<float>::max();for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x){auto cell=base+glm::ivec2(x,z);auto candidate=point(cell);float dx=p.x-candidate.x,dz=p.y-candidate.y,dist=dx*dx+dz*dz;if(dist<nearest){nearest=dist;winner=cell;center=candidate;}}return center;}
float WorldSampler::continentSizeMod(glm::ivec2 cell)const{return (g.continent.w>0.0f&&cell!=glm::ivec2(0))?map(value(g.worldSeeds0.z,cell),0.0f,g.continent.w):1.0f;}
float WorldSampler::bridgeRoll(int ax,int ay,int bx,int by)const{if(ax>bx||(ax==bx&&ay>by)){std::swap(ax,bx);std::swap(ay,by);}uint32_t s=g.upliftSeeds.x^(static_cast<uint32_t>(bx)*73856093u)^(static_cast<uint32_t>(by)*19349663u);return value(s,{ax,ay});}
float WorldSampler::tectonicRoll(int ax,int ay,int bx,int by)const{if(ax>bx||(ax==bx&&ay>by)){std::swap(ax,bx);std::swap(ay,by);}uint32_t s=g.upliftSeeds.y^(static_cast<uint32_t>(bx)*73856093u)^(static_cast<uint32_t>(by)*19349663u);return value(s,{ax,ay});}
bool WorldSampler::shouldSkip(int cx,int cy)const{
    if(g.continent.z<=0.0f||(cx==0&&cy==0))return false;
    float t=g.continent.z;
    if(!upliftBridged()||g.upliftExtra.z<=0.0f)return value(g.worldSeeds0.y,{cx,cy})<t;
    float lonelyT=std::sqrt(t*std::sqrt(t));
    float strength=std::clamp(g.upliftExtra.z/.35f,0.0f,1.0f);
    float maxEffectiveT=lerp(t,lonelyT,strength);
    float rawSkip=value(g.worldSeeds0.y,{cx,cy});
    if(rawSkip>=maxEffectiveT)return false;
    if(continentSizeMod({cx,cy})<1.0f)return true;
    static const int nbrDx[8]={-1,0,1,-1,1,-1,0,1};
    static const int nbrDy[8]={-1,-1,-1,0,0,1,1,1};
    int bridges=0;
    for(int i=0;i<8;++i){int nx=cx+nbrDx[i],ny=cy+nbrDy[i];if(continentSizeMod({nx,ny})>=1.0f&&bridgeRoll(cx,cy,nx,ny)<g.upliftExtra.z)++bridges;}
    if(bridges==0)return true;
    float biasedT=(bridges==1)?(t*t):(t*t*t);
    return rawSkip<lerp(t,biasedT,strength);
}
bool WorldSampler::canBridge(int cx,int cy)const{return !shouldSkip(cx,cy)&&continentSizeMod({cx,cy})>=1.0f;}
bool WorldSampler::isBridged(int ax,int ay,int bx,int by)const{return upliftBridged()&&g.upliftExtra.z>0.0f&&canBridge(ax,ay)&&canBridge(bx,by)&&bridgeRoll(ax,ay,bx,by)<g.upliftExtra.z;}
static bool isSectorTooThin(float lo,float hi,float len){
    static const float minTan=std::tan(20.0f*3.14159265358979323846f/180.0f);
    float dPerp=.5f*len,dot=dPerp*dPerp+lo*hi,cross=dPerp*(hi-lo);
    return dot>0.0f&&cross<dot*minTan;
}
void WorldSampler::populateVoronoi(int cellX,int cellY,VoronoiPolygon& v)const{
    v.cellX=cellX;v.cellY=cellY;
    auto s0=point({cellX,cellY});v.seedX=s0.x;v.seedY=s0.y;
    std::array<int,48> candCx{},candCy{};std::array<float,48> candX{},candY{};int candCount=0;
    for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx){
        if(dx==0&&dy==0)continue;
        int cx=cellX+dx,cy=cellY+dy;auto p=point({cx,cy});
        candCx[candCount]=cx;candCy[candCount]=cy;candX[candCount]=p.x;candY[candCount]=p.y;++candCount;
    }
    int e=0;
    for(int i=0;i<candCount;++i){
        float px=candX[i],py=candY[i],diffX=px-v.seedX,diffY=py-v.seedY,lenSq=diffX*diffX+diffY*diffY;
        if(lenSq<1.0e-5f)continue;
        float len=std::sqrt(lenSq),inv=1.0f/len,nx=diffX*inv,ny=diffY*inv,mx=(v.seedX+px)*.5f,my=(v.seedY+py)*.5f,tx=-ny,ty=nx;
        float lo=-1.0e9f,hi=1.0e9f;bool ok=true;
        for(int j=0;j<candCount;++j){
            if(j==i)continue;
            float pxJ=candX[j],pyJ=candY[j],diffJx=pxJ-v.seedX,diffJy=pyJ-v.seedY,lenJSq=diffJx*diffJx+diffJy*diffJy;
            if(lenJSq<1.0e-5f)continue;
            float invJ=1.0f/std::sqrt(lenJSq),nxJ=diffJx*invJ,nyJ=diffJy*invJ,mxJ=(v.seedX+pxJ)*.5f,myJ=(v.seedY+pyJ)*.5f;
            float a=tx*nxJ+ty*nyJ,b=(mxJ-mx)*nxJ+(myJ-my)*nyJ;
            if(std::abs(a)<1.0e-6f){if(b<0.0f){ok=false;break;}continue;}
            float t=b/a;if(a>0.0f){if(t<hi)hi=t;}else if(t>lo)lo=t;
        }
        if(ok&&lo<hi&&e<24){
            v.ncx[e]=candCx[i];v.ncy[e]=candCy[i];
            v.dx[e]=candX[i]-v.seedX;v.dy[e]=candY[i]-v.seedY;
            v.mx[e]=mx;v.my[e]=my;v.nx[e]=nx;v.ny[e]=ny;v.tx[e]=tx;v.ty[e]=ty;
            v.len[e]=len;v.lo[e]=lo;v.hi[e]=hi;
            v.tMid[e]=(lo+hi)*.5f;v.halfLen[e]=(hi-lo)*.5f;
            ++e;
        }
    }
    float totalWeight=0.0f,numX=0.0f,numY=0.0f;
    for(int k=0;k<e;++k){
        float l=v.len[k],span=v.hi[k]-v.lo[k],w=l*span;totalWeight+=w;
        float dcX=(l*v.nx[k]+2.0f*v.tMid[k]*v.tx[k])*(1.0f/3.0f);
        float dcY=(l*v.ny[k]+2.0f*v.tMid[k]*v.ty[k])*(1.0f/3.0f);
        numX+=w*dcX;numY+=w*dcY;
    }
    float invW=totalWeight>1.0e-6f?(1.0f/totalWeight):0.0f;
    v.centroidX=totalWeight>1.0e-6f?(v.seedX+numX*invW):v.seedX;
    v.centroidY=totalWeight>1.0e-6f?(v.seedY+numY*invW):v.seedY;
    v.edgeCount=e;
    bool cellCanBridge=canBridge(cellX,cellY);
    for(int k=0;k<e;++k){
        float hcVal=(v.mx[k]-v.centroidX)*v.dx[k]+(v.my[k]-v.centroidY)*v.dy[k];
        v.hc[k]=std::max(hcVal,1.0e-5f);
        v.mTan[k]=(v.mx[k]-v.centroidX)*v.tx[k]+(v.my[k]-v.centroidY)*v.ty[k];
        bool notTooThin=!isSectorTooThin(v.lo[k],v.hi[k],v.len[k]);
        v.isBridged[k]=cellCanBridge&&isBridged(cellX,cellY,v.ncx[k],v.ncy[k])&&notTooThin;
        v.isTectonicActive[k]=v.isBridged[k]&&g.upliftExtra.w>0.0f&&tectonicRoll(cellX,cellY,v.ncx[k],v.ncy[k])<g.upliftExtra.w;
    }
}
const VoronoiPolygon& WorldSampler::getVoronoi(int cellX,int cellY)const{
    if(voronoiCache.size()<64)voronoiCache.resize(64);
    int h=((cellX&7)<<3)|(cellY&7);
    auto& entry=voronoiCache[h];
    if(entry.cellX==cellX&&entry.cellY==cellY)return entry;
    populateVoronoi(cellX,cellY,entry);
    return entry;
}
static float distToSegmentSq(float px,float py,float ax,float ay,float bx,float by){
    float dx=bx-ax,dy=by-ay,l2=dx*dx+dy*dy,t=l2<1.0e-12f?0.0f:std::clamp(((px-ax)*dx+(py-ay)*dy)/l2,0.0f,1.0f);
    float ex=px-(ax+t*dx),ey=py-(ay+t*dy);return ex*ex+ey*ey;
}
float WorldSampler::boundaryDistanceSq(float x,float y,int cellX,int cellY)const{
    const auto& v=getVoronoi(cellX,cellY);
    bool hasAnyBridge=false;
    if(g.upliftExtra.z>0.0f&&canBridge(cellX,cellY)){for(int k=0;k<v.edgeCount;++k)if(v.isBridged[k]){hasAnyBridge=true;break;}}
    if(!hasAnyBridge){
        float bestSq=std::numeric_limits<float>::max();
        for(int k=0;k<v.edgeCount;++k){
            float p1x=v.mx[k]+v.lo[k]*v.tx[k],p1y=v.my[k]+v.lo[k]*v.ty[k];
            float p2x=v.mx[k]+v.hi[k]*v.tx[k],p2y=v.my[k]+v.hi[k]*v.ty[k];
            bestSq=std::min(bestSq,distToSegmentSq(x,y,p1x,p1y,p2x,p2y));
        }
        return bestSq==std::numeric_limits<float>::max()?4.0f:bestSq;
    }
    float bestSq=std::numeric_limits<float>::max();
    for(int dy=-2;dy<=2;++dy){int cy=cellY+dy;for(int dx=-2;dx<=2;++dx){int cx=cellX+dx;
        if(shouldSkip(cx,cy))continue;
        const auto& cv=(cx==cellX&&cy==cellY)?v:getVoronoi(cx,cy);
        for(int k=0;k<cv.edgeCount;++k){
            if(cv.isBridged[k])continue;
            int ncx=cv.ncx[k],ncy=cv.ncy[k];
            if(!shouldSkip(ncx,ncy)&&(ncx<cx||(ncx==cx&&ncy<cy))&&std::abs(ncx-cellX)<=2&&std::abs(ncy-cellY)<=2)continue;
            float p1x=cv.mx[k]+cv.lo[k]*cv.tx[k],p1y=cv.my[k]+cv.lo[k]*cv.ty[k];
            float p2x=cv.mx[k]+cv.hi[k]*cv.tx[k],p2y=cv.my[k]+cv.hi[k]*cv.ty[k];
            bestSq=std::min(bestSq,distToSegmentSq(x,y,p1x,p1y,p2x,p2y));
        }
    }}
    return bestSq==std::numeric_limits<float>::max()?4.0f:bestSq;
}
float WorldSampler::coast(float x,float z,float edge,float frequency)const{
    bool bridged=upliftBridged();
    float complexity=bridged?g.upliftExtra.y:1.0f;
    if(complexity<=0.0f)return edge;
    if(edge>g.controlPoints.y&&edge<g.controlPoints.w){
        float alpha=edge/g.controlPoints.w;
        float rawCliff=bridged?map(simplexNode(x*frequency,z*frequency,g.worldSeeds1.y,static_cast<int>(g.continent.x)/2,3,2.2f,.35f,true),.1f,.25f)
                              :map(simplexNode(x*frequency,z*frequency,g.worldSeeds1.y,static_cast<int>(g.continent.x)/2,2,2.0f,.5f,true),.1f,.25f);
        float cliff=lerp(1.0f,rawCliff,complexity);
        edge=lerp(edge*cliff,edge,alpha);
        if(edge<g.controlPoints.y){
            float rawBay=bridged?(simplexNode(x*frequency,z*frequency,g.worldSeeds1.z,100,2,2.5f,.4f,false)*.1f+.9f)
                                :(simplexNode(x*frequency,z*frequency,g.worldSeeds1.z,100,1,2.0f,.5f,false)*.1f+.9f);
            float bay=lerp(1.0f,rawBay,complexity);
            edge=g.controlPoints.y*bay;
        }
    }
    return edge;
}
static float lineDistance(glm::vec2 p,glm::vec2 a,glm::vec2 b){float mx=(a.x+b.x)*.5f,mz=(a.y+b.y)*.5f,dx=b.x-a.x,dz=b.y-a.y,ex=mx-dz,ez=mz+dx;dx=ex-mx;dz=ez-mz;float v=(p.x-mx)*dx+(p.y-mz)*dz;v/=dx*dx+dz*dz;float ox=mx+dx*v,oz=mz+dz*v;dx=p.x-ox;dz=p.y-oz;return dx*dx+dz*dz;}
glm::ivec2 WorldSampler::centerForCell(glm::ivec2 cell)const{
    int kind=static_cast<int>(g.continentNoise.w)&3;float frequency=1.0f/(g.continent.x*4);
    if(upliftBridged()){const auto& v=getVoronoi(cell.x,cell.y);return {javaRound(v.centroidX/frequency),javaRound(v.centroidY/frequency)};}
    auto center=point(cell);
    if(kind==3){glm::vec2 sum(0);for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x)if(x||z)sum+=point(cell+glm::ivec2(x,z));center={lerp(center.x,sum.x/8,.35f),lerp(center.y,sum.y/8,.35f)};}
    if(kind!=0)return {static_cast<int>(center.x/frequency),static_cast<int>(center.y/frequency)};
    return {javaRound(center.x/frequency),javaRound(center.y/frequency)};
}
bool WorldSampler::isInBridgedSector(int cx,int cz,float dx,float dz)const{
    if(!upliftBridged()||g.upliftExtra.z<=0.0f)return false;
    float frequency=1.0f/(g.continent.x*4),x=static_cast<float>(cx)*frequency,y=static_cast<float>(cz)*frequency;
    glm::ivec2 cell;closest({x,y},cell);
    if(!canBridge(cell.x,cell.y))return false;
    const auto& v=getVoronoi(cell.x,cell.y);
    int minIndex=-1;float minS=std::numeric_limits<float>::max();
    for(int i=0;i<v.edgeCount;++i){
        float dotN=dx*v.nx[i]+dz*v.ny[i];
        if(dotN>.0001f){float sDist=(v.hc[i]/v.len[i])/dotN;if(sDist<minS){minS=sDist;minIndex=i;}}
    }
    if(minIndex<0||!v.isBridged[minIndex])return false;
    float dotT=dx*v.tx[minIndex]+dz*v.ty[minIndex],tEdge=minS*dotT-v.mTan[minIndex],halfLen=v.halfLen[minIndex];
    if(halfLen<=1.0e-6f)return false;
    float uEdge=(tEdge-v.tMid[minIndex])/halfLen;
    return uEdge>=-1.0f&&uEdge<=1.0f;
}
bool WorldSampler::isInBridgedSector(float rawX,float rawZ)const{
    if(!upliftBridged()||g.upliftExtra.z<=0.0f)return false;
    float frequency=1.0f/(g.continent.x*4),x=rawX*frequency,y=rawZ*frequency;
    glm::ivec2 cell;closest({x,y},cell);
    if(!canBridge(cell.x,cell.y))return false;
    const auto& v=getVoronoi(cell.x,cell.y);
    int minIndex=-1;float minGrad=std::numeric_limits<float>::max();
    for(int i=0;i<v.edgeCount;++i){
        float hx=(v.mx[i]-x)*v.dx[i]+(v.my[i]-y)*v.dy[i],pv=hx/v.hc[i];
        if(pv<minGrad){minGrad=pv;minIndex=i;}
    }
    if(minIndex<0||!v.isBridged[minIndex])return false;
    float r=1.0f-std::clamp(minGrad,0.0f,1.0f);if(r<=.0001f)return false;
    float dTan=(x-v.centroidX)*v.tx[minIndex]+(y-v.centroidY)*v.ty[minIndex],tEdge=dTan/r-v.mTan[minIndex],halfLen=v.halfLen[minIndex];
    if(halfLen<=1.0e-6f)return false;
    float uEdge=(tEdge-v.tMid[minIndex])/halfLen;
    return uEdge>=-1.0f&&uEdge<=1.0f;
}
WorldPoint WorldSampler::sample(float rawX,float rawZ,bool restrictSingle)const{
    int kind=static_cast<int>(g.continentNoise.w)&3;
    if(kind==1||kind==2){int scale=static_cast<int>(g.continent.x),small=scale/2;float frequency=1.0f/(scale*4);
    float ax=rawX+(perlinNode(rawX,rawZ,g.worldSeeds0.y,20,2)-.5f)*20,az=rawZ+(perlinNode(rawX,rawZ,g.worldSeeds0.y+1u,20,2)-.5f)*20;
    glm::vec2 p{(rawX+(simplexNode(ax,az,g.worldSeeds0.z,small,3,2.0f,.5f,false)-.5f)*small)*frequency,(rawZ+(simplexNode(ax,az,g.worldSeeds0.z+1u,small,3,2.0f,.5f,false)-.5f)*small)*frequency};
    glm::ivec2 base{floor(p.x),floor(p.y)};float first=999999,second=999999;WorldPoint result;result.cell=base;
    int metric=(static_cast<int>(g.continentNoise.w)>>4)&3;
    for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x){auto cell=base+glm::ivec2(x,z);auto center=point(cell);float dx=center.x-p.x,dz=center.y-p.y,distance=dx*dx+dz*dz;if(metric==1)distance=std::abs(dx)+std::abs(dz);if(metric==2)distance=std::abs(dx)+std::abs(dz)+distance;if(distance<first){second=first;first=distance;result.cell=cell;}else if(distance<second)second=distance;}
    result.center=centerForCell(result.cell);float edge=1-first/second;edge=edge<=.2f?0:edge>=1?1:(edge-.2f)/.8f;
    if(edge<g.controlPoints.w)edge*=std::clamp(simplexNode(rawX,rawZ,g.worldSeeds0.w,scale*2,1,2.0f,.5f,false)+.65f,0.0f,1.0f)*(edge/g.controlPoints.w);result.edge=edge;
    if(kind==2&&restrictSingle&&result.center!=sample(0,0,false).center){result.edge=0;result.center={0,0};}return result;
    }
    bool bridged=upliftBridged();
    float frequency=1.0f/(g.continent.x*4.0f);
    glm::vec2 warped=warpUplift(rawX,rawZ,bridged);
    glm::vec2 p{warped.x*frequency,warped.y*frequency};WorldPoint result;auto center=closest(p,result.cell);result.center=centerForCell(result.cell);bool isDefault=result.cell==glm::ivec2(0);
    if(shouldSkip(result.cell.x,result.cell.y))return result;
    float nearest=bridged?boundaryDistanceSq(p.x,p.y,result.cell.x,result.cell.y):std::numeric_limits<float>::max();
    if(!bridged)for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x){if(x==0&&z==0)continue;auto other=point(result.cell+glm::ivec2(x,z));nearest=std::min(nearest,lineDistance(p,center,other));}
    if(g.continent.w>0&&!isDefault){result.size=map(value(g.worldSeeds0.z,result.cell),0,g.continent.w);nearest*=result.size;if(kind==3)result.size=1;}
    float edge=map(std::sqrt(nearest),.05f,.25f);edge=coast(p.x,p.y,edge,1.0f/frequency);
    if(!bridged&&edge<g.controlPoints.w&&edge>=g.controlPoints.y)edge=coast(p.x,p.y,edge,1.0f/frequency);
    result.edge=edge;return result;
}
float WorldSampler::distanceToOcean(int cx,int cz,float dx,float dz)const{int kind=static_cast<int>(g.continentNoise.w)&3;float distance=g.continent.x*((kind==1||kind==2)?2.0f:4.0f),high=distance;bool found=false;glm::ivec2 current{cx,cz};for(int i=0;i<10;++i){auto nearest=sample(static_cast<float>(cx)+dx*distance,static_cast<float>(cz)+dz*distance,false).center;distance+=distance;if(nearest==current)continue;float low=0;high=distance;for(int j=0;j<50;++j){float mid=(low+high)/2;nearest=sample(static_cast<float>(cx)+dx*mid,static_cast<float>(cz)+dz*mid,false).center;if(nearest==current)low=mid;else high=mid;if(high-low<50)break;}found=true;break;}if(!found)high=distance;float low=0;for(int i=0;i<50;++i){float mid=(low+high)/2,edge=sample(static_cast<float>(cx)+dx*mid,static_cast<float>(cz)+dz*mid,false).edge;if(edge>g.controlPoints.y)low=mid;else high=mid;if(high-low<10)break;}return high;}
bool buildVoronoiBuffer(const TerrainParams& params,const GraphSeeds& g,VoronoiBufferKey& key,std::vector<uint32_t>& words){
    const bool active=(params.meta.z==5u||params.meta.z==6u)&&((static_cast<int>(g.continentNoise.w)&3)==0)&&g.filterMeta2.x>=2u;
    if(!active){
        VoronoiBufferKey next{};
        if(key==next&&words.size()>=8u)return false;
        key=next;words.assign(8u,0u);return true;
    }
    const float tectonic=g.continent.x*4.0f,frequency=1.0f/tectonic;
    const float mesoStrength=static_cast<float>(round(tectonic*.33f));
    const float macroStrength=g.upliftExtra.x>0.0f?tectonic*.40f*g.upliftExtra.x:0.0f;
    const float margin=(mesoStrength+macroStrength)*.5f+5.0f*tectonic;
    const float half=params.domain.x*.5f+margin;
    const int32_t minX=floor((params.domain.y-half)*frequency),maxX=floor((params.domain.y+half)*frequency);
    const int32_t minZ=floor((params.domain.z-half)*frequency),maxZ=floor((params.domain.z+half)*frequency);
    const int32_t gridW=maxX-minX+1,gridH=maxZ-minZ+1;
    if(gridW<=0||gridH<=0||int64_t(gridW)*gridH>131072)
        throw std::runtime_error("Voronoi continent grid exceeds 131072 cells; increase Continent Scale or reduce View Extent");
    VoronoiBufferKey next{true,minX,minZ,gridW,gridH,g.worldSeeds0,g.upliftSeeds,g.continent,g.upliftExtra,g.upliftExtra2};
    if(key==next&&!words.empty())return false;
    WorldSampler sampler(g);
    constexpr uint32_t stride=4u+24u*12u; // 292 uints per Voronoi cell
    words.assign(8u+size_t(gridW)*size_t(gridH)*stride,0u);
    words[0]=static_cast<uint32_t>(gridW);words[1]=static_cast<uint32_t>(gridH);
    words[2]=static_cast<uint32_t>(minX);words[3]=static_cast<uint32_t>(minZ);
    words[4]=stride;
    for(int32_t gz=0;gz<gridH;++gz){
        for(int32_t gx=0;gx<gridW;++gx){
            int cx=minX+gx,cz=minZ+gz;
            const auto& v=sampler.getVoronoi(cx,cz);
            const size_t cb=8u+(size_t(gz)*size_t(gridW)+size_t(gx))*stride;
            bool skipped=sampler.shouldSkip(cx,cz);
            bool hasAnyBridge=false;
            for(int k=0;k<v.edgeCount;++k)if(v.isBridged[k]){hasAnyBridge=true;break;}
            words[cb+0]=std::bit_cast<uint32_t>(v.centroidX);
            words[cb+1]=std::bit_cast<uint32_t>(v.centroidY);
            words[cb+2]=static_cast<uint32_t>(v.edgeCount);
            words[cb+3]=(hasAnyBridge?2u:0u)|(skipped?1u:0u);
            for(int k=0;k<v.edgeCount;++k){
                const size_t eb=cb+4u+size_t(k)*12u;
                words[eb+0]=std::bit_cast<uint32_t>(v.mx[k]);
                words[eb+1]=std::bit_cast<uint32_t>(v.my[k]);
                words[eb+2]=std::bit_cast<uint32_t>(v.tx[k]);
                words[eb+3]=std::bit_cast<uint32_t>(v.ty[k]);
                words[eb+4]=std::bit_cast<uint32_t>(v.lo[k]);
                words[eb+5]=std::bit_cast<uint32_t>(v.hi[k]);
                words[eb+6]=std::bit_cast<uint32_t>(v.len[k]);
                words[eb+7]=std::bit_cast<uint32_t>(v.hc[k]);
                words[eb+8]=std::bit_cast<uint32_t>(v.mTan[k]);
                words[eb+9]=static_cast<uint32_t>(int32_t(v.ncx[k]));
                words[eb+10]=static_cast<uint32_t>(int32_t(v.ncy[k]));
                words[eb+11]=(v.isBridged[k]?1u:0u)|(v.isTectonicActive[k]?2u:0u)|(sampler.shouldSkip(v.ncx[k],v.ncy[k])?4u:0u);
            }
        }
    }
    key=next;return true;
}
}
