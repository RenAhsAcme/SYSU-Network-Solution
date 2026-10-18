#include "misc.h"
#include <stdlib.h>
#include <string.h>
void chk_free(void **ptr) { if (ptr) { free(*ptr); *ptr = NULL; } }
uint8_t bit_reverse(uint8_t n) {
    n = (n >> 4) | (n << 4);
    n = ((n & 0xcc) >> 2) | ((n & 0x33) << 2);
    return ((n & 0xaa) >> 1) | ((n & 0x55) << 1);
}
void *memdup(const void *src, int n) {
    if (!src || n <= 0) return NULL;
    void *p = malloc((size_t)n);
    if (p) memcpy(p, src, (size_t)n);
    return p;
}
