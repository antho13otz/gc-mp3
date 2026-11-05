/* osk.h - on-screen keyboard used for the address bar, page forms and settings. */
#pragma once
#include <stdbool.h>

enum { OSK_TEXT = 0, OSK_URL = 1, OSK_IP = 2, OSK_PASSWORD = 3 };

typedef void (*OskDone)(bool accepted, const char *text, void *user);

void osk_open(const char *title, const char *initial, int maxlen, int mode, OskDone done, void *user);
bool osk_active(void);
int osk_phone_get(char *title, int tcap, char *out, int cap);   /* GC Station : clavier du telephone */
void osk_phone_set(const char *latin1, int accept);
void osk_frame(void);    /* update + draw; call after drawing the screen underneath */
