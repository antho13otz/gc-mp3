/* Appli (build 375) : pont entre l'appli et mGBA (emulateur GBA, licence MPL 2.0, telecharge a la compilation : voir
 * CMakeLists.txt et .github/workflows/android.yml). Ce fichier est propre a GC Station ; il n'appelle que l'interface de
 * mGBA (coeur, memoire, pilote du cable GameCube "JOYBUS").
 *
 * Usage (GbaNatif.kt, un seul fil) : demarrer(programme) -> le programme recu de la console (deja dechiffre) est mis dans
 * la memoire de travail de la GBA et lance directement (sans BIOS) ; image(...) -> une image (240 x 160), le son, et les
 * mots du jeu donnes a la GBA ;
 * joy(commande, donnees) -> une commande du cable comme si elle venait de la GameCube (00, FF, 14, 15) ; regs() -> JOYSTAT,
 * JOY_TRANS, JOY_RECV vus par la GBA, sans rien changer. */
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include <mgba/flags.h>                                /* (reglages de la compilation de mGBA : M_CORE_GBA, couleurs...) */
#include <mgba/core/core.h>
#include <mgba/core/blip_buf.h>
#include <mgba/gba/core.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/sio.h>
#include <mgba/internal/gba/video.h>
#include <mgba-util/vfs.h>

#define W 240
#define H 160
#define SON_HZ 32768

static struct mCore* core = NULL;
static color_t* ecran = NULL;
static struct GBASIODriver joy;

static void fin(void) {
	if (core) { core->deinit(core); core = NULL; }
	free(ecran); ecran = NULL;
}

