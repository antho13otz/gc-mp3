// GC Station v206 : assistant IA (inclus dans main.c, apres web_extra.c).
// Une conversation avec une IA, dans l'onglet WEB (page gc:ia) : la question se tape au clavier (ou depuis le
// telephone : page /ia de l'onglet TELEPHONE, ou elle se colle aussi la cle), la reponse s'affiche comme une page.
// Fournisseurs : Anthropic (Claude, API Messages) ou tout service « compatible OpenAI » (OpenAI, Mistral,
// OpenRouter, et Ollama / LM Studio sur un PC de la maison, sans cle). La demande part en tache de fond :
// la musique et l'ecran continuent pendant que l'IA reflechit.
// Reglages : SD:/gc-mp3/reglages/assistant.txt (fournisseur, cle, modele, adresse).

#define IA_FILE  GCD_REG "/assistant.txt"
#define IA_MAXMSG 40
#define IA_ANS_MAX (24 * 1024)

static char ia_prov[16] = "anthropic";          /* anthropic / openai */
static char ia_key[256] = "";
static char ia_model[96] = "";
static char ia_url[URL_MAX] = "";
static int ia_cfg_loaded = 0;
static struct { u8 role; char *text; } ia_msg[IA_MAXMSG];   /* role : 0 toi, 1 assistant, 2 erreur (pas envoyee) */
static int ia_n = 0;
static hfetch_t ia_hf;
static int ia_busy;
static u64 ia_t0 = 0;
static char *ia_body = NULL;
static char ia_xhdr[400];
/* depuis le fil du telephone : repris par la boucle principale */
static volatile int ia_ph_cfg = 0, ia_ph_ask = 0;
static char ia_ph_prov[16], ia_ph_key[256], ia_ph_model[96], ia_ph_url[URL_MAX], ia_ph_q[2000];

static const char *ia_default_model(void) { return !strcmp(ia_prov, "openai") ? "gpt-4o-mini" : "claude-haiku-5-5"; }
static const char *ia_endpoint(void) {
    if (ia_url[0]) return ia_url;
    return !strcmp(ia_prov, "openai") ? "https://api.openai.com/v1/chat/completions" : "https://api.anthropic.com/v1/messages";
}
static void ia_load_cfg(void) {
    if (ia_cfg_loaded) return;
    ia_cfg_loaded = 1;
    FILE *f = sd_ok ? fopen(IA_FILE, "r") : NULL;
    if (!f) return;
    char line[URL_MAX + 64];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        if (!strcasecmp(k, "fournisseur")) snprintf(ia_prov, sizeof(ia_prov), "%s", !strcasecmp(v, "openai") ? "openai" : "anthropic");
        else if (!strcasecmp(k, "cle")) snprintf(ia_key, sizeof(ia_key), "%s", v);
        else if (!strcasecmp(k, "modele")) snprintf(ia_model, sizeof(ia_model), "%s", v);
        else if (!strcasecmp(k, "adresse")) snprintf(ia_url, sizeof(ia_url), "%s", v);
    }
    fclose(f);
}
static void ia_save_cfg(void) {
    if (!sd_ok) return;
    mkdir(GCD_REG, 0777);
    FILE *f = fopen(IA_FILE, "w");
    if (!f) return;
    fprintf(f, "# GC Station : assistant IA (onglet WEB > Assistant IA ; reglable aussi depuis le telephone, page /ia)\n"
               "# fournisseur = anthropic (Claude) ou openai (OpenAI, Mistral, OpenRouter, Ollama, LM Studio...)\n"
               "# adresse : vide = l'adresse du fournisseur ; Ollama sur un PC : http://192.168.1.20:11434/v1/chat/completions\n");
    fprintf(f, "fournisseur = %s\ncle = %s\nmodele = %s\nadresse = %s\n", ia_prov, ia_key, ia_model, ia_url);
    fclose(f);
}
static int ia_ready(void) { ia_load_cfg(); return ia_key[0] || (ia_url[0] && !strcmp(ia_prov, "openai")); }

