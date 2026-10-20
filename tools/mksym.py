#!/usr/bin/env python3
# Table des symboles pour le journal de plantage : la liste des fonctions
# (adresse + nom) est ajoutee au programme, pour ecrire "plantage dans
# gc_decode" au lieu d'une simple adresse.
#   mksym.py --empty sortie.c                 table vide (1re edition des liens)
#   mksym.py NM programme.elf sortie.c        table des fonctions du programme
#   mksym.py --check NM avant.elf apres.elf   verifie que le code n'a pas bouge
import os, subprocess, sys

MIN_ADDR = int(os.environ.get("MKSYM_MIN", "0x80000000"), 16)


def exec_ranges(elf):
    """zones de code (sections executables) lues dans l'en-tete ELF"""
    import struct
    d = open(elf, "rb").read()
    if d[:4] != b"\x7fELF":
        return None
    is64, e = d[4] == 2, "<" if d[5] == 1 else ">"
    if is64:
        shoff, = struct.unpack_from(e + "Q", d, 0x28)
        shentsize, shnum = struct.unpack_from(e + "HH", d, 0x3A)
    else:
        shoff, = struct.unpack_from(e + "I", d, 0x20)
        shentsize, shnum = struct.unpack_from(e + "HH", d, 0x2E)
    ranges = []
    for i in range(shnum):
        o = shoff + i * shentsize
        if is64:
            flags, addr, _, size = struct.unpack_from(e + "QQQQ", d, o + 8)
        else:
            flags, addr, _, size = struct.unpack_from(e + "IIII", d, o + 8)
        if flags & 4 and size:                               # SHF_EXECINSTR
            ranges.append((addr, addr + size))
    return ranges


def text_symbols(nm, elf):
    ranges = exec_ranges(elf) or []
    out = subprocess.run([nm, "-n", "-C", "--defined-only", elf], capture_output=True, text=True, check=True).stdout
    syms, seen = [], set()
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) != 3 or parts[1] not in ("T", "t", "W", "w"):
            continue
        a = int(parts[0], 16)
        if a < MIN_ADDR or a in seen or not any(lo <= a < hi for lo, hi in ranges):
            continue
        seen.add(a)
        syms.append((a, parts[2][:80]))
    return syms


def c_string(names):
    out, line = [], '"'
    for n in names:
        s = n.replace("\\", "\\\\").replace('"', '\\"').encode("ascii", "replace").decode() + "\\0"
        if len(line) + len(s) > 100:
            out.append(line + '"')
            line = '"'
        line += s
    out.append(line + '"')
    return "\n".join(out)


def write_table(path, syms):
    offs, pos = [], 0
    for _, n in syms:
        offs.append(pos)
        pos += len(n.encode("ascii", "replace")) + 1
    with open(path, "w") as f:
        f.write("/* genere par tools/mksym.py */\n")
        f.write("const unsigned gcmp3_symtab_n = %d;\n" % len(syms))
        f.write("const unsigned gcmp3_sym_addr[%d] = {\n" % max(1, len(syms)))
        f.write(",\n".join("0x%08X" % a for a, _ in syms) or "0")
        f.write("\n};\nconst unsigned gcmp3_sym_off[%d] = {\n" % max(1, len(syms)))
        f.write(",\n".join(str(o) for o in offs) or "0")
        f.write("\n};\nconst char gcmp3_sym_names[] =\n%s;\n" % (c_string([n for _, n in syms]) if syms else '""'))


if len(sys.argv) == 3 and sys.argv[1] == "--empty":
    write_table(sys.argv[2], [])
elif len(sys.argv) == 5 and sys.argv[1] == "--check":
    a = text_symbols(sys.argv[2], sys.argv[3])
    b = text_symbols(sys.argv[2], sys.argv[4])
    if a != b:
        sys.exit("mksym: le code a bouge entre les deux editions des liens")
    print("mksym: table des symboles valide (%d fonctions)" % len(b))
elif len(sys.argv) == 4:
    syms = text_symbols(sys.argv[1], sys.argv[2])
    write_table(sys.argv[3], syms)
    print("mksym: %d fonctions" % len(syms))
else:
    sys.exit("usage: mksym.py --empty out.c | NM elf out.c | --check NM a.elf b.elf")
