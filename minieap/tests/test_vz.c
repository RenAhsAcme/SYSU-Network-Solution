#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "checkV4.h"
#include "rjmd5.h"
#include "rjsha1.h"
#include "rjripemd128.h"
#include "rjtiger.h"
#include "rjwhirlpool.h"
#include "vz_reference.h"

static unsigned tests;
static void to_hex(const uint8_t *bytes, size_t len, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i*2] = digits[bytes[i] >> 4];
        out[i*2+1] = digits[bytes[i] & 15];
    }
    out[len*2] = 0;
}
static void from_hex(const char *text, uint8_t *out) {
    for (size_t i = 0; i < strlen(text)/2; i++) {
        unsigned value;
        assert(sscanf(text+2*i,"%2x",&value) == 1);
        out[i] = value;
    }
}
static void verify(int ok, const char *label) {
    tests++;
    if (!ok) { fprintf(stderr,"failed: %s\n",label); exit(1); }
}
static void primitive(int kind, const uint8_t *data, size_t len, size_t split, char *out) {
    uint8_t digest[64];
    size_t size = 0;
#define HASH_CASE(n, type, init, update, final, bytes) \
    case n: { \
        type ctx; \
        init(&ctx); \
        update(&ctx,data,split); \
        update(&ctx,data+split,len-split); \
        final(&ctx,digest); \
        size = bytes; \
        break; \
    }
    switch (kind) {
    HASH_CASE(0, md5_ctx, rhash_md5_init, rhash_md5_update, rhash_md5_final, 16)
    HASH_CASE(1, sha1_ctx, rhash_sha1_init, rhash_sha1_update, rhash_sha1_final, 20)
    HASH_CASE(2, struct ampheck_ripemd128, ampheck_ripemd128_init,
              ampheck_ripemd128_update, ampheck_ripemd128_finish, 16)
    HASH_CASE(3, tiger_ctx, rhash_tiger_init, rhash_tiger_update, rhash_tiger_final, 24)
    HASH_CASE(4, whirlpool_ctx, rhash_whirlpool_init, rhash_whirlpool_update, rhash_whirlpool_final, 64)
    default: abort();
    }
#undef HASH_CASE
    to_hex(digest,size,out);
}
static void dump_matrix(void) {
    uint32_t rng = 0x19c33a71;
    for (unsigned n = 0; n < 1024; n++) {
        uint8_t challenge[16];
        for (int i = 0; i < 16; i++) {
            rng = rng * 1664525u + 1013904223u;
            challenge[i] = rng >> 24;
        }
        if (n < sizeof(vz_vectors)/sizeof(*vz_vectors))
            from_hex(vz_vectors[n].challenge,challenge);
        else if (n < 272) { challenge[0] = n-16; challenge[3] = n*17; }
        char text[33];
        to_hex(challenge,16,text);
        unsigned char *digest = computeV4(challenge,16);
        if (!digest) exit(1);
        printf("%s %.*s\n",text,128,digest);
    }
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1],"--dump")) { dump_matrix(); return 0; }
    uint8_t storage[160] = {0}, *data = storage+1;
    for (size_t i = 0; i < 129; i++) data[i] = i*73+11;
    for (int kind = 0; kind < 5; kind++) {
        for (size_t i = 0; i < sizeof(primitive_lengths)/sizeof(*primitive_lengths); i++) {
            size_t len = primitive_lengths[i];
            for (size_t split = 0; split <= len; split++) {
                char out[129];
                primitive(kind,data,len,split,out);
                verify(!strcmp(out,primitive_vectors[kind][i]),"primitive reference/unaligned/streaming");
            }
        }
    }
    for (size_t i = 0; i < sizeof(vz_vectors)/sizeof(*vz_vectors); i++) {
        uint8_t challenge[17] = {0};
        from_hex(vz_vectors[i].challenge,challenge+1);
        unsigned char *out = computeV4(challenge+1,16);
        verify(out && !memcmp(out,vz_vectors[i].digest,128),"V3 reference/unaligned");
        verify(computeV4(challenge+1,17) && !memcmp(out,vz_vectors[i].digest,128),"legacy length compatibility");
    }
    verify(computeV4(NULL,16) == NULL,"null challenge");
    for (int len = -1; len < 16; len++) verify(computeV4(data,len) == NULL,"short challenge");
    printf("V3 tests: %u assertions passed; endian: %s; char: %s\n",tests,
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
           "big",
#else
           "little",
#endif
           (char)255 < 0 ? "signed" : "unsigned");
    return 0;
}
