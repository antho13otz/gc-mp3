#!/usr/bin/env python3
# Reseau plus rapide : recompile la pile reseau de libogc (lwIP + pilote du
# Broadband Adapter = libbba) avec une fenetre TCP de 16 paquets au lieu d'un
# seul. Avec 1 paquet (1460 octets) par aller-retour, un serveur a 30 ms ne
# peut pas depasser ~50 Ko/s ; avec 16 paquets, la limite passe a ~750 Ko/s.
#
# Les sources viennent de la meme version de libogc que celle installee dans
# le conteneur (on ne change que ce reglage). Si quoi que ce soit echoue, le
# script s'arrete sans rien produire et le Makefile garde la libbba normale.
#
# Usage : python3 tools/fast_net.py libogc.tar.gz
import os, re, shutil, subprocess, sys, tarfile

WND_SEGMENTS = 16
OUT = "netfast"
SOURCES = [
    "network.c", "netio.c", "arch/gc/netif/gcif.c",
    "core/inet.c", "core/mem.c", "core/dhcp.c", "core/raw.c", "core/memp.c",
    "core/netif.c", "core/pbuf.c", "core/stats.c", "core/sys.c", "core/tcp.c",
    "core/tcp_in.c", "core/tcp_out.c", "core/udp.c", "core/ipv4/icmp.c",
    "core/ipv4/ip.c", "core/ipv4/ip_frag.c", "core/ipv4/ip_addr.c",
    "netif/etharp.c", "netif/loopif.c",
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
open(opts, "w").write(s2)

# repere lisible par GC-MP3 (affiche dans l'onglet TELEPHONE)
net = os.path.join(src, "lwip", "network.c")
if not os.path.isfile(net):
    fail("network.c introuvable")
with open(net, "a") as f:
    f.write("\nconst int gcmp3_tcp_wnd = TCP_WND;\n")

inc = ["gc", "libogc", "gc/netif", "gc/ipv4", "gc/ogc", "gc/ogc/machine", "external/bsd/include"]
flags = ["-O2", "-DGEKKO", "-mogc", "-mcpu=750", "-meabi", "-mhard-float", "-msdata=eabi",
         "-fno-strict-aliasing", "-DHW_DOL", "-w"] + ["-I" + os.path.join(src, d) for d in inc]
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
