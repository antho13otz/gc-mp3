#!/usr/bin/env python3
# v189 : lancement d'une appli avec le retour a GC Station : programme de copie (gl_stub de gclib.c) a son adresse,
# table des sections et arguments aux adresses de gclib.c, programme de retour (natret) en 0x80001800 : la copie doit
# laisser la table intacte et lancer l'appli (v187 : la fin du programme de copie ecrasait la table).
import os, sys, struct, re
sys.path.insert(0, os.environ.get('UNICORN_DIR', 'work/unicorn') + '/bindings/python')
os.environ['LIBUNICORN_PATH'] = os.environ.get('UNICORN_DIR', 'work/unicorn') + '/build'
from unicorn import *
from unicorn.ppc_const import *
src = open(sys.argv[1], encoding='latin-1').read()
words = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', re.search(r'static const u32 gl_stub\[\] = \{(.*?)\};', src, re.S).group(1))]
m = re.search(r'u32 stub_at = ret \? (0x[0-9A-Fa-f]+)u : GL_STUB_AT, tab_at = ret \? (0x[0-9A-Fa-f]+)u : GL_TAB_AT, argv_at = ret \? (0x[0-9A-Fa-f]+)u', src)
stub_at, tab_at, argv_at = (int(x, 16) for x in m.groups())
ret = open(sys.argv[2], 'rb').read()
mu = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN); mu.mem_map(0x80000000, 0x01800000)
text = struct.pack('>III', 0x38600042, 0x48000000, 0) + bytes(range(256)) * 20
data = bytes((i * 7) & 255 for i in range(5000))
dst = 0x81600000; mu.mem_write(dst, text); mu.mem_write(dst + 0x10000, data)
argv = struct.pack('>IIIIII', 0x5f617267, 0x816FFC00, 10, 0, 0, 0)
tab = [0x80003100, dst, len(text), 0x80400000, dst + 0x10000, len(data), 0x80003108, argv_at, 24]
# meme ordre que gl_boot_mem : table, arguments, programme de retour, puis programme de copie
mu.mem_write(tab_at, b''.join(struct.pack('>I', x) for x in tab)); mu.mem_write(argv_at, argv)
mu.mem_write(0x80001800, ret)
mu.mem_write(stub_at, b''.join(struct.pack('>I', w) for w in words))
ok = stub_at + len(words) * 4 <= tab_at and tab_at + len(tab) * 4 <= argv_at and 0x80001800 + len(ret) <= stub_at and argv_at + 24 <= 0x80002F00
for r, v in ((UC_PPC_REG_3, tab_at), (UC_PPC_REG_4, 3), (UC_PPC_REG_5, 0x80003100), (UC_PPC_REG_1, 0x80002FF0)): mu.reg_write(r, v)
mu.emu_start(stub_at, 0x80003104, count=10000000)
ok &= mu.reg_read(UC_PPC_REG_3) == 0x42 and bytes(mu.mem_read(0x80003100, 8)) == text[:8] and bytes(mu.mem_read(0x80003120, len(text) - 32)) == text[32:]
ok &= bytes(mu.mem_read(0x80400000, len(data))) == data and bytes(mu.mem_read(0x80003108, 24)) == argv
ok &= bytes(mu.mem_read(0x80001804, 8)) == b'STUBHAXX'
print('lancement d\'appli : copie %X, table %X, arguments %X, retour %d octets' % (stub_at, tab_at, argv_at, len(ret)))
print('TOUT OK' if ok else 'ECHEC')
