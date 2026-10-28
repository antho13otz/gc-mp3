# Crédits / Credits

GC Station utilise le travail de nombreux projets libres. Merci à leurs auteurs.
GC Station is built on the work of many open-source projects. Thanks to their authors.

## Lanceur de secours / Backup launcher — Swiss (GPL-2.0)

Le lanceur de secours (`gc-mp3-loader.bin`, et `gc-mp3-igr.img` pour le retour en jeu) est compilé à partir de
[Swiss](https://github.com/emukidid/swiss-gc) (emukidid et contributeurs), commit `99b59e7a042cb5633cd341d86b7b92a65cd39851`,
avec les changements décrits dans `tools/mkloader.py`. Ces fichiers sont distribués sous **GPL-2.0** ; leur code source
correspondant = Swiss à ce commit + `tools/mkloader.py` (voir `.github/workflows/build.yml`).
Les en-têtes du disque de démarrage (`tools/mkdisc.py`) viennent aussi de Swiss.
Swiss reste la référence pour la compatibilité des jeux.

The backup launcher is built from Swiss at the commit above, patched by `tools/mkloader.py`, and is distributed under
GPL-2.0. Pulse (`source/natres`) is GC Station's own code, not derived from Swiss.

## Bibliothèques / Libraries

| Projet | Utilisé pour | Licence |
|---|---|---|
| [libogc](https://github.com/devkitPro/libogc), [libfat](https://github.com/devkitPro/libfat) (devkitPro) | système GameCube, carte SD | licences des projets |
| [devkitPPC](https://devkitpro.org) | compilateur | GPL (outil, pas inclus dans le programme) |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) 3.6.7 | HTTPS | Apache-2.0 |
| [minimp3](https://github.com/lieff/minimp3) | MP3 | CC0 |
| [dr_flac](https://github.com/mackron/dr_libs) | FLAC | domaine public / MIT-0 |
| [stb_vorbis, stb_image](https://github.com/nothings/stb) | OGG Vorbis, JPEG / PNG | domaine public / MIT |
| Helix AAC decoder (RealNetworks), via [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio) | AAC / M4A | RPSL / RCSL (RealNetworks) |
| [game-music-emu](https://github.com/libgme/game-music-emu) 0.6.5 | SPC, NSF, GBS, VGM… | LGPL-2.1 |
| [libxmp](https://github.com/libxmp/libxmp) 4.7.4 | MOD, XM, IT, S3M… | MIT |
| [libwebp](https://chromium.googlesource.com/webm/libwebp) 1.6.0 | images WebP | BSD-3-Clause |
| [TJpgDec](http://elm-chan.org/fsw/tjpgd/) R0.03 (ChaN) | JPEG des photos | licence libre de ChaN |
| [QR Code generator](https://github.com/nayuki/QR-Code-generator) (Nayuki) | QR codes | MIT |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) v0.9 (Bernhard Schelling), `source/tsf.h` | banques de sons SoundFont pour le MIDI | MIT |
| [DejaVu fonts](https://dejavu-fonts.github.io) | police de l'interface | licence Bitstream Vera / DejaVu |

Le reste (interface, lecteur, navigateur web et son moteur de pages, synthétiseur MIDI, client SMB2, Pulse,
compresseur deflate…) est écrit pour GC Station.
GC Station a été développé avec l'aide d'une IA (Claude, d'Anthropic).

Les noms GameCube et Nintendo appartiennent à Nintendo ; GC Station n'est ni fait ni approuvé par Nintendo.
Aucun jeu n'est fourni : uniquement des copies de tes propres disques.
- **Natural Earth** (www.naturalearthdata.com) : carte du monde du globe des radios (terres et lacs 1:50m), domaine public.
- **Cube Browser** (Arial-Bold, https://github.com/Arial-Bold/Cubebrowser), licence MIT : le navigateur de l'onglet WEB (`source/cb`, adapté pour GC Station ; licence dans `source/cb/LICENSE-CubeBrowser.txt`).
