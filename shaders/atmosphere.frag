#version 450
// Second sub-pass of the viewport render pass. Reads the shaded scene and its depth as input attachments and writes the
// final image. With every atmosphere option off it is an exact copy of the scene. Render-only: nothing here feeds generation.
layout(input_attachment_index=0,set=1,binding=0) uniform subpassInput sceneColor;
layout(input_attachment_index=1,set=1,binding=1) uniform subpassInput sceneDepth;
layout(rgba32f,set=0,binding=1) uniform readonly image2D surfaceField;
layout(std430,set=0,binding=9) readonly buffer ViewEffects {
    vec4 effects;vec4 screenFocus;vec4 viewportSize;vec4 sun;vec4 sunShadow;uvec4 terrainMask;vec4 scaleCue;vec4 detailHole;
    vec4 atmoWater;   // enabled, ripple strength, reflection strength, sea level (scene units)
    vec4 atmoFog;     // enabled, density per 1000 blocks, height falloff per 1000 blocks, fog base height
    vec4 atmoCloud;   // enabled, coverage, density, base height
    vec4 atmoCloud2;  // (unused), wind speed, scale (blocks per noise cell), vertical exaggeration
};
layout(push_constant) uniform Post {mat4 inverseViewProjection;vec4 eye;vec4 domain;vec4 clip;} pc; // inverseViewProjection is camera-relative (no eye translation); clip: near, far // eye: xyz position, w time; domain: extent, field resolution, unused, unused
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;

float hash21(vec2 p){uvec2 q=uvec2(ivec2(floor(p)));uint h=q.x*1597334677u^q.y*3812015801u;h=(h^(h>>16))*2246822519u;h^=h>>13;h*=3266489917u;h^=h>>16;return float(h&0xffffffu)/16777216.0;}   /* integer hash: the fract() hash made rows of related values (straight streaks in the flat cloud layer) */
float valueNoise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);return mix(mix(hash21(i),hash21(i+vec2(1,0)),f.x),mix(hash21(i+vec2(0,1)),hash21(i+vec2(1,1)),f.x),f.y);}
float fbm(vec2 p){float a=.5,s=0.0;for(int i=0;i<4;++i){s+=a*valueNoise(p);p=p*2.03+vec2(17.1,9.7);a*=.5;}return s;}

vec3 skyColor(vec3 dir,vec3 sunDir){
    float day=clamp(sunDir.y*2.2+.35,.12,1.0);float h=clamp(dir.y,0.0,1.0);
    vec3 zenith=vec3(.16,.34,.74),horizon=vec3(.70,.82,.93);vec3 color=mix(horizon,zenith,pow(h,.5));
    float s=max(dot(dir,sunDir),0.0);color+=vec3(1.0,.84,.60)*(pow(s,6.0)*.14+pow(s,500.0)*3.0);
    return color*day;
}
vec3 unproject(vec2 ndc,float depth){vec4 w=pc.inverseViewProjection*vec4(ndc,depth,1.0);return w.xyz/w.w;}

