#!/usr/bin/env python3
# v174 : police de la console (ANSI) faite ici pour le banc : glyphes vides, au format de la ROM (en-tete OSFontHeader,
# largeurs, planche 2 bits, le tout compresse en Yay0). Le jeu (OSInitFont) la lit sur l'EXI comme la vraie : sans
# elle, les jeux qui ecrivent avec la police de la console plantaient dans le banc (Cocoto). Rien de la vraie ROM.
import struct

def yay0(data):
    masks, links, chunks = [], bytearray(), bytearray()
    bits = []; i = 0; n = len(data)
    while i < n:
        best = 0; bd = 0
        for dist in (1, 2, 4, 8, 16, 32):                 # (motifs simples : assez pour des glyphes vides)
            if dist > i: break
            l = 0
            while i + l < n and l < 0x111 and data[i + l] == data[i + l - dist]: l += 1
            if l > best: best, bd = l, dist
        if best >= 3:
            bits.append(0)
            if best >= 0x12: links += struct.pack('>H', bd - 1); chunks.append(best - 0x12)
            else: links += struct.pack('>H', ((best - 2) << 12) | (bd - 1))
            i += best
        else:
            bits.append(1); chunks.append(data[i]); i += 1
    while len(bits) % 32: bits.append(0)
    for k in range(0, len(bits), 32):
        v = 0
        for b in bits[k:k + 32]: v = v << 1 | b
        masks.append(v)
    mb = b''.join(struct.pack('>I', m) for m in masks)
    link_off = 16 + len(mb); chunk_off = link_off + len(links)
    return b'Yay0' + struct.pack('>III', n, link_off, chunk_off) + mb + bytes(links) + bytes(chunks)

def ansi():
    first, last = 0x20, 0xFF
    nch = last - first + 1
    wt = 0x30; img = (wt + nch + 31) & ~31
    sheet2 = bytes(512 * 512 // 4)                       # (planche de 512 x 512 en 2 bits : vide)
    h = struct.pack('>HHHHHHHHHHIHHHHHHIIBBBB', 0, first, last, 0x20, 16, 4, 24, 0, 24, 24,
                    512 * 512 // 2, 0, 21, 21, 512, 512, wt, img, 512 * 512 // 2, 0x00, 0x55, 0xAA, 0xFF)
    body = bytearray(h) + bytes(wt - len(h)) + bytes([12] * nch)
    body += bytes(img - len(body)) + sheet2
    return yay0(bytes(body))

if __name__ == '__main__':
    d = ansi(); print(len(d))
