#pragma once
// Minecraft 1.21.1 random sources, bit-exact: XoroshiroRandomSource, its positional factory and
// RandomSupport's seed upgrade / MD5 hashOf. Used for per-seed tables (FTF strata stacks, NormalNoise
// permutations) that the GPU then reads.
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace mc {

inline uint64_t mixStafford13(uint64_t z){z=(z^(z>>30))*0xBF58476D1CE4E5B9ull;z=(z^(z>>27))*0x94D049BB133111EBull;return z^(z>>31);}

// RandomSupport.seedFromHashOf: MD5 of the UTF-8 string, read as two big-endian longs.
inline std::array<uint64_t,2> md5Seed(const std::string& text){
    static const uint32_t K[64]={0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const int S[64]={7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    std::string m=text;uint64_t bits=uint64_t(text.size())*8;m.push_back(char(0x80));while(m.size()%64!=56)m.push_back(0);for(int i=0;i<8;++i)m.push_back(char((bits>>(8*i))&255));
    uint32_t h[4]={0x67452301,0xefcdab89,0x98badcfe,0x10325476};
    for(size_t off=0;off<m.size();off+=64){uint32_t w[16];for(int i=0;i<16;++i){w[i]=0;for(int b=0;b<4;++b)w[i]|=uint32_t(uint8_t(m[off+i*4+b]))<<(8*b);}
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3];
        for(int i=0;i<64;++i){uint32_t f;int g;if(i<16){f=(b&c)|(~b&d);g=i;}else if(i<32){f=(d&b)|(~d&c);g=(5*i+1)%16;}else if(i<48){f=b^c^d;g=(3*i+5)%16;}else{f=c^(b|~d);g=(7*i)%16;}
            uint32_t t=d;d=c;c=b;b=b+std::rotl(a+f+K[i]+w[g],S[i]);a=t;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;}
    uint8_t digest[16];for(int i=0;i<4;++i)for(int b=0;b<4;++b)digest[i*4+b]=uint8_t(h[i]>>(8*b));
    uint64_t lo=0,hi=0;for(int i=0;i<8;++i){lo=(lo<<8)|digest[i];hi=(hi<<8)|digest[8+i];}
    return {lo,hi};
}

class Xoroshiro {
public:
    uint64_t lo,hi;
    Xoroshiro(uint64_t l,uint64_t h):lo(l),hi(h){if((lo|hi)==0){lo=0x9E3779B97F4A7C15ull;hi=0x6A09E667F3BCC909ull;}}   // Xoroshiro128PlusPlus: an all-zero state is replaced
    static Xoroshiro fromSeed(int64_t seed){uint64_t l=uint64_t(seed)^0x6A09E667F3BCC909ull,h=l+0x9E3779B97F4A7C15ull;return {mixStafford13(l),mixStafford13(h)};}   // new XoroshiroRandomSource(long)
    uint64_t nextLong(){uint64_t l=lo,m=hi,n=std::rotl(l+m,17)+l;m^=l;lo=std::rotl(l,49)^m^(m<<21);hi=std::rotl(m,28);return n;}
    int32_t nextInt(){return int32_t(uint32_t(nextLong()));}
    int32_t nextInt(int32_t bound){   // XoroshiroRandomSource.nextInt(int): Lemire's method
        uint64_t l=uint32_t(nextInt()),m=l*uint64_t(bound),n=m&0xFFFFFFFFull;
        if(n<uint64_t(bound)){uint32_t i=uint32_t(-bound)%uint32_t(bound);while(n<i){l=uint32_t(nextInt());m=l*uint64_t(bound);n=m&0xFFFFFFFFull;}}
        return int32_t(int64_t(m)>>32);
    }
    float nextFloat(){return float(nextLong()>>40)*5.9604645E-8f;}
    double nextDouble(){return double(nextLong()>>11)*1.1102230246251565E-16;}
    struct Positional {uint64_t lo,hi;
        Xoroshiro fromHashOf(const std::string& name)const{auto s=md5Seed(name);return {s[0]^lo,s[1]^hi};}
        Xoroshiro at(int x,int y,int z)const{int64_t l=int64_t(int64_t(int32_t(uint32_t(x)*3129871u))^(int64_t(z)*116129781ll)^int64_t(y));uint64_t u=uint64_t(l);u=u*u*42317861ull+u*11ull;return {uint64_t(int64_t(u)>>16)^lo,hi};}   // Mth.getSeed
    };
    Positional forkPositional(){uint64_t a=nextLong(),b=nextLong();return {a,b};}
};

// LegacyRandomSource (java.util.Random's LCG) with BitRandomSource's nextInt/nextDouble; WorldgenRandom delegates to it.
class Legacy {
public:
    uint64_t seed;
    explicit Legacy(int64_t s):seed((uint64_t(s)^0x5DEECE66Dull)&((1ull<<48)-1)){}
    int32_t next(int bits){seed=(seed*0x5DEECE66Dull+11ull)&((1ull<<48)-1);return int32_t(int64_t(seed)>>(48-bits));}
    int32_t nextInt(int32_t bound){if((bound&(bound-1))==0)return int32_t((int64_t(bound)*int64_t(next(31)))>>31);int32_t i,j;do{i=next(31);j=i%bound;}while(int32_t(uint32_t(i)-uint32_t(j)+uint32_t(bound-1))<0);return j;}
    double nextDouble(){int64_t i=next(26),j=next(27);return double((i<<27)+j)*1.1102230246251565E-16;}
};
// SimplexNoise(RandomSource): offsets, then a 256-entry permutation (the 2D getValue only reads the permutation).
template<class R> std::array<uint8_t,256> simplexPermutation(R& random){random.nextDouble();random.nextDouble();random.nextDouble();std::array<uint8_t,256> p{};for(int i=0;i<256;++i)p[i]=uint8_t(i);for(int i=0;i<256;++i){int j=random.nextInt(256-i);std::swap(p[i],p[i+j]);}return p;}
// ImprovedNoise(RandomSource): xo, yo, zo, then the byte permutation.
struct Improved {double xo,yo,zo;std::array<uint8_t,256> p;};
inline Improved improvedNoise(Xoroshiro random){Improved n{};n.xo=random.nextDouble()*256.0;n.yo=random.nextDouble()*256.0;n.zo=random.nextDouble()*256.0;for(int i=0;i<256;++i)n.p[i]=uint8_t(i);for(int i=0;i<256;++i){int j=random.nextInt(256-i);std::swap(n.p[i],n.p[i+j]);}return n;}

// RandomState: root = XOROSHIRO.newInstance(seed).forkPositional(); getOrCreateRandomFactory(id) = root.fromHashOf(id).forkPositional().
inline Xoroshiro::Positional randomStateRoot(int64_t seed){return Xoroshiro::fromSeed(seed).forkPositional();}
inline Xoroshiro::Positional randomFactory(const Xoroshiro::Positional& root,const std::string& id){return root.fromHashOf(id).forkPositional();}

}
