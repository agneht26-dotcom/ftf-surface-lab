#version 450
// Instanced blocky trees: vertex pulling from the mesh and instance buffers, one indirect draw per (kind, LOD).
struct Instance {vec4 posScale;uvec4 misc;};
layout(std430,set=0,binding=9) readonly buffer ViewEffects {vec4 effects;vec4 screenFocus;vec4 viewportSize;vec4 sun;};
layout(std430,set=0,binding=11) readonly buffer Instances {Instance item[];} instances;
layout(std430,set=0,binding=13) readonly buffer Mesh {vec4 vertex[];} mesh;
layout(push_constant) uniform Draw {mat4 vp;vec4 view;uvec4 draw;} d; // view: origin x, origin z, vertical exaggeration; draw: first mesh vertex, first instance
layout(location=0) out vec3 color;
layout(location=1) out vec3 normal;
layout(location=2) out vec2 crownUV;layout(location=3) flat out uint overview;
const vec3 normals[6]=vec3[6](vec3(1,0,0),vec3(-1,0,0),vec3(0,1,0),vec3(0,-1,0),vec3(0,0,1),vec3(0,0,-1));
const vec3 trunkColors[12]=vec3[12](vec3(.42,.32,.20),vec3(.42,.32,.20),vec3(.82,.82,.76),vec3(.26,.18,.11),vec3(.26,.18,.11),vec3(.22,.15,.09),vec3(.35,.27,.10),vec3(.35,.27,.10),vec3(.42,.40,.36),vec3(.24,.16,.08),vec3(.30,.20,.28),vec3(.36,.23,.19));
vec3 unpack(uint c){return vec3(float((c>>16)&255u),float((c>>8)&255u),float(c&255u))/255.0;}
void main(){
    Instance inst=instances.item[d.draw.y+uint(gl_InstanceIndex)];
    overview=(inst.misc.x>>8)&1u;crownUV=vec2(0);
    vec3 baseWorld=vec3(inst.posScale.x-d.view.x,inst.posScale.y*d.view.z,inst.posScale.z-d.view.y);
    if(d.draw.z!=0u){vec4 c=d.vp*vec4(baseWorld+vec3(0,d.view.w*.5,0),1);vec4 r=d.vp*vec4(baseWorld+vec3(max(40.0,d.view.w)),1);float margin=abs(r.x-c.x)+abs(r.y-c.y)+abs(r.z-c.z);if(c.w>0&&(abs(c.x)>c.w+margin||abs(c.y)>c.w+margin||c.z< -margin||c.z>c.w+margin)){gl_Position=vec4(2,2,2,1);normal=vec3(0,1,0);color=vec3(0);return;}}
    if(d.draw.w!=0u&&(d.vp*vec4(baseWorld,1)).w>float(d.draw.w)){gl_Position=vec4(2,2,2,1);normal=vec3(0,1,0);color=vec3(0);return;} // terrain distance culling (view depth, cheap)
    overview=0u; // Both LODs use world-scale block geometry, never map-sized canopy markers.
    vec4 v=mesh.vertex[d.draw.x+uint(gl_VertexIndex)];uint attr=floatBitsToUint(v.w);
    uint kind=inst.misc.x&255u,rotation=(inst.misc.x>>16)&3u;vec3 p=v.xyz,n=normals[(attr>>16)&7u];
    if(rotation==1u){p.xz=vec2(-p.z,p.x);n.xz=vec2(-n.z,n.x);}else if(rotation==2u){p.xz=-p.xz;n.xz=-n.xz;}else if(rotation==3u){p.xz=vec2(p.z,-p.x);n.xz=vec2(n.z,-n.x);}
    if((inst.misc.z&0x80000000u)!=0u)p.y*=inst.posScale.w;else p*=inst.posScale.w;
    vec3 world=vec3(inst.posScale.x-d.view.x,inst.posScale.y*d.view.z,inst.posScale.z-d.view.y)+p;
    gl_Position=d.vp*vec4(world,1);normal=n;
    float shade=float((attr>>8)&255u)/255.0;vec3 base;
    if((attr&255u)==1u)base=(inst.misc.z&0x80000000u)!=0u?unpack(inst.misc.z&0xffffffu):trunkColors[min(kind,11u)];
    else{vec3 tint=unpack(inst.misc.y);
        if((inst.misc.z&0x80000000u)==0u&&kind==2u)tint=vec3(.50,.655,.333);else if((inst.misc.z&0x80000000u)==0u&&(kind==3u||kind==4u||kind==5u))tint=vec3(.380,.600,.380);else if((inst.misc.z&0x80000000u)==0u&&kind==10u)tint=vec3(.94,.66,.78);
        base=tint*vec3(.88,.92,.86);}
    color=base*shade;
}
