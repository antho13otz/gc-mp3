#!/usr/bin/env python3
"""Convertit une liste de certificats racines (cacert.pem) en source/cacert.h.
Usage : python3 pem2h.py cacert.pem cacert.h"""
import sys
pem = open(sys.argv[1], 'rb').read().decode('ascii', 'ignore')
# on ne garde que les blocs de certificats (pas les commentaires)
blocks, cur = [], None
for line in pem.splitlines():
    if line.startswith('-----BEGIN CERTIFICATE-----'):
        cur = [line]
    elif cur is not None:
        cur.append(line)
        if line.startswith('-----END CERTIFICATE-----'):
            blocks.append('\n'.join(cur) + '\n')
            cur = None
if len(blocks) < 10:
    raise SystemExit('ERREUR : seulement %d certificats trouves' % len(blocks))
out = ['/* Certificats racines (liste Mozilla via curl.se) - genere par tools/pem2h.py */\n',
       '/* %d certificats */\n' % len(blocks), 'static const char cacert_pem[] =\n']
for b in blocks:
    for line in b.splitlines():
        out.append('"%s\\n"\n' % line)
out.append(';\n')
open(sys.argv[2], 'w').write(''.join(out))
print('%d certificats -> %s' % (len(blocks), sys.argv[2]))
