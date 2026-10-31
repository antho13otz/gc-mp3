#!/bin/bash
# v232 : tests automatiques de GC Station dans le simulateur (compilation en ligne, et a la main).
#   bash tools/pc-sim/ci.sh            (depuis la racine du depot)
# 1. compile le simulateur avec AddressSanitizer (debordements, memoire liberee puis relue...)
# 2. lance des scenarios (demarrage, tour des onglets, accueil de Cube Browser)
# 3. pilote GC Station comme le telephone : lien, clavier, souris, photo, page memoire
# Echoue (code 1) si GC Station s'arrete, si AddressSanitizer trouve une erreur, ou si le telephone n'a pas
# la bonne reponse. Journaux et captures : tools/pc-sim/work/run/ci/
set -u
R=$(pwd); P=$R/tools/pc-sim; RUN=$P/work/run; CI=$RUN/ci
BIN=$P/gcstation-asan
FAIL=0
fail() { echo "ECHEC : $*"; FAIL=1; [ -n "${GITHUB_ACTIONS:-}" ] && echo "::error::$*"; }
note() { echo "-- $*"; }
pause() { python3 -c "import time; time.sleep($1)"; }

[ -d $P/work/libs/mbedtls ] || sh $P/fetch.sh || { echo "bibliotheques introuvables"; exit 1; }
[ -d "$RUN/sd:/mp3" ] || sh $P/demo-sd.sh $RUN || { echo "carte SD de demonstration impossible"; exit 1; }
note "compilation (AddressSanitizer)"
make -s -C $P -j"$(nproc 2>/dev/null || echo 2)" O=work/obj-asan BIN=gcstation-asan SAN="-fsanitize=address -fno-omit-frame-pointer" 2>&1 | grep -E " error:|\*\*\* " && { echo "compilation ratee"; exit 1; }
[ -x $BIN ] || { echo "compilation ratee"; exit 1; }
rm -rf $CI; mkdir -p $CI
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export PCSIM_PAL=1 PCSIM_COMPOSANTE=1

check_log() {   # check_log <nom> <code de sortie>
    local log=$CI/$1.log
    if grep -q "ERROR: AddressSanitizer" $log; then
        fail "$1 : erreur AddressSanitizer"
        grep -A14 "ERROR: AddressSanitizer" $log | grep "ERROR\|#[0-9] \|READ\|WRITE" | head -14
    elif [ "$2" != 0 ]; then
        fail "$1 : GC Station s'est arrete (code $2)"; tail -15 $log
    elif ! grep -qE "fin du scenario|\[pcsim\] quit" $log; then
        fail "$1 : scenario pas termine"; tail -15 $log
    else note "$1 : ok"; fi
}

# --- scenarios ---
for sc in tour web-accueil; do
    note "scenario $sc"
    mkdir -p $CI/$sc
    (cd $RUN && PCSIM_FAST=1 PCSIM_SCRIPT=$P/scenarios/$sc.txt PCSIM_OUT=$CI/$sc timeout 600 $BIN > $CI/$sc.log 2>&1)
    check_log $sc $?
done

# --- telephone ---
note "telephone"
mkdir -p $CI/tel
printf 'wait 90s\nshot fin\n' > $CI/tel.txt
rm -f "$RUN/sd:/photos/telephone-ci.jpg"
(cd $RUN && PCSIM_SCRIPT=$CI/tel.txt PCSIM_OUT=$CI/tel timeout 300 $BIN > $CI/telephone.log 2>&1; echo $? > $CI/tel.code) &
SIM=$!
H=""
for i in $(seq 1 120); do            # serveur du telephone : port 80 (root) ou 8080 (simulateur : +8000)
    for port in 80 8080; do
        if curl -s -m 2 "http://127.0.0.1:$port/api/state" 2>/dev/null | grep -q '"title"'; then H="http://127.0.0.1:$port"; break 2; fi
    done
    pause 0.5
done
expect() {   # expect <description> <motif> <commande curl...>
    local what=$1 pat=$2; shift 2
    local r="" k
    for k in 1 2 3; do                     # (reponse vide : serveur occupe ; etat pas encore a jour : on retente)
        r=$(curl -s -m 15 "$@" 2>&1)
        echo "$r" | grep -q -- "$pat" && { note "telephone : $what"; return; }
        [ -n "$r" ] && ! echo "$*" | grep -q "api/state" && break
        pause 2
    done
    fail "telephone : $what (reponse : ${r:0:200})"
}
if [ -z "$H" ]; then
    fail "telephone : le serveur ne repond pas"
else
    expect "page" "GC Station" "$H/"
    expect "lien vers Cube Browser" '"ok":1' "$H/api/open?u=example.com"
    pause 2
    expect "adresse de la page" '"web":"https://example.com' "$H/api/state"
    expect "touche X (clavier d'adresse)" '"ok":1' "$H/api/key?k=x"
    pause 1.5
    expect "clavier ouvert vu par le telephone" '"kb":1' "$H/api/state"
    expect "texte tape" '"ok":1' "$H/api/type?go=0&t=gamecube%20%C3%A9t%C3%A9"
    pause 1
    expect "texte affiche (accents)" '"kbt":"gamecube été"' "$H/api/state"
    expect "texte valide" '"ok":1' "$H/api/type?go=1&t=gamecube"
    pause 1
    expect "souris" '"ok":1' "$H/api/mouse?dx=60&dy=30&w=200&c=1"
    expect "manette" '"ok":1' "$H/api/key?k=b"
    python3 -c "
