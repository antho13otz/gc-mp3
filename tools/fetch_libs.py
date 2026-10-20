#!/usr/bin/env python3
# Extrait de deux archives GitHub (versions figees) uniquement les fichiers
# utilises par GC-MP3, puis verifie leur empreinte globale.
#   game-music-emu 0.6.5 (SPC, NSF, GBS, VGM...)  -> dossier gme/
#   libxmp 4.7.4 partie "lite" (MOD, XM, IT, S3M) -> dossier xmp/
# Usage : python3 tools/fetch_libs.py gme.tar.gz xmp.tar.gz
import hashlib, os, shutil, sys, tarfile

GME_SHA = "cc518dd1e7860836802d841190ff88514c7bb7fe83ecb5d0c5afa66bc49669c5"
XMP_SHA = "1ba6812c5d21c520d6f88c5944ffea7d2beb8c8ed5dfc9703712759a82be1164"

GME_FILES = [
    "Ay_Apu.cpp", "Ay_Apu.h", "Ay_Cpu.cpp", "Ay_Cpu.h", "Ay_Emu.cpp",
    "Ay_Emu.h", "Blip_Buffer.cpp", "Blip_Buffer.h", "Classic_Emu.cpp",
    "Classic_Emu.h", "Data_Reader.cpp", "Data_Reader.h",
    "Dual_Resampler.cpp", "Dual_Resampler.h", "Effects_Buffer.cpp",
    "Effects_Buffer.h", "Fir_Resampler.cpp", "Fir_Resampler.h", "Gb_Apu.cpp",
    "Gb_Apu.h", "Gb_Cpu.cpp", "Gb_Cpu.h", "Gb_Oscs.cpp", "Gb_Oscs.h",
    "Gbs_Emu.cpp", "Gbs_Emu.h", "Gme_File.cpp", "Gme_File.h", "Gym_Emu.cpp",
    "Gym_Emu.h", "Hes_Apu.cpp", "Hes_Apu.h", "Hes_Apu_Adpcm.cpp",
    "Hes_Apu_Adpcm.h", "Hes_Cpu.cpp", "Hes_Cpu.h", "Hes_Emu.cpp",
    "Hes_Emu.h", "Kss_Cpu.cpp", "Kss_Cpu.h", "Kss_Emu.cpp", "Kss_Emu.h",
    "Kss_Scc_Apu.cpp", "Kss_Scc_Apu.h", "M3u_Playlist.cpp", "M3u_Playlist.h",
    "Multi_Buffer.cpp", "Multi_Buffer.h", "Music_Emu.cpp", "Music_Emu.h",
    "Nes_Apu.cpp", "Nes_Apu.h", "Nes_Cpu.cpp", "Nes_Cpu.h",
    "Nes_Fds_Apu.cpp", "Nes_Fds_Apu.h", "Nes_Fme7_Apu.cpp", "Nes_Fme7_Apu.h",
    "Nes_Mmc5_Apu.h", "Nes_Namco_Apu.cpp", "Nes_Namco_Apu.h", "Nes_Oscs.cpp",
    "Nes_Oscs.h", "Nes_Vrc6_Apu.cpp", "Nes_Vrc6_Apu.h", "Nes_Vrc7_Apu.cpp",
    "Nes_Vrc7_Apu.h", "Nsf_Emu.cpp", "Nsf_Emu.h", "Nsfe_Emu.cpp",
    "Nsfe_Emu.h", "Sap_Apu.cpp", "Sap_Apu.h", "Sap_Cpu.cpp", "Sap_Cpu.h",
    "Sap_Emu.cpp", "Sap_Emu.h", "Sms_Apu.cpp", "Sms_Apu.h", "Sms_Oscs.h",
    "Snes_Spc.cpp", "Snes_Spc.h", "Spc_Cpu.cpp", "Spc_Cpu.h", "Spc_Dsp.cpp",
    "Spc_Dsp.h", "Spc_Emu.cpp", "Spc_Emu.h", "Spc_Filter.cpp",
    "Spc_Filter.h", "Vgm_Emu.cpp", "Vgm_Emu.h", "Vgm_Emu_Impl.cpp",
    "Vgm_Emu_Impl.h", "Ym2413_Emu.cpp", "Ym2413_Emu.h", "Ym2612_Emu.h",
    "Ym2612_GENS.cpp", "Ym2612_GENS.h", "blargg_common.h", "blargg_config.h",
    "blargg_endian.h", "blargg_source.h", "ext/emu2413.c", "ext/emu2413.h",
    "gb_cpu_io.h", "gme.cpp", "gme.h", "gme_types.h", "hes_cpu_io.h",
    "nes_cpu_io.h", "sap_cpu_io.h",
]

