#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_gpu_shader_fp64 : require
#include "shared.glsl"
#define FTF_NO_GRAPH_CELL_HELPERS
#include "cell_state.glsl"
layout(rgba32f,set=0,binding=1) uniform readonly image2D surfaceField;
layout(rgba8,set=0,binding=2) uniform readonly image2D colors;
layout(rgba32f,set=0,binding=8) uniform readonly image2D coarseField;
layout(std430,set=0,binding=9) readonly buffer ViewEffects {vec4 effects;vec4 screenFocus;vec4 viewportSize;vec4 sun;vec4 sunShadow;uvec4 terrainMask;vec4 biomeSearchArea;vec4 scaleCue;vec4 detailHole;};
layout(push_constant) uniform Draw { mat4 vp; vec4 view; uvec4 meta; vec4 focus; vec4 limits; } d;
layout(location=0) out vec3 color;
layout(location=1) out vec3 normal;
layout(location=2) out float altitude;
layout(location=3) out float surfaceY;
layout(location=4) out vec2 worldXZ;
float heightAt(ivec2 q){q=clamp(q,ivec2(0),ivec2(d.meta.x)-1);float h=imageLoad(surfaceField,q).x;float water=d.view.y;if(d.meta.z==5u){float local=cells[uint(q.y)*d.meta.x+uint(q.x)].waterHeight;if(local>0.0)water=max(water,local*float(d.meta.y>>16)+1.0);}
return (d.meta.z==3u||d.meta.z==6u)?d.view.y:(d.meta.z==1u?h*200.0:(d.meta.y&256u)!=0u?h:max(h,water));}
void main(){
    uint layer=d.meta.y&255u;
    int n=int(d.meta.x),cells=int(d.meta.w&255u),stride=(d.meta.w&65536u)!=0u?int((d.meta.w>>8)&255u):1,row=(cells+stride-1)/stride+1,tiles=(n-1+cells-1)/cells;
    ivec2 patchOrigin=ivec2(gl_InstanceIndex%tiles,gl_InstanceIndex/tiles)*cells;
    ivec2 q=min(ivec2(gl_VertexIndex%row,gl_VertexIndex/row)*stride,ivec2(cells))+patchOrigin;
    q=min(q,ivec2(n-1));vec4 s=imageLoad(surfaceField,q);
    float spacing=d.view.x/float(n-1);float h=heightAt(q);
    float dx=heightAt(q+ivec2(1,0))-heightAt(q-ivec2(1,0));
    float dz=heightAt(q+ivec2(0,1))-heightAt(q-ivec2(0,1));
    normal=normalize(vec3(-dx*d.view.z,2.0*spacing,-dz*d.view.z));
    if((d.meta.w&65536u)!=0u){
        ivec2 local=q-patchOrigin;bool vertical=local.x==0||local.x==cells||q.x==n-1,horizontal=local.y==0||local.y==cells||q.y==n-1;
        if(vertical||horizontal){int bits=vertical?(local.x==0?17:20):(local.y==0?23:26);int edgeStride=1<<int((d.meta.w>>bits)&7u);int axis=vertical?q.y:q.x;int lo=(axis/edgeStride)*edgeStride,hi=min(lo+edgeStride,n-1);ivec2 a=vertical?ivec2(q.x,lo):ivec2(lo,q.y),b=vertical?ivec2(q.x,hi):ivec2(hi,q.y);h=mix(heightAt(a),heightAt(b),hi==lo?0.0:float(axis-lo)/float(hi-lo));}
    }
    vec3 pos=vec3((float(q.x)/float(n-1)-.5)*d.view.x,h*d.view.z,(float(q.y)/float(n-1)-.5)*d.view.x);
    color=imageLoad(colors,q).rgb;
    if(layer==1u)color=mix(vec3(.12,.23,.35),vec3(.91,.74,.32),clamp((s.x-d.view.y+100.0)/600.0,0,1));
    if(layer==2u)color=mix(vec3(.20,.45,.77),vec3(.9,.34,.13),s.z);
    if(layer==3u)color=mix(vec3(.72,.53,.27),vec3(.1,.46,.40),s.w);
    if(d.meta.z==3u&&layer==1u)color=vec3(s.y);
    if((d.meta.z==4u||d.meta.z==5u)&&layer==4u){float assignment=imageLoad(colors,q).a*255.0;color=(.48+.36*cos(6.2831853*(assignment*.61803399+vec3(0,.33,.67))))*mix(.35,1.0,s.y);}
    if(d.meta.z==6u){if(layer==1u)color=vec3(s.x);else if(layer==2u)color=mix(vec3(.04,.13,.25),vec3(.9,.85,.55),s.y);else if(layer==3u)color=vec3(s.z);}
    altitude=s.x-d.view.y;surfaceY=s.x;worldXZ=pos.xz+d.limits.zw;
    if(d.focus.w>0.0){
        pos.xz+=d.focus.xy;
        vec2 uv=clamp((pos.xz-(detailHole.w>.5?d.focus.xy:vec2(0)))/d.focus.z+.5,vec2(0),vec2(1));
        ivec2 nbase=imageSize(coarseField);vec2 f=uv*vec2(nbase-1);ivec2 a=ivec2(f);vec2 t=fract(f);ivec2 b=min(a+1,nbase-1);
        // Match the two triangles of the base mesh exactly at the transition.
        float h00=imageLoad(coarseField,a).x,h10=imageLoad(coarseField,ivec2(b.x,a.y)).x,h01=imageLoad(coarseField,ivec2(a.x,b.y)).x,h11=imageLoad(coarseField,b).x;
        if((d.meta.y&256u)==0u){h00=max(h00,d.view.y);h10=max(h10,d.view.y);h01=max(h01,d.view.y);h11=max(h11,d.view.y);}
        float coarse=t.x+t.y<=1.0?h00+(h10-h00)*t.x+(h01-h00)*t.y:h11+(h01-h11)*(1.0-t.x)+(h10-h11)*(1.0-t.y);
        float blend=smoothstep(d.focus.w*.70,d.focus.w*.94,length(pos.xz-d.focus.xy));
        if(screenFocus.w>0.0){
            vec4 clip=d.vp*vec4(pos,1);vec2 pixel=(clip.xy/clip.w*.5+.5)*viewportSize.xy;
            float screenBlend=smoothstep(screenFocus.z*.70,screenFocus.z*.94,length(pixel-screenFocus.xy));
            float edge=max(abs(pos.x-d.focus.x),abs(pos.z-d.focus.y))/d.focus.w;
            blend=max(screenBlend,smoothstep(.88,.96,edge));
        }
        pos.y=mix(pos.y,coarse*d.view.z,blend);
    }
    gl_Position=d.vp*vec4(pos,1);
}
