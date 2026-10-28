/* browser.h - the main browsing screen: toolbar, address bar, page view,
 * status bar, history and bookmarks. */
#pragma once
#include <stdbool.h>

void browser_init(void);
void browser_frame(bool interactive);   /* update + draw */
bool browser_take_settings_request(void);
void browser_navigate(const char *url);
void browser_navigate_replace(const char *url);   /* GC Station */
const char *browser_current_url(void);            /* GC Station */
void browser_clear_history(void);
void browser_clear_bookmarks(void);
void browser_open_music(void);   /* shows the music player panel */
/* GC Station v214 */
int browser_view(void);                       /* CBV_* (cb_host.h) */
int browser_bookmark_count(void);
void browser_bookmark(int i, const char **title, const char **url);
