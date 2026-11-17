#!/usr/bin/env python3
"""build 1067 : rejoue les blocs pris / rendus par GC Station (PCSIM_ALLOCLOG) dans un tas comme celui de la console
(un seul morceau de memoire, le meilleur trou qui convient, sinon au-dessus), et dit, a chaque repere « gros bloc »
(tampon du lancement d'un jeu) et a la fin : le plus grand bloc libre d'un seul tenant, et les blocs encore pris qui le
coupent (avec l'endroit du code, par addr2line).
  python3 tools/pc-sim/tas_morcele.py journal-tas.txt tools/pc-sim/gcstation [taille du tas en octets]"""
import sys, bisect, subprocess, collections
log, exe = sys.argv[1], sys.argv[2]
HEAP = int(float(sys.argv[3])) if len(sys.argv) > 3 else int(12.66 * 1048576)
ALIGN = 8
free_ = [(0, HEAP)]                 # trous (debut, taille), tries par adresse
live = {}                           # id -> (adresse, taille, sites)
def take(n):
    n = (n + ALIGN - 1) // ALIGN * ALIGN
    best = -1
    for i, (a, s) in enumerate(free_):
        if s >= n and (best < 0 or s < free_[best][1]): best = i
    if best < 0: return None
    a, s = free_[best]
    if s == n: free_.pop(best)
    else: free_[best] = (a + n, s - n)
    return a
def give(a, n):
    n = (n + ALIGN - 1) // ALIGN * ALIGN
    i = bisect.bisect(free_, (a, 0))
    free_.insert(i, (a, n))
    if i + 1 < len(free_) and free_[i][0] + free_[i][1] == free_[i + 1][0]:
        free_[i] = (free_[i][0], free_[i][1] + free_[i + 1][1]); free_.pop(i + 1)
    if i > 0 and free_[i - 1][0] + free_[i - 1][1] == free_[i][0]:
        free_[i - 1] = (free_[i - 1][0], free_[i - 1][1] + free_[i][1]); free_.pop(i)
names = {}
def sym(addrs):
    todo = [x for x in addrs if x not in names]
    if todo:
        out = subprocess.run(['addr2line', '-f', '-e', exe] + ['%x' % (x - 1) for x in todo], capture_output=True, text=True).stdout.split('\n')
        for k, x in enumerate(todo): names[x] = out[2 * k] if 2 * k < len(out) else '?'
    return [names[x] for x in addrs]
def report(title):
    big = max((s for a, s in free_), default=0)
    used = sum(n for a, n, st in live.values())
    print('== %s : %.2f Mo pris, plus grand bloc libre %.2f Mo (%d trous)' % (title, used / 1048576, big / 1048576, len(free_)))
    # les blocs qui, enleves, donneraient le plus grand bloc : on cherche la fenetre de 4,5 Mo la moins occupee
    W = int(4.5 * 1048576)
    blocks = sorted(live.values())
    best = None
    j = 0; occ = 0
    starts = sorted(set([0] + [a for a, n, st in blocks]))
    for s0 in starts:
        if s0 + W > HEAP: break
        o = [b for b in blocks if b[0] < s0 + W and b[0] + b[1] > s0]
        tot = sum(min(b[0] + b[1], s0 + W) - max(b[0], s0) for b in o)
        if best is None or tot < best[0]: best = (tot, s0, o)
    if best:
        tot, s0, o = best
        print('   fenetre de 4,5 Mo la plus libre a +%.2f Mo : %d blocs pris dedans (%.0f Ko)' % (s0 / 1048576, len(o), tot / 1024))
        bysite = collections.defaultdict(lambda: [0, 0])
        for a, n, st in o: bysite[st][0] += n; bysite[st][1] += 1
        for st, (n, c) in sorted(bysite.items(), key=lambda x: -x[1][0])[:15]:
            print('     %7.1f Ko %4d  %s' % (n / 1024, c, ' < '.join(sym(list(st)))))
for line in open(log):
    p = line.split()
    if not p: continue
    if p[0] == 'a':
        n = int(p[2]); a = take(n)
        if a is None: print('!! plus de place pour %d octets' % n); continue
        live[p[1]] = (a, n, tuple(int(x, 16) for x in p[3:6]))
    elif p[0] == 'f':
        b = live.pop(p[1], None)
        if b: give(b[0], b[1])
    elif p[0] == 'm':
        report(' '.join(p[1:]))
report('fin')
