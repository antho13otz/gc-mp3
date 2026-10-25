/* v146 : bi2.bin (norme des manettes, arguments de Clone Wars) et corrections des .rel : verifications (avec le debut de h2.c) */
int main(void) {
    static u8 b[NA_BI2];
    int ok = 1;
    memset(b, 0, sizeof(b)); na_wbe32(b + 8, 0x1F00); strcpy((char *)b + 0x1F00, "vieux");
    na_bi2_fix(b, "GSXF64");
    u32 a = be32(b + 8), n = be32(b + a);
    printf("args @%X n=%u :", a, n);
    for (u32 i = 0; i < n; i++) printf(" [%s]", (char *)b + be32(b + a + 4 + 4 * i));
    printf(" fin=%u spec=%u\n", be32(b + a + 4 + 4 * n), be32(b + 0x24));
    ok &= n == 4 && !strcmp((char *)b + be32(b + a + 16), "2") && be32(b + a + 4 + 4 * n) == 0 && be32(b + 0x24) == 5 && a < NA_BI2;
    memset(b, 0, sizeof(b)); na_bi2_fix(b, "GSXE64"); a = be32(b + 8);
    ok &= be32(b + a) == 1 && !strcmp((char *)b + be32(b + a + 4), "Clonewars.elf");
    memset(b, 0, sizeof(b)); na_bi2_fix(b, "GALE01"); ok &= be32(b + 8) == 0 && be32(b + 0x24) == 5;
    memset(b, 0, sizeof(b)); na_bi2_fix(b, "DPOJ8P"); ok &= be32(b + 4) == 0x01800000u;
    int nr = 0; for (unsigned i = 0; i < sizeof(na_ffixes) / sizeof(na_ffixes[0]); i++) if (na_id_in(na_ffixes[i].ids, "GHAP08")) nr++;
    printf("corrections .rel GHAP08 : %d ; table : %u lignes\n", nr, (unsigned)(sizeof(na_fixes) / sizeof(na_fixes[0])));
    ok &= nr == 30;
    printf(ok ? "TOUT OK\n" : "ECHEC\n");
    return !ok;
}