from PIL import Image
Image.new('RGB', (800, 600), (40, 120, 200)).save('$CI/telephone-ci.jpg', quality=85)" 2>/dev/null
    if [ -f $CI/telephone-ci.jpg ]; then
        expect "envoi d'une photo" "ok" -T $CI/telephone-ci.jpg "$H/upload?dir=photos&name=telephone-ci.jpg"
        expect "photo sur la tele" '"ok":1' "$H/api/showname?n=telephone-ci.jpg"
        pause 2
    fi
    # v301 : jaquettes et disques envoyes par le telephone
    jl=$(curl -s -m 20 "$H/api/jaqliste")
    jid=$(echo "$jl" | python3 -c "import json,sys; l=json.load(sys.stdin); print(l[0][0] if l else '')" 2>/dev/null)
    if [ -n "$jid" ]; then
        note "telephone : liste des jaquettes ($(echo "$jl" | grep -o "\[\"" | wc -l) jeux)"
        python3 -c "
from PIL import Image
Image.new('RGBA', (160, 160), (200, 40, 40, 255)).save('$CI/disque-ci.png')" 2>/dev/null
        expect "envoi de l'image d'un disque" "ok" -T $CI/disque-ci.png "$H/upload?dir=jaquette&nid=$jid&kind=d&name=d.png"
        expect "image du disque vue par la console" "\"$jid\",[01],[01],[01],1" "$H/api/jaqliste"
        expect "type d'image refuse" "refus" -T $CI/disque-ci.png "$H/upload?dir=jaquette&nid=$jid&kind=z&name=d.png"
        rm -f "$RUN/sd:/gc-mp3/jaquettes/disques/$jid.png" "$RUN/sd:/gc-mp3/jaquettes/disques/$jid.tex"
    else fail "telephone : liste des jaquettes vide ($jl)"; fi
    # v303 : tele tactile (toucher un endroit de l'image) : refus si hors de l'image, puis un onglet touche
    expect "tele tactile : hors de l'image refuse" '"ok":0' "$H/api/toucher?u=2&v=0.5&c=1"
    expect "tele tactile : fleche deplacee" '"ok":1' "$H/api/toucher?u=0.5&v=0.5&c=0"
    expect "page memoire" "moire de la GameCube" "$H/memoire"
    expect "chiffres de la memoire" '"hist":\[' "$H/api/memoire"
    expect "page rapport de bug" "Rapport de bug" "$H/rapport"
    expect "journal des dernieres actions" "Derni" "$H/api/journal"
    r=$(curl -s -m 20 -o $CI/capture.bmp -w "%{http_code} %{size_download}" "$H/capture.bmp")
    case "$r" in "200 "*) [ "${r#200 }" -gt 100000 ] && note "telephone : capture de la tele" || fail "telephone : capture trop petite ($r)";; *) fail "telephone : capture de la tele ($r)";; esac
    # v285 : second ecran : la tele en JPEG, petite et nette, lisible par Python (Pillow) si present
    for w in 320 640; do
        r=$(curl -s -m 20 -o $CI/ecran$w.jpg -w "%{http_code} %{size_download}" "$H/api/ecran?w=$w&q=60")
        case "$r" in "200 "*) if [ "$(head -c 2 $CI/ecran$w.jpg | od -An -tx1 | tr -d ' ')" = ffd8 ] && \
                                ! python3 -c "from PIL import Image; i=Image.open('$CI/ecran$w.jpg'); i.load(); assert i.size[0]==$w, i.size" 2>&1 | grep -q Error; then
                                note "telephone : second ecran $w ($r octets)"; else fail "telephone : second ecran $w : image illisible"; fi;;
                         *) fail "telephone : second ecran $w ($r)";; esac
    done
    # v294 : images partielles : une image entiere (s=0), puis seulement ce qui a change (ou rien : 204)
    r=$(curl -s -m 20 -D $CI/ecranz.h -o $CI/ecranz.jpg -w "%{http_code}" "$H/api/ecranz?w=320&q=60&s=0")
    sq=$(grep -i '^X-Seq' $CI/ecranz.h | tr -d '\r' | awk '{print $2}')
    if [ "$r" = 200 ] && grep -qi '^X-Plein: 1' $CI/ecranz.h && [ -n "$sq" ]; then
        r2=$(curl -s -m 20 -D $CI/ecranz2.h -o $CI/ecranz2.bin -w "%{http_code}" "$H/api/ecranz?w=320&q=60&s=$sq")
        case "$r2" in 200|204) grep -qi '^X-Plein: 0' $CI/ecranz2.h && note "telephone : images partielles (image entiere puis $r2)" || fail "telephone : images partielles : 2e image entiere";;
                      *) fail "telephone : images partielles ($r2)";; esac
    else fail "telephone : images partielles : 1re image ($r)"; fi
    expect "etat apres tout ca" '"title"' "$H/api/state"
fi
wait $SIM
check_log telephone "$(cat $CI/tel.code 2>/dev/null || echo 1)"
rm -f "$RUN/sd:/photos/telephone-ci.jpg"

[ $FAIL = 0 ] && echo "TOUS LES TESTS SONT PASSES" || echo "DES TESTS ONT ECHOUE"
exit $FAIL
