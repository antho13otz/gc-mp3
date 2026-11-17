#!/usr/bin/env python3
# Reseau plus rapide : recompile la pile reseau de libogc (lwIP + pilote du
# Broadband Adapter = libbba) avec une fenetre TCP de 16 paquets au
# lieu d'un seul (v125 : 44 paquets et 256 tampons en v117 coupaient les pages web sur la console). Avec 1 paquet (1460 octets) par aller-retour, un serveur a 30 ms ne
# peut pas depasser ~50 Ko/s ; avec 16 paquets, la limite passe a ~750 Ko/s.
#
# Les sources viennent de la meme version de libogc que celle installee dans
# le conteneur (on ne change que ce reglage). Si quoi que ce soit echoue, le
# script s'arrete sans rien produire et le Makefile garde la libbba normale.
#
# Usage : python3 tools/fast_net.py libogc.tar.gz
import os, re, shutil, subprocess, sys, tarfile

WND_SEGMENTS = 2                                   # v125 : retour a 16 (44 en v117 : pages web coupees sur la console)
# Build 1050 : 2. Le Broadband Adapter n'a que 15 pages de 256 octets pour recevoir (BBA_RXBUF_START 0x01 a BBA_RXBUF_END
# 0x0f dans bba.c de libogc : 3,75 Ko) : 2 paquets pleins (1514 octets = 6 pages). Avec 16, le serveur envoie 23 Ko d'un
# coup a 100 Mbit/s, plus vite que la console ne vide la memoire de l'adaptateur par le bus EXI : paquets perdus, puis la
# pile reseau attend ses reenvois (de plus en plus espaces) : mise a jour « reprise 1 : rien recu 20 s, 62 Ko » (build
# 1049). Avec 2, tout ce qui est en route tient dans l'adaptateur ; l'accuse de reception part a chaque 2e paquet (pas
# d'accuse retarde, le defaut de libogc a 1 paquet : 5 Ko/s).
PBUF_POOL = 128                                    # v125 : retour a 128 (256 en v117)
OUT = "netfast"
SOURCES = [
    "network.c", "netio.c", "arch/gc/netif/gcif.c",
    "core/inet.c", "core/mem.c", "core/dhcp.c", "core/raw.c", "core/memp.c",
    "core/netif.c", "core/pbuf.c", "core/stats.c", "core/sys.c", "core/tcp.c",
    "core/tcp_in.c", "core/tcp_out.c", "core/udp.c", "core/ipv4/icmp.c",
    "core/ipv4/ip.c", "core/ipv4/ip_frag.c", "core/ipv4/ip_addr.c",
    "netif/etharp.c", "netif/loopif.c",
    "netif/w5500if.c",                             # v326 : adaptateurs WIZnet W5500 (ETH2SP1...), tools/netif
    "netif/w6x00if.c",                             # v454 : adaptateurs WIZnet W6100 / W6300 (ETH2SP1 recents), tools/netif
    "netif/enc28j60if.c",                          # v455 : adaptateurs Microchip ENC28J60 (ETH2GC Lite...), tools/netif
]


def fail(msg):
    shutil.rmtree(OUT, ignore_errors=True)
    sys.exit("fast_net: " + msg + " -> libbba normale")


if len(sys.argv) != 2:
    fail("usage: fast_net.py libogc.tar.gz")
dkp = os.environ.get("DEVKITPPC", "/opt/devkitpro/devkitPPC")
cc = os.path.join(dkp, "bin", "powerpc-eabi-gcc")
ar = os.path.join(dkp, "bin", "powerpc-eabi-ar")

shutil.rmtree(OUT, ignore_errors=True)
src = os.path.join(OUT, "src")
os.makedirs(src)
with tarfile.open(sys.argv[1], "r:gz") as t:
    keep = ("lwip/", "gc/", "libogc/", "external/bsd/include/")
    for m in t.getmembers():
        parts = m.name.split("/", 1)
        if len(parts) != 2 or not m.isfile() or not parts[1].startswith(keep):
            continue
        if ".." in parts[1].split("/"):
            continue
        dest = os.path.join(src, parts[1])
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as o:
            o.write(t.extractfile(m).read())

opts = os.path.join(src, "gc", "lwip", "lwipopts.h")
if not os.path.isfile(opts):
    fail("lwipopts.h introuvable")
s = open(opts).read()
s2, n = re.subn(r"#define[ \t]+TCP_WND[ \t]+[^\n]*",
                "#define TCP_WND                 (%d*TCP_MSS)" % WND_SEGMENTS, s, count=1)
if n != 1:
    fail("reglage TCP_WND introuvable")
s2, n = re.subn(r"#define[ \t]+PBUF_POOL_SIZE[ \t]+[0-9]+", "#define PBUF_POOL_SIZE          %d" % PBUF_POOL, s2, count=1)
if n != 1:
    fail("reglage PBUF_POOL_SIZE introuvable")
