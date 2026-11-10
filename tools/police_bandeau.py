#!/usr/bin/env python3
# v397 (v398 : 12 x 24) : police du bandeau des succes sur la tele (source/natres/bandeau.c), tiree de DejaVu Sans Mono Bold (licence
# Bitstream Vera / DejaVu, comme la police de l'interface : CREDITS.md). Cellules de 12 x 24 pixels, une ligne = 16 bits
# (bit 15 = pixel de gauche). Ecrit source/natres/bandeau_police.h.
#   python3 tools/police_bandeau.py /usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf
# Codes : 0 = fin du texte, 1 = trophee, 2-6 = icones des notifications (v399 : message, appel, alarme, ami, info ; dessines ici), 32-126 = ASCII, puis EXTRA (meme liste dans l'appli : Bandeau.kt).
import sys
from PIL import Image, ImageDraw, ImageFont
EXTRA = "àâäçéèêëîïôöùûüœÀÂÇÉÈÊÎÔÙÛŒ«»…"
LW, LH = 11, 22   # (v400 : un peu plus petit ; avant 12 x 24)
ttf = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
font = ImageFont.truetype(ttf, 17)
asc, desc = font.getmetrics()
TROPHEE = [   # 12 x 24, dessine a la main
    "............", "............", "............", "............", "############", "############", "#.########.#",
    "#.########.#", "#.########.#", "#.########.#", ".#.######.#.", "..########..", "...######...", "....####....",
    ".....##.....", ".....##.....", ".....##.....", "....####....", "...######...", "..########..", "..########..",
    "............", "............", "............"]
# v399 : icones des notifications (codes 2 a 6), 12 x 24, dessinees a la main comme le trophee
ICONES = {
    2: [  # message (bulle)
        "............", "............", "............", "............", "............", ".##########.", "############",
        "############", "##........##", "############", "############", "##......####", "############", "############",
        ".##########.", "...###......", "..##........", ".#..........", "............", "............", "............",
        "............", "............", "............"],
    3: [  # appel (combine)
        "............", "............", "............", "............", "###.........", "####........", "####........",
        "####........", ".##.........", ".###........", "..###.......", "...###......", "....###.....", ".....###.##.",
        "......######", ".......#####", "........####", ".........##.", "............", "............", "............",
        "............", "............", "............"],
    4: [  # alarme (reveil)
        "............", "............", "............", "##........##", "###......###", ".##.####.##.", "..########..",
        ".###....###.", ".##..#...##.", "##...#....##", "##...#....##", "##...####.##", "##........##", ".##......##.",
        ".###....###.", "..########..", "...######...", "..##....##..", ".##......##.", "............", "............",
        "............", "............", "............"],
    5: [  # ami (personne)
        "............", "............", "............", "....####....", "...######...", "..########..", "..########..",
        "..########..", "..########..", "...######...", "....####....", "............", "...######...", ".##########.",
        "############", "############", "############", "############", "............", "............", "............",
        "............", "............", "............"],
    6: [  # info (rond avec un i)
        "............", "............", "............", "...######...", "..########..", ".####..####.", "#####..#####",
        "############", "#####..#####", "#####..#####", "#####..#####", "#####..#####", "#####..#####", ".####..####.",
        "..########..", "...######...", "............", "............", "............", "............", "............",
        "............", "............", "............"],
}
def glyphe(ch):
    im = Image.new("L", (LW, LH), 0)
    ImageDraw.Draw(im).text((0, (LH - (asc + desc)) // 2 + 1), ch, font=font, fill=255)
    return [sum(1 << (15 - x) for x in range(LW) if im.getpixel((x, y)) >= 110) for y in range(LH)]
def reduit(d):                                            # (v400 : dessin 12 x 24 -> LW x LH, au plus proche)
    return ["".join(d[min(23, (y * 24 + 12) // LH)][min(11, (x * 12 + 6) // LW)] for x in range(LW)) for y in range(LH)]
codes = {1: [sum(1 << (15 - x) for x in range(LW) if r[x] == "#") for r in reduit(TROPHEE)]}
for k, d in ICONES.items():
    assert len(d) == 24 and all(len(r) == 12 for r in d), k
    codes[k] = [sum(1 << (15 - x) for x in range(LW) if r[x] == "#") for r in reduit(d)]
for c in range(32, 127): codes[c] = glyphe(chr(c))
for i, ch in enumerate(EXTRA): codes[127 + i] = glyphe(ch)
N = 127 + len(EXTRA)
out = ["/* v397 : police du bandeau des succes (tools/police_bandeau.py, DejaVu Sans Mono Bold : licence Bitstream Vera / DejaVu).",
       "   Fichier fabrique : ne pas modifier a la main. " + "%d x %d pixels, 16 bits" % (LW, LH) + " par ligne (bit 15 = gauche). */",
       "#ifndef BANDEAU_POLICE_H", "#define BANDEAU_POLICE_H   /* (v399 : aussi incluse par notif_tele.c) */",
       "#define BAN_LW %d" % LW, "#define BAN_LH %d" % LH, "#define BAN_NG %d" % N,
       "/* caracteres apres l'ASCII (codes 127 et suivants), en UTF-8 */",
       "static const char *const ban_extra[] = { " + ", ".join('"%s"' % "".join("\\x%02X" % b for b in ch.encode("utf-8")) for ch in EXTRA) + " };",
       "static const unsigned short ban_police[BAN_NG * BAN_LH] = {"]
for c in range(N):
    g = codes.get(c, [0] * LH)
    out.append("    " + ", ".join("0x%04X" % v for v in g) + ",")
out.append("};")
out.append("#endif")
open(sys.argv[2] if len(sys.argv) > 2 else "source/natres/bandeau_police.h", "w").write("\n".join(out) + "\n")
# apercu
txt = ["\x01 \x02 \x03 \x04 \x05 \x06 Succès débloqué", "Mêlée générale : 99 vies, «ça» marche…"]
im = Image.new("L", (LW * 40 * 3, LH * 2 * 3), 0)
for li, t in enumerate(txt):
    for k, ch in enumerate(t):
        c = ord(ch) if ord(ch) < 7 else ord(ch) if ord(ch) < 127 else 127 + EXTRA.index(ch)
        for y, row in enumerate(codes[c]):
            for x in range(LW):
                if row & (1 << (15 - x)):
                    for a in range(3):
                        for b in range(3): im.putpixel(((k * LW + x) * 3 + a, (li * LH + y) * 3 + b), 255)
im.save("/tmp/apercu_police.png")
print("glyphes :", N, "octets :", N * LH * 2)
