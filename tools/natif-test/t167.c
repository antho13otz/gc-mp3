/* v167 : morceaux du BSS a mettre a zero (na_bss_gaps de natif.c), sur la disposition de Crash Bandicoot GCBP7D */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8; typedef uint32_t u32;
typedef struct { u32 bss, bsz; } nat_info_t;
static u32 be32(const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
#include "na_bss_gaps.inc"
static void put(u8 *h, int i, u32 a, u32 sz) { u8 *p = h + 0x48 + i * 4, *q = h + 0x90 + i * 4; p[0] = a >> 24; p[1] = a >> 16; p[2] = a >> 8; p[3] = a; q[0] = sz >> 24; q[1] = sz >> 16; q[2] = sz >> 8; q[3] = sz; }
int main(void) {
    u8 h[0x100]; memset(h, 0, sizeof(h));
    put(h, 0, 0x80003100, 416); put(h, 1, 0x800032A0, 1055040); put(h, 7, 0x80104BE0, 132000); put(h, 8, 0x80124F80, 308288);
    put(h, 9, 0x803D6D00, 6528); put(h, 10, 0x803D9680, 4512);
    nat_info_t in = { 0x801703C0, 2527924 };
    u32 t[60]; u32 n = na_bss_gaps(h, &in, 0x817D6F40, t, 20);
    int ok = n == 2 && t[0] == 0x801703C0 && t[1] == 0 && t[2] == 0x803D6D00 - 0x801703C0 && t[3] == 0x803D8680 && t[5] == 0x803D9674 - 0x803D8680;
    for (u32 i = 0; i < n; i++) printf("  zero %08X  %u octets\n", (unsigned)t[i * 3], (unsigned)t[i * 3 + 2]);
    /* BSS au-dessus du plafond : coupe ; BSS entierement couvert : rien */
    nat_info_t in2 = { 0x80300000, 0x100000 }; u8 h2[0x100]; memset(h2, 0, sizeof(h2)); put(h2, 0, 0x80300000, 0x100000);
    ok &= na_bss_gaps(h2, &in2, 0x817D0000, t, 20) == 0;
    nat_info_t in3 = { 0x81700000, 0x200000 }; memset(h2, 0, sizeof(h2));
    ok &= na_bss_gaps(h2, &in3, 0x81780000, t, 20) == 1 && t[2] == 0x80000;
    puts(ok ? "OK   BSS mis a zero hors des sections (Crash Bandicoot GCBP7D)\nTOUT OK" : "FAUX BSS a mettre a zero");
    return !ok;
}