/* ---------- JSON (juste ce qu'il faut) ---------- */
static void sb_raw(sbuf_t *s, const char *t, int l) {
    if (l < 0) l = (int)strlen(t);
    if (s->n + l + 1 > s->cap) {
        int nc = s->cap ? s->cap * 2 : 8192;
        while (nc < s->n + l + 1) nc *= 2;
        char *nb = (char *)realloc(s->b, nc);
        if (!nb) return;
        s->b = nb; s->cap = nc;
    }
    memcpy(s->b + s->n, t, l);
    s->n += l;
    s->b[s->n] = 0;
}
static void ia_json_str(sbuf_t *s, const char *t) {
    sb_raw(s, "\"", 1);
    for (; *t; t++) {
        unsigned char c = (unsigned char)*t;
        if (c == '"') sb_raw(s, "\\\"", 2);
        else if (c == '\\') sb_raw(s, "\\\\", 2);
        else if (c == '\n') sb_raw(s, "\\n", 2);
        else if (c == '\r') {}
        else if (c == '\t') sb_raw(s, "\\t", 2);
        else if (c < 0x20) { char e[8]; snprintf(e, sizeof(e), "\\u%04x", c); sb_raw(s, e, 6); }
        else sb_raw(s, (const char *)t, 1);
    }
    sb_raw(s, "\"", 1);
}
static const char *js_ws(const char *p) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++; return p; }
static const char *js_skip(const char *p);
static const char *js_skip_str(const char *p) {        /* p sur '"' -> apres la fin */
    p++;
    while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
    return *p ? p + 1 : p;
}
static const char *js_skip(const char *p) {
    p = js_ws(p);
    if (*p == '"') return js_skip_str(p);
    if (*p == '{' || *p == '[') {
        char o = *p, c = o == '{' ? '}' : ']';
        int d = 0;
        while (*p) {
            if (*p == '"') { p = js_skip_str(p); continue; }
            if (*p == o) d++;
            else if (*p == c) { if (--d == 0) return p + 1; }
            p++;
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}
/* valeur au chemin (cles « a » ou rangs « #2 ») -> pointeur sur la valeur, ou NULL */
static const char *js_path(const char *p, const char *const *path, int np) {
    for (int k = 0; k < np; k++) {
        p = js_ws(p);
        if (path[k][0] == '#') {
            if (*p != '[') return NULL;
            int want = atoi(path[k] + 1), i = 0;
            p = js_ws(p + 1);
            if (*p == ']') return NULL;
            while (*p) {
                if (i == want) break;
                p = js_ws(js_skip(p));
                if (*p != ',') return NULL;
                p = js_ws(p + 1);
                i++;
            }
            if (!*p) return NULL;
        } else {
            if (*p != '{') return NULL;
            p = js_ws(p + 1);
            int found = 0;
            while (*p == '"') {
                const char *ks = p + 1, *ke = js_skip_str(p) - 1;
                p = js_ws(js_skip_str(p));
                if (*p != ':') return NULL;
                p = js_ws(p + 1);
                if ((int)strlen(path[k]) == ke - ks && !strncmp(ks, path[k], ke - ks)) { found = 1; break; }
                p = js_ws(js_skip(p));
                if (*p == ',') p = js_ws(p + 1);
            }
            if (!found) return NULL;
        }
    }
    return js_ws(p);
}
/* chaine JSON (p sur '"') -> UTF-8 */
static int js_str(const char *p, char *out, int cap) {
    if (!p || *p != '"') return 0;
    p++;
    int o = 0;
    while (*p && *p != '"' && o < cap - 5) {
        if (*p != '\\') { out[o++] = *p++; continue; }
        p++;
        unsigned cp = 0;
        switch (*p) {
        case 'n': out[o++] = '\n'; p++; continue;
        case 't': out[o++] = '\t'; p++; continue;
        case 'r': p++; continue;
        case 'b': case 'f': p++; continue;
        case 'u': {
            for (int i = 1; i <= 4 && p[i]; i++) cp = cp * 16 + (unsigned)(hexv(p[i]) < 0 ? 0 : hexv(p[i]));
            p += 5;
            if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u') {   /* paire (emoji...) */
                unsigned lo = 0;
                for (int i = 2; i <= 5 && p[i]; i++) lo = lo * 16 + (unsigned)(hexv(p[i]) < 0 ? 0 : hexv(p[i]));
                p += 6;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            if (cp >= 0x10000 || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x200D) continue;   /* (emoji : pas dans la police) */
            if (cp < 0x80) out[o++] = (char)cp;
            else if (cp < 0x800) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
            else { out[o++] = (char)(0xF0 | (cp >> 18)); out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
            continue;
        }
        default: out[o++] = *p++; continue;
        }
    }
    out[o] = 0;
    return 1;
}

/* ---------- la conversation ---------- */
static void ia_add(int role, const char *t) {
    if (ia_n == IA_MAXMSG) {                    /* (la plus ancienne paire s'en va) */
        free(ia_msg[0].text); free(ia_msg[1].text);
        memmove(&ia_msg[0], &ia_msg[2], sizeof(ia_msg[0]) * (IA_MAXMSG - 2));
        ia_n -= 2;
    }
    ia_msg[ia_n].role = (u8)role;
    ia_msg[ia_n].text = strdup(t ? t : "");
    if (ia_msg[ia_n].text) ia_n++;
}
static void ia_clear(void) { for (int i = 0; i < ia_n; i++) free(ia_msg[i].text); ia_n = 0; }

static void ia_md_inline(sbuf_t *s, const char *t, int n) {   /* **gras**, `code`, echappe le HTML */
    int b = 0, c = 0;
    for (int i = 0; i < n; i++) {
        char ch = t[i];
        if (ch == '*' && i + 1 < n && t[i + 1] == '*') { sb_raw(s, b ? "</b>" : "<b>", -1); b = !b; i++; continue; }
        if (ch == '`') { sb_raw(s, c ? "</code>" : "<code>", -1); c = !c; continue; }
        if (ch == '<') sb_raw(s, "&lt;", 4);
        else if (ch == '>') sb_raw(s, "&gt;", 4);
        else if (ch == '&') sb_raw(s, "&amp;", 5);
        else sb_raw(s, &t[i], 1);
    }
    if (b) sb_raw(s, "</b>", 4);
    if (c) sb_raw(s, "</code>", 7);
}
/* reponse (markdown simple) -> HTML : paragraphes, titres, listes, blocs de code, tableaux en lignes */
static void ia_md(sbuf_t *s, const char *t) {
    int inpre = 0, inlist = 0, inpara = 0;
    while (*t) {
        const char *e = strchr(t, '\n');
        int n = e ? (int)(e - t) : (int)strlen(t);
        const char *l = t;
        int ln = n;
        while (ln > 0 && (*l == ' ' || *l == '\t') && !inpre) { l++; ln--; }
        if (ln >= 3 && !strncmp(l, "```", 3)) {
            if (inpara) { sb_raw(s, "</p>", 4); inpara = 0; }
            if (inlist) { sb_raw(s, inlist == 1 ? "</ul>" : "</ol>", 5); inlist = 0; }
            sb_raw(s, inpre ? "</pre>" : "<pre>", -1);
            inpre = !inpre;
        } else if (inpre) {
            ia_md_inline(s, t, n); sb_raw(s, "\n", 1);
        } else if (ln == 0) {
            if (inpara) { sb_raw(s, "</p>", 4); inpara = 0; }
            if (inlist) { sb_raw(s, inlist == 1 ? "</ul>" : "</ol>", 5); inlist = 0; }
        } else if (l[0] == '#') {
            int h = 0; while (h < ln && l[h] == '#') h++;
            if (inpara) { sb_raw(s, "</p>", 4); inpara = 0; }
            if (inlist) { sb_raw(s, inlist == 1 ? "</ul>" : "</ol>", 5); inlist = 0; }
            sb_raw(s, "<h4>", 4); ia_md_inline(s, l + h, ln - h); sb_raw(s, "</h4>", 5);
        } else if ((l[0] == '-' || l[0] == '*' || l[0] == '+') && ln > 1 && l[1] == ' ') {
            if (inpara) { sb_raw(s, "</p>", 4); inpara = 0; }
            if (inlist != 1) { if (inlist) sb_raw(s, "</ol>", 5); sb_raw(s, "<ul>", 4); inlist = 1; }
            sb_raw(s, "<li>", 4); ia_md_inline(s, l + 2, ln - 2); sb_raw(s, "</li>", 5);
        } else if (isdigit((u8)l[0]) && ln > 2 && (l[1] == '.' || (isdigit((u8)l[1]) && l[2] == '.'))) {
            int k = l[1] == '.' ? 2 : 3;
            if (inpara) { sb_raw(s, "</p>", 4); inpara = 0; }
            if (inlist != 2) { if (inlist) sb_raw(s, "</ul>", 5); sb_raw(s, "<ol>", 4); inlist = 2; }
            while (k < ln && l[k] == ' ') k++;
            sb_raw(s, "<li>", 4); ia_md_inline(s, l + k, ln - k); sb_raw(s, "</li>", 5);
        } else if (l[0] == '|') {               /* tableau : une ligne de texte par rangee */
            if (ln > 2 && strspn(l, "|-: ") == (size_t)ln) {}
            else { if (inpara) { sb_raw(s, "</p>", 4); inpara = 0; } sb_raw(s, "<div>", 5); ia_md_inline(s, l, ln); sb_raw(s, "</div>", 6); }
        } else {
            if (inlist) { sb_raw(s, inlist == 1 ? "</ul>" : "</ol>", 5); inlist = 0; }
            if (!inpara) { sb_raw(s, "<p>", 3); inpara = 1; } else sb_raw(s, "<br>", 4);
            ia_md_inline(s, l, ln);
        }
        t += n;
        if (*t == '\n') t++;
    }
    if (inpre) sb_raw(s, "</pre>", 6);
    if (inpara) sb_raw(s, "</p>", 4);
    if (inlist) sb_raw(s, inlist == 1 ? "</ul>" : "</ol>", 5);
}

/* la page gc:ia */
static void ia_render(int mode, int to_bottom) {
    ia_load_cfg();
    sbuf_t s = { 0 };
    sb_raw(&s, "<html><head><title>Assistant IA</title><style>"
               "body{background:#f4f2fb;margin:0;padding:8px}h1{font-size:22px;margin:4px 0}"
               ".info{color:#6b6880;font-size:12px}.toi{background:#e3dcff;border-radius:10px;padding:6px 10px;margin:10px 0 4px 60px}"
               ".ia{background:#fff;border:1px solid #ddd6f5;border-radius:10px;padding:2px 12px;margin:4px 40px 10px 0}"
               ".err{background:#fff0f0;border:1px solid #f0b4b4;color:#8a1f1f;border-radius:10px;padding:6px 10px;margin:4px 0 10px}"
               ".qui{font-size:12px;color:#7a6fb0;font-weight:bold}pre{background:#1d1b2b;color:#e7e3ff;padding:8px;border-radius:6px}"
               "code{background:#ece8fb}pre code{background:none}.attente{color:#7a6fb0;font-style:italic;margin:8px 0}"
               "form{margin:12px 0}</style></head><body>", -1);
    sb_raw(&s, "<h1>Assistant IA</h1>", -1);
    char t[600];
    if (!ia_ready()) {
        sb_raw(&s, "<p>Pose tes questions \xC3\xA0 une IA depuis la GameCube. Il faut d'abord une <b>cl\xC3\xA9</b> (compte chez le fournisseur) :</p><ul>"
                   "<li>le plus simple : onglet <b>T\xC3\x89L\xC3\x89PHONE</b>, scanne le QR code, puis la page <b>/ia</b> : colle la cl\xC3\xA9 depuis le t\xC3\xA9l\xC3\xA9phone ;</li>"
                   "<li>ou le fichier SD:/gc-mp3/reglages/assistant.txt (fournisseur, cle, modele, adresse).</li></ul>"
                   "<p>Fournisseurs : <b>Anthropic</b> (Claude), ou <b>compatible OpenAI</b> : OpenAI, Mistral, OpenRouter, et "
                   "<b>Ollama</b> ou LM Studio sur un PC de la maison (sans cl\xC3\xA9 : seulement l'adresse).</p>", -1);
        if (sd_ok) { ia_save_cfg(); sb_raw(&s, "<p class=\"info\">Le fichier de r\xC3\xA9glages vient d'\xC3\xAAtre cr\xC3\xA9\xC3\xA9 sur la carte SD.</p>", -1); }
    } else {
        snprintf(t, sizeof(t), "<p class=\"info\">%s \xC2\xB7 mod\xC3\xA8le %s \xC2\xB7 <a href=\"gc:ia-nouveau\">Nouvelle conversation</a></p>",
                 !strcmp(ia_prov, "openai") ? "Compatible OpenAI" : "Anthropic (Claude)", ia_model[0] ? ia_model : ia_default_model());
        sb_raw(&s, t, -1);
        /* v209 : (navigateur Cube Browser) le champ en haut, puis le plus recent d'abord */
        sb_raw(&s, "<form action=\"gc:ia-envoyer\"><input type=\"text\" name=\"q\" size=\"50\"> <input type=\"submit\" value=\"Envoyer\"></form>", -1);
        if (!ia_n) sb_raw(&s, "<p>Pose ta question dans le champ ci-dessus (A sur le champ : clavier ; ou depuis le t\xC3\xA9l\xC3\xA9phone, page /ia).</p>", -1);
        if (ia_busy) sb_raw(&s, "<p class=\"attente\">L'assistant r\xC3\xA9" "fl\xC3\xA9" "chit... (la r\xC3\xA9ponse s'affichera ici)</p>", -1);
        for (int i = ia_n - 1; i >= 0; i--) {
            int k = i;
            if (ia_msg[i].role != 0 && i > 0 && ia_msg[i - 1].role == 0) k = i - 1;   /* (question, puis sa reponse) */
            for (int m = k; m <= i; m++) {
                if (ia_msg[m].role == 0) {
                    sb_raw(&s, "<div class=\"toi\"><div class=\"qui\">Toi</div>", -1);
                    ia_md_inline(&s, ia_msg[m].text, (int)strlen(ia_msg[m].text));
                    sb_raw(&s, "</div>", 6);
                } else if (ia_msg[m].role == 1) {
                    sb_raw(&s, "<div class=\"ia\"><div class=\"qui\">Assistant</div>", -1);
                    ia_md(&s, ia_msg[m].text);
                    sb_raw(&s, "</div>", 6);
                } else {
                    sb_raw(&s, "<div class=\"err\">", -1);
                    ia_md_inline(&s, ia_msg[m].text, (int)strlen(ia_msg[m].text));
                    sb_raw(&s, "</div>", 6);
                }
            }
            if (k < i) sb_raw(&s, "<hr>", 4);
            i = k;
        }
    }
    sb_raw(&s, "</body></html>", -1);
    if (!s.b) return;
    web_show((unsigned char *)s.b, s.n, "gc:ia", WK_HTML, 0, mode);
    if (to_bottom && ia_ready()) { pg_scroll = pg.height; web_scroll_clamp(); }
}

/* envoie la conversation */
static void ia_send(void) {
    sbuf_t b = { 0 };
    int oa = !strcmp(ia_prov, "openai");
    const char *sys = lang_cur() == SYS_LANG_ENGLISH
        ? "You are the assistant of GC Station, a media center running on a Nintendo GameCube. Answer in the user's language, clearly and briefly: "
          "the answer is read on a TV, so prefer short paragraphs and simple lists, no wide tables."
        : "Tu es l'assistant de GC Station, un centre multim\xC3\xA9" "dia qui tourne sur une Nintendo GameCube. R\xC3\xA9ponds dans la langue de "
          "l'utilisateur, clairement et sans d\xC3\xA9tour : la r\xC3\xA9ponse se lit sur une t\xC3\xA9l\xC3\xA9, donc des paragraphes courts et des listes "
          "simples, pas de grands tableaux.";
    char num[64];
    sb_raw(&b, "{\"model\":", -1);
    ia_json_str(&b, ia_model[0] ? ia_model : ia_default_model());
    snprintf(num, sizeof(num), ",\"max_tokens\":%d,", 1500);
    sb_raw(&b, num, -1);
    if (!oa) { sb_raw(&b, "\"system\":", -1); ia_json_str(&b, sys); sb_raw(&b, ",", 1); }
    sb_raw(&b, "\"messages\":[", -1);
    int first = 1;
    if (oa) { sb_raw(&b, "{\"role\":\"system\",\"content\":", -1); ia_json_str(&b, sys); sb_raw(&b, "}", 1); first = 0; }
    /* (les 24 derniers messages, sans les erreurs ; toujours en commencant par une question) */
    int start = ia_n > 24 ? ia_n - 24 : 0;
    while (start < ia_n && ia_msg[start].role != 0) start++;
    for (int i = start; i < ia_n; i++) {
        if (ia_msg[i].role == 2 || (i + 1 < ia_n && ia_msg[i + 1].role == 2)) continue;   /* (question restee sans reponse : pas envoyee) */
        if (!first) sb_raw(&b, ",", 1);
        first = 0;
        sb_raw(&b, ia_msg[i].role == 0 ? "{\"role\":\"user\",\"content\":" : "{\"role\":\"assistant\",\"content\":", -1);
        ia_json_str(&b, ia_msg[i].text);
        sb_raw(&b, "}", 1);
    }
    sb_raw(&b, "]}", 2);
    if (!b.b) return;
    free(ia_body);
    ia_body = b.b;
    if (oa) snprintf(ia_xhdr, sizeof(ia_xhdr), "%s%s%s", ia_key[0] ? "Authorization: Bearer " : "", ia_key, ia_key[0] ? "\r\n" : "");
    else snprintf(ia_xhdr, sizeof(ia_xhdr), "x-api-key: %s\r\nanthropic-version: 2023-06-01\r\n", ia_key);
    hf_abort(&ia_hf);
    ia_hf.post = ia_body; ia_hf.post_len = b.n; ia_hf.post_type = "application/json";
    ia_hf.xhdr = ia_xhdr;
    ia_hf.tmo_ms = 120000;
    ia_hf.any_code = 1;
    ia_hf.maxbytes = 512 * 1024;
    ia_hf.accept = "application/json";
    ia_hf.gz = 0;
    hf_start(&ia_hf, ia_endpoint(), NULL);
    ia_busy = 1;
    ia_t0 = now_ms();
}
static void ia_ask(const char *q) {
    while (*q == ' ') q++;
    if (!*q) return;
    if (!ia_ready()) { ia_render(NAV_REPLACE, 0); return; }
    if (ia_busy) { web_note("L'assistant r\xC3\xA9pond d\xC3\xA9j\xC3\xA0 : attends sa r\xC3\xA9ponse", 1); return; }
    if (!net_ok) { web_note("Pas de r\xC3\xA9seau", 1); return; }
    ia_add(0, q);
    ia_send();
    ia_render(strcmp(pg.url, "gc:ia") ? NAV_PUSH : NAV_REPLACE, 1);
}
/* reponse arrivee ? (chaque image) */
static void ia_pump(void) {
    if (ia_ph_cfg) {                            /* reglages envoyes par le telephone */
        snprintf(ia_prov, sizeof(ia_prov), "%s", !strcmp(ia_ph_prov, "openai") ? "openai" : "anthropic");
        if (ia_ph_key[0] && strcmp(ia_ph_key, "(inchangee)")) snprintf(ia_key, sizeof(ia_key), "%s", ia_ph_key);
        snprintf(ia_model, sizeof(ia_model), "%s", ia_ph_model);
        snprintf(ia_url, sizeof(ia_url), "%s", ia_ph_url);
        ia_cfg_loaded = 1;
        ia_save_cfg();
        ia_ph_cfg = 0;
        web_note("Assistant IA : r\xC3\xA9glages enregistr\xC3\xA9s", 0);
        { const char *cu = cb_current_url(); if (cu && !strncmp(cu, "gc:ia", 5)) ia_render(NAV_REPLACE, 0); }
    }
    if (ia_ph_ask) {                            /* question tapee sur le telephone : a la tele */
        char q[2000];
        snprintf(q, sizeof(q), "%s", ia_ph_q);
        ia_ph_ask = 0;
        if (tab != TAB_WEB && tab_shown(TAB_WEB)) { tab = TAB_WEB; tab_enter(tab); }
        ia_ask(q);
    }
    if (!ia_busy) return;
    hf_pump(&ia_hf);
    if (ia_hf.st != HF_DONE && ia_hf.st != HF_FAIL) {
        static u64 last = 0;                    /* (la page se met a jour : « reflechit » reste visible) */
        (void)last;
        return;
    }
    ia_busy = 0;
    int oa = !strcmp(ia_prov, "openai");
    char *ans = (char *)malloc(IA_ANS_MAX);
    if (!ans) return;
    ans[0] = 0;
    int ok = 0;
    if (ia_hf.st == HF_DONE && ia_hf.buf) {
        char *js = (char *)realloc(ia_hf.buf, ia_hf.len + 1);
        if (js) { ia_hf.buf = (unsigned char *)js; js[ia_hf.len] = 0; }
        else js = (char *)ia_hf.buf;
        if (ia_hf.code == 200) {
            if (oa) {
                static const char *const pth[] = { "choices", "#0", "message", "content" };
                ok = js_str(js_path(js, pth, 4), ans, IA_ANS_MAX);
            } else {
                int o = 0;
                for (int i = 0; i < 8; i++) {   /* blocs de texte de la reponse */
                    char idx[8]; snprintf(idx, sizeof(idx), "#%d", i);
                    const char *pth[] = { "content", idx, "text" };
                    const char *v = js_path(js, pth, 3);
                    if (!v) { const char *pth2[] = { "content", idx }; if (!js_path(js, pth2, 2)) break; continue; }
                    if (js_str(v, ans + o, IA_ANS_MAX - o)) { o += (int)strlen(ans + o); ok = 1; }
                }
            }
            if (ok && !ans[0]) snprintf(ans, IA_ANS_MAX, "(r\xC3\xA9ponse vide)");
        }
        if (!ok) {
            char msg[600] = "";
            static const char *const pe[] = { "error", "message" };
            if (!js_str(js_path(js, pe, 2), msg, sizeof(msg))) {
                static const char *const pe2[] = { "error" };
                js_str(js_path(js, pe2, 1), msg, sizeof(msg));
            }
            const char *why = ia_hf.code == 401 || ia_hf.code == 403 ? "cl\xC3\xA9 refus\xC3\xA9" "e" : ia_hf.code == 404 ? "adresse ou mod\xC3\xA8le inconnu" :
                              ia_hf.code == 429 ? "trop de demandes ou plus de cr\xC3\xA9" "dit" : ia_hf.code == 529 || ia_hf.code == 503 ? "service surcharg\xC3\xA9, r\xC3\xA9" "essaie" : "erreur";
            snprintf(ans, IA_ANS_MAX, "Pas de r\xC3\xA9ponse (%s, HTTP %d)%s%s", why, ia_hf.code, msg[0] ? " : " : "", msg);
        }
    } else snprintf(ans, IA_ANS_MAX, "Pas de r\xC3\xA9ponse : le serveur ne r\xC3\xA9pond pas ou la connexion a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e (adresse : %s).", ia_endpoint());
    hf_drop(&ia_hf);
    free(ia_body); ia_body = NULL;
    ia_hf.post = NULL;
    ia_add(ok ? 1 : 2, ans);
    free(ans);
    const char *cu = cb_current_url();
    if (cu && !strncmp(cu, "gc:ia", 5)) ia_render(NAV_REPLACE, 0);   /* v209 : page du navigateur (Cube Browser) */
    else if (!strcmp(pg.url, "gc:ia")) {
        /* la reponse : au debut de la reponse (pas tout en bas) */
        ia_render(NAV_REPLACE, 0);
        int k = -1;
        for (int i = 0; i < pg.nitems; i++) if (!strcmp(pg.text + pg.items[i].off, "Assistant")) k = i;
        if (k >= 0) { pg_scroll = pg.items[k].y - 30; web_scroll_clamp(); }
    } else web_note(ok ? "L'assistant a r\xC3\xA9pondu (onglet WEB)" : "Assistant : pas de r\xC3\xA9ponse", !ok);
}

/* ---------- page /ia du telephone (onglet TELEPHONE) ---------- */
static const char ia_phone_page[] =
"<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>GC Station - Assistant IA</title><style>body{font-family:sans-serif;background:#120a2e;color:#eee;padding:16px;max-width:560px;margin:auto}"
"input,select,textarea{width:100%;box-sizing:border-box;font-size:16px;padding:10px;margin:6px 0 12px;border-radius:8px;border:1px solid #6b55c9;background:#1d1446;color:#fff}"
"button{font-size:17px;padding:12px 18px;border:0;border-radius:10px;background:#8d63ff;color:#fff;width:100%}"
".card{background:#1d1446;border-radius:14px;padding:14px;margin:14px 0}small{color:#b9aee8}#ok{color:#9fe8a8}</style></head><body>"
"<h2>Assistant IA</h2>"
"<div class=\"card\"><b>Poser une question</b><small> : la r\xC3\xA9ponse s'affiche sur la t\xC3\xA9l\xC3\xA9 (onglet WEB)</small>"
"<textarea id=\"q\" rows=\"3\"></textarea><button onclick=\"ask()\">Envoyer \xC3\xA0 la GameCube</button><p id=\"okq\"></p></div>"
"<div class=\"card\"><b>R\xC3\xA9glages</b><br><small>Fournisseur</small><select id=\"p\"><option value=\"anthropic\">Anthropic (Claude)</option>"
"<option value=\"openai\">Compatible OpenAI (OpenAI, Mistral, OpenRouter, Ollama...)</option></select>"
"<small>Cl\xC3\xA9 (laisse vide pour garder l'actuelle ; Ollama : aucune)</small><input id=\"k\" type=\"password\" autocomplete=\"off\">"
"<small>Mod\xC3\xA8le (vide : claude-haiku-5-5, ou gpt-4o-mini)</small><input id=\"m\">"
"<small>Adresse (vide : celle du fournisseur ; Ollama : http://IP-du-PC:11434/v1/chat/completions)</small><input id=\"u\">"
"<button onclick=\"save()\">Enregistrer sur la GameCube</button><p id=\"ok\"></p></div>"
"<p><a href=\"/\" style=\"color:#c9a8ff\">\xE2\x86\x90 T\xC3\xA9l\xC3\xA9" "commande</a></p>"
"<script>function e(i){return encodeURIComponent(document.getElementById(i).value)}"
"fetch('/api/ia-cfg').then(r=>r.json()).then(c=>{document.getElementById('p').value=c.p;document.getElementById('m').value=c.m;document.getElementById('u').value=c.u;"
"document.getElementById('k').placeholder=c.k?'cl\xC3\xA9 d\xC3\xA9j\xC3\xA0 enregistr\xC3\xA9" "e':'aucune cl\xC3\xA9'})"
"function save(){fetch('/api/ia?p='+e('p')+'&k='+e('k')+'&m='+e('m')+'&u='+e('u')).then(r=>{document.getElementById('ok').textContent=r.ok?'Enregistr\xC3\xA9 \xE2\x9C\x93':'Erreur'})}"
"function ask(){var q=e('q');if(!q)return;fetch('/api/ia-q?q='+q).then(r=>{document.getElementById('okq').textContent=r.ok?'Envoy\xC3\xA9 \xE2\x9C\x93 (regarde la t\xC3\xA9l\xC3\xA9)':'Erreur (la GameCube est occup\xC3\xA9" "e ?)';"
"if(r.ok)document.getElementById('q').value=''})}</script></body></html>";

static void tp_reply(s32 c, int code, const char *ctype, const void *body, int len);
static int tp_param(const char *q, const char *key, char *out, int cap);
/* routes du telephone ; 1 si traitee (fil du telephone : rien de la boucle principale ici) */
static int ia_phone_route(s32 c, const char *target, const char *q) {
    if (!strcmp(target, "/ia") || !strcmp(target, "/ia/")) { tp_reply(c, 200, "text/html; charset=utf-8", ia_phone_page, (int)strlen(ia_phone_page)); return 1; }
    if (!strcmp(target, "/api/ia-cfg")) {
        ia_load_cfg();
        char j[URL_MAX + 300];
        sbuf_t s = { 0 };
        sb_raw(&s, "{\"p\":", -1); ia_json_str(&s, ia_prov);
        sb_raw(&s, ",\"m\":", -1); ia_json_str(&s, ia_model);
        sb_raw(&s, ",\"u\":", -1); ia_json_str(&s, ia_url);
        sb_raw(&s, ia_key[0] ? ",\"k\":1}" : ",\"k\":0}", -1);
        snprintf(j, sizeof(j), "%s", s.b ? s.b : "{}");
        free(s.b);
        tp_reply(c, 200, "application/json", j, (int)strlen(j));
        return 1;
    }
    if (!strcmp(target, "/api/ia")) {
        if (ia_ph_cfg) { tp_reply(c, 409, "application/json", "{\"ok\":0}", 8); return 1; }
        tp_param(q ? q : "", "p", ia_ph_prov, sizeof(ia_ph_prov));
        tp_param(q ? q : "", "k", ia_ph_key, sizeof(ia_ph_key));
        tp_param(q ? q : "", "m", ia_ph_model, sizeof(ia_ph_model));
        tp_param(q ? q : "", "u", ia_ph_url, sizeof(ia_ph_url));
        for (char *p = ia_ph_key; *p; p++) if (*p == '\r' || *p == '\n') *p = ' ';
        char *kt = trim(ia_ph_key); if (kt != ia_ph_key) memmove(ia_ph_key, kt, strlen(kt) + 1);
        ia_ph_cfg = 1;
        tp_reply(c, 200, "application/json", "{\"ok\":1}", 8);
        return 1;
    }
    if (!strcmp(target, "/api/ia-q")) {
        if (ia_ph_ask) { tp_reply(c, 409, "application/json", "{\"ok\":0}", 8); return 1; }
        tp_param(q ? q : "", "q", ia_ph_q, sizeof(ia_ph_q));
        int ok = ia_ph_q[0] != 0;
        if (ok) ia_ph_ask = 1;
        tp_reply(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}", 8);
        return 1;
    }
    return 0;
}
