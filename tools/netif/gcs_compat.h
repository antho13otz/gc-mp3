/*  GC Station v455 : ce que les pilotes reseau de libogc2 (W5500, W6100/W6300, ENC28J60 ; licence zlib) attendent et que
    la libogc de devkitPro n'a pas. Inclus par les versions modifiees de ces pilotes (tools/netif/*if.c), produites par
    tools/netif/adapte.py a partir des fichiers de libogc2.

    gcmp3_net_skip_exi2 : mis a 1 par GC Station quand la carte SD est dans le port serie 2 (SD2SP2) : les pilotes
    n'essaient alors pas ce port (canal EXI 2), occupe par la carte. Defini dans network.c (tools/fast_net.py). */
#ifndef GCS_NETIF_COMPAT_H
#define GCS_NETIF_COMPAT_H

#include <gctypes.h>
#include <ogc/exi.h>
#include <ogc/irq.h>
#include <ogc/cache.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>                            /* (gettime, diff_usec : ENC28J60) */

extern int gcmp3_net_skip_exi2;

#ifndef EXI_SPEEDMAX
#define EXI_SPEEDMAX (EXI_SPEED32MHZ + 1)
#endif
#ifndef ECID0
#define ECID0 924
#define ECID1 925
#define ECID2 926
#define ECID3 927
#endif

static inline s32 EXI_LockEx(s32 chan, s32 dev) { return EXI_LockSync(chan, dev); }

/* transfert : debut et fin hors de l'alignement de 32 octets en immediat, le reste en DMA (cache vide / ecrit avant) */
static inline s32 EXI_DmaEx(s32 chan, void *data, u32 len, u32 mode)
{
	u8 *p = data;
	u32 head = -((u32)p) & 31;
	if (!p || !len) return 0;
	if (len < head + 32) return EXI_ImmEx(chan, p, len, mode);
	if (head) { if (!EXI_ImmEx(chan, p, head, mode)) return 0; p += head; len -= head; }
	u32 body = len & ~31u;
	if (mode == EXI_READ) DCInvalidateRange(p, body); else DCStoreRange(p, body);
	if (!EXI_Dma(chan, p, body, mode, NULL) || !EXI_Sync(chan)) return 0;
	p += body; len -= body;
	if (len && !EXI_ImmEx(chan, p, len, mode)) return 0;
	return 1;
}

/* identifiant lu a 1 MHz sans les verifications de EXI_GetID (l'ENC28J60 n'a pas de vrai identifiant EXI) */
static inline s32 EXI_GetIDEx(s32 chan, s32 dev, u32 *id)
{
	u16 cmd = 0x0000;
	s32 ret = 0;
	if (chan == EXI_CHANNEL_2 && gcmp3_net_skip_exi2) { *id = 0xffffffff; return 1; }
	if (!EXI_Select(chan, dev, EXI_SPEED1MHZ)) return 0;
	if (!EXI_ImmEx(chan, &cmd, 2, EXI_WRITE)) ret |= 1;
	if (!EXI_ImmEx(chan, id, 4, EXI_READ)) ret |= 2;
	if (!EXI_Deselect(chan)) ret |= 4;
	return !ret;
}

/* IRQ_Request de devkitPro prend un 3e argument (contexte) */
#define GCS_IRQ_REQUEST(n, h) IRQ_Request((n), (raw_irq_handler_t)(void *)(h), NULL)

/* attente d'un drapeau, au plus ms millisecondes, appelee avec les interruptions coupees (*level) : 0 = drapeau vu.
   (remplace LWP_ThreadTimedSleep / LWP_SemTimedWait, absents de devkitPro) */
static inline int gcs_wait_flag(volatile u8 *v, u8 mask, u32 ms, u32 *level)
{
	for (u32 t = 0; t <= ms; t += 5) {
		if (*v & mask) return 0;
		IRQ_Restore(*level);
		usleep(5000);
		*level = IRQ_Disable();
	}
	return (*v & mask) ? 0 : 1;
}

#endif
