# Banc d'essai du câble GameCube ↔ GBA (8 novembre 2026)

Fait tourner un programme GBA « multiboot » dans mGBA (la même version que l'appli) face à une GameCube simulée qui
suit l'algorithme de Pac-Man vs. (PRJP01, lu dans son main.dol : 0x8000BA24 poignée de main, 0x8000B6D0 réception).
Aucun fichier de jeu dans le dépôt : le programme GBA vient du disque du propriétaire (gbademo/packman.bin, avec le code
« PRJP » en 0xAC, comme le jeu l'envoie).

    cmake -S tools/gba-test -B /tmp/gbt && cmake --build /tmp/gbt
    /tmp/gbt/hote packman_prjp.bin 0 <pause en cycles> <1 = attendre JOYCNT & 7 == 0>

Résultats : pause 1500 sans attente → poignée de main bloquée (JOYSTAT 0x22, mot jamais lu) ; pause 20000, ou pause 300
avec attente → connecté (0x30), puis paquets de la GBA. D'où la règle de gba_jni.c (build 482).
