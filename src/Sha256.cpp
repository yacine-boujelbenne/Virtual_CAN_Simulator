#include "Sha256.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
constexpr uint32_t kRoundConstants[64]={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
inline uint32_t rotateRight(uint32_t value,unsigned bits) {return (value>>bits)|(value<<(32-bits));}
}

std::string sha256Hex(const std::string &data) {
    std::array<uint32_t,8> state{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::vector<uint8_t> message(data.begin(),data.end());
    uint64_t bitLength=static_cast<uint64_t>(data.size())*8;
    message.push_back(0x80);
    while(message.size()%64!=56) message.push_back(0x00);
    for(int shift=56;shift>=0;shift-=8) message.push_back(static_cast<uint8_t>((bitLength>>shift)&0xff));
    std::array<uint32_t,64> schedule{};
    for(size_t offset=0;offset<message.size();offset+=64) {
        for(size_t index=0;index<16;++index)
            schedule[index]=(static_cast<uint32_t>(message[offset+index*4])<<24)|(static_cast<uint32_t>(message[offset+index*4+1])<<16)|
                (static_cast<uint32_t>(message[offset+index*4+2])<<8)|static_cast<uint32_t>(message[offset+index*4+3]);
        for(size_t index=16;index<64;++index) {
            uint32_t s0=rotateRight(schedule[index-15],7)^rotateRight(schedule[index-15],18)^(schedule[index-15]>>3);
            uint32_t s1=rotateRight(schedule[index-2],17)^rotateRight(schedule[index-2],19)^(schedule[index-2]>>10);
            schedule[index]=schedule[index-16]+s0+schedule[index-7]+s1;
        }
        uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for(size_t index=0;index<64;++index) {
            uint32_t s1=rotateRight(e,6)^rotateRight(e,11)^rotateRight(e,25);
            uint32_t choice=(e&f)^((~e)&g);
            uint32_t temp1=h+s1+choice+kRoundConstants[index]+schedule[index];
            uint32_t s0=rotateRight(a,2)^rotateRight(a,13)^rotateRight(a,22);
            uint32_t majority=(a&b)^(a&c)^(b&c);
            uint32_t temp2=s0+majority;
            h=g;g=f;f=e;e=d+temp1;d=c;c=b;b=a;a=temp1+temp2;
        }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    static const char *digits="0123456789abcdef";
    std::string out;
    out.reserve(64);
    for(uint32_t word:state)for(int shift=28;shift>=0;shift-=4) out.push_back(digits[(word>>shift)&0xf]);
    return out;
}
