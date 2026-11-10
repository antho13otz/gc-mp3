// Appli privee build 498 : presence Discord (« Joue a ... » sur le profil Discord) par le Social SDK de Discord.
// Le SDK n'est PAS dans le depot (ses conditions ne permettent pas de le redistribuer) : il est ajoute a la main dans
// android/app/libs-prive/ (dossier ignore par git) pour l'appli privee. Ce pont ne se lie pas au SDK a la compilation :
// il ouvre libdiscord_partner_sdk.so au moment de l'appel (dlopen / dlsym), donc l'appli se compile sans le SDK.
// Seule l'interface C publique du SDK est utilisee ; les quelques types ci-dessous sont declares ici (structures opaques).
#include <jni.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "GCDiscord", __VA_ARGS__)

typedef struct { uint8_t* ptr; size_t size; } DStr;
typedef struct { void* opaque; } DObj;                 // (Discord_Client, Discord_Activity, ... : un seul pointeur opaque)
typedef void (*FreeFn)(void*);

typedef void (*CbStatut)(int statut, int erreur, int32_t detail, void* u);
typedef void (*CbAutor)(DObj* res, DStr code, DStr redirige, void* u);
typedef void (*CbJeton)(DObj* res, DStr acces, DStr rafraichir, int type, int32_t expire, DStr scopes, void* u);
typedef void (*CbResultat)(DObj* res, void* u);
typedef void (*CbLog)(DStr msg, int gravite, void* u);

static struct {
    void (*Free)(void*);
    void (*RunCallbacks)(void);
    void (*Client_Init)(DObj*);
    void (*Client_SetApplicationId)(DObj*, uint64_t);
    void (*Client_SetStatusChangedCallback)(DObj*, CbStatut, FreeFn, void*);
    void (*Client_AddLogCallback)(DObj*, CbLog, FreeFn, void*, int);
    void (*Client_GetDefaultPresenceScopes)(DStr*);
    void (*Client_CreateAuthorizationCodeVerifier)(DObj*, DObj*);
    void (*Client_Authorize)(DObj*, DObj*, CbAutor, FreeFn, void*);
    void (*Client_GetToken)(DObj*, uint64_t, DStr, DStr, DStr, CbJeton, FreeFn, void*);
    void (*Client_RefreshToken)(DObj*, uint64_t, DStr, CbJeton, FreeFn, void*);
    void (*Client_UpdateToken)(DObj*, int, DStr, CbResultat, FreeFn, void*);
    void (*Client_Connect)(DObj*);
    void (*Client_Disconnect)(DObj*);
    void (*Client_UpdateRichPresence)(DObj*, DObj*, CbResultat, FreeFn, void*);
    void (*Client_ClearRichPresence)(DObj*);
    void (*Verifier_Challenge)(DObj*, DObj*);
    void (*Verifier_Verifier)(DObj*, DStr*);
    void (*Verifier_Drop)(DObj*);
    void (*Challenge_Drop)(DObj*);
    void (*Args_Init)(DObj*);
    void (*Args_Drop)(DObj*);
    void (*Args_SetClientId)(DObj*, uint64_t);
    void (*Args_SetScopes)(DObj*, DStr);
    void (*Args_SetCodeChallenge)(DObj*, DObj*);
    void (*Result_Drop)(DObj*);
    bool (*Result_Successful)(DObj*);
    void (*Result_Error)(DObj*, DStr*);
    void (*Activity_Init)(DObj*);
    void (*Activity_Drop)(DObj*);
    void (*Activity_SetType)(DObj*, int);
    void (*Activity_SetName)(DObj*, DStr);
    void (*Activity_SetDetails)(DObj*, DStr*);
    void (*Activity_SetState)(DObj*, DStr*);
    void (*Activity_SetAssets)(DObj*, DObj*);
    void (*Activity_SetTimestamps)(DObj*, DObj*);
    void (*Assets_Init)(DObj*);
    void (*Assets_Drop)(DObj*);
    void (*Assets_SetLargeImage)(DObj*, DStr*);
    void (*Assets_SetLargeText)(DObj*, DStr*);
    void (*Times_Init)(DObj*);
    void (*Times_Drop)(DObj*);
    void (*Times_SetStart)(DObj*, uint64_t);
} D;

