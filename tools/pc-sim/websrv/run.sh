#!/bin/sh
# Banc d'essai du navigateur : serveur de test + simulateur ; la page est ouverte comme depuis le telephone.
#   sh tools/pc-sim/websrv/run.sh [page.html] [scenario]
D=$(cd "$(dirname "$0")" && pwd)
PAGE=${1:-page.html}
SC=${2:-$D/../scenarios/web-test.txt}
python3 "$D/serve.py" 8765 > /tmp/websrv.log 2>&1 &
SRV=$!
( sleep 6; for i in $(seq 1 150); do for P in 80 8080; do curl -s --max-time 2 "http://127.0.0.1:$P/api/open?u=http%3A%2F%2F127.0.0.1%3A8765%2F$PAGE" | grep -q ok && exit 0; done; sleep 1; done ) &
sh "$D/../run.sh" "$SC"
kill $SRV 2>/dev/null
