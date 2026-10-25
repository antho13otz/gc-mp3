#!/bin/sh
# v119 : essai FTP : serveur de test (latence par commande, connexions limitees) + scenario
#   sh tools/pc-sim/ftp-run.sh DOSSIER [scenario]   (FTP_DELAY=0.03 FTP_MAXCONN=2 ...)
D=$(cd "$(dirname "$0")" && pwd)
SC=${2:-$D/scenarios/ftp-test.txt}
python3 "$D/netsrv.py" "$1" > /tmp/ftpsrv.log 2>&1 &
SRV=$!
sleep 1
sh "$D/run.sh" "$SC"
kill $SRV 2>/dev/null
