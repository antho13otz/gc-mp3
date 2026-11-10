/* Appli (build 491) : RetroAchievements (retroachievements.org) pour les jeux joues sur la vraie GameCube.
   rcheevos (bibliotheque de RetroAchievements, licence MIT, src/main/cpp/rcheevos : telechargee en version figee par
   ci/appli.sh) fait tout le travail : compte, succes du jeu, verification a chaque image, deblocages. Ce fichier est
   seulement le pont avec l'appli (RetroNatif.kt) :
   - memoire : une copie de la memoire du jeu (24 Mo, adresses RetroAchievements 0x00000000-0x017FFFFF = 0x80000000-
     0x817FFFFF de la GameCube), remplie par le direct de Pulse (les morceaux auxquels l'appli s'abonne) ;
   - quels morceaux : ceux que rcheevos lit (note a chaque lecture, par tranches de 16 octets) : l'appli s'y abonne ;
     une lecture d'une tranche pas encore recue est comptee (« manque ») : l'appli attend alors qu'elle arrive ;
   - reseau : rcheevos demande, l'appli fait la requete HTTPS (RetroNatif.requete) et rend la reponse (nReponse) ;
   - evenements (succes debloques...) et resultats (connexion, chargement du jeu) : rendus a l'appli.
   Tous les appels viennent de l'appli, un seul a la fois (verrou cote Kotlin). */
#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <android/log.h>
#include "rc_client.h"
#include "rc_api_request.h"
#include "rc_api_editor.h"
/* (en-tetes internes de rcheevos : seulement pour nSonder, la memoire mise a jour sans verifier les succes) */
#include "client/rc_client_internal.h"
#include "client/rc_client_game.h"
#include "runtime/rc_eval_state.h"
#include "runtime/rc_modified_memref.h"

#define MEM_TAILLE 0x01800000u
#define TRANCHE    16u
#define NTR        (MEM_TAILLE / TRANCHE)
#define NUTIL      4096                                    /* tranches suivies au plus */

static JNIEnv *g_env;
static jclass g_cls;
static rc_client_t *g_client;
static uint8_t *g_mem;                                     /* copie de la memoire du jeu */
static uint8_t *g_recu;                                    /* (bits) tranche recue dans la derniere image appliquee */
static uint8_t *g_suivie;                                  /* (bits) tranche dans g_util */
static uint32_t g_util[NUTIL], g_utilImg[NUTIL]; static int g_nutil;
static uint32_t g_img, g_manque, g_nouvelles;
static void suivre_epingles(void);

static int bit(const uint8_t *b, uint32_t i) { return (b[i >> 3] >> (i & 7)) & 1; }
static void mettre(uint8_t *b, uint32_t i) { b[i >> 3] |= (uint8_t)(1u << (i & 7)); }

/* ---------- appels vers l'appli ---------- */
static void journal(const char *m) {
    __android_log_print(ANDROID_LOG_INFO, "GCS-RA", "%s", m);
    if (!g_env || !g_cls) return;
    jmethodID f = (*g_env)->GetStaticMethodID(g_env, g_cls, "journal", "(Ljava/lang/String;)V");
    jstring s = (*g_env)->NewStringUTF(g_env, m);
    (*g_env)->CallStaticVoidMethod(g_env, g_cls, f, s);
    (*g_env)->DeleteLocalRef(g_env, s);
}
static void resultat(int quoi, int r, const char *msg) {
    jmethodID f = (*g_env)->GetStaticMethodID(g_env, g_cls, "resultat", "(IILjava/lang/String;)V");
    jstring s = (*g_env)->NewStringUTF(g_env, msg ? msg : "");
    (*g_env)->CallStaticVoidMethod(g_env, g_cls, f, (jint)quoi, (jint)r, s);
    (*g_env)->DeleteLocalRef(g_env, s);
}

/* ---------- memoire ---------- */
static uint32_t RC_CCONV lire(uint32_t a, uint8_t *buf, uint32_t n, rc_client_t *c) {
    (void)c;
    if (a >= MEM_TAILLE) return 0;
    if (n > MEM_TAILLE - a) n = MEM_TAILLE - a;
    memcpy(buf, g_mem + a, n);
    for (uint32_t t = a / TRANCHE; t <= (a + n - 1) / TRANCHE; t++) {
        if (!bit(g_suivie, t)) {
            if (g_nutil < NUTIL) { mettre(g_suivie, t); g_util[g_nutil] = t; g_utilImg[g_nutil] = g_img; g_nutil++; g_nouvelles++; }
        } else for (int i = g_nutil - 1; i >= 0; i--) if (g_util[i] == t) { g_utilImg[i] = g_img; break; }
        if (!bit(g_recu, t)) g_manque++;
    }
    return n;                                              /* (jamais moins : rcheevos couperait le succes pour toujours) */
}

