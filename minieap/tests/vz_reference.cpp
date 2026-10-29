#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "rg/vz/md5vz.h"
#include "rg/vz/ripemd128.h"
#include "rg/vz/sha1vz.h"
#include "rg/vz/tiger.h"
#include "rg/vz/vz_kdf.h"
#include "rg/vz/whirlpool_variant.h"

static std::string hex(const unsigned char* data, size_t len) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < len; i++) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}
static std::string primitive(int kind, const unsigned char* data, size_t len) {
    using namespace rjsu;
    using namespace rjsu::vz;
    unsigned char out[64];
    switch (kind) {
    case 0: {
        Md5Engine engine(md5vz_config());
        engine.Update(data, len);
        return engine.FinalHex();
    }
    case 1: sha1vz_hash(data, len, out); return hex(out, 20);
    case 2: ripemd128_hash(ripemd128_variant_config(), data, len, out); return hex(out, 16);
    case 3: tiger_hash(tiger_variant_config(), data, len, out); return hex(out, 24);
    default: whirlpool_hash(kVzWhirlpoolIv, data, len, out); return hex(out, 64);
    }
}
int main(int argc, char** argv) {
    if (argc == 2 && !std::strcmp(argv[1], "--vectors")) {
        std::puts("/* Frozen from Desktop/reconstruction/src/rg/vz; no original-client runtime claim. */");
        std::puts("static const struct { const char *challenge, *digest; } vz_vectors[] = {");
        for (int n = 0; n < 16; n++) {
            std::array<unsigned char,16> challenge{};
            if (n < 5) challenge[0] = n;
            else if (n == 5 || n == 10) challenge.fill(255);
            else if (n == 6 || n == 7)
                for (int i = 0; i < 16; i++) challenge[i] = (n == 6 ? 0 : 128) + i;
            else if (n == 8)
                for (int i = 0; i < 16; i++) challenge[i] = i % 2 ? 0 : 128;
            else if (n == 9) challenge[0] = 128;
            else { challenge.fill(128); challenge[0] += (n - 11 - 256 % 5 + 5) % 5; }
            std::array<unsigned char,128> out{};
            rjsu::vz::vz_api_v3_segment(challenge, &out);
            std::printf("    {\"%s\", \"%.*s\"},\n", hex(challenge.data(),16).c_str(),128,out.data());
        }
        std::puts("};");
        std::puts("static const size_t primitive_lengths[] = {0,1,3,31,32,55,56,63,64,65,127,128,129};");
        std::puts("static const char *primitive_vectors[5][13] = {");
        unsigned char data[129];
        for (size_t i = 0; i < sizeof(data); i++) data[i] = (i * 73 + 11) & 255;
        const size_t lengths[] = {0,1,3,31,32,55,56,63,64,65,127,128,129};
        for (int kind = 0; kind < 5; kind++) {
            std::puts("    {");
            for (size_t len : lengths) std::printf("        \"%s\",\n", primitive(kind,data,len).c_str());
            std::puts("    },");
        }
        std::puts("};");
        return 0;
    }
    char challenge_hex[33], candidate[129];
    unsigned count = 0, modes[5] = {};
    while (std::scanf("%32s %128s", challenge_hex, candidate) == 2) {
        std::array<unsigned char,16> challenge{};
        if (std::strlen(challenge_hex) != 32 || std::strlen(candidate) != 128) return 2;
        for (int i = 0; i < 16; i++) {
            unsigned value;
            if (std::sscanf(challenge_hex + 2*i,"%2x",&value) != 1) return 2;
            challenge[i] = value;
        }
        std::array<unsigned char,128> expected{};
        rjsu::vz::vz_api_v3_segment(challenge, &expected);
        if (std::memcmp(candidate,expected.data(),128)) {
            std::fprintf(stderr,"V3 mismatch for %s\n",challenge_hex);
            return 1;
        }
        modes[(challenge[0]+challenge[3])%5]++;
        count++;
    }
    if (!count) return 2;
    std::printf("reference differential: %u challenges passed; modes %u/%u/%u/%u/%u\n",
                count,modes[0],modes[1],modes[2],modes[3],modes[4]);
}