JNIEXPORT jboolean JNICALL Java_fr_gcstation_telecommande_GbaNatif_demarrer(JNIEnv* env, jclass cls, jbyteArray prog) {
	(void) cls;
	fin();
	jsize n = (*env)->GetArrayLength(env, prog);
	if (n < 0xC0 || n > 0x40000) return JNI_FALSE;
	uint8_t* img = malloc((size_t) n);
	if (!img) return JNI_FALSE;
	(*env)->GetByteArrayRegion(env, prog, 0, n, (jbyte*) img);
	img[0xC4] = 0x01;                                   /* (octet du mode de demarrage : ecrit par le BIOS ; 1 = par le cable) */
	/* build 383 : demarre par le cable, une vraie GBA entre en 0x020000E0 (pas en 0xC0) : le debut du programme y passe le
	   cable en mode JOY (RCNT), donne le code du jeu au jeu et attend que le jeu le lui renvoie. mGBA entre en 0xC0 : on y
	   met un saut vers 0xE0 (B 0xE0 = EA000006) */
	img[0xC0] = 0x06; img[0xC1] = 0x00; img[0xC2] = 0x00; img[0xC3] = 0xEA;
	core = GBACoreCreate();
	if (!core || !core->init(core)) { free(img); core = NULL; return JNI_FALSE; }
	mCoreInitConfig(core, NULL);
	ecran = calloc(W * H, BYTES_PER_PIXEL);
	if (!ecran) { free(img); fin(); return JNI_FALSE; }
	core->setVideoBuffer(core, ecran, W);
	core->setAudioBufferSize(core, 2048);
	blip_set_rates(core->getAudioChannel(core, 0), core->frequency(core), SON_HZ);
	blip_set_rates(core->getAudioChannel(core, 1), core->frequency(core), SON_HZ);
	struct VFile* vf = VFileMemChunk(img, (size_t) n);   /* (copie : img peut etre libere) */
	free(img);
	if (!vf) { fin(); return JNI_FALSE; }
	struct GBA* gba = core->board;
	GBALoadMB(gba, vf);                                 /* (programme "multiboot" : en 0x02000000 ; sans cartouche) */
	GBASIOJOYCreate(&joy);
	GBASIOSetDriver(&gba->sio, &joy, SIO_JOYBUS);
	core->reset(core);                                  /* (programme multiboot charge : mGBA saute le BIOS, entree en 0x020000C0) */
	return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_fr_gcstation_telecommande_GbaNatif_arreter(JNIEnv* env, jclass cls) {
	(void) env; (void) cls;
	fin();
}

/* une image : touches (bits GBA : 0 A, 1 B, 2 Select, 3 Start, 4 droite, 5 gauche, 6 haut, 7 bas, 8 R, 9 L) ; pixels (ARGB,
 * 240 x 160) ; son (stereo 16 bits, 32768 Hz) ; mots : ce que le jeu a ecrit a la GBA (15), en attente, donnes un par un
 * pendant l'image des que la GBA a lu le precedent (JOYSTAT bit 1 efface). res : [echantillons stereo ecrits, mots donnes] */
JNIEXPORT jint JNICALL Java_fr_gcstation_telecommande_GbaNatif_image(JNIEnv* env, jclass cls, jint touches, jintArray pixels, jshortArray son,
                                                                     jintArray mots, jint nmots, jintArray res) {
	(void) cls;
	if (!core) return -1;
	struct GBA* gba = core->board;
	core->setKeys(core, (uint32_t) touches & 0x3FF);
	jint* m = nmots > 0 ? (*env)->GetIntArrayElements(env, mots, NULL) : NULL;
	int donnes = 0;
	uint32_t img = gba->video.frameCounter;
	int32_t t0 = mTimingCurrentTime(&gba->timing);
	while (gba->video.frameCounter == img && mTimingCurrentTime(&gba->timing) - t0 < VIDEO_TOTAL_LENGTH + VIDEO_HORIZONTAL_LENGTH) {
		if (m && donnes < nmots && !(gba->memory.io[REG_JOYSTAT >> 1] & JOYSTAT_RECV)) {
			uint32_t w = (uint32_t) m[donnes++];
			uint8_t d[8] = { (uint8_t) w, (uint8_t) (w >> 8), (uint8_t) (w >> 16), (uint8_t) (w >> 24) };
			GBASIOJOYSendCommand(&joy, JOY_RECV, d);
		}
		ARMRunLoop(core->cpu);
	}
	if (m) (*env)->ReleaseIntArrayElements(env, mots, m, JNI_ABORT);
	jint* px = (*env)->GetIntArrayElements(env, pixels, NULL);
	if (px) {
		for (int i = 0; i < W * H; i++) {
			color_t c = ecran[i];
#if BYTES_PER_PIXEL == 4
			uint32_t r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
#else
#ifdef COLOR_5_6_5
			uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
			r = r << 3 | r >> 2; g = g << 2 | g >> 4; b = b << 3 | b >> 2;
#else
			uint32_t r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
			r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2;
#endif
#endif
			px[i] = (jint) (0xFF000000u | r << 16 | g << 8 | b);
		}
		(*env)->ReleaseIntArrayElements(env, pixels, px, 0);
	}
	/* son : toujours vide (sinon les tampons de mGBA debordent) ; rendu a l'appli s'il y a la place */
	struct blip_t* gauche = core->getAudioChannel(core, 0);
	struct blip_t* droite = core->getAudioChannel(core, 1);
	jsize cap = (*env)->GetArrayLength(env, son) / 2;
	int dispo = blip_samples_avail(gauche);
	if (dispo > cap) dispo = cap;
	jint ecrits = 0;
	if (dispo > 0) {
		jshort* s = (*env)->GetShortArrayElements(env, son, NULL);
		if (s) {
			blip_read_samples(gauche, s, dispo, 1);
			blip_read_samples(droite, s + 1, dispo, 1);
			ecrits = dispo;
			(*env)->ReleaseShortArrayElements(env, son, s, 0);
		}
	}
	int reste;
	while ((reste = blip_samples_avail(gauche)) > 0) {    /* (au-dela de la place donnee : jete) */
		short tmp[512];
		int k = reste > 256 ? 256 : reste;
		blip_read_samples(gauche, tmp, k, 1);
		blip_read_samples(droite, tmp + 1, k, 1);
	}
	jint r[2] = { ecrits, donnes };
	(*env)->SetIntArrayRegion(env, res, 0, 2, r);
	return 0;
}

/* commande du cable venue de la GameCube : donnees (5 octets au plus) lues et reecrites ; rend le nombre d'octets de reponse */
JNIEXPORT jint JNICALL Java_fr_gcstation_telecommande_GbaNatif_joy(JNIEnv* env, jclass cls, jint cmd, jbyteArray data) {
	(void) cls;
	if (!core) return -1;
	uint8_t d[8] = { 0 };
	jsize n = (*env)->GetArrayLength(env, data);
	if (n > 8) n = 8;
	(*env)->GetByteArrayRegion(env, data, 0, n, (jbyte*) d);
	int r = GBASIOJOYSendCommand(&joy, (enum GBASIOJOYCommand) cmd, d);
	(*env)->SetByteArrayRegion(env, data, 0, n, (const jbyte*) d);
	return r;
}

/* registres du cable cote GBA, sans les changer : [JOYSTAT, JOY_TRANS (32 bits), JOY_RECV (32 bits), JOYCNT] */
JNIEXPORT void JNICALL Java_fr_gcstation_telecommande_GbaNatif_regs(JNIEnv* env, jclass cls, jintArray out) {
	(void) cls;
	if (!core) return;
	struct GBA* gba = core->board;
	uint16_t* io = gba->memory.io;
	jint v[4];
	v[0] = io[REG_JOYSTAT >> 1];
	v[1] = (jint) ((uint32_t) io[REG_JOY_TRANS_LO >> 1] | (uint32_t) io[(REG_JOY_TRANS_LO >> 1) + 1] << 16);
	v[2] = (jint) ((uint32_t) io[REG_JOY_RECV_LO >> 1] | (uint32_t) io[(REG_JOY_RECV_LO >> 1) + 1] << 16);
	v[3] = io[REG_JOYCNT >> 1];
	(*env)->SetIntArrayRegion(env, out, 0, 4, v);
}
