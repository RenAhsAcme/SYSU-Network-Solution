#ifndef SYSU_MISC_H
#define SYSU_MISC_H
#include <stdint.h>
void chk_free(void **ptr);
uint8_t bit_reverse(uint8_t n);
void *memdup(const void *src, int n);
#endif
