// GC Station v151 : catalogue d'applis (homebrews GameCube) a installer depuis Internet. Inclus dans main.c (apres saves.c).
//  - le workflow catalogue.yml publie chaque semaine la release « catalogue » du depot : catalogue.json et un
//    paquet par appli (<id>-<version>.gcsa : zlib( "GCSA1\n" + chemin "\n" taille "\n" contenu ... ), voir
//    tools/catalogue.py) ;
//  - START > Reglages > Outils > « Catalogue d'applis » : la liste, ce qui est installe, les mises a jour ;
//    A = installer / mettre a jour (taille et empreinte SHA-256 verifiees, fichiers seulement dans
//    SD:/apps/<id>/), X = supprimer ; la version installee est notee dans SD:/apps/<id>/gcstation.txt.
#include "mbedtls/sha256.h"
#define CT_MAX 64
typedef struct { char id[24], nom[48], ver[32], have[32], desc[200], file[80], sha[72], depot[80]; long size, sdsize; } ct_ent;
static ct_ent ct[CT_MAX];
static int ct_n = 0, ct_sel = 0, ct_confirm = -1, ct_st = 0;   /* ct_st : 0 rien, 1 liste demandee, 2 paquet demande */
static char ct_msg[160] = "", ct_date[32] = "";
static int ct_cur = -1;
static hfetch_t ct_hf = { .c = { -1, 0, 0 } };
static void ct_url(char *out, int cap, const char *file) {
#ifndef GEKKO
    if (getenv("PCSIM_CATA")) { snprintf(out, cap, "%s/%s", getenv("PCSIM_CATA"), file); return; }   /* (simulateur : serveur de test) */
#endif
    snprintf(out, cap, "https://github.com/%s/releases/download/catalogue/%s", upd_repo, file);
}
static int ct_safe_id(const char *s) {
    if (!s[0] || strlen(s) > 20) return 0;
    for (const char *p = s; *p; p++) if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') return 0;
    return 1;
}
static void ct_have(ct_ent *e) {                          /* version installee (notre fichier, sinon meta.xml) */
    char p[NAME_LEN], l[160];
    e->have[0] = 0;
    snprintf(p, sizeof(p), SD_ROOT "/apps/%s/gcstation.txt", e->id);
    FILE *f = sd_ok ? fopen(p, "r") : NULL;
    if (f) { while (fgets(l, sizeof(l), f)) if (!strncmp(l, "version=", 8)) { snprintf(e->have, sizeof(e->have), "%s", trim(l + 8)); } fclose(f); return; }
    snprintf(p, sizeof(p), SD_ROOT "/apps/%s/boot.dol", e->id);
    struct stat st;
    if (sd_ok && stat(p, &st) == 0) snprintf(e->have, sizeof(e->have), "?");   /* (installee autrement : version inconnue) */
}
static void ct_parse(const char *js, int len) {
    const char *end = js + len, *p = strstr(js, "\"applis\"");
    ct_n = 0;
    json_field(js, end, "date", ct_date, sizeof(ct_date));
    while (p && p < end && ct_n < CT_MAX) {
        const char *o = strchr(p, '{');
        if (!o || o >= end) break;
        const char *c = strchr(o, '}');
        if (!c || c > end) break;
        ct_ent *e = &ct[ct_n];
        memset(e, 0, sizeof(*e));
        char num[24];
        json_field(o, c, "id", e->id, sizeof(e->id));
        json_field(o, c, "nom", e->nom, sizeof(e->nom));
        json_field(o, c, "version", e->ver, sizeof(e->ver));
        json_field(o, c, "description", e->desc, sizeof(e->desc));
        json_field(o, c, "fichier", e->file, sizeof(e->file));
        json_field(o, c, "sha256", e->sha, sizeof(e->sha));
        json_field(o, c, "depot", e->depot, sizeof(e->depot));
        const char *t = strstr(o, "\"taille\"");
        if (t && t < c) { t += 8; while (t < c && (*t == ':' || *t == ' ')) t++; e->size = atol(t); }
        t = strstr(o, "\"taille_sd\"");
        if (t && t < c) { t += 11; while (t < c && (*t == ':' || *t == ' ')) t++; e->sdsize = atol(t); }
        (void)num;
        if (ct_safe_id(e->id) && e->file[0] && !strchr(e->file, '/') && e->size > 0 && strlen(e->sha) == 64) { ct_have(e); ct_n++; }
        p = c + 1;
    }
    if (ct_sel >= ct_n) ct_sel = ct_n ? ct_n - 1 : 0;
}
static void ct_fetch_list(void) {
    if (net_phase != NET_DONE) { net_start_async(); snprintf(ct_msg, sizeof(ct_msg), "R\xC3\xA9seau en cours de connexion... (Y pour r\xC3\xA9" "essayer)"); return; }
    if (!net_ok) { snprintf(ct_msg, sizeof(ct_msg), "Pas de r\xC3\xA9seau"); return; }
    char url[URL_MAX];
    ct_url(url, sizeof(url), "catalogue.json");
    ct_hf.maxbytes = 256 * 1024; ct_hf.image = 0; ct_hf.accept = "application/json";
    hf_start(&ct_hf, url, NULL);
    ct_st = 1;
    snprintf(ct_msg, sizeof(ct_msg), "Lecture du catalogue...");
}
static void ct_open(void) {
    tl_screen = TL_CATA; ct_confirm = -1;
    if (!ct_n && ct_st == 0) ct_fetch_list();
    else for (int i = 0; i < ct_n; i++) ct_have(&ct[i]);
}
static int ct_mkdirs(const char *path) {                 /* dossiers du chemin d'un fichier */
    char p[NAME_LEN];
    snprintf(p, sizeof(p), "%s", path);
    for (char *s = p + strlen(SD_ROOT) + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0777); *s = '/'; }
    return 1;
}
static void ct_rmtree(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char p[NAME_LEN];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        if (is_dir(p, e)) ct_rmtree(p); else remove(p);
    }
    closedir(d);
    rmdir(dir);
}
/* paquet recu : verifie, decompresse, fichiers ecrits (seulement sous apps/<id>/) */
static const char *ct_install(ct_ent *e, const u8 *b, long n) {
    if (n != e->size) return "Paquet re\xC3\xA7u incomplet";
    unsigned char h[32];
    char hx[65];
    mbedtls_sha256(b, (size_t)n, h, 0);
    for (int i = 0; i < 32; i++) snprintf(hx + i * 2, 3, "%02x", h[i]);
    if (strcasecmp(hx, e->sha)) return "Paquet ab\xC3\xAEm\xC3\xA9 (empreinte diff\xC3\xA9rente)";
    int ol = 0;
    char *raw = stbi_zlib_decode_malloc((const char *)b, (int)n, &ol);
    if (!raw || ol < 6 || memcmp(raw, "GCSA1\n", 6)) { free(raw); return "Paquet illisible"; }
    char pre[48];
    snprintf(pre, sizeof(pre), "apps/%s/", e->id);
    /* 1er passage : tout verifier ; 2e : ecrire */
    for (int pass = 0; pass < 2; pass++) {
        int p = 6, files = 0;
        while (p < ol) {
            char *nl = memchr(raw + p, '\n', ol - p);
            if (!nl || nl - (raw + p) >= NAME_LEN - 16) { free(raw); return "Paquet illisible"; }
            char path[NAME_LEN];
            memcpy(path, raw + p, nl - (raw + p)); path[nl - (raw + p)] = 0;
            p = (int)(nl - raw) + 1;
            char *nl2 = memchr(raw + p, '\n', ol - p);
            if (!nl2) { free(raw); return "Paquet illisible"; }
            long sz = atol(raw + p);
            p = (int)(nl2 - raw) + 1;
            if (sz < 0 || sz > ol - p) { free(raw); return "Paquet illisible"; }
            if (strncmp(path, pre, strlen(pre)) || strstr(path, "..") || strchr(path, '\\') || strchr(path, ':')) { free(raw); return "Paquet refus\xC3\xA9 (fichier hors de son dossier)"; }
            if (pass) {
                char full[NAME_LEN + 8], part[NAME_LEN + 16];
                snprintf(full, sizeof(full), SD_ROOT "/%s", path);
                snprintf(part, sizeof(part), "%s.part", full);
                ct_mkdirs(full);
                FILE *f = fopen(part, "wb");
                int ok = f && fwrite(raw + p, 1, sz, f) == (size_t)sz;
                if (f && fclose(f) != 0) ok = 0;
                if (!ok) { remove(part); free(raw); return "\xC3\x89" "criture sur la carte SD impossible"; }
                remove(full);
                if (rename(part, full) != 0) { free(raw); return "\xC3\x89" "criture sur la carte SD impossible"; }
                service_audio();
            }
            p += (int)sz; files++;
        }
        if (!files) { free(raw); return "Paquet vide"; }
    }
    free(raw);
    char v[NAME_LEN];
    snprintf(v, sizeof(v), SD_ROOT "/apps/%s/gcstation.txt", e->id);
    FILE *f = fopen(v, "w");
    if (f) { fprintf(f, "# install\xC3\xA9" "e par GC Station (catalogue d'applis)\nversion=%s\ndepot=%s\n", e->ver, e->depot); fclose(f); }
    ct_have(e);
    ap_scanned = 0;                                       /* (liste des applis a relire) */
    crumb("catalogue : %s %s installee", e->id, e->ver);
    return NULL;
}
static void ct_start_install(int i) {
    if (i < 0 || i >= ct_n || ct_st) return;
    if (!sd_ok || sd_virtual) { snprintf(ct_msg, sizeof(ct_msg), "Il faut une carte SD"); return; }
    unsigned long long tot = 0, fre = 0;
    if (sd_space(&tot, &fre) && fre < (unsigned long long)ct[i].sdsize + (4ull << 20)) { snprintf(ct_msg, sizeof(ct_msg), "Pas assez de place sur la carte SD"); return; }
    char url[URL_MAX];
    ct_url(url, sizeof(url), ct[i].file);
    ct_hf.maxbytes = ct[i].size + 1024; ct_hf.image = 0; ct_hf.accept = "application/octet-stream";
    hf_start(&ct_hf, url, NULL);
    ct_st = 2; ct_cur = i;
    snprintf(ct_msg, sizeof(ct_msg), "T\xC3\xA9l\xC3\xA9" "chargement de %s...", ct[i].nom);
}
static void ct_pump(void) {
    if (!ct_st) return;
    hf_pump(&ct_hf);
    if (ct_hf.st == HF_FAIL) {
        snprintf(ct_msg, sizeof(ct_msg), ct_st == 1 ? (ct_hf.code == 404 ? "Catalogue pas encore publi\xC3\xA9" : "Catalogue injoignable (Y pour r\xC3\xA9" "essayer)") : "T\xC3\xA9l\xC3\xA9" "chargement interrompu");
        hf_drop(&ct_hf); ct_hf.st = HF_IDLE; ct_st = 0;
        return;
    }
    if (ct_hf.st != HF_DONE) return;
    if (ct_st == 1) {
        char *js = (char *)malloc(ct_hf.len + 1);
        if (js) { memcpy(js, ct_hf.buf, ct_hf.len); js[ct_hf.len] = 0; ct_parse(js, ct_hf.len); free(js); }
        int up = 0;
        for (int i = 0; i < ct_n; i++) if (ct[i].have[0] && strcmp(ct[i].have, ct[i].ver)) up++;
        if (!ct_n) snprintf(ct_msg, sizeof(ct_msg), "Catalogue vide ou illisible");
        else if (up) snprintf(ct_msg, sizeof(ct_msg), "%d mise%s \xC3\xA0 jour disponible%s", up, up > 1 ? "s" : "", up > 1 ? "s" : "");
        else ct_msg[0] = 0;
    } else {
        const char *e = ct_cur >= 0 && ct_cur < ct_n ? ct_install(&ct[ct_cur], ct_hf.buf, ct_hf.len) : "Appli introuvable";
        if (e) snprintf(ct_msg, sizeof(ct_msg), "%s", e);
        else snprintf(ct_msg, sizeof(ct_msg), "%s %s install\xC3\xA9" "e (Applis)", ct[ct_cur].nom, ct[ct_cur].ver);
    }
    hf_drop(&ct_hf); ct_hf.st = HF_IDLE; ct_st = 0;
}
static void ct_input(u32 down, u32 nav, int fire) {
    if (ct_confirm >= 0) {
        if (down & PAD_BUTTON_A) {
            int i = ct_confirm; ct_confirm = -1;
            char d[NAME_LEN];
            snprintf(d, sizeof(d), SD_ROOT "/apps/%s", ct[i].id);
            ct_rmtree(d);
            ct_have(&ct[i]); ap_scanned = 0;
            snprintf(ct_msg, sizeof(ct_msg), "%s supprim\xC3\xA9" "e", ct[i].nom);
        }
        if (down & PAD_BUTTON_B) ct_confirm = -1;
        return;
    }
    if (fire && (nav & PAD_BUTTON_DOWN) && ct_sel < ct_n - 1) ct_sel++;
    if (fire && (nav & PAD_BUTTON_UP) && ct_sel > 0) ct_sel--;
    if ((down & PAD_BUTTON_A) && ct_n && !ct_st) {
        if (ct[ct_sel].have[0] && !strcmp(ct[ct_sel].have, ct[ct_sel].ver)) snprintf(ct_msg, sizeof(ct_msg), "D\xC3\xA9j\xC3\xA0 \xC3\xA0 jour (Applis pour la lancer)");
        else ct_start_install(ct_sel);
    }
    if ((down & PAD_BUTTON_X) && ct_n && ct[ct_sel].have[0] && !ct_st) ct_confirm = ct_sel;
    if ((down & PAD_BUTTON_Y) && !ct_st) { ct_n = 0; ct_fetch_list(); }
    if (down & PAD_BUTTON_B) { if (ct_st) { hf_abort(&ct_hf); hf_drop(&ct_hf); ct_hf.st = HF_IDLE; ct_st = 0; } tl_screen = TL_NONE; }
}
static void draw_cata(void) {
    char buf[240], fv[240];
    tl_frame("Catalogue d'applis", NULL);
    const int vis = 9;
    const f32 rh = 30;
    int start = ct_sel - vis / 2;
    if (start > ct_n - vis) start = ct_n - vis;
    if (start < 0) start = 0;
    for (int i = 0; i < vis && start + i < ct_n; i++) {
        int r = start + i; f32 ry = 90 + i * rh;
        int on = r == ct_sel;
        ct_ent *e = &ct[r];
        gp_zone(52, ry, 536, rh - 3, &ct_sel, r, GPZ_SEL);
        if (on) gx_rrect_fill(52, ry, 536, rh - 3, 6, TL_SEL1, TL_SEL2);
        gx_text1(F_MED, 64, ry + 18, e->nom, on ? RGBA(255, 255, 255, 255) : RGBA(230, 222, 255, 255));
        col_t c;
        if (!e->have[0]) { snprintf(buf, sizeof(buf), "%s  -  %ld Ko", e->ver, (e->size + 1023) / 1024); c = RGBA(170, 155, 225, 255); }
        else if (strcmp(e->have, e->ver)) { snprintf(buf, sizeof(buf), "mise \xC3\xA0 jour : %s \xE2\x86\x92 %s", e->have, e->ver); c = RGBA(255, 210, 120, 255); }
        else { snprintf(buf, sizeof(buf), "install\xC3\xA9" "e (%s)", e->have); c = RGBA(130, 230, 160, 255); }
        gx_text_r(F_SMALL, 578, ry + 18, buf, c);
    }
    if (ct_n) {
        ct_ent *e = &ct[ct_sel];
        fit_text(F_SMALL, e->desc, 530, fv, sizeof(fv));
        gx_text1(F_SMALL, 58, 90 + vis * rh + 10, fv, RGBA(210, 200, 240, 255));
        snprintf(buf, sizeof(buf), "Source : github.com/%s%s%s", e->depot, ct_date[0] ? "   -   catalogue du " : "", ct_date);
        fit_text(F_SMALL, buf, 530, fv, sizeof(fv));
        gx_text1(F_SMALL, 58, 90 + vis * rh + 28, fv, RGBA(150, 135, 210, 255));
    }
    if (ct_msg[0]) { fit_text(F_SMALL, ct_msg, 530, fv, sizeof(fv)); gx_text1(F_SMALL, 58, 90 + vis * rh + 46, fv, RGBA(255, 210, 120, 255)); }
    if (ct_confirm >= 0) tl_confirm("Supprimer cette appli de la carte SD ?", ct[ct_confirm].nom);
    tl_msgbox();
    static const char *const k[] = { "A", "X", "Y", "B" }, *const l[] = { "INSTALLER", "SUPPRIMER", "ACTUALISER", "RETOUR" };
    tl_legend(k, l, 4);
}
