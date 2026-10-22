#!/usr/bin/env python3
# GC Station v49 : fichier binaire -> tableau C (programme resident du lanceur natif)
import sys
src, dst, name = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(src, 'rb').read()
if len(data) < 0x40 or data[:4] != b'GCSN':
    sys.exit('bin2h : %s ne commence pas par GCSN' % src)
with open(dst, 'w') as f:
    f.write('/* genere par tools/bin2h.py a partir de %s : ne pas modifier */\n' % src)
    f.write('static const unsigned char %s[%d] __attribute__((aligned(32))) = {\n' % (name, len(data)))
    for i in range(0, len(data), 16):
        f.write('    ' + ', '.join('0x%02X' % b for b in data[i:i + 16]) + ',\n')
    f.write('};\n')
print('%s : %d octets' % (dst, len(data)))
