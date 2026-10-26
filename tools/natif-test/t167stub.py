#!/usr/bin/env python3
# v167 : le petit programme de saut de Pulse (na_zstub de natif.c) dans l'emulateur PowerPC : copies, mise a zero
# du BSS (source 0), puis saut au point d'entree. Memoire remplie de restes (0xA5) avant.
import os, sys, struct, re
os.environ['LIBUNICORN_PATH'] = os.environ.get('UNICORN_DIR', 'work/unicorn') + '/build'
sys.path.insert(0, os.environ.get('UNICORN_DIR', 'work/unicorn') + '/bindings/python')
from unicorn import *
from unicorn.ppc_const import *
src = open(sys.argv[1], encoding='latin-1').read()
m = re.search(r'static const u32 na_zstub\[\] = \{(.*?)\};', src, re.S)
stub = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', m.group(1))]
mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
mu.mem_map(0x80000000, 0x00400000)
mu.mem_write(0x80003000, b'\xA5' * 0x3FD000)                 # (restes de GC Station)
mu.mem_write(0x80001800, b''.join(struct.pack('>I', w) for w in stub))
src_data = bytes(range(256)) * 4                               # 1 Ko de "section"
mu.mem_write(0x80380000, src_data)
tab = [0x80100000, 0, 0x10001,                                 # BSS : taille impaire (fin d'octet a octet)
       0x80200000, 0x80380000, len(src_data),                  # une section
       0x80300000, 0, 0x40]                                    # un 2e morceau de BSS
mu.mem_write(0x80002000, b''.join(struct.pack('>I', w) for w in tab))
mu.mem_write(0x80003100, struct.pack('>I', 0x48000000))        # point d'entree : b . (boucle)
mu.reg_write(UC_PPC_REG_3, 0x80002000); mu.reg_write(UC_PPC_REG_4, 3); mu.reg_write(UC_PPC_REG_5, 0x80003100)
try: mu.emu_start(0x80001800, 0x80003100, count=2000000)
except UcError as e: print('erreur emulateur', e)
pc = mu.reg_read(UC_PPC_REG_PC)
z1 = mu.mem_read(0x80100000, 0x10001) == b'\0' * 0x10001
z1b = mu.mem_read(0x80110001, 4) == b'\xA5' * 4                 # (rien apres la fin)
cp = mu.mem_read(0x80200000, len(src_data)) == src_data
z2 = mu.mem_read(0x80300000, 0x40) == b'\0' * 0x40 and mu.mem_read(0x80300040, 4) == b'\xA5' * 4
bef = mu.mem_read(0x800FFFFC, 4) == b'\xA5' * 4
ok = pc == 0x80003100 and z1 and z1b and cp and z2 and bef
print(('OK   ' if ok else 'FAUX ') + 'saut de Pulse : BSS mis a zero (%s/%s), section copiee (%s), saut au point d\'entree (pc=%08X)' % (z1 and z1b and bef, z2, cp, pc))
print('TOUT OK' if ok else 'ECHEC')
