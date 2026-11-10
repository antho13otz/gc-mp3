#!/bin/bash
# v223 : .dol compresse (xz, avec le "packer" de Swiss), pour le reglage "Version compressee".
# Image : ghcr.io/extremscorner/libogc2:latest. Usage : bash ci/compresse.sh NUMERO (gc-mp3player.elf du job console)
set -euo pipefail
N=$1
SWISS_COMMIT=99b59e7a042cb5633cd341d86b7b92a65cd39851
S=$(command -v sudo || true)
python3 - <<'PY'
import struct, sys
d = open("gc-mp3player.elf", "rb").read()
if d[:4] != b"\x7fELF": sys.exit("ERREUR: pas un ELF")
e = struct.unpack_from(">I", d, 24)[0]
phoff, phn = struct.unpack_from(">I", d, 28)[0], struct.unpack_from(">H", d, 44)[0]
ok = False
for i in range(phn):
    typ, off, va, pa, fsz = struct.unpack_from(">5I", d, phoff + i * 32)
    if typ == 1 and fsz and va <= e < va + fsz and d[off + e - va + 4:off + e - va + 8] == b"_arg": ok = True
print("depart : 0x%08X" % e)
if e != 0x80003100 or not ok: sys.exit("ERREUR: depart inattendu : le packer de Swiss demarre en 0x80003100")
PY
$S apt-get update -q >/dev/null
$S apt-get install -y -q --no-install-recommends -t bookworm-backports 7zip >/dev/null
rm -rf work-swiss
git init -q work-swiss
git -C work-swiss remote add origin https://github.com/emukidid/swiss-gc.git
git -C work-swiss sparse-checkout set cube/packer
git -C work-swiss fetch -q --depth 1 origin $SWISS_COMMIT
git -C work-swiss checkout -q FETCH_HEAD
cd work-swiss/cube
# notre programme (libogc) n'a que le bloc _arg : le bloc _env de Swiss (depart + 40) ecraserait son code
grep -q 'memmove(entrypoint + 40, &__envp' packer/source/main.c
sed -i '/memmove(entrypoint + 40, &__envp/d' packer/source/main.c
mkdir -p swiss
cp ../../gc-mp3player.elf swiss/swiss.elf
make -C packer
[ "$(stat -c%s packer/build/executable.bin)" -lt 12000000 ] || { echo "image trop grande"; exit 1; }
cd ../..
mkdir -p sortie
cp work-swiss/cube/packer/swiss.dol sortie/gc-station-compresse-b$N.bin
python3 -I ci/dol.py sortie/gc-station-compresse-b$N.bin $N sans-verif
