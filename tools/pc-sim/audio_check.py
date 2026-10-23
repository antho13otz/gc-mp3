#!/usr/bin/env python3
# GC Station sur PC : coupures du son dans un WAV enregistre par le simulateur.
# Une coupure = au moins 15 ms de silence parfait (0) au milieu de la lecture
# (GC Station n'a pas rempli le tampon du son a temps : on l'entendrait sur la console).
import sys, wave
import numpy as np
w = wave.open(sys.argv[1]); sr = w.getframerate()
d = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, 2)
z = np.all(d == 0, axis=1)
nz = np.flatnonzero(~z)
if len(nz) == 0:
    print('son : aucun (silence)'); sys.exit(0)
a, b = nz[0], nz[-1]
seg = z[a:b]
gaps = []; i = 0; n = len(seg); lim = int(sr * 0.015)
while i < n:
    if seg[i]:
        j = i
        while j < n and seg[j]: j += 1
        if j - i >= lim: gaps.append(((a + i) / sr, (j - i) * 1000 / sr))
        i = j
    else: i += 1
tot = sum(g[1] for g in gaps)
print('son : %.1f s de lecture, %d coupures (%.0f ms en tout)%s' % ((b - a) / sr, len(gaps), tot,
      '' if not gaps else ' : ' + ', '.join('%.1f s (%.0f ms)' % g for g in gaps[:12]) + (' ...' if len(gaps) > 12 else '')))