float fieldHeight(vec2 xz){
    vec2 coord=clamp(xz/pc.domain.x+.5,vec2(0),vec2(1))*(pc.domain.y-1.0);ivec2 size=ivec2(pc.domain.y);ivec2 a=min(ivec2(coord),size-2);vec2 t=coord-vec2(a);
    float h00=imageLoad(surfaceField,a).x,h10=imageLoad(surfaceField,a+ivec2(1,0)).x,h01=imageLoad(surfaceField,a+ivec2(0,1)).x,h11=imageLoad(surfaceField,a+ivec2(1,1)).x;
    return mix(mix(h00,h10,t.x),mix(h01,h11,t.x),t.y);
}
float rippleHeight(vec2 p,float time){return fbm(p*.18+vec2(time*.35,time*.21))+.5*fbm(p*.55-vec2(time*.5,-time*.4));}
vec3 shadeWater(vec3 scene,vec3 position,vec3 view,vec3 sunDir,float depth){
    float strength=atmoWater.y,distanceFade=1.0/(1.0+length(position-pc.eye.xyz)*.00012);
    // anti-aliasing: ripples smaller than a pixel's footprint on the water would shimmer as the camera moves (walk mode), so fade them out
    float footprint=length(position-pc.eye.xyz)*1.2/max(viewportSize.y,1.0)/max(abs(view.y),.03);distanceFade*=smoothstep(2.5,.4,footprint);
    vec2 p=position.xz;float e=.6;float h=rippleHeight(p,pc.eye.w);
    vec2 gradient=vec2(rippleHeight(p+vec2(e,0),pc.eye.w)-h,rippleHeight(p+vec2(0,e),pc.eye.w)-h)/e;
    vec3 n=normalize(vec3(-gradient.x*strength*.35*distanceFade,1.0,-gradient.y*strength*.35*distanceFade));
    float cosine=clamp(dot(n,view),0.0,1.0),fresnel=.02+.98*pow(1.0-cosine,5.0);
    vec3 reflected=reflect(-view,n);reflected.y=abs(reflected.y);vec3 sky=skyColor(reflected,sunDir);
    float glint=pow(max(dot(reflected,sunDir),0.0),220.0)*sun.w*2.2*mix(.15,1.0,distanceFade);
    vec3 refracted=scene*mix(vec3(.55,.80,.92),vec3(1.0),exp(-depth*.03));            // absorption: deeper water loses red first
    refracted=mix(vec3(.02,.09,.16)*scene+vec3(.01,.04,.07),refracted,exp(-depth*.012));
    return mix(refracted,sky,clamp(fresnel*atmoWater.z,0.0,1.0))+vec3(glint)*atmoWater.z;   // shore foam removed in 1.10 (user: buggy)
}
void main(){
    vec3 scene=subpassLoad(sceneColor).rgb;float depth=subpassLoad(sceneDepth).r;
    bool anyEffect=atmoWater.x>.5||atmoFog.x>.5||atmoCloud.x>.5;
    if(!anyEffect){outColor=vec4(scene,1.0);return;}
    vec2 ndc=gl_FragCoord.xy/viewportSize.xy*2.0-1.0;bool sky=depth>=1.0;
    vec3 eye=pc.eye.xyz;vec3 dir=normalize(unproject(ndc,0.0));vec3 forward=normalize(unproject(vec2(0),0.0)); // near plane: at depth 1.0 the walk near/far ratio (1:1.6e7) cancels to noise in float
    // distance from linear depth (stable with the tiny walk-mode near plane, where unprojecting a depth near 1.0 flickered)
    float n=pc.clip.x,f=pc.clip.y;float travel=sky?3.0e6:(n*f/max(f-depth*(f-n),1e-20))/max(dot(dir,forward),1e-3);
    vec3 sunDir=normalize(sun.xyz);vec3 color=scene;
    if(sky)color=skyColor(dir,sunDir); // any atmosphere option replaces the flat clear colour with the sky the water reflects
    // Water: the sea plane is hit analytically; it is visible when the generated floor there lies below it and nothing is drawn in front.
    if(!sky&&atmoWater.x>.5&&abs(dir.y)>1e-5){float seaLevel=atmoWater.w,exaggeration=max(atmoCloud2.w,1e-4);float t=(seaLevel-eye.y)/dir.y;
        if(t>0.0&&travel>t*.97-2.0){vec3 hit=eye+dir*t;float floorHeight=fieldHeight(hit.xz)*exaggeration;
            if(floorHeight<seaLevel-.02*exaggeration&&abs(hit.x)<pc.domain.x*.5&&abs(hit.z)<pc.domain.x*.5)color=shadeWater(scene,hit,-dir,sunDir,max(0.0,(seaLevel-floorHeight)/exaggeration));}}
    // Exponential height fog, integrated along the ray.
    if(atmoFog.x>.5){float dens=atmoFog.y*.001,fall=atmoFog.z*.001;float t=sky?min(travel,200000.0):travel;
        float baseEye=exp(-fall*max(eye.y-atmoFog.w,0.0)),dy=dir.y*t;float amount;
        if(abs(fall*dy)<1e-3)amount=dens*baseEye*t;else amount=dens*baseEye*(1.0-exp(-fall*dy))/(fall*dir.y);
        float fogFactor=1.0-exp(-max(amount,0.0));vec3 fogColor=skyColor(normalize(vec3(dir.x,.02,dir.z)),sunDir)*.92+vec3(.04);
        color=mix(color,fogColor,clamp(fogFactor,0.0,1.0)*pc.domain.z);}
    // 2D clouds: one flat layer at the cloud height, hit analytically (above or below it), drawn when nothing is in front.
    if(atmoCloud.x>.5&&abs(dir.y)>1e-5){float base=atmoCloud.w;float t=(base-eye.y)/dir.y;
        if(t>0.0&&(sky||t<travel)){vec3 p=eye+dir*t;float cell=max(atmoCloud2.z,100.0);vec2 wind=vec2(1.0,.35)*atmoCloud2.y*pc.eye.w;vec2 q=(p.xz+wind)/cell;
            float footprint=t*length(fwidth(dir))/cell;   /* noise cells per pixel: fade fine detail, then the whole layer, where it would alias */
            float shape=fbm(q)+.35*(fbm(q*3.7)-.5)*(1.0-smoothstep(.15,.6,footprint));
            float coverage=smoothstep(1.0-atmoCloud.y,1.0-atmoCloud.y+.22,shape);
            float alpha=clamp(coverage*atmoCloud.z,0.0,1.0)*(1.0-smoothstep(1.5,6.0,footprint));
            if(alpha>.001){float lit=fbm(q+sunDir.xz*.25);float shade=mix(1.0,.62,smoothstep(.45,.85,lit)*coverage);
                vec3 sunLight=vec3(1.0,.95,.86)*(.55+.45*clamp(sunDir.y*2.0,0.0,1.0));vec3 ambient=skyColor(vec3(0,1,0),sunDir)*.45;
                vec3 cloud=ambient+sunLight*shade*.75;if(eye.y>base)cloud*=1.05;else cloud*=mix(1.0,.78,coverage);   /* undersides a little darker */
                color=mix(color,cloud,alpha*pc.domain.w);}}}
    outColor=vec4(color,1.0);
}