/* ---------- reseau ---------- */
typedef struct { rc_client_server_callback_t cb; void *data; int id; } attente_t;
#define NATT 64
static attente_t g_att[NATT]; static int g_attId = 1;
static void RC_CCONV serveur(const rc_api_request_t *rq, rc_client_server_callback_t cb, void *data, rc_client_t *c) {
    (void)c;
    int k = -1;
    for (int i = 0; i < NATT; i++) if (!g_att[i].cb) { k = i; break; }
    if (k < 0) { rc_api_server_response_t r = { NULL, 0, RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR }; cb(&r, data); return; }
    g_att[k].cb = cb; g_att[k].data = data; g_att[k].id = g_attId++;
    jmethodID f = (*g_env)->GetStaticMethodID(g_env, g_cls, "requete", "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    jstring u = (*g_env)->NewStringUTF(g_env, rq->url);
    jstring p = rq->post_data ? (*g_env)->NewStringUTF(g_env, rq->post_data) : NULL;
    jstring t = rq->content_type ? (*g_env)->NewStringUTF(g_env, rq->content_type) : NULL;
    (*g_env)->CallStaticVoidMethod(g_env, g_cls, f, (jint)g_att[k].id, u, p, t);
    (*g_env)->DeleteLocalRef(g_env, u);
    if (p) (*g_env)->DeleteLocalRef(g_env, p);
    if (t) (*g_env)->DeleteLocalRef(g_env, t);
}

/* ---------- evenements ---------- */
static void RC_CCONV evenement(const rc_client_event_t *e, rc_client_t *c) {
    (void)c;
    const rc_client_achievement_t *a = e->achievement;
    /* (build 500 : aussi les classements (leaderboards), leurs compteurs, les defis et la progression : obligatoires en
       hardcore. Champs passes a l'appli selon le type : id, titre, description, nombre, texte 1, texte 2) */
    char url[256] = "", num[32] = "";
    const char *ti = "", *de = "", *msg = "";
    int id = 0, nb = 0;
    if (a) { rc_client_achievement_get_image_url(a, RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED, url, sizeof(url));
        id = (int)a->id; ti = a->title ? a->title : ""; de = a->description ? a->description : ""; nb = (int)a->points;
        if (e->type == RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW || e->type == RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE) msg = a->measured_progress; }
    const rc_client_leaderboard_t *l = e->leaderboard;
    if (l && (e->type == RC_CLIENT_EVENT_LEADERBOARD_STARTED || e->type == RC_CLIENT_EVENT_LEADERBOARD_FAILED || e->type == RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED)) {
        id = (int)l->id; ti = l->title ? l->title : ""; de = l->description ? l->description : ""; nb = 0;
        snprintf(url, sizeof(url), "%s", l->tracker_value ? l->tracker_value : ""); }
    if (e->leaderboard_tracker && e->type >= RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW && e->type <= RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE) {
        id = (int)e->leaderboard_tracker->id; ti = e->leaderboard_tracker->display; }
    if (e->leaderboard_scoreboard && e->type == RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD) {
        const rc_client_leaderboard_scoreboard_t *sb = e->leaderboard_scoreboard;
        id = (int)sb->leaderboard_id; ti = l && l->title ? l->title : ""; de = sb->submitted_score; nb = (int)sb->new_rank;
        snprintf(url, sizeof(url), "%s", sb->best_score); snprintf(num, sizeof(num), "%u", sb->num_entries); msg = num; }
    if (e->server_error && e->server_error->error_message) msg = e->server_error->error_message;
    jmethodID f = (*g_env)->GetStaticMethodID(g_env, g_cls, "evenement", "(IILjava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;)V");
    jstring jt = (*g_env)->NewStringUTF(g_env, ti);
    jstring jd = (*g_env)->NewStringUTF(g_env, de);
    jstring ju = (*g_env)->NewStringUTF(g_env, url);
    jstring jm = (*g_env)->NewStringUTF(g_env, msg);
    (*g_env)->CallStaticVoidMethod(g_env, g_cls, f, (jint)e->type, (jint)id, jt, jd, (jint)nb, ju, jm);
    (*g_env)->DeleteLocalRef(g_env, jt); (*g_env)->DeleteLocalRef(g_env, jd); (*g_env)->DeleteLocalRef(g_env, ju); (*g_env)->DeleteLocalRef(g_env, jm);
}
static void RC_CCONV journalRc(const char *m, const rc_client_t *c) { (void)c; journal(m); }
static void RC_CCONV finConnexion(int r, const char *m, rc_client_t *c, void *u) { (void)c; (void)u; resultat(1, r, m); }
static void RC_CCONV finJeu(int r, const char *m, rc_client_t *c, void *u) { (void)c; (void)u; resultat(2, r, m); }

#define F(nom) Java_fr_gcstation_telecommande_RetroNatif_##nom
#define ENTREE(env) do { g_env = env; } while (0)

JNIEXPORT jboolean JNICALL F(nInit)(JNIEnv *env, jobject self) {
    ENTREE(env);
    if (g_client) return JNI_TRUE;
    if (!g_cls) { jclass l = (*env)->FindClass(env, "fr/gcstation/telecommande/RetroNatif"); g_cls = (jclass)(*env)->NewGlobalRef(env, l); }
    g_mem = (uint8_t *)calloc(1, MEM_TAILLE);
    g_recu = (uint8_t *)calloc(1, NTR / 8);
    g_suivie = (uint8_t *)calloc(1, NTR / 8);
    if (!g_mem || !g_recu || !g_suivie) return JNI_FALSE;
    g_client = rc_client_create(lire, serveur);
    if (!g_client) return JNI_FALSE;
    rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_WARN, journalRc);
    rc_client_set_event_handler(g_client, evenement);
    rc_client_set_hardcore_enabled(g_client, 0);           /* (Softcore : Hardcore seulement si RetroAchievements valide l'appli) */
    return JNI_TRUE;
}
JNIEXPORT jstring JNICALL F(nClauseAgent)(JNIEnv *env, jobject self) {
    ENTREE(env);
    char b[64] = "";
    if (g_client) rc_client_get_user_agent_clause(g_client, b, sizeof(b));
    return (*env)->NewStringUTF(env, b);
}
JNIEXPORT void JNICALL F(nReponse)(JNIEnv *env, jobject self, jint id, jint statut, jbyteArray corps) {
    ENTREE(env);
    for (int i = 0; i < NATT; i++) if (g_att[i].cb && g_att[i].id == id) {
        rc_client_server_callback_t cb = g_att[i].cb; void *d = g_att[i].data;
        g_att[i].cb = NULL;
        jsize n = corps ? (*env)->GetArrayLength(env, corps) : 0;
        char *b = (char *)malloc((size_t)n + 1);
        if (!b) return;
        if (n) (*env)->GetByteArrayRegion(env, corps, 0, n, (jbyte *)b);
        b[n] = 0;
        rc_api_server_response_t r = { n ? b : NULL, (size_t)n, statut };
        cb(&r, d);
        free(b);
        return;
    }
}
JNIEXPORT void JNICALL F(nConnexionMotDePasse)(JNIEnv *env, jobject self, jstring u, jstring p) {
    ENTREE(env);
    const char *su = (*env)->GetStringUTFChars(env, u, 0), *sp = (*env)->GetStringUTFChars(env, p, 0);
    rc_client_begin_login_with_password(g_client, su, sp, finConnexion, NULL);
    (*env)->ReleaseStringUTFChars(env, u, su); (*env)->ReleaseStringUTFChars(env, p, sp);
}
JNIEXPORT void JNICALL F(nConnexionJeton)(JNIEnv *env, jobject self, jstring u, jstring t) {
    ENTREE(env);
    const char *su = (*env)->GetStringUTFChars(env, u, 0), *st = (*env)->GetStringUTFChars(env, t, 0);
    rc_client_begin_login_with_token(g_client, su, st, finConnexion, NULL);
    (*env)->ReleaseStringUTFChars(env, u, su); (*env)->ReleaseStringUTFChars(env, t, st);
}
JNIEXPORT void JNICALL F(nDeconnexion)(JNIEnv *env, jobject self) { ENTREE(env); rc_client_logout(g_client); }
/* {nom affiche, identifiant, jeton, points (Softcore), points (Hardcore)} ou null */
JNIEXPORT jobjectArray JNICALL F(nUtilisateur)(JNIEnv *env, jobject self) {
    ENTREE(env);
    const rc_client_user_t *u = g_client ? rc_client_get_user_info(g_client) : NULL;
    if (!u) return NULL;
    char s1[16], s2[16];
    snprintf(s1, sizeof(s1), "%u", u->score_casual); snprintf(s2, sizeof(s2), "%u", u->score);
    const char *v[5] = { u->display_name ? u->display_name : "", u->username ? u->username : "", u->token ? u->token : "", s1, s2 };
    jobjectArray r = (*env)->NewObjectArray(env, 5, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (int i = 0; i < 5; i++) { jstring s = (*env)->NewStringUTF(env, v[i]); (*env)->SetObjectArrayElement(env, r, i, s); (*env)->DeleteLocalRef(env, s); }
    return r;
}
static void raz_memoire(void) {
    memset(g_recu, 0, NTR / 8); memset(g_suivie, 0, NTR / 8); g_nutil = 0; g_manque = g_nouvelles = 0;
}
JNIEXPORT void JNICALL F(nChargerJeu)(JNIEnv *env, jobject self, jstring h) {
    ENTREE(env);
    raz_memoire(); memset(g_mem, 0, MEM_TAILLE);
    const char *sh = (*env)->GetStringUTFChars(env, h, 0);
    rc_client_begin_load_game(g_client, sh, finJeu, NULL);
    (*env)->ReleaseStringUTFChars(env, h, sh);
}
JNIEXPORT void JNICALL F(nFermerJeu)(JNIEnv *env, jobject self) { ENTREE(env); rc_client_unload_game(g_client); raz_memoire(); }
/* {id, titre, url de l'image} ou null */
JNIEXPORT jobjectArray JNICALL F(nJeu)(JNIEnv *env, jobject self) {
    ENTREE(env);
    const rc_client_game_t *g = g_client ? rc_client_get_game_info(g_client) : NULL;
    if (!g) return NULL;
    char id[16], url[256] = "";
    snprintf(id, sizeof(id), "%u", g->id);
    rc_client_game_get_image_url(g, url, sizeof(url));
    const char *v[3] = { id, g->title ? g->title : "", url };
    jobjectArray r = (*env)->NewObjectArray(env, 3, (*env)->FindClass(env, "java/lang/String"), NULL);
    for (int i = 0; i < 3; i++) { jstring s = (*env)->NewStringUTF(env, v[i]); (*env)->SetObjectArrayElement(env, r, i, s); (*env)->DeleteLocalRef(env, s); }
    return r;
}
/* succes du jeu, une ligne par succes : id \t points \t debloque (0/1) \t titre \t description \t progression \t image */
static void propre(char *s) { for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' '; }
JNIEXPORT jstring JNICALL F(nListe)(JNIEnv *env, jobject self) {
    ENTREE(env);
    rc_client_achievement_list_t *l = g_client ? rc_client_create_achievement_list(g_client, RC_CLIENT_ACHIEVEMENT_CATEGORY_PROMOTED, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE) : NULL;
    if (!l) return (*env)->NewStringUTF(env, "");
    size_t cap = 4096, n = 0; char *out = (char *)malloc(cap); out[0] = 0;
    for (uint32_t b = 0; b < l->num_buckets; b++) for (uint32_t i = 0; i < l->buckets[b].num_achievements; i++) {
        const rc_client_achievement_t *a = l->buckets[b].achievements[i];
        char t[200], d[400], url[256] = "", ligne[1100];
        snprintf(t, sizeof(t), "%s", a->title ? a->title : ""); propre(t);
        snprintf(d, sizeof(d), "%s", a->description ? a->description : ""); propre(d);
        rc_client_achievement_get_image_url(a, a->unlocked ? RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED : RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE, url, sizeof(url));
        int k = snprintf(ligne, sizeof(ligne), "%u\t%u\t%d\t%s\t%s\t%s\t%s\n", a->id, a->points, a->unlocked ? 1 : 0, t, d, a->measured_progress, url);
        if (k < 0) continue;
        if (n + (size_t)k + 1 > cap) { cap = (n + k + 1) * 2; char *o2 = (char *)realloc(out, cap); if (!o2) break; out = o2; }
        memcpy(out + n, ligne, (size_t)k); n += (size_t)k; out[n] = 0;
    }
    rc_client_destroy_achievement_list(l);
    jstring r = (*env)->NewStringUTF(env, out);
    free(out);
    return r;
}
/* tranches lues par rcheevos (pas lues depuis maxAge images : oubliees), en plages {adresse, longueur}, plages
   voisines reunies (ecart <= ecart octets) */
static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
JNIEXPORT jintArray JNICALL F(nPlages)(JNIEnv *env, jobject self, jint maxAge, jint ecart) {
    ENTREE(env);
    suivre_epingles();                                     /* (build 494 : jauges) */
    int k = 0;
    for (int i = 0; i < g_nutil; i++) {                     /* (oubli des tranches plus lues) */
        if (g_img - g_utilImg[i] > (uint32_t)maxAge) { g_suivie[g_util[i] >> 3] &= (uint8_t)~(1u << (g_util[i] & 7)); continue; }
        g_util[k] = g_util[i]; g_utilImg[k] = g_utilImg[i]; k++;
    }
    g_nutil = k;
    uint32_t *t = (uint32_t *)malloc(sizeof(uint32_t) * (k ? k : 1));
    for (int i = 0; i < k; i++) t[i] = g_util[i];
    qsort(t, (size_t)k, sizeof(uint32_t), cmp_u32);
    jint *p = (jint *)malloc(sizeof(jint) * 2 * (k ? k : 1)); int np = 0;
    for (int i = 0; i < k; i++) {
        uint32_t a = t[i] * TRANCHE;
        if (np && (uint32_t)p[np * 2 - 2] + (uint32_t)p[np * 2 - 1] + (uint32_t)ecart >= a) p[np * 2 - 1] = (jint)(a + TRANCHE - (uint32_t)p[np * 2 - 2]);
        else { p[np * 2] = (jint)a; p[np * 2 + 1] = (jint)TRANCHE; np++; }
    }
    jintArray r = (*env)->NewIntArray(env, np * 2);
    (*env)->SetIntArrayRegion(env, r, 0, np * 2, p);
    free(t); free(p);
    return r;
}
/* une image du direct : les plages (adresse, longueur) et leurs octets a la suite (depuis off) */
JNIEXPORT void JNICALL F(nAppliquer)(JNIEnv *env, jobject self, jintArray plages, jbyteArray data, jint off) {
    ENTREE(env);
    jsize np = (*env)->GetArrayLength(env, plages) / 2, nd = (*env)->GetArrayLength(env, data);
    jint *p = (*env)->GetIntArrayElements(env, plages, 0);
    jbyte *d = (*env)->GetByteArrayElements(env, data, 0);
    memset(g_recu, 0, NTR / 8);
    int o = off;
    for (jsize i = 0; i < np; i++) {
        uint32_t a = (uint32_t)p[i * 2], n = (uint32_t)p[i * 2 + 1];
        if (a >= MEM_TAILLE || n > MEM_TAILLE - a || o + (int)n > nd) break;
        memcpy(g_mem + a, d + o, n); o += (int)n;
        for (uint32_t t = a / TRANCHE; t <= (a + n - 1) / TRANCHE; t++) if (t * TRANCHE >= a && t * TRANCHE + TRANCHE <= a + n) mettre(g_recu, t);
    }
    (*env)->ReleaseIntArrayElements(env, plages, p, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, data, d, JNI_ABORT);
}
/* une image verifiee ; rend {lectures de tranches pas recues, nouvelles tranches lues} */
JNIEXPORT jintArray JNICALL F(nImage)(JNIEnv *env, jobject self) {
    ENTREE(env);
    g_manque = g_nouvelles = 0; g_img++;
    rc_client_do_frame(g_client);
    jint v[2] = { (jint)g_manque, (jint)g_nouvelles };
    jintArray r = (*env)->NewIntArray(env, 2);
    (*env)->SetIntArrayRegion(env, r, 0, 2, v);
    return r;
}
/* sonde : rcheevos lit toute la memoire qu'il suit (et les pointeurs), SANS verifier les succes : les adresses a
   suivre sont connues avant de jouer pour de vrai (rodage), et quand une adresse nouvelle manque (attente) ;
   rend {lectures de tranches pas recues, nouvelles tranches lues} */
static uint32_t RC_CCONV lire_ud(uint32_t a, uint8_t *b, uint32_t n, void *ud) { (void)ud; return lire(a, b, n, NULL); }
JNIEXPORT jintArray JNICALL F(nSonder)(JNIEnv *env, jobject self) {
    ENTREE(env);
    g_manque = g_nouvelles = 0; g_img++;
    if (g_client && g_client->game && rc_client_is_game_loaded(g_client) && g_client->game->runtime.memrefs) {
        rc_eval_state_t st;
        memset(&st, 0, sizeof(st));
        st.read_memory = lire_ud; st.read_memory_userdata = g_client;
        rc_update_memref_values(g_client->game->runtime.memrefs, &st);
    }
    jint v[2] = { (jint)g_manque, (jint)g_nouvelles };
    jintArray r = (*env)->NewIntArray(env, 2);
    (*env)->SetIntArrayRegion(env, r, 0, 2, v);
    return r;
}
/* tout recommence (succes en attente : il faut revoir leur condition fausse) : apres les premieres images (memoire
   encore vide) ou quand le jeu est relance */
JNIEXPORT void JNICALL F(nRecommencer)(JNIEnv *env, jobject self) { ENTREE(env); rc_client_reset(g_client); }
JNIEXPORT void JNICALL F(nAttente)(JNIEnv *env, jobject self) { ENTREE(env); rc_client_idle(g_client); }
/* build 500 : mode hardcore (choisi avant de charger le jeu ; le couper en cours de partie est permis, l'activer non :
   l'appli ne l'active qu'au lancement d'un jeu) */
JNIEXPORT void JNICALL F(nHardcore)(JNIEnv *env, jobject self, jboolean on) { ENTREE(env); if (g_client) rc_client_set_hardcore_enabled(g_client, on ? 1 : 0); }
JNIEXPORT jboolean JNICALL F(nEstHardcore)(JNIEnv *env, jobject self) { ENTREE(env); return g_client && rc_client_get_hardcore_enabled(g_client) ? JNI_TRUE : JNI_FALSE; }

/* ---------- build 494 : la phrase « ce que tu fais » (Rich Presence) et les notes d'adresses des developpeurs RA ---------- */
JNIEXPORT jstring JNICALL F(nPresence)(JNIEnv *env, jobject self) {
    ENTREE(env);
    char b[512] = "";
    if (g_client && rc_client_has_rich_presence(g_client)) rc_client_get_rich_presence_message(g_client, b, sizeof(b));
    return (*env)->NewStringUTF(env, b);
}
/* notes du jeu (« code notes », publiques sur le site) : demandees au site, rendues a l'appli (RetroNatif.notes) en
   lignes « adresse \t note » (retours a la ligne de la note remplaces par \u001F) */
static void RC_CCONV finNotes(const rc_api_server_response_t *r, void *d) {
    (void)d;
    rc_api_fetch_code_notes_response_t rep;
    memset(&rep, 0, sizeof(rep));
    int ok = rc_api_process_fetch_code_notes_server_response(&rep, r) == RC_OK && rep.response.succeeded;
    size_t cap = 65536, n = 0; char *out = (char *)malloc(cap); out[0] = 0;
    if (ok) for (uint32_t i = 0; i < rep.num_notes; i++) {
        const char *t = rep.notes[i].note ? rep.notes[i].note : "";
        size_t lt = strlen(t);
        if (n + lt + 32 > cap) { cap = (n + lt + 32) * 2; char *o2 = (char *)realloc(out, cap); if (!o2) break; out = o2; }
        n += (size_t)snprintf(out + n, cap - n, "%u\t", rep.notes[i].address);
        for (size_t k = 0; k < lt; k++) { char c = t[k]; out[n++] = (c == '\n' || c == '\r') ? '\x1F' : (c == '\t' ? ' ' : c); }
        out[n++] = '\n'; out[n] = 0;
    }
    rc_api_destroy_fetch_code_notes_response(&rep);
    jmethodID f = (*g_env)->GetStaticMethodID(g_env, g_cls, "notes", "(ZLjava/lang/String;)V");
    jstring s = (*g_env)->NewStringUTF(g_env, out);
    (*g_env)->CallStaticVoidMethod(g_env, g_cls, f, (jboolean)ok, s);
    (*g_env)->DeleteLocalRef(g_env, s);
    free(out);
}
JNIEXPORT void JNICALL F(nNotes)(JNIEnv *env, jobject self) {
    ENTREE(env);
    const rc_client_game_t *g = g_client ? rc_client_get_game_info(g_client) : NULL;
    if (!g || !g->id) return;
    rc_api_fetch_code_notes_request_t p; p.game_id = g->id;
    rc_api_request_t rq; memset(&rq, 0, sizeof(rq));
    if (rc_api_init_fetch_code_notes_request(&rq, &p) == RC_OK) serveur(&rq, finNotes, NULL, g_client);
    rc_api_destroy_request(&rq);
}
/* adresses epinglees (jauges) : toujours suivies, meme si rcheevos ne les lit pas */
#define NEPI 32
static uint32_t g_epi[NEPI], g_epiN[NEPI]; static int g_nepi;
JNIEXPORT void JNICALL F(nEpingles)(JNIEnv *env, jobject self, jintArray a, jintArray n) {
    ENTREE(env);
    jsize k = (*env)->GetArrayLength(env, a); if (k > NEPI) k = NEPI;
    (*env)->GetIntArrayRegion(env, a, 0, k, (jint *)g_epi);
    (*env)->GetIntArrayRegion(env, n, 0, k, (jint *)g_epiN);
    g_nepi = k;
}
static void suivre_epingles(void) {
    for (int i = 0; i < g_nepi; i++) {
        uint32_t a = g_epi[i], n = g_epiN[i] ? g_epiN[i] : 4;
        if (a >= MEM_TAILLE || n > MEM_TAILLE - a) continue;
        for (uint32_t t = a / TRANCHE; t <= (a + n - 1) / TRANCHE; t++) {
            if (!bit(g_suivie, t)) { if (g_nutil < NUTIL) { mettre(g_suivie, t); g_util[g_nutil] = t; g_utilImg[g_nutil] = g_img; g_nutil++; } }
            else for (int j = g_nutil - 1; j >= 0; j--) if (g_util[j] == t) { g_utilImg[j] = g_img; break; }
        }
    }
}
/* octets de la copie de la memoire (gros-boutiste, comme la GameCube) ; null : pas encore recus */
JNIEXPORT jbyteArray JNICALL F(nOctets)(JNIEnv *env, jobject self, jint a, jint n) {
    ENTREE(env);
    if (!g_mem || a < 0 || n <= 0 || n > 64 || (uint32_t)a >= MEM_TAILLE || (uint32_t)n > MEM_TAILLE - (uint32_t)a) return NULL;
    for (uint32_t t = (uint32_t)a / TRANCHE; t <= ((uint32_t)a + n - 1) / TRANCHE; t++) if (!bit(g_recu, t)) return NULL;
    jbyteArray r = (*env)->NewByteArray(env, n);
    (*env)->SetByteArrayRegion(env, r, 0, n, (const jbyte *)(g_mem + a));
    return r;
}

/* ---------- build 496 : partage de la vue 3D (Partage.kt) : ARGB -> YUV 4:2:0 (BT.601) dans les plans du codeur video,
   en C (en Kotlin, ~25 ms par image : trop lent pour 60 images/s) ---------- */
JNIEXPORT void JNICALL Java_fr_gcstation_telecommande_Partage_nYuv(JNIEnv *env, jobject self, jintArray argb, jint w, jint h,
        jobject yb, jint ys, jint yp, jobject ub, jint us, jint up, jobject vb, jint vs, jint vp) {
    uint8_t *Y = (uint8_t *)(*env)->GetDirectBufferAddress(env, yb), *U = (uint8_t *)(*env)->GetDirectBufferAddress(env, ub), *V = (uint8_t *)(*env)->GetDirectBufferAddress(env, vb);
    if (!Y || !U || !V) return;
    jint *px = (*env)->GetIntArrayElements(env, argb, 0);
    for (int y = 0; y < h; y++) {
        const uint32_t *l = (const uint32_t *)px + (size_t)y * w;
        uint8_t *yl = Y + (size_t)y * ys;
        for (int x = 0; x < w; x++) {
            uint32_t c = l[x]; int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
            yl[(size_t)x * yp] = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
        }
        if (y & 1) continue;
        uint8_t *ul = U + (size_t)(y / 2) * us, *vl = V + (size_t)(y / 2) * vs;
        for (int x = 0; x < w; x += 2) {
            uint32_t c = l[x]; int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
            ul[(size_t)(x / 2) * up] = (uint8_t)(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            vl[(size_t)(x / 2) * vp] = (uint8_t)(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }
    (*env)->ReleaseIntArrayElements(env, argb, px, JNI_ABORT);
}