static JavaVM* vm;
static jclass cls;
static DObj client;
static uint64_t appli;
static int pret;
static char verif[256];                               // (verificateur PKCE de la demande de connexion en cours)

static JNIEnv* env_() {
    JNIEnv* e = NULL;
    if ((*vm)->GetEnv(vm, (void**)&e, JNI_VERSION_1_6) != JNI_OK) (*vm)->AttachCurrentThread(vm, &e, NULL);
    return e;
}
static jstring js(JNIEnv* e, DStr s) {
    char* b = malloc(s.size + 1);
    if (!b) return (*e)->NewStringUTF(e, "");
    if (s.size) memcpy(b, s.ptr, s.size);
    b[s.size] = 0;
    jstring r = (*e)->NewStringUTF(e, b);
    free(b);
    return r;
}
static DStr ds(const char* c) { DStr s = { (uint8_t*)c, c ? strlen(c) : 0 }; return s; }
static void libere(DStr s) { if (s.ptr) D.Free(s.ptr); }

/** fin d'un appel : DiscordNatif.retour(quoi, ok, message) ; libere le resultat */
static void retour(int quoi, DObj* res) {
    JNIEnv* e = env_();
    bool ok = D.Result_Successful(res);
    DStr m = {0};
    if (!ok) D.Result_Error(res, &m);
    jstring jm = js(e, m);
    libere(m);
    D.Result_Drop(res);
    jmethodID f = (*e)->GetStaticMethodID(e, cls, "retour", "(IZLjava/lang/String;)V");
    if (f) (*e)->CallStaticVoidMethod(e, cls, f, quoi, ok, jm);
    (*e)->DeleteLocalRef(e, jm);
}

static void surStatut(int statut, int erreur, int32_t detail, void* u) {
    (void)u;
    JNIEnv* e = env_();
    jmethodID f = (*e)->GetStaticMethodID(e, cls, "statut", "(III)V");
    if (f) (*e)->CallStaticVoidMethod(e, cls, f, statut, erreur, (jint)detail);
}
static void surLog(DStr msg, int gravite, void* u) {
    (void)u;
    JNIEnv* e = env_();
    jstring m = js(e, msg);
    libere(msg);
    jmethodID f = (*e)->GetStaticMethodID(e, cls, "journal", "(ILjava/lang/String;)V");
    if (f) (*e)->CallStaticVoidMethod(e, cls, f, gravite, m);
    (*e)->DeleteLocalRef(e, m);
}
static void surJeton(DObj* res, DStr acces, DStr rafraichir, int type, int32_t expire, DStr scopes, void* u) {
    (void)type; (void)u;
    JNIEnv* e = env_();
    bool ok = D.Result_Successful(res);
    DStr m = {0};
    if (!ok) D.Result_Error(res, &m);
    jstring ja = js(e, acces), jr = js(e, rafraichir), jm = js(e, m);
    libere(acces); libere(rafraichir); libere(scopes); libere(m);
    D.Result_Drop(res);
    jmethodID f = (*e)->GetStaticMethodID(e, cls, "jeton", "(ZLjava/lang/String;Ljava/lang/String;ILjava/lang/String;)V");
    if (f) (*e)->CallStaticVoidMethod(e, cls, f, ok, ja, jr, (jint)expire, jm);
    (*e)->DeleteLocalRef(e, ja); (*e)->DeleteLocalRef(e, jr); (*e)->DeleteLocalRef(e, jm);
}
static void surAutor(DObj* res, DStr code, DStr redirige, void* u) {
    (void)u;
    if (!D.Result_Successful(res)) { libere(code); libere(redirige); retour(1, res); return; }
    D.Result_Drop(res);
    D.Client_GetToken(&client, appli, code, ds(verif), redirige, surJeton, NULL, NULL);
    libere(code); libere(redirige);
}
static void surJetonMis(DObj* res, void* u) {
    (void)u;
    bool ok = D.Result_Successful(res);
    if (ok) D.Client_Connect(&client);
    retour(2, res);
}
static void surPresence(DObj* res, void* u) { (void)u; retour(3, res); }

#define SYM(champ, nom) if (!(*(void**)&D.champ = dlsym(h, "Discord_" nom))) { LOG("symbole absent : %s", nom); return JNI_FALSE; }

