#version 450
#extension GL_GOOGLE_include_directive : require
#define FTF_NO_GRAPH_CELL_HELPERS
#include "cell_state.glsl"
layout(location=0) in vec3 color;
layout(location=1) in vec3 normal;
layout(location=2) in float altitude;
layout(location=3) in float surfaceY;
layout(location=4) in vec2 worldXZ;
layout(location=0) out vec4 outColor;
layout(rgba8,set=0,binding=2) uniform readonly image2D terrainColors;
layout(rgba32f,set=0,binding=1) uniform readonly image2D surfaceField;
layout(rgba32f,set=0,binding=8) uniform readonly image2D coarseField;
layout(std430,set=0,binding=9) readonly buffer ViewEffects {vec4 effects;vec4 screenFocus;vec4 viewportSize;vec4 sun;vec4 sunShadow;uvec4 terrainMask;vec4 biomeSearchArea;vec4 scaleCue;vec4 detailHole;};
layout(push_constant) uniform Draw { mat4 vp; vec4 view; uvec4 meta; vec4 focus; vec4 limits; } d;
layout(std430,set=0,binding=7) readonly buffer BiomeLookup {uint words[];} biomeLookup;
uint canonicalBiome(uint biome){if(biome>=biomeLookup.words[1])return 0xffffffffu;uint footer=uint(biomeLookup.words.length())-8u;if(biomeLookup.words[footer]!=0x4654464du)return biome;uint alias=biomeLookup.words[biomeLookup.words[footer+6u]+biome];return (alias&131072u)!=0u?alias&65535u:biome;}
float sampleHeight(vec2 world){
    vec2 p=world-d.limits.zw;float extent=d.view.x;
    bool coarse=d.focus.w>0.0&&(length(p)>d.focus.w||any(greaterThan(abs(p),vec2(extent*.5))));
    if(coarse){if(detailHole.w<.5)p+=d.focus.xy;extent=d.focus.z;}
    ivec2 size=coarse?imageSize(coarseField):imageSize(surfaceField);
    vec2 f=clamp(p/extent+.5,vec2(0),vec2(1))*vec2(size-1);ivec2 a=ivec2(f),b=min(a+1,size-1);vec2 t=fract(f);
    float h00=coarse?imageLoad(coarseField,a).x:imageLoad(surfaceField,a).x;
    float h10=coarse?imageLoad(coarseField,ivec2(b.x,a.y)).x:imageLoad(surfaceField,ivec2(b.x,a.y)).x;
    float h01=coarse?imageLoad(coarseField,ivec2(a.x,b.y)).x:imageLoad(surfaceField,ivec2(a.x,b.y)).x;
    float h11=coarse?imageLoad(coarseField,b).x:imageLoad(surfaceField,b).x;
    float h=mix(mix(h00,h10,t.x),mix(h01,h11,t.x),t.y);
    if((d.meta.y&256u)==0u)h=max(h,d.view.y);return h*d.view.z;
}
float terrainAO(vec3 n){
    float spacing=d.view.x/float(d.meta.x-1u),radius=max(96.0,spacing*6.0);
    float base=sampleHeight(worldXZ),occlusion=0.0;
    for(int direction=0;direction<8;++direction){
        float angle=6.2831853*(float(direction)/8.0);vec2 ray=vec2(cos(angle),sin(angle));float horizon=0.0;
        for(int step=1;step<=4;++step){float distance=radius*float(step*step)/16.0;
            float rise=sampleHeight(worldXZ+ray*distance)-base;
            // Remove the local tangent slope so flat slopes do not self-occlude.
            float tangent=-dot(n.xz,ray)/max(.15,n.y);
            horizon=max(horizon,(rise-3.0)/distance-tangent);
        }
        occlusion+=horizon/sqrt(1.0+horizon*horizon);
    }
    return clamp(1.0-occlusion*.085*effects.x,.05,1.0);
}
float hashCloud(vec2 p){uvec2 key=uvec2(ivec2(p));uint h=key.x*1597334677u^key.y*3812015801u;h^=h>>16;h*=2246822519u;h^=h>>13;h*=3266489917u;h^=h>>16;return float(h&0x00ffffffu)/16777215.0;}
float cloudNoise(vec2 p){vec2 a=floor(p),t=fract(p);t=t*t*(3.0-2.0*t);return mix(mix(hashCloud(a),hashCloud(a+vec2(1,0)),t.x),mix(hashCloud(a+vec2(0,1)),hashCloud(a+vec2(1,1)),t.x),t.y);}
float cloudShade(){
    float time=effects.z*effects.w;
    vec2 p=(worldXZ+vec2(time*120.0,time*45.0))/2100.0;
    float density=cloudNoise(p)*.65+cloudNoise(p*2.03+17.2)*.25+cloudNoise(p*4.07-8.3)*.10;
    return 1.0-effects.y*smoothstep(.40,.68,density);
}
float terrainSunShadow(){
    if(sunShadow.w<=0.0||sunShadow.x<=0.0||sun.y<=.01)return 1.0;
    float horizontal=length(sun.xz);if(horizontal<.01)return 1.0;
    vec2 ray=sun.xz/horizontal;float tangent=sun.y/horizontal,origin=sampleHeight(worldXZ)+1.5,visibility=1.0;
    for(int step=1;step<=24;++step){float f=float(step)/24.0,distance=sunShadow.y*f*f;vec2 point=worldXZ+ray*distance;
        vec2 local=point-d.limits.zw;if(d.focus.w>0.0)local+=d.focus.xy;float extent=d.focus.w>0.0?d.focus.z:d.view.x;if(any(greaterThan(abs(local),vec2(extent*.5))))break;
        float separation=origin+distance*tangent-sampleHeight(point);
        visibility=min(visibility,clamp(separation/max(1.0,distance*sunShadow.z*.12),0.0,1.0));
    }
    return mix(1.0,visibility,sunShadow.x);
}
// Scale cue for walk mode: a faint 1-block checker with block lines and brighter 16-block (chunk) lines near the player.
vec3 applyScaleGrid(vec3 base){
    float near=length(worldXZ-scaleCue.xz);float fade=1.0-smoothstep(24.0,160.0,near);if(fade<=0.0||scaleCue.w<=0.0)return base;
    vec2 w=max(fwidth(worldXZ),vec2(1e-5));float pixelsPerBlock=1.0/max(w.x,w.y);float blockVisible=smoothstep(2.5,7.0,pixelsPerBlock);
    vec2 cell=floor(worldXZ);float checker=mod(cell.x+cell.y,2.0);
    vec2 f=fract(worldXZ);vec2 edge=min(f,1.0-f)/w;float blockLine=1.0-clamp(min(edge.x,edge.y),0.0,1.0);
    vec2 chunk=mod(worldXZ,16.0);vec2 chunkEdge=min(chunk,16.0-chunk)/w;float chunkLine=1.0-clamp(min(chunkEdge.x,chunkEdge.y)/1.5,0.0,1.0);
    vec3 result=base*(1.0-.07*checker*blockVisible);result=mix(result,result*.62,blockLine*.5*blockVisible);result=mix(result,vec3(1.0,.82,.28),chunkLine*.40*smoothstep(.15,.7,pixelsPerBlock));
    return mix(base,result,fade*scaleCue.w);
}
void main(){
    if(detailHole.z>0.0&&distance(worldXZ,detailHole.xy)<detailHole.z)discard;
    if(d.focus.w!=0.0){
        vec2 local=worldXZ-d.limits.zw;
        if(screenFocus.w>0.0){
            float screenDistance=length(gl_FragCoord.xy-screenFocus.xy);
            if(d.focus.w>0.0){if(screenDistance>screenFocus.z||any(greaterThan(abs(local+d.focus.xy),vec2((detailHole.w>.5?detailHole.w:d.focus.z)*.5))))discard;}
            else if(screenDistance<screenFocus.z&&all(lessThan(abs(local-d.focus.xy),vec2(-d.focus.w*.96))))discard;
        }else{
            float distanceFromCenter=d.focus.w>0.0?length(local):length(local-d.focus.xy);
            if(d.focus.w>0.0&&(distanceFromCenter>d.focus.w||any(greaterThan(abs(local+d.focus.xy),vec2((detailHole.w>.5?detailHole.w:d.focus.z)*.5)))))discard;
            if(d.focus.w<0.0&&distanceFromCenter<-d.focus.w)discard;
        }
    }
    bool terrainMatch=true;
    if(terrainMask.z!=0u&&(d.meta.z==4u||d.meta.z==5u)){
        ivec2 q=ivec2(clamp((worldXZ-d.limits.zw)/d.view.x+.5,vec2(0),vec2(1))*float(d.meta.x-1u));
        uint assignment=uint(round(imageLoad(terrainColors,q).a*255.0));uint full=d.meta.z==5u?cells[uint(q.y)*d.meta.x+uint(q.x)].terrain:0u,role=full&255u;
        terrainMatch=terrainMask.w>=200u?(canonicalBiome(cells[uint(q.y)*d.meta.x+uint(q.x)].biome)==canonicalBiome(terrainMask.w-200u)&&distance(worldXZ,biomeSearchArea.xy)<=biomeSearchArea.z):terrainMask.w>=100u?role==terrainMask.w-100u:assignment<32u?(terrainMask.x&(1u<<assignment))!=0u:assignment<64u?(terrainMask.y&(1u<<(assignment-32u)))!=0u:false;
        if(terrainMask.w==112u)terrainMatch=role==12u||(role==10u&&(full&512u)!=0u)||(role!=4u&&role!=5u&&role!=6u&&role!=8u&&role!=10u&&role!=11u&&imageLoad(surfaceField,q).x<d.view.y);   /* lakes: widened river stretches, and inland ground below sea level (Minecraft fills it with water) */
        if(terrainMask.w==110u)terrainMatch=role==10u&&(full&512u)==0u;
        if(terrainMask.w==9u&&role==2u)terrainMatch=true;if(terrainMask.w==12u&&role==3u)terrainMatch=true;
        if((terrainMask.z&2u)!=0u&&!terrainMatch)discard;
    }
    vec3 n=normalize(normal);vec3 c=applyScaleGrid(color);
    if(d.meta.z!=5u&&(d.meta.y&255u)==0u&&altitude>6.0){
        float rock=1.0-smoothstep(.38,.72,n.y);
        c=mix(c,vec3(.42,.43,.40),rock*.7);
        c=mix(c,vec3(.89,.91,.88),smoothstep(350.0,520.0,altitude));
    }
    float light=.53+.52*sun.w*max(0,dot(n,normalize(sun.xyz)))*(d.view.w>0.0?terrainSunShadow():1.0);
    vec3 shaded=pow(max(c*mix(1.0,light,d.view.w),vec3(0)),vec3(d.meta.z==5u?1.0:.82));
    if(d.meta.z!=3u&&d.meta.z!=6u){if((d.meta.y&512u)!=0u&&effects.x>0.0)shaded*=terrainAO(n);if((d.meta.y&1024u)!=0u&&effects.y>0.0)shaded*=cloudShade();}
    if((terrainMask.z&1u)!=0u&&(d.meta.z==4u||d.meta.z==5u)){if(terrainMatch)shaded=mix(shaded,vec3(1.0,.72,.12),.60);else shaded*=.38;}
    float stripe=step(.5,fract((worldXZ.x+worldXZ.y)*128.0/max(1.0,d.view.x)));
    if((d.meta.y&4096u)!=0u&&surfaceY>d.limits.y)shaded=mix(shaded,vec3(1.0,.70,.10),mix(.35,.75,1.0-stripe));  // amber: above the chosen height
    if((d.meta.y&2048u)!=0u&&surfaceY>d.limits.x)shaded=mix(shaded,vec3(1.0,.045,.09),mix(.4,.85,stripe));     // red: squeezed by FTF's ceiling
    // The teal ring that used to outline the high-detail patch was a purely visual overlay and has been removed.
    outColor=vec4(shaded,1);
}
