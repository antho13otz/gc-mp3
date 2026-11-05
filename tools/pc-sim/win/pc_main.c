/* GC Station PC (Windows) : main.c de GC Station, plus les entrees propres au PC, dans la meme unite de compilation
   (elles touchent aux variables de main.c et tab_phone.c). Ce fichier est hors de source/ : la console n'en voit rien,
   et le changer ne fait pas de nouvelle version de la console (build.yml). */
#include "../../../source/main.c"

/* ---------- GC Station PC (Windows) seulement : souris, molette, texte tape au clavier, touches multimedia ----------
   (tools/pc-sim/pcsim_win.c ; appele par la boucle, a la lecture des manettes ; rien de tout ceci sur la console)
   La souris passe par le meme chemin que la tele tactile de l'appli (/api/toucher) : la fleche va ou elle pointe, le
   clic gauche = A sur ce qui est dessous, le clic droit = B ; la molette fait defiler (croix haut / bas, ou la page web). */
bool osk_active(void);                                    /* (cb/osk.c) */
void gcs_pc_touch(float u, float v, int c) { tp_tu = u; tp_tv = v; tp_tc = c; tp_touch = 1; }
void gcs_pc_wheel(int w) {
    if (!w) return;
    if (tab == TAB_WEB && !menu_open && !tl_screen && !kb_open) { tp_mw -= w * 40; return; }   /* (w > 0 : vers le haut) */
    tp_keys |= w > 0 ? PAD_BUTTON_UP : PAD_BUTTON_DOWN;
}
void gcs_pc_media(int k) {                                /* 0 lecture / pause, 1 suivant, 2 precedent, 3 stop */
    static const int c[4] = { PC_TOGGLE, PC_NEXT, PC_PREV, PC_STOP };
    if (k >= 0 && k < 4) { tp_push(c[k], 0); last_input_ms = now_ms(); }
}
/* le clavier du PC tape directement : 1 si une zone de texte a la main (clavier de GC Station, ou celui du web) */
int gcs_pc_text_active(void) { return kb_open || (tab == TAB_WEB && osk_active()); }
/* cp : une lettre (Unicode) ; k : 1 effacer, 2 valider, 3 fermer, 4 gauche, 5 droite, 6 chercher (Ctrl + F / Ctrl + L :
   la barre d'adresse sur WEB, la recherche sur MUSIQUE, comme une lettre tapee au clavier USB de la console : gckb.c) */
void gcs_pc_text(unsigned cp, int k) {
    static char ob[300]; static int was = 0;
    last_input_ms = now_ms(); saver_on = 0;
    if (k == 6) {
        if (kb_open || menu_open || tl_screen) return;
        if (tab == TAB_WEB) kb_start(KB_URL, "", -1, -1);
        else if (tab == TAB_MUSIC && !am_open) kb_start(KB_LIBFIND, "", -1, -1);
        return;
    }
    if (kb_open) {
        if (cp) {
            char u[5]; int n = 0;
            if (cp < 0x80) u[n++] = (char)cp;
            else if (cp < 0x800) { u[n++] = (char)(0xC0 | cp >> 6); u[n++] = (char)(0x80 | (cp & 63)); }
            else { u[n++] = (char)(0xE0 | cp >> 12); u[n++] = (char)(0x80 | ((cp >> 6) & 63)); u[n++] = (char)(0x80 | (cp & 63)); }
            u[n] = 0;
            kb_append(u);
        }
        if (k == 1) kb_backspace();
        if (k == 2) kb_validate();
        if (k == 3) kb_open = 0;
        if (k == 4 || k == 5) kb_move(k == 4 ? -1 : 1);
        return;
    }
    if (tab != TAB_WEB || !osk_active()) { was = 0; return; }
    if (k == 3) { tp_keys |= PAD_BUTTON_B; was = 0; return; }   /* (Echap : comme B) */
    char t[64], cur[300];
    int m = osk_phone_get(t, sizeof(t), cur, sizeof(cur));
    if (!was) { snprintf(ob, sizeof(ob), "%s", m == 1 ? cur : ""); was = 1; }   /* (mot de passe : texte cache, on part de rien) */
    size_t l = strlen(ob);
    if (cp >= 0x20 && cp < 0x100 && l + 1 < sizeof(ob)) { ob[l] = (char)cp; ob[l + 1] = 0; }   /* (le web : Latin-1) */
    if (k == 1 && l) ob[l - 1] = 0;
    osk_phone_set(ob, k == 2);
    if (k == 2 || k == 3) was = 0;
}
