/* cb_stubs.c - GC Station : les parties de Cube Browser qui ne servent pas dans GC Station (sons du
 * navigateur, lecteur MIDI, Game Boy Advance, mises a jour de Cube Browser et de Swiss, lancement de .dol) :
 * GC Station a deja les siennes. Ces fonctions repondent "rien a faire". */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "sound.h"
#include "midi.h"
#include "music_ui.h"
#include "update.h"
#include "swissupd.h"
#include "dolload.h"
#include "gbalink.h"

void sound_init(void) {}
void sound_play(Sfx s) { (void)s; }
void sound_shutdown(void) {}

void midi_init(void) {}
void midi_shutdown(void) {}
void midi_release_memory(void) {}
int  midi_scan(void) { return 0; }
int  midi_count(void) { return 0; }
const char *midi_file(int i) { (void)i; return ""; }
int  midi_current(void) { return -1; }
void midi_play(int i) { (void)i; }
void midi_toggle_pause(void) {}
void midi_stop(void) {}
void midi_next(void) {}
void midi_prev(void) {}
void midi_set_shuffle(bool on) { (void)on; }
void midi_set_repeat(bool on) { (void)on; }
bool midi_shuffle(void) { return false; }
bool midi_repeat(void) { return false; }
MidiState midi_state(void) { return MIDI_STOPPED; }
const char *midi_title(void) { return ""; }
float midi_position(void) { return 0; }
float midi_length(void) { return 0; }
int   midi_voices_in_use(void) { return 0; }
const char *midi_instruments(void) { return ""; }
const char *midi_message(void) { return ""; }
float midi_dsp_load(void) { return 0; }
unsigned midi_audio_irq_worst_us(void) { return 0; }
void midi_instruments_changed(void) {}

void music_ui_reset(void) {}
bool music_ui_draw(bool interactive) { (void)interactive; return false; }

UpdateState update_state(void) { return UPD_LATEST; }
UpdateStep  update_step(void) { return UPD_STEP_DOWNLOAD; }
const char *update_latest_version(void) { return ""; }
const char *update_notes(void) { return ""; }
const char *update_error(void) { return "Mises \xC3\xA0 jour : START > Mise \xC3\xA0 jour de GC Station"; }
const char *update_target(void) { return ""; }
void update_request_check(bool quiet) { (void)quiet; }
void update_request_install(void) {}
void update_poll(void) {}
bool update_restart(char *err, size_t errlen) { snprintf(err, errlen, "Non disponible dans GC Station"); return false; }
void update_worker_check(volatile int *cancel) { (void)cancel; }
void update_worker_install(volatile int *cancel) { (void)cancel; }

UpdateState swiss_state(void) { return UPD_NONE; }
UpdateStep  swiss_step(void) { return UPD_STEP_DOWNLOAD; }
const char *swiss_latest(void) { return ""; }
const char *swiss_installed(void) { return ""; }
const char *swiss_notes(void) { return ""; }
const char *swiss_error(void) { return "Non disponible dans GC Station"; }
const char *swiss_target(void) { return ""; }
void swiss_request_check(void) {}
void swiss_request_install(void) {}
void swiss_poll(void) {}
bool swiss_launch(char *err, size_t errlen) { snprintf(err, errlen, "Non disponible dans GC Station"); return false; }
void swiss_worker_check(volatile int *cancel) { (void)cancel; }
void swiss_worker_install(volatile int *cancel) { (void)cancel; }
bool swiss_find_dol(char *out, size_t cap) { (void)out; (void)cap; return false; }
void swiss_destination(char *out, size_t cap) { if (cap) out[0] = 0; }

bool dol_check(const char *path, char *err, size_t errlen) { (void)path; snprintf(err, errlen, "Non disponible dans GC Station"); return false; }
bool dol_launch(const char *path, char *err, size_t errlen) { (void)path; snprintf(err, errlen, "Non disponible dans GC Station"); return false; }
void system_restart(void) {}
int dol_find(const char *root, char (*out)[256], int max) { (void)root; (void)out; (void)max; return 0; }

void gbalink_init(void) {}
void gbalink_frame(void) {}
bool gbalink_linked(void) { return false; }
