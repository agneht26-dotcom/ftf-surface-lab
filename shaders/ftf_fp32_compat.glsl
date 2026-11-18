#ifndef FTF_FP32_COMPAT_GLSL
#define FTF_FP32_COMPAT_GLSL

#define double float
#define dvec2 vec2
#define dvec3 vec3
#define dvec4 vec4
#define dmat2 mat2
#define dmat3 mat3
#define dmat4 mat4

float ftfPackDouble2x32(uvec2 v){
    uint sign = v.y & 0x80000000u;
    int exp11 = int((v.y >> 20u) & 0x7ffu);
    if(exp11 == 0) return 0.0;
    if(exp11 == 0x7ff) return uintBitsToFloat(sign | 0x7f800000u | (((v.y & 0xfffffu) != 0u || v.x != 0u) ? 1u : 0u));
    int exp8 = exp11 - 896;
    uint frac23 = ((v.y & 0xfffffu) << 3u) | (v.x >> 29u);
    if(exp8 >= 255) return uintBitsToFloat(sign | 0x7f7fffffu);
    if(exp8 <= 0){
        if(exp8 < -23) return 0.0;
        float m = uintBitsToFloat(sign | 0x3f800000u | frac23);
        return ldexp(m, exp11 - 1023);
    }
    return uintBitsToFloat(sign | (uint(exp8) << 23u) | frac23);
}

uvec2 ftfUnpackDouble2x32(float x){
    uint b = floatBitsToUint(x);
    uint sign = b & 0x80000000u;
    int exp8 = int((b >> 23u) & 0xffu);
    uint frac23 = b & 0x7fffffu;
    if(exp8 == 0 && frac23 == 0u) return uvec2(0u, sign);
    if(exp8 == 0xff) return uvec2(0u, sign | 0x7ff00000u | (frac23 >> 3u));
    int exp11 = exp8 + 896;
    if(exp8 == 0){
        int k = findMSB(frac23);
        frac23 = (frac23 << uint(23 - k)) & 0x7fffffu;
        exp11 -= (23 - k);
    }
    return uvec2(frac23 << 29u, sign | (uint(exp11) << 20u) | (frac23 >> 3u));
}

#define packDouble2x32 ftfPackDouble2x32
#define unpackDouble2x32 ftfUnpackDouble2x32

#endif
