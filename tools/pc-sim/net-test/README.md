# Banc d'essai radio / musique (576p 50 Hz, faux Internet local)

`sh tools/pc-sim/net-test/suite.sh` : prepare les flux et certificats (setup.sh), lance un serveur HTTPS / HTTP local
pour tous les sites de `hosts.txt` (radios de test de `stations.txt` : redirection, ICY, m3u, pls, AAC, OGG, HLS,
coupure, serveur lent ; pochettes MusicBrainz / Cover Art Archive, paroles LRCLIB), puis joue chaque scenario de `sc/`.
Besoins : openssl, ffmpeg, python3 + Pillow, port 80 et 443 (root). Resultats : `tools/pc-sim/work/net-test/res/`.
