/* storage.c - mounts the SD card through libfat and picks the data folder.
 * The SD2SP2 sits in Serial Port 2; SD Gecko adapters in memory card slots
 * A/B are tried as fallbacks. See storage.h for how the folder is chosen. */
#include "storage.h"
#include "cb_host.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool mounted;
static const char *device = "none";
static char data_dir[320] = STORAGE_ROOT_DIR;
static char prog_dir[4], prog_file[4], moved_from[4];

/* GC Station : la carte SD est deja montee ; le dossier des donnees du navigateur est SD:/gc-mp3/web. */
bool storage_init(const char *argv0) {
    (void)argv0;
    snprintf(data_dir, sizeof(data_dir), "%s", cb_host_data_dir());
    mounted = data_dir[0] != 0;
    device = "GC Station";
    if (mounted) mkdir(data_dir, 0777);
    return mounted;
}

bool storage_ok(void) { return mounted; }
const char *storage_device_name(void) { return device; }
const char *storage_dir(void) { return data_dir; }
const char *storage_program_dir(void) { return prog_dir[0] ? prog_dir : NULL; }
const char *storage_program_file(void) { return prog_file[0] ? prog_file : NULL; }
bool storage_dir_is_root(void) { return strcmp(data_dir, STORAGE_ROOT_DIR) == 0; }
const char *storage_moved_from(void) { return moved_from[0] ? moved_from : NULL; }

void storage_path(char *out, size_t cap, const char *name) {
    snprintf(out, cap, "%s/%s", data_dir, name);
}

bool storage_move_data(bool to_root, char *msg, size_t cap) {
    (void)to_root;
    snprintf(msg, cap, "Le dossier du navigateur est %s (GC Station)", data_dir);
    return false;
}

char *storage_read(const char *path, size_t *len_out) {
    if (!mounted) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > 8 * 1024 * 1024) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = 0;
    if (len_out) *len_out = got;
    return buf;
}

/* Write to a temp file, then swap it in, so a power cut never leaves a
 * half-written file in place. settings_load() also checks for the temp file. */
bool storage_write_atomic(const char *path, const char *tmp_path, const void *data, size_t len) {
    if (!mounted) return false;
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fflush(f) == 0) && ok;
    fclose(f);
    if (!ok) { remove(tmp_path); return false; }
    remove(path);
    return rename(tmp_path, path) == 0;
}
