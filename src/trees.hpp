#pragma once
// Blocky Minecraft-style tree meshes for the surface preview: twelve tree kinds, each with a near mesh (every exposed voxel face)
// and a far mesh (a few boxes). Vertices are vec4(x,y,z,attr bits): attr = colour index (1 trunk, 2 leaf) | shade<<8 | normal<<16.
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

constexpr uint32_t treeTypeCount=12;
inline const char* treeTypeName(uint32_t type){
    static const char* names[treeTypeCount]={"Oak","Fancy oak","Birch","Spruce","Pine","Mega spruce","Jungle","Mega jungle","Acacia","Dark oak","Cherry","Mangrove"};
    return type<treeTypeCount?names[type]:"?";
}
struct TreeMeshes {
    std::vector<glm::vec4> vertices;
    std::array<uint32_t,treeTypeCount*2> first{},count{}; // index = type*2+lod
};

namespace treemesh {
struct Voxels {
    static constexpr int W=33,H=40;std::vector<uint8_t> cells=std::vector<uint8_t>(W*H*W,0);
    uint8_t& at(int x,int y,int z){return cells[(static_cast<size_t>(y)*W+(z+W/2))*W+(x+W/2)];}
    uint8_t get(int x,int y,int z)const{if(y<0||y>=H||x<-W/2||x>W/2||z<-W/2||z>W/2)return 0;return cells[(static_cast<size_t>(y)*W+(z+W/2))*W+(x+W/2)];}
    void put(int x,int y,int z,uint8_t c){if(y>=0&&y<H&&x>=-W/2&&x<=W/2&&z>=-W/2&&z<=W/2)at(x,y,z)=c;}
    void column(int x,int z,int y0,int y1,uint8_t c){for(int y=y0;y<=y1;++y)put(x,y,z,c);}
    // Leaves: squares without corners (radius 2 style), plus shapes, or round discs about (cx,cz).
    void leafSquare(int y,int r,bool corners,int cx=0,int cz=0){for(int x=-r;x<=r;++x)for(int z=-r;z<=r;++z){if(!corners&&r>=2&&std::abs(x)==r&&std::abs(z)==r)continue;if(get(cx+x,y,cz+z)==0)put(cx+x,y,cz+z,2);}}
    void leafPlus(int y,int cx=0,int cz=0){for(int x=-1;x<=1;++x)for(int z=-1;z<=1;++z)if(std::abs(x)+std::abs(z)<=1&&get(cx+x,y,cz+z)==0)put(cx+x,y,cz+z,2);}
    void leafDisc(int y,float radius,float cx=0,float cz=0){int r=static_cast<int>(radius)+1;for(int x=-r-2;x<=r+2;++x)for(int z=-r-2;z<=r+2;++z){float dx=x-cx,dz=z-cz;if(dx*dx+dz*dz<=radius*radius+.5f&&get(x,y,z)==0)put(x,y,z,2);}}
    void leafBlob(float cx,float cy,float cz,float rx,float ry,float rz){for(int y=int(cy-ry)-1;y<=int(cy+ry)+1;++y)for(int x=int(cx-rx)-1;x<=int(cx+rx)+1;++x)for(int z=int(cz-rz)-1;z<=int(cz+rz)+1;++z){float a=(x-cx)/rx,b=(y-cy)/ry,c=(z-cz)/rz;if(a*a+b*b+c*c<=1.0f&&get(x,y,z)==0)put(x,y,z,2);}}
};
inline void buildVoxels(uint32_t type,Voxels& v){
    auto trunk=[&](int x,int z,int y0,int y1){v.column(x,z,y0,y1,1);};
    auto trunk2=[&](int y0,int y1){trunk(-1,-1,y0,y1);trunk(0,-1,y0,y1);trunk(-1,0,y0,y1);trunk(0,0,y0,y1);};
    switch(type){
    case 0:trunk(0,0,0,4);v.leafSquare(3,2,false);v.leafSquare(4,2,false);v.leafSquare(5,1,true);v.leafPlus(6);break;                           // oak
    case 1:trunk(0,0,0,7);v.leafBlob(0,9.0f,0,4.5f,3.5f,4.5f);v.column(0,0,8,9,1);break;                                                         // fancy oak
    case 2:trunk(0,0,0,5);v.leafSquare(4,2,false);v.leafSquare(5,2,false);v.leafSquare(6,1,true);v.leafPlus(7);break;                             // birch
    case 3:{trunk(0,0,0,6);const int radius[]={2,1,2,1,1,0};for(int i=0;i<6;++i){int y=2+i;int r=radius[i];if(r==2)v.leafSquare(y,2,false);else if(r==1)v.leafPlus(y);else v.put(0,y,0,2);}v.put(0,8,0,2);break;} // spruce
    case 4:{trunk(0,0,0,8);const int radius[]={1,2,1,2,1,1,0};for(int i=0;i<7;++i){int y=3+i;int r=radius[i];if(r==2)v.leafSquare(y,2,false);else if(r==1)v.leafPlus(y);else v.put(0,y,0,2);}v.put(0,10,0,2);break;} // pine
    case 5:{trunk2(0,17);const float radius[]={4.2f,3.4f,4.2f,3.4f,3.4f,2.6f,3.4f,2.6f,2.6f,1.8f,2.6f,1.8f,1.0f,0.6f};for(int i=0;i<14;++i)v.leafDisc(4+i,radius[i],-.5f,-.5f);v.leafDisc(18,.4f,-.5f,-.5f);break;} // mega spruce
    case 6:trunk(0,0,0,9);v.leafDisc(8,3.2f);v.leafDisc(9,3.2f);v.leafDisc(10,2.2f);v.leafDisc(11,1.2f);break;                                     // jungle
    case 7:trunk2(0,20);v.leafDisc(16,5.4f,-.5f,-.5f);v.leafDisc(17,5.4f,-.5f,-.5f);v.leafDisc(18,4.6f,-.5f,-.5f);v.leafDisc(19,3.4f,-.5f,-.5f);v.leafDisc(20,2.2f,-.5f,-.5f);v.leafDisc(21,1.2f,-.5f,-.5f);break; // mega jungle
    case 8:trunk(0,0,0,3);trunk(1,0,4,5);trunk(-1,1,3,5);v.leafDisc(6,3.0f,1,0);v.leafDisc(7,2.2f,1,0);v.leafDisc(5,1.6f,-1,1);v.leafDisc(6,1.6f,-1,1);break; // acacia
    case 9:trunk2(0,5);v.leafDisc(4,3.4f,-.5f,-.5f);v.leafDisc(5,4.4f,-.5f,-.5f);v.leafDisc(6,4.6f,-.5f,-.5f);v.leafDisc(7,3.6f,-.5f,-.5f);v.leafDisc(8,2.2f,-.5f,-.5f);break; // dark oak
    case 10:trunk(0,0,0,6);trunk(1,0,5,7);v.leafBlob(0.5f,8.5f,0,4.0f,2.8f,4.0f);v.leafDisc(6,4.2f,.5f,0);break;                                  // cherry
    default:trunk(0,0,0,6);v.put(1,0,1,1);v.put(-1,0,1,1);v.put(1,0,-1,1);v.put(-1,0,-1,1);v.put(1,1,1,1);v.put(-1,1,1,1);v.put(1,1,-1,1);v.put(-1,1,-1,1);
        v.leafDisc(6,3.2f);v.leafDisc(7,3.2f);v.leafDisc(8,2.2f);v.leafDisc(9,1.2f);break;                                                         // mangrove
    }
}
inline uint32_t packAttr(uint32_t color,float shade,uint32_t normal){return color|(static_cast<uint32_t>(std::clamp(shade,0.0f,1.0f)*255.0f)<<8)|(normal<<16);}
inline float bitsToFloat(uint32_t bits){float f;std::memcpy(&f,&bits,4);return f;}
// 0:+x 1:-x 2:+y 3:-y 4:+z 5:-z
inline void quad(std::vector<glm::vec4>& out,glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d,uint32_t attr){
    float packed=bitsToFloat(attr);for(auto p:{a,b,c,a,c,d})out.emplace_back(p,packed);
}
inline void box(std::vector<glm::vec4>& out,glm::vec3 lo,glm::vec3 hi,uint32_t color,float shadeBottom,float shadeTop){
    auto side=[&](float shade){return shade;};(void)side;
    auto at=[&](float t){return shadeBottom+(shadeTop-shadeBottom)*t;};
    quad(out,{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{hi.x,lo.y,hi.z},packAttr(color,.8f*at(.5f),0));
    quad(out,{lo.x,lo.y,hi.z},{lo.x,hi.y,hi.z},{lo.x,hi.y,lo.z},{lo.x,lo.y,lo.z},packAttr(color,.72f*at(.5f),1));
    quad(out,{lo.x,hi.y,lo.z},{lo.x,hi.y,hi.z},{hi.x,hi.y,hi.z},{hi.x,hi.y,lo.z},packAttr(color,1.0f*at(1.0f),2));
    quad(out,{lo.x,lo.y,hi.z},{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},packAttr(color,.55f*at(0.0f),3));
    quad(out,{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z},packAttr(color,.9f*at(.5f),4));
    quad(out,{hi.x,lo.y,lo.z},{lo.x,lo.y,lo.z},{lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},packAttr(color,.65f*at(.5f),5));
}
}

inline TreeMeshes buildTreeMeshes(){
    using namespace treemesh;TreeMeshes meshes;
    for(uint32_t type=0;type<treeTypeCount;++type){
        Voxels voxels;buildVoxels(type,voxels);int top=0;
        for(int y=0;y<Voxels::H;++y)for(int z=-Voxels::W/2;z<=Voxels::W/2;++z)for(int x=-Voxels::W/2;x<=Voxels::W/2;++x)if(voxels.get(x,y,z))top=std::max(top,y);
        const float height=float(top+1);
        // Near mesh: every exposed voxel face (voxel (x,y,z) occupies [x,x+1)x[y,y+1)x[z,z+1), the trunk centred on the origin).
        meshes.first[type*2]=uint32_t(meshes.vertices.size());
        for(int y=0;y<Voxels::H;++y)for(int z=-Voxels::W/2;z<=Voxels::W/2;++z)for(int x=-Voxels::W/2;x<=Voxels::W/2;++x){
            uint8_t c=voxels.get(x,y,z);if(!c)continue;glm::vec3 lo{float(x)-.5f,float(y),float(z)-.5f},hi=lo+glm::vec3(1);float g=.72f+.28f*(float(y)/height);
            auto& o=meshes.vertices;
            if(!voxels.get(x+1,y,z))quad(o,{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{hi.x,lo.y,hi.z},packAttr(c,.80f*g,0));
            if(!voxels.get(x-1,y,z))quad(o,{lo.x,lo.y,hi.z},{lo.x,hi.y,hi.z},{lo.x,hi.y,lo.z},{lo.x,lo.y,lo.z},packAttr(c,.72f*g,1));
            if(!voxels.get(x,y+1,z))quad(o,{lo.x,hi.y,lo.z},{lo.x,hi.y,hi.z},{hi.x,hi.y,hi.z},{hi.x,hi.y,lo.z},packAttr(c,1.0f*g,2));
            if(!voxels.get(x,y-1,z))quad(o,{lo.x,lo.y,hi.z},{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},packAttr(c,.55f*g,3));
            if(!voxels.get(x,y,z+1))quad(o,{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z},packAttr(c,.90f*g,4));
            if(!voxels.get(x,y,z-1))quad(o,{hi.x,lo.y,lo.z},{lo.x,lo.y,lo.z},{lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},packAttr(c,.65f*g,5));
        }
        meshes.count[type*2]=uint32_t(meshes.vertices.size())-meshes.first[type*2];
        // Far mesh: trunk box plus the leaf mass as two stacked boxes.
        meshes.first[type*2+1]=uint32_t(meshes.vertices.size());
        glm::ivec3 tlo{999},thi{-999},llo{999},lhi{-999};int leafMid=0,leafLow=999,leafHigh=-999;
        for(int y=0;y<Voxels::H;++y)for(int z=-Voxels::W/2;z<=Voxels::W/2;++z)for(int x=-Voxels::W/2;x<=Voxels::W/2;++x){uint8_t c=voxels.get(x,y,z);if(c==1){tlo=glm::min(tlo,glm::ivec3(x,y,z));thi=glm::max(thi,glm::ivec3(x,y,z));}else if(c==2){leafLow=std::min(leafLow,y);leafHigh=std::max(leafHigh,y);}}
        leafMid=(leafLow+leafHigh+1)/2;
        if(thi.x>=tlo.x)box(meshes.vertices,glm::vec3(tlo)-glm::vec3(.5f,0,.5f),glm::vec3(thi)+glm::vec3(.5f,1,.5f),1,.7f,1.0f);
        for(int half=0;half<2;++half){llo={999,999,999};lhi={-999,-999,-999};int y0=half?leafMid:leafLow,y1=half?leafHigh:leafMid-1;
            for(int y=y0;y<=y1;++y)for(int z=-Voxels::W/2;z<=Voxels::W/2;++z)for(int x=-Voxels::W/2;x<=Voxels::W/2;++x)if(voxels.get(x,y,z)==2){llo=glm::min(llo,glm::ivec3(x,y,z));lhi=glm::max(lhi,glm::ivec3(x,y,z));}
            if(lhi.x>=llo.x)box(meshes.vertices,glm::vec3(llo)-glm::vec3(.5f,0,.5f),glm::vec3(lhi)+glm::vec3(.5f,1,.5f),2,.75f,1.0f);}
        meshes.count[type*2+1]=uint32_t(meshes.vertices.size())-meshes.first[type*2+1];
    }
    return meshes;
}
