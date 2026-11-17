#!/usr/bin/env python3
# GC Station v455 : produit les pilotes reseau de tools/netif a partir de ceux de libogc2 (Extrems' Corner.org, licence zlib) :
#   python3 tools/netif/adapte.py <dossier libogc2>
# (libogc2 : https://github.com/extremscorner/libogc2 ; fichiers lwip/netif/{w5500,w6x00,enc28j60}if.c et
#  lwip/include/netif/*.h). Les changements, marques dans chaque fichier comme le demande la licence, se limitent a ce que
# la libogc de devkitPro n'a pas (gcs_compat.h), et a ne pas essayer le port serie 2 quand la carte SD y est.
import os, re, sys

if len(sys.argv) != 2:
    sys.exit("usage: adapte.py <dossier libogc2>")
up = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
NOTE = """
/*  GC Station v455 : version modifiee pour la libogc de devkitPro (altered source version, see notice above).
    Origine : libogc2 (Extrems' Corner.org), lwip/netif/%s, licence zlib (texte ci-dessus), produite par
    tools/netif/adapte.py. Changements : gcs_compat.h fournit ce que la libogc de devkitPro n'a pas (EXI_LockEx, EXI_DmaEx,
    EXI_GetIDEx, EXI_SPEEDMAX, ECIDn, IRQ_Request a 3 arguments%s) ; le port serie 2 (canal EXI 2) n'est pas essaye quand
    la carte SD y est (gcmp3_net_skip_exi2). */
"""

def must(s, a, b, n=1):
    c = s.count(a)
    if c != n:
        sys.exit("adapte.py : %r trouve %d fois (attendu %d)" % (a[:60], c, n))
    return s.replace(a, b)

def adapt(name, prefix, extra_note="", extra=None):
    s = open(os.path.join(up, "lwip", "netif", name)).read()
    e = s.index("-------------------------------------------------------------*/") + len("-------------------------------------------------------------*/")
    s = s[:e] + NOTE % (name, extra_note) + s[e:]
    hdr = '#include "netif/%s.h"\n' % name[:-2]
    s = must(s, hdr, hdr + '#include "gcs_compat.h"\n')
    s = must(s, "static void DbgHandler(u32 irq, frame_context *ctx)", "static void DbgHandler(u32 irq, void *ctx)   /* (GC Station : pas de frame_context dans devkitPro) */")
    s = must(s, "IRQ_Request(IRQ_PI_DEBUG, DbgHandler);", "GCS_IRQ_REQUEST(IRQ_PI_DEBUG, DbgHandler);")
    s = must(s, "if (%s_Init(EXI_CHANNEL_2, EXI_DEVICE_0," % prefix, "if (!gcmp3_net_skip_exi2 && %s_Init(EXI_CHANNEL_2, EXI_DEVICE_0," % prefix)
    if extra:
        s = extra(s)
    open(os.path.join(here, name), "w").write(s)
    h = open(os.path.join(up, "lwip", "include", "netif", name[:-2] + ".h")).read()
    e = h.index("-------------------------------------------------------------*/") + len("-------------------------------------------------------------*/")
    h = h[:e] + "\n/* GC Station : copie sans changement (libogc2, lwip/include/netif/%s.h). */\n" % name[:-2] + h[e:]
    open(os.path.join(here, name[:-2] + ".h"), "w").write(h)
    print("ok", name)

def enc(s):
    # pas de LWP_ThreadTimedSleep / LWP_SemTimedWait dans devkitPro : drapeaux attendus par gcs_wait_flag
    s = must(s, "#include <ogc/timesupp.h>\n", "")    # (absent de devkitPro ; usleep vient de unistd.h, gcs_compat.h)
    s = must(s, "\tsem_t txSemaphore;\n", "\tvolatile u8 txReady;                /* (GC Station : a la place du semaphore) */\n")
    s = must(s, "\t\t\tLWP_ThreadBroadcast(enc28j60if->linkupQueue);\n", "")
    s = must(s, "\t\tLWP_SemPost(enc28j60if->txSemaphore);\n", "\t\tenc28j60if->txReady = 1;\n")
    s = must(s, "LWP_ThreadTimedSleep(enc28j60if->linkupQueue, &(struct timespec){2, 500000000})",
             "gcs_wait_flag(&netif->flags, NETIF_FLAG_LINK_UP, 2500, &level)")
    s = must(s, """	if (LWP_SemTimedWait(enc28j60if->txSemaphore, &(struct timespec){2, 500000000})) {
		IRQ_Restore(level);
		return ERR_IF;
	}
""", """	if (gcs_wait_flag(&enc28j60if->txReady, 1, 2500, &level)) {
		IRQ_Restore(level);
		return ERR_IF;
	}
	enc28j60if->txReady = 0;
""")
    s = must(s, "\tLWP_SemInit(&enc28j60if->txSemaphore, 1, 1);\n", "\tenc28j60if->txReady = 1;\n")
    s = must(s, "\tLWP_SemDestroy(enc28j60if->txSemaphore);\n", "")
    return s

adapt("w5500if.c", "W5500")
adapt("w6x00if.c", "W6X00")
adapt("enc28j60if.c", "ENC28J60", " ; attentes de LWP_ThreadTimedSleep / LWP_SemTimedWait par gcs_wait_flag", enc)
