#!/usr/bin/env python3
# v92 : disque de demarrage GC Station (image .iso a graver sur un mini-DVD, consoles avec puce).
# Meme methode que les disques de swiss-gc : en-tete GameCube "El Torito" de swiss-gc (GPL v2,
# buildtools/iso/eltorito-<region>.hdr, version figee) + le .dol de GC Station, avec xorrisofs.
# Au demarrage, GC Station lance la version plus recente de la carte SD s'il y en a une (main.c, disc_chain).
# Usage : python3 tools/mkdisc.py gc-mp3player.dol <dossier des .hdr> BUILD <dossier de sortie>
import os, shutil, struct, subprocess, sys, tempfile
dol, hdrdir, build, out = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
os.makedirs(out, exist_ok=True)
d = open(dol, "rb").read()
if d[-32:-16] != b"GCSTATION-BUILD=":
    sys.exit("ERREUR : le .dol n'a pas son numero de build a la fin")
tool = shutil.which("xorrisofs") or shutil.which("mkisofs")
if not tool:
    sys.exit("ERREUR : xorrisofs / mkisofs absent")
regions = {"e": "pal", "u": "ntsc-u", "j": "ntsc-j"}
for r, nom in regions.items():
    h = bytearray(open(os.path.join(hdrdir, "eltorito-%s.hdr" % r), "rb").read())
    if len(h) != 32768 or struct.unpack_from(">I", h, 0x1C)[0] != 0xC2339F3D:
        sys.exit("ERREUR : en-tete %s invalide" % r)
    titre = b"GC STATION (build %s)" % build.encode()
    h[0x20:0x20 + 0x3E0] = titre.ljust(0x3E0, b"\0")            # (nom du disque)
    with tempfile.TemporaryDirectory() as t:
        hp = os.path.join(t, "eltorito.hdr"); open(hp, "wb").write(h)
        files = os.path.join(t, "disque"); os.makedirs(files)
        shutil.copy(dol, os.path.join(files, "gcstation.dol"))
        open(os.path.join(files, "LISEZMOI.txt"), "w").write(
            "GC Station (build %s) - disque de demarrage\r\n\r\n"
            "Au demarrage, si la carte SD (SD2SP2, SD Gecko) contient une version plus recente de\r\n"
            "GC Station (mise a jour depuis START > Mise a jour), c'est elle qui est lancee.\r\n"
            "B maintenu au demarrage : rester sur la version du disque.\r\n"
            "En-tete de demarrage : swiss-gc (GPL v2), https://github.com/emukidid/swiss-gc\r\n" % build)
        iso = os.path.join(out, "gc-station-disque-%s-b%s.iso" % (nom, build))
        cmd = [tool, "-R", "-J", "-G", hp, "-no-emul-boot", "-eltorito-platform", "PPC", "-b", "gcstation.dol", "-o", iso,
               os.path.join(files, "gcstation.dol"), os.path.join(files, "LISEZMOI.txt")]
        subprocess.run(cmd, check=True)
    v = open(iso, "rb").read()
    if v[:6] != bytes(h[:6]) or struct.unpack_from(">I", v, 0x1C)[0] != 0xC2339F3D or d not in v:
        sys.exit("ERREUR : %s incorrect" % iso)
    print("%s : %d octets" % (iso, len(v)))
