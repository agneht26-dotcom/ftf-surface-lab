#version 450
layout(std430,set=0,binding=9) readonly buffer ViewEffects {vec4 effects;vec4 screenFocus;vec4 viewportSize;vec4 sun;};
layout(location=0) in vec3 color;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 crownUV;layout(location=3) flat in uint overview;
layout(location=0) out vec4 outColor;
void main(){
    vec3 base=color;if(overview==2u){if(length(crownUV-vec2(.5))>.48)discard;}else if(overview!=0u){float x=abs(crownUV.x-.5),y=crownUV.y;bool canopy=length(vec2((crownUV.x-.5)*2.0,(y-.66)*2.7))<.95;if(!canopy){if(y>.52||x>.055)discard;base=vec3(.35,.24,.14);}}
    vec3 n=normalize(normal);float light=.53+.52*sun.w*max(0.0,dot(n,normalize(sun.xyz)));
    outColor=vec4(max(base*light,vec3(0)),1);
}
