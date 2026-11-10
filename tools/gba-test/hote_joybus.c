/* Banc d'essai : le programme GBA de Pac-Man vs. (PROOT/gbademo/packman.bin) dans mGBA, avec une "GameCube" fidele qui
   suit l'algorithme lu dans main.dol (PRJP01, 0x8000BA24 / 0x8000B6D0) : remise a zero, etat 0x28, lecture de
   l'identifiant, renvoi, etat 0x30, ecriture du port, puis reception des paquets (etat & 0x0A == 8, lecture). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/sio.h>
#include <mgba-util/vfs.h>

static struct mCore* core;
static struct GBASIODriver joy;
static struct GBA* gba;
static long cmds = 0;
static int PAUSE = 1500, ATTENTE = 0;
static void libre(void) { if (!ATTENTE) return; int32_t t0 = mTimingCurrentTime(&gba->timing); while ((gba->memory.io[REG_JOYCNT >> 1] & 7) && mTimingCurrentTime(&gba->timing) - t0 < 280896) ARMRunLoop(core->cpu); }

static void run(int cyc) { int32_t t0 = mTimingCurrentTime(&gba->timing); while (mTimingCurrentTime(&gba->timing) - t0 < cyc) ARMRunLoop(core->cpu); }
static int st(void) { uint8_t d[8] = { 0 }; GBASIOJOYSendCommand(&joy, JOY_POLL, d); cmds++; run(PAUSE); return d[2]; }
static int rst(void) { uint8_t d[8] = { 0 }; GBASIOJOYSendCommand(&joy, JOY_RESET, d); cmds++; run(PAUSE); return d[2]; }
static uint32_t rd(int* s) { libre(); uint8_t d[8] = { 0 }; GBASIOJOYSendCommand(&joy, JOY_TRANS, d); cmds++; run(PAUSE); if (s) *s = d[4];
	return d[0] | d[1] << 8 | d[2] << 16 | (uint32_t) d[3] << 24; }
static int wr(uint32_t w) { libre(); uint8_t d[8] = { w, w >> 8, w >> 16, w >> 24 }; GBASIOJOYSendCommand(&joy, JOY_RECV, d); cmds++; run(PAUSE); return d[0]; }
static void regs(const char* q) { uint16_t* io = gba->memory.io;
	printf("  [%s] GBA JOYSTAT %02X JOYCNT %02X TRANS %08X RECV %08X pc %08X\n", q, io[REG_JOYSTAT >> 1], io[REG_JOYCNT >> 1],
	       io[REG_JOY_TRANS_LO >> 1] | io[(REG_JOY_TRANS_LO >> 1) + 1] << 16, io[REG_JOY_RECV_LO >> 1] | io[(REG_JOY_RECV_LO >> 1) + 1] << 16,
	       ((struct ARMCore*) core->cpu)->gprs[15]); }

int main(int argc, char** argv) {
	FILE* f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	uint8_t* img = malloc(n); if (fread(img, 1, n, f) != (size_t) n) return 1; fclose(f);
	img[0xC4] = 1; img[0xC0] = 0x06; img[0xC1] = 0; img[0xC2] = 0; img[0xC3] = 0xEA;   /* (comme gba_jni.c) */
	core = GBACoreCreate(); core->init(core); mCoreInitConfig(core, NULL);
	static color_t ecran[240 * 160]; core->setVideoBuffer(core, ecran, 240);
	struct VFile* vf = VFileMemChunk(img, n); gba = core->board; GBALoadMB(gba, vf);
	GBASIOJOYCreate(&joy); GBASIOSetDriver(&gba->sio, &joy, SIO_JOYBUS); core->reset(core);
	int mode = argc > 2 ? atoi(argv[2]) : 0;
	if (argc > 3) PAUSE = atoi(argv[3]);
	if (argc > 4) ATTENTE = atoi(argv[4]);
	run(280896 * 10);                                  /* (le programme demarre : 10 images) */
	regs("depart");
	{ int sx; uint32_t c = rd(&sx); printf("fin du demarrage : code lu %08X (etat %02X)\n", c, sx); run(20000); int sw = wr(c); printf("code renvoye -> %02X\n", sw); run(280896 * 5); regs("apres demarrage"); }
	for (int essai = 0; essai < 20; essai++) {        /* poignee de main (0x8000BA24) */
		int s0 = rst(); run(280896 / 4);
		int s = st();
		printf("reset -> %02X ; etat %02X\n", s0, s);
		if (s != 0x28) { regs("etat pas 28"); run(280896); continue; }
		int sx; uint32_t id = rd(&sx); printf("identifiant lu %08X (etat %02X)\n", id, sx); regs("apres lecture");
		int sw = wr(id); printf("renvoi -> %02X\n", sw); regs("apres renvoi");
		if (essai == 0) { FILE* o = fopen("/tmp/pac/iwram.bin", "wb"); for (uint32_t a = 0x03000000; a < 0x03008000; a += 4) { uint32_t v = core->rawRead32(core, a, -1); fwrite(&v, 4, 1, o); } fclose(o); }
		int ok = 0;
		for (int k = 0; k < 4000; k++) { s = st(); if (s == 0x30) { ok = 1; break; } if (k < 2 || k % 1000 == 0) printf("  attente 30 : etat %02X\n", s); run(5000); }
		if (!ok) { regs("pas 30"); continue; }
		printf("etat 30 : connecte (%d)\n", essai);
		uint32_t port = mode ? 0x30000003u : 0x03000030u;
		int sp = wr(port); printf("ecriture du port %08X -> %02X\n", port, sp); regs("apres port");
		break;
	}
	for (int pq = 0; pq < 5; pq++) {                    /* reception (0x8000B6D0) : paquets de la GBA */
		int s, k = 0;
		while (((s = st()) & 0x0A) != 8 && k < 4000) { k++; run(3000); }
		if (k >= 4000) { printf("paquet %d : rien (etat %02X)\n", pq, s); regs("rien"); break; }
		int sx; uint32_t h = rd(&sx); int nw = ((h >> 24) + 3) / 4;
		printf("paquet %d : en-tete %08X (octet bas %02X, %d mots) apres %d etats\n", pq, h, h & 0xFF, nw, k);
		for (int i = 0; i < nw; i++) {
			k = 0; while (((s = st()) & 0x0A) != 8 && k < 4000) { k++; run(3000); }
			uint32_t w = rd(&sx); if (i < 4) printf("   mot %d : %08X\n", i, w);
		}
	}
	regs("fin"); printf("%ld commandes\n", cmds);
	return 0;
}