XMP_FILES = [
    "include/xmp.h", "src/callbackio.h", "src/common.h", "src/control.c",
    "src/dataio.c", "src/effects.c", "src/effects.h", "src/extras.h",
    "src/far_extras.h", "src/filetype.c", "src/filter.c", "src/flow.c",
    "src/flt_extras.h", "src/format.c", "src/format.h", "src/hio.c",
    "src/hio.h", "src/hmn_extras.h", "src/lfo.c", "src/lfo.h", "src/load.c",
    "src/load_helpers.c", "src/loaders/common.c", "src/loaders/iff.h",
    "src/loaders/it.h", "src/loaders/it_load.c", "src/loaders/itsex.c",
    "src/loaders/loader.h", "src/loaders/lzw.h", "src/loaders/med.h",
    "src/loaders/mod.h", "src/loaders/mod_load.c", "src/loaders/s3m.h",
    "src/loaders/s3m_load.c", "src/loaders/sample.c", "src/loaders/vorbis.h",
    "src/loaders/xm.h", "src/loaders/xm_load.c", "src/md5.h",
    "src/mdataio.h", "src/med_extras.h", "src/memio.c", "src/memio.h",
    "src/miniz.h", "src/misc.c", "src/mix_all.c", "src/mix_all.h",
    "src/mixer.c", "src/mixer.h", "src/path.h", "src/paula.h",
    "src/period.c", "src/period.h", "src/player.c", "src/player.h",
    "src/precomp_blep.h", "src/precomp_lut.h", "src/read_event.c",
    "src/rng.c", "src/rng.h", "src/scan.c", "src/smix.c", "src/tempfile.h",
    "src/virtual.c", "src/virtual.h", "src/win32.c",
]


# Correctifs appliques apres verification (bugs trouves en testant des
# fichiers abimes) : NSF avec toutes les puces d'extension = jusqu'a 29 voix,
# mais la table des types de voix n'en a que 16 (lecture hors tableau) ;
# et une instruction illegale n'avancait pas l'horloge (lecture bloquee).
PATCHES = {
    "gme/Nsf_Emu.cpp": [(
        "wave_type  |10, wave_type  |11, wave_type |12, wave_type |13\n\t};",
        "wave_type  |10, wave_type  |11, wave_type |12, wave_type |13,\n"
        "\t\twave_type  |14, wave_type  |15, wave_type |16, wave_type |17,\n"
        "\t\twave_type  |18, wave_type  |19, wave_type |20, wave_type |21,\n"
        "\t\twave_type  |22, wave_type  |23, wave_type |24, wave_type |25,\n"
        "\t\twave_type  |26, wave_type  |27, wave_type |28, wave_type |29,\n"
        "\t\twave_type  |30, wave_type  |31, wave_type |32, wave_type |33\n\t};",
    ), (
        # instruction illegale : le temps n'avancait plus (blocage sur un NSF abime)
        "\t\t\t\tr.pc++;\n",
        "\t\t\t\tr.pc++;\n\t\t\t\tset_time( time() + 6 );\n",
    )],
}


def patch():
    for path, subs in PATCHES.items():
        with open(path) as f:
            s = f.read()
        for old, new in subs:
            if s.count(old) != 1:
                sys.exit("ERREUR: correctif impossible dans " + path)
            s = s.replace(old, new)
        with open(path, "w") as f:
            f.write(s)
        print("corrige", path)


def extract(archive, prefix, files, dest, want):
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    h = hashlib.sha256()
    with tarfile.open(archive, "r:gz") as t:
        members = {}
        for m in t.getmembers():
            parts = m.name.split("/", 1)
            if len(parts) == 2 and m.isfile():
                members[parts[1]] = m
        for f in files:
            m = members.get(prefix + f)
            if m is None:
                sys.exit("ERREUR: %s absent de %s" % (f, archive))
            data = t.extractfile(m).read()
            h.update(data)
            out = os.path.join(dest, f)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, "wb") as o:
                o.write(data)
    if h.hexdigest() != want:
        sys.exit("ERREUR: empreinte incorrecte pour %s" % archive)
    print("OK", dest, len(files), "fichiers")


if len(sys.argv) != 3:
    sys.exit("usage: fetch_libs.py gme.tar.gz xmp.tar.gz")
extract(sys.argv[1], "gme/", GME_FILES, "gme", GME_SHA)
extract(sys.argv[2], "", XMP_FILES, "xmp", XMP_SHA)
patch()