open(opts, "w").write(s2)

# repere lisible par GC-MP3 (affiche dans l'onglet TELEPHONE)
net = os.path.join(src, "lwip", "network.c")
if not os.path.isfile(net):
    fail("network.c introuvable")
# v326 : sans Broadband Adapter, l'adaptateur W5500 (ETH2SP1 dans le port serie 1, ETH2GC dans un port de carte
# memoire) : pilote de libogc2 (licence zlib) adapte, dans tools/netif
here = os.path.dirname(os.path.abspath(__file__))
shutil.copy(os.path.join(here, "netif", "w5500if.c"), os.path.join(src, "lwip", "netif", "w5500if.c"))
shutil.copy(os.path.join(here, "netif", "w5500if.h"), os.path.join(src, "gc", "netif", "w5500if.h"))
for n in ("w6x00if", "enc28j60if"):                 # v454 / v455
    shutil.copy(os.path.join(here, "netif", n + ".c"), os.path.join(src, "lwip", "netif", n + ".c"))
    shutil.copy(os.path.join(here, "netif", n + ".h"), os.path.join(src, "gc", "netif", n + ".h"))
shutil.copy(os.path.join(here, "netif", "gcs_compat.h"), os.path.join(src, "lwip", "netif", "gcs_compat.h"))   # v455
ns = open(net).read()
bba = re.search(r"\n([ \t]*)pnet = netif_add\(&g_hNetIF,[^\n]*bba_init, net_input\);\n", ns)
if not bba:
    fail("ajout du W5500 : appel de bba_init introuvable dans network.c")
ind = bba.group(1)
inc = ["gc", "libogc", "gc/netif", "gc/ipv4", "gc/ogc", "gc/ogc/machine", "external/bsd/include"]
flags = ["-O2", "-DGEKKO", "-mogc", "-mcpu=750", "-meabi", "-mhard-float", "-msdata=eabi",
         "-fno-strict-aliasing", "-DHW_DOL", "-w"] + ["-I" + os.path.join(src, d) for d in inc]
# v454 / v455 : les pilotes W6x00 et ENC28J60 sont facultatifs : s'ils ne se compilent pas, la pile est faite sans eux
def essai(n):
    ok = subprocess.run([cc] + flags + ["-c", os.path.join(src, "lwip", "netif", n + ".c"), "-o", os.path.join(OUT, n + "_essai.o")]).returncode == 0
    if not ok:
        print("fast_net: ATTENTION : pilote %s non compile : sans lui" % n)
        SOURCES.remove("netif/%s.c" % n)
    return ok
w6 = essai("w6x00if")
enc = essai("enc28j60if")
# v454 : puis les W6100 / W6300 (meme identifiant EXI que le W5500, autre version de puce), avant le W5500 comme libogc2
# v455 : puis l'ENC28J60 (ETH2GC Lite), en dernier comme dans libogc2
ns = (ns[:bba.end()] + (ind + "if(!pnet) pnet = netif_add(&g_hNetIF,&loc_ip, &mask, &gw, NULL, w6x00if_init, net_input);   /* GC Station v454 */\n" if w6 else "")
      + ind + "if(!pnet) pnet = netif_add(&g_hNetIF,&loc_ip, &mask, &gw, NULL, w5500if_init, net_input);   /* GC Station v326 */\n"
      + (ind + "if(!pnet) pnet = netif_add(&g_hNetIF,&loc_ip, &mask, &gw, NULL, enc28j60if_init, net_input);   /* GC Station v455 */\n" if enc else "")
      + ns[bba.end():])
ns = '#include "netif/w5500if.h"\n' + ('#include "netif/w6x00if.h"\n' if w6 else '') + ('#include "netif/enc28j60if.h"\n' if enc else '') + ns
with open(net, "w") as f:
    f.write(ns)
with open(net, "a") as f:
    f.write("\nconst int gcmp3_tcp_wnd = TCP_WND;\nconst int gcmp3_w5500 = 1;\nconst int gcmp3_w6x00 = %d;\nconst int gcmp3_enc28j60 = %d;\nint gcmp3_net_skip_exi2 = 0;\nconst char *gcmp3_netif_name(void) { return g_hNetIF.name; }\n" % (w6, enc))

objs = []
for f in SOURCES:
    c = os.path.join(src, "lwip", f)
    o = os.path.join(OUT, os.path.basename(f)[:-2] + ".o")
    if not os.path.isfile(c):
        fail(f + " introuvable")
    r = subprocess.run([cc] + flags + ["-c", c, "-o", o])
    if r.returncode != 0:
        fail("compilation de " + f + " impossible")
    objs.append(o)
lib = os.path.join(OUT, "libbba_fast.a")
if subprocess.run([ar, "rcs", lib] + objs).returncode != 0:
    fail("archive impossible")
print("fast_net: libbba_fast.a prete, fenetre TCP = %d x 1460 octets" % WND_SEGMENTS)