JNIEXPORT jboolean JNICALL Java_fr_gcstation_telecommande_DiscordNatif_nInit(JNIEnv* e, jclass c, jlong id) {
    if (pret) return JNI_TRUE;
    void* h = dlopen("libdiscord_partner_sdk.so", RTLD_NOW);
    if (!h) { LOG("SDK Discord absent"); return JNI_FALSE; }
    SYM(Free, "Free") SYM(RunCallbacks, "RunCallbacks")
    SYM(Client_Init, "Client_Init") SYM(Client_SetApplicationId, "Client_SetApplicationId")
    SYM(Client_SetStatusChangedCallback, "Client_SetStatusChangedCallback") SYM(Client_AddLogCallback, "Client_AddLogCallback")
    SYM(Client_GetDefaultPresenceScopes, "Client_GetDefaultPresenceScopes")
    SYM(Client_CreateAuthorizationCodeVerifier, "Client_CreateAuthorizationCodeVerifier")
    SYM(Client_Authorize, "Client_Authorize") SYM(Client_GetToken, "Client_GetToken") SYM(Client_RefreshToken, "Client_RefreshToken")
    SYM(Client_UpdateToken, "Client_UpdateToken") SYM(Client_Connect, "Client_Connect") SYM(Client_Disconnect, "Client_Disconnect")
    SYM(Client_UpdateRichPresence, "Client_UpdateRichPresence") SYM(Client_ClearRichPresence, "Client_ClearRichPresence")
    SYM(Verifier_Challenge, "AuthorizationCodeVerifier_Challenge") SYM(Verifier_Verifier, "AuthorizationCodeVerifier_Verifier")
    SYM(Verifier_Drop, "AuthorizationCodeVerifier_Drop") SYM(Challenge_Drop, "AuthorizationCodeChallenge_Drop")
    SYM(Args_Init, "AuthorizationArgs_Init") SYM(Args_Drop, "AuthorizationArgs_Drop") SYM(Args_SetClientId, "AuthorizationArgs_SetClientId")
    SYM(Args_SetScopes, "AuthorizationArgs_SetScopes") SYM(Args_SetCodeChallenge, "AuthorizationArgs_SetCodeChallenge")
    SYM(Result_Drop, "ClientResult_Drop") SYM(Result_Successful, "ClientResult_Successful") SYM(Result_Error, "ClientResult_Error")
    SYM(Activity_Init, "Activity_Init") SYM(Activity_Drop, "Activity_Drop") SYM(Activity_SetType, "Activity_SetType")
    SYM(Activity_SetName, "Activity_SetName") SYM(Activity_SetDetails, "Activity_SetDetails") SYM(Activity_SetState, "Activity_SetState")
    SYM(Activity_SetAssets, "Activity_SetAssets") SYM(Activity_SetTimestamps, "Activity_SetTimestamps")
    SYM(Assets_Init, "ActivityAssets_Init") SYM(Assets_Drop, "ActivityAssets_Drop")
    SYM(Assets_SetLargeImage, "ActivityAssets_SetLargeImage") SYM(Assets_SetLargeText, "ActivityAssets_SetLargeText")
    SYM(Times_Init, "ActivityTimestamps_Init") SYM(Times_Drop, "ActivityTimestamps_Drop") SYM(Times_SetStart, "ActivityTimestamps_SetStart")
    (*e)->GetJavaVM(e, &vm);
    cls = (*e)->NewGlobalRef(e, c);
    appli = (uint64_t)id;
    D.Client_Init(&client);
    D.Client_SetApplicationId(&client, appli);
    D.Client_SetStatusChangedCallback(&client, surStatut, NULL, NULL);
    D.Client_AddLogCallback(&client, surLog, NULL, NULL, 3);           // (avertissements et erreurs)
    pret = 1;
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_fr_gcstation_telecommande_DiscordNatif_nTick(JNIEnv* e, jclass c) {
    (void)e; (void)c;
    if (pret) D.RunCallbacks();
}

/** connexion au compte Discord : ouvre Discord (ou le navigateur) pour accepter ; la suite arrive par jeton() */
JNIEXPORT void JNICALL Java_fr_gcstation_telecommande_DiscordNatif_nAutoriser(JNIEnv* e, jclass c) {
    (void)e; (void)c;
    if (!pret) return;
    DObj v = {0}, ch = {0}, a = {0};
    DStr sv = {0}, sc = {0};
    D.Client_CreateAuthorizationCodeVerifier(&client, &v);
    D.Verifier_Verifier(&v, &sv);
    size_t n = sv.size < sizeof verif - 1 ? sv.size : sizeof verif - 1;
    memcpy(verif, sv.ptr, n); verif[n] = 0;
    libere(sv);
    D.Verifier_Challenge(&v, &ch);
    D.Args_Init(&a);
    D.Args_SetClientId(&a, appli);
    D.Client_GetDefaultPresenceScopes(&sc);
    D.Args_SetScopes(&a, sc);
    libere(sc);
    D.Args_SetCodeChallenge(&a, &ch);
    D.Client_Authorize(&client, &a, surAutor, NULL, NULL);
    D.Args_Drop(&a); D.Challenge_Drop(&ch); D.Verifier_Drop(&v);
}

JNIEXPORT void JNICALL Java_fr_gcstation_telecommande_DiscordNatif_nRafraichir(JNIEnv* e, jclass c, jstring r) {
    (void)c;
    if (!pret) return;
    const char* s = (*e)->GetStringUTFChars(e, r, NULL);
    D.Client_RefreshToken(&client, appli, ds(s), surJeton, NULL, NULL);
    (*e)->ReleaseStringUTFChars(e, r, s);
}

/** jeton d'acces obtenu : le donne au SDK puis se connecte (retour(2, ...)) */
JNIEXPORT void JNICALL Java_fr_gcstation_telecommande_DiscordNatif_nJeton(JNIEnv* e, jclass c, jstring a) {
    (void)c;
    if (!pret) return;
    const char* s = (*e)->GetStringUTFChars(e, a, NULL);
    D.Client_UpdateToken(&client, 1 /* Bearer */, ds(s), surJetonMis, NULL, NULL);
    (*e)->ReleaseStringUTFChars(e, a, s);
}

JNIEXPORT void JNICALL Java_fr_gcstation_telecommande_DiscordNatif_nDeconnecter(JNIEnv* e, jclass c) {
    (void)e; (void)c;
    if (pret) D.Client_Disconnect(&client);
}

/** « Joue a GC Station » : details (le jeu), etat (ce qu'on fait), image (cle ou adresse https), debut (ms) */
JNIEXPORT void JNICALL Java_fr_gcstation_telecommande_DiscordNatif_nPresence(JNIEnv* e, jclass c, jstring det, jstring eta,
                                                                             jstring img, jstring txt, jlong debut) {
    (void)c;
    if (!pret) return;
    const char* sd = (*e)->GetStringUTFChars(e, det, NULL);
    const char* se = (*e)->GetStringUTFChars(e, eta, NULL);
    const char* si = (*e)->GetStringUTFChars(e, img, NULL);
    const char* st = (*e)->GetStringUTFChars(e, txt, NULL);
    DObj a = {0}, as = {0}, t = {0};
    D.Activity_Init(&a);
    D.Activity_SetType(&a, 0);                                        // (Playing)
    D.Activity_SetName(&a, ds("GC Station"));
    DStr xd = ds(sd), xe = ds(se), xi = ds(si), xt = ds(st);
    if (xd.size) D.Activity_SetDetails(&a, &xd);
    if (xe.size) D.Activity_SetState(&a, &xe);
    if (xi.size) {
        D.Assets_Init(&as);
        D.Assets_SetLargeImage(&as, &xi);
        if (xt.size) D.Assets_SetLargeText(&as, &xt);
        D.Activity_SetAssets(&a, &as);
        D.Assets_Drop(&as);
    }
    if (debut > 0) {
        D.Times_Init(&t);
        D.Times_SetStart(&t, (uint64_t)debut);
        D.Activity_SetTimestamps(&a, &t);
        D.Times_Drop(&t);
    }
    D.Client_UpdateRichPresence(&client, &a, surPresence, NULL, NULL);
    D.Activity_Drop(&a);
    (*e)->ReleaseStringUTFChars(e, det, sd); (*e)->ReleaseStringUTFChars(e, eta, se);
    (*e)->ReleaseStringUTFChars(e, img, si); (*e)->ReleaseStringUTFChars(e, txt, st);
}

JNIEXPORT void JNICALL Java_fr_gcstation_telecommande_DiscordNatif_nEffacer(JNIEnv* e, jclass c) {
    (void)e; (void)c;
    if (pret) D.Client_ClearRichPresence(&client);
}
