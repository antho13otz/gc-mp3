package fr.gcstation.telecommande

// Appli build 491 : RetroAchievements (retroachievements.org) pour les jeux joues sur la vraie GameCube.
// - La console (GC Station v393) calcule l'empreinte du jeu (comme rcheevos) et la donne dans le message de lancement
//   (Live.Game.raHash) ; elle ne parle jamais au site.
// - Pendant la partie, Pulse envoie a chaque image la memoire que les succes du jeu lisent (le direct, MemoireJeu) ;
//   le telephone fait tourner rcheevos (bibliotheque de RetroAchievements, licence MIT, pont : cpp/ra_jni.c), parle au
//   site (compte, succes, deblocages) et previent par une notification.
// - Rodage : avant de verifier les succes, rcheevos lit seulement la memoire (nSonder) jusqu'a connaitre toutes les
//   adresses a suivre et les avoir recues ; puis tout recommence (nRecommencer : chaque succes doit voir sa condition
//   fausse avant de pouvoir se debloquer). Une adresse nouvelle pas encore recue (pointeur qui change) : on sonde sans
//   verifier jusqu'a ce qu'elle arrive.
// - Mode normal (Softcore) seulement : le Hardcore demande que RetroAchievements valide l'appli.
// Le mot de passe n'est jamais garde : seulement le jeton rendu par le site.

import android.content.Context
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** le pont avec rcheevos (cpp/ra_jni.c) ; appels un seul a la fois (RetroSucces.verrou) */
object RetroNatif {
    val ok: Boolean = try { System.loadLibrary("rapont"); true } catch (e: Throwable) { false }
    external fun nInit(): Boolean
    external fun nClauseAgent(): String
    external fun nReponse(id: Int, statut: Int, corps: ByteArray?)
    external fun nConnexionMotDePasse(u: String, p: String)
    external fun nConnexionJeton(u: String, t: String)
    external fun nDeconnexion()
    external fun nUtilisateur(): Array<String>?
    external fun nChargerJeu(hash: String)
    external fun nFermerJeu()
    external fun nJeu(): Array<String>?
    external fun nListe(): String
    external fun nPlages(maxAge: Int, ecart: Int): IntArray
    external fun nAppliquer(plages: IntArray, data: ByteArray, off: Int)
    external fun nImage(): IntArray
    external fun nSonder(): IntArray
    external fun nRecommencer()
    external fun nAttente()
    external fun nPresence(): String
    external fun nHardcore(on: Boolean)                     // (build 500)
    external fun nEstHardcore(): Boolean
    external fun nNotes()
    external fun nEpingles(a: IntArray, n: IntArray)
    external fun nOctets(a: Int, n: Int): ByteArray?

    @Volatile var agent = "GCRemote/1.0.0"
    private val http = java.util.concurrent.Executors.newFixedThreadPool(2)
    /** (rcheevos) une requete au site : faite a cote, la reponse rendue a rcheevos */
    @JvmStatic fun requete(id: Int, url: String, post: String?, type: String?) {
        http.execute {
            var statut: Int; var corps: ByteArray? = null
            try {
                val c = java.net.URL(url).openConnection() as java.net.HttpURLConnection
                c.connectTimeout = 15000; c.readTimeout = 30000
                c.setRequestProperty("User-Agent", agent)
                if (post != null) {
                    c.requestMethod = "POST"; c.doOutput = true
                    c.setRequestProperty("Content-Type", type ?: "application/x-www-form-urlencoded")
                    c.outputStream.use { it.write(post.toByteArray(Charsets.UTF_8)) }
                }
                statut = c.responseCode
                corps = (if (statut >= 400) c.errorStream else c.inputStream)?.use { it.readBytes() }
            } catch (e: Exception) { statut = -2 }                 // (RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR : rcheevos redemandera)
            synchronized(RetroSucces.verrou) { nReponse(id, statut, corps) }
        }
    }
    @JvmStatic fun journal(m: String) = RetroSucces.noter(m)
    @JvmStatic fun notes(ok: Boolean, t: String) = RetroSucces.notesRecues(ok, t)
    @JvmStatic fun resultat(quoi: Int, r: Int, msg: String) = RetroSucces.resultat(quoi, r, msg)
    @JvmStatic fun evenement(type: Int, id: Int, titre: String, desc: String, points: Int, url: String, msg: String) =
        RetroSucces.evenement(type, id, titre, desc, points, url, msg)
}

/** build 494 : une note d'adresse des developpeurs RetroAchievements (« code note ») : adresse, texte, taille lue */
class NoteRA(val adr: Int, val texte: String) {
    val titre: String = texte.split('\u001F').firstOrNull { it.isNotBlank() }?.trim()?.take(120) ?: ""
    /** taille et genre d'apres le texte de la note : [8-bit], [16-bit BE], [32-bit], [float]... (GameCube : gros-boutiste) */
    val octets: Int; val flottant: Boolean
    init {
        val t = texte.lowercase()
        flottant = t.contains("float")
        octets = when {
            flottant -> 4
            Regex("\\b(8[- ]?bit|byte|bool|1[- ]?byte|bit ?flags?)\\b").containsMatchIn(t) -> 1
            Regex("\\b(16[- ]?bit|2[- ]?bytes?|word)\\b").containsMatchIn(t) -> 2
            Regex("\\b(24[- ]?bit)\\b").containsMatchIn(t) -> 3
            else -> 4
        }
    }
    val pointeur: Boolean get() = texte.lowercase().let { it.contains("pointer") || it.contains("ptr") }
}

/** un succes du jeu (liste de rcheevos) */
class SuccesRA(val id: Int, val points: Int, val obtenu: Boolean, val titre: String, val desc: String, val progres: String, val image: String)

object RetroSucces {
    val verrou = Any()
    private var ctx: Context? = null
    private var pret = false
    // ---------- etat montre par l'ecran ----------
    var utilisateur by mutableStateOf("")                  // (nom affiche ; "" : pas connecte)
    var points by mutableStateOf("")
    var etat by mutableStateOf("")
    var jeu by mutableStateOf("")
    var liste by mutableStateOf<List<SuccesRA>>(emptyList())
    var rev by mutableIntStateOf(0)
    var presence by mutableStateOf("")                     // (build 494 : « ce que tu fais en ce moment »)
    var jeuImage = ""                                      // (build 498 : icone du jeu sur RetroAchievements, pour Discord)
    // build 500 / 526 : le mode hardcore (reglage, refus des aides, textes) est dans l'appli privee seulement
    // (ModeHardcore.kt, src/prive) ; l'appli publique n'a que le mode normal (softcore) : ModeHardcore y est vide.
    var hardcore by mutableStateOf(false)                    // (la partie en cours est en hardcore : jamais dans l'appli publique)
    var raisonNormal by mutableStateOf("")                   // (pourquoi cette partie reste en mode normal)
    // build 527 : qualite de la liaison pendant la partie (images de la memoire recues / perdues en route), mise a jour toutes
    // les 2 s, et le bilan de la derniere partie
    var liaison by mutableStateOf("")
    private fun texteLiaison(vues: Long, manquees: Long): String {
        // (build 528 : les images que la console n'a pas envoyees (bus pris par le jeu) ne sont pas des pertes du Wi-Fi)
        val nonEnv = MemoireJeu.nonEnvoyees.coerceIn(0L, manquees); val perdues = manquees - nonEnv
        val tot = vues + perdues
        if (tot <= 0) return ""
        val pc = 100.0 * vues / tot
        return "Liaison avec la console : " + String.format(java.util.Locale.FRANCE, "%.2f", pc) + " % des images reçues" +
            (if (perdues == 0L) " (aucune perdue sur $tot)" else " ($perdues perdues sur $tot)") +
            (if (nonEnv > 0) " ; $nonEnv pas envoyées par la console (bus pris par le jeu)" else "")
    }
    // build 527 : defis, progression et classements aussi sur la tele (bandeau de Pulse), comme les succes
    private var teleProgresT = 0L
    private fun tele(etiquette: String, texte: String) {
        ctx?.let { c -> if (Bandeau.raSurTele(c) && Live.game != null) Api.pool { Bandeau.notifier(etiquette, texte, Bandeau.TROPHEE)?.let { noter("bandeau de la télé : $it") } } }
    }
    var trackers by mutableStateOf<Map<Int, String>>(emptyMap())     // (compteurs des classements en cours)
    var defis by mutableStateOf<Map<Int, String>>(emptyMap())        // (succes « defi » en cours)
    var progres by mutableStateOf("")                                // (progression d'un succes mesure)
    /** pendant une partie hardcore : une aide est refusee (avec un message) ; rend true si c'est refuse */
    fun refuseEnHardcore(c: Context?, quoi: String): Boolean = ModeHardcore.refuse(c, quoi)
    internal fun enPartie() = phase != RIEN
    var notes by mutableStateOf<List<NoteRA>>(emptyList())
    var notesEtat by mutableStateOf("")
    var epinglees by mutableStateOf<List<NoteRA>>(emptyList())
    private val lignesJournal = ArrayDeque<String>()
    fun journalTexte(): String = synchronized(lignesJournal) { lignesJournal.joinToString("\n") }
    fun noter(m: String) {
        val l = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.FRANCE).format(java.util.Date()) + " " + m
        synchronized(lignesJournal) { lignesJournal.addLast(l); while (lignesJournal.size > 500) lignesJournal.removeFirst() }   // (build 540 : 500 lignes au lieu de 60 : journal détaillé)
        android.util.Log.i("GCS-RA", m)
    }
    internal fun pr(c: Context) = c.getSharedPreferences("gcs", 0)
    internal val ui by lazy { android.os.Handler(android.os.Looper.getMainLooper()) }

    /** au demarrage de l'appli : reconnexion avec le jeton garde */
    fun demarrer(c: Context) {
        ctx = c.applicationContext
        MemoireJeu.ctxAppli = c.applicationContext                     // (build 538 : Wi-Fi du direct, meme sans l'ecran compagnon)
        if (pret) return
        if (!RetroNatif.ok) { etat = "Module RetroAchievements absent de cette version de l'appli"; return }
        synchronized(verrou) {
            if (!RetroNatif.nInit()) { etat = "RetroAchievements : mémoire insuffisante"; return }
            RetroNatif.agent = "GCRemote/1." + BuildConfig.VERSION_CODE + ".0 (Android " + android.os.Build.VERSION.RELEASE + ") " + RetroNatif.nClauseAgent()
            pret = true
            ModeHardcore.charger(c)                                // (build 500 ; 526 : appli privee seulement)
            val u = pr(c).getString("ra_user", "") ?: ""; val t = pr(c).getString("ra_token", "") ?: ""
            if (u.isNotEmpty() && t.isNotEmpty()) { etat = "Connexion…"; RetroNatif.nConnexionJeton(u, t) }
        }
    }
    fun connecter(u: String, p: String) {
        if (!pret) return
        etat = "Connexion…"
        synchronized(verrou) { RetroNatif.nConnexionMotDePasse(u.trim(), p) }
    }
    fun deconnecter() {
        if (!pret) return
        arreter()
        synchronized(verrou) { RetroNatif.nDeconnexion() }
        ctx?.let { pr(it).edit().remove("ra_user").remove("ra_token").apply() }
        utilisateur = ""; points = ""; etat = "Déconnecté"
    }

    // ---------- resultats et evenements (rcheevos, sous le verrou) ----------
    fun resultat(quoi: Int, r: Int, msg: String) {
        if (quoi == 1) {
            if (r == 0) {
                val u = RetroNatif.nUtilisateur()
                if (u != null) {
                    ctx?.let { pr(it).edit().putString("ra_user", u[1]).putString("ra_token", u[2]).apply() }
                    ui.post { utilisateur = u[0]; points = u[3]; etat = "Connecté" }
                    noter("connecté : " + u[0])
                }
                ui.post { Live.game?.let { g -> ctx?.let { partie(it, g) } } }
            } else {
                noter("connexion refusée : $msg ($r)")
                ui.post { etat = "Connexion refusée : " + msg.ifEmpty { "erreur $r" } }
                if (r == -34 || r == -35) ctx?.let { pr(it).edit().remove("ra_token").apply() }   // (RC_INVALID_CREDENTIALS, RC_EXPIRED_TOKEN)
            }
        } else if (quoi == 2) {
            if (r == 0) {
                val g = RetroNatif.nJeu()
                val inconnu = g == null || g[0] == "0"
                noter("jeu chargé : " + (g?.getOrNull(1) ?: "?") + " (" + (g?.getOrNull(0) ?: "?") + ")")
                jeuImage = if (inconnu) "" else (g!!.getOrNull(2) ?: ""); ui.post { jeu = if (inconnu) "" else g!![1]; etat = if (inconnu) "Ce jeu n'a pas de succès RetroAchievements (ou cette version n'est pas reconnue)" else "Rodage : lecture de la mémoire…" }
                if (!inconnu) phase = RODAGE
                // (build 500 : le mode de la partie est montre au debut de chaque partie, comme l'exige RetroAchievements)
                if (!inconnu) ctx?.let { PlaybackService.notify(it, 9398, "🏆 RetroAchievements : " + g!![1],
                    if (hardcore) ModeHardcore.TEXTE_PARTIE else "Mode normal" + (if (raisonNormal.isNotEmpty()) " ($raisonNormal)" else "")) }
                majListe()
                if (!inconnu) {                                // (build 494 : notes et jauges gardees pour ce jeu)
                    jeuId = g!![0]
                    val gardees = ctx?.let { pr(it).getString("ra_epi_" + jeuId, "") } ?: ""
                    ui.post { notes = emptyList(); notesEtat = ""; epinglees = emptyList() }
                    if (gardees.isNotEmpty()) ui.post { pendantes = gardees.split(',').mapNotNull { it.toIntOrNull() }.toSet(); chargerNotes() }
                }
            } else {
                noter("jeu refusé : $msg ($r)")
                ui.post { etat = "Jeu non chargé : " + msg.ifEmpty { "erreur $r" } }
            }
        }
    }
    /** badge d'un succes (image du site), null si pas recu en 3 s */
    private fun badgeDe(url: String): android.graphics.Bitmap? = if (!url.startsWith("https://")) null else try {
        val c = java.net.URL(url).openConnection() as java.net.HttpURLConnection
        c.connectTimeout = 3000; c.readTimeout = 3000
        c.setRequestProperty("User-Agent", RetroNatif.agent)
        c.inputStream.use { android.graphics.BitmapFactory.decodeStream(it) }
    } catch (e: Exception) { null }
    fun evenement(type: Int, id: Int, titre: String, desc: String, points: Int, url: String, msg: String) {
        when (type) {
            1 -> {                                             // RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED
                noter("succès débloqué : $titre ($id)")
                ExtrasRA.succes(ctx, jeu, titre, desc, points, url, hardcore)   // (build 554, appli privee : historique)
                // (build 535 : avec le badge du succes, sur le telephone et sur la tele ; sans lui si on ne l'a pas en 3 s)
                ctx?.let { c -> Api.pool {
                    val b = badgeDe(url)
                    PlaybackService.notify(c, 9400 + id % 64, "🏆 RetroAchievements : $titre", "$desc · $points points", b)
                    // (build 509, appli privee : aussi sur la tele, dans le bandeau de la console v397 ; build 521 : publique aussi)
                    if (Bandeau.raSurTele(c)) Bandeau.succes(titre, b)?.let { noter("bandeau de la télé : $it") }
                } }
                majListe()
            }
            2 -> { classement(id, titre, "en cours", desc = desc); noter("classement commencé : $titre"); ctx?.let { PlaybackService.notify(it, 9300 + id % 64, "🏁 Classement : $titre", desc) }; tele("Classement", titre) }
            3 -> { classement(id, titre, "raté"); noter("classement raté : $titre"); ctx?.let { PlaybackService.notify(it, 9300 + id % 64, "🏁 Classement raté : $titre", desc) }; tele("Classement raté", titre) }
            4 -> { classement(id, titre, "score envoyé", score = url); noter("classement envoyé : $titre ($url)"); ctx?.let { PlaybackService.notify(it, 9300 + id % 64, "🏁 Score envoyé : $titre", url) } }
            13 -> { classement(id, titre, "rang $points sur $msg", score = desc, meilleur = url); noter("classement : rang $points sur $msg ($titre)"); ctx?.let { PlaybackService.notify(it, 9300 + id % 64, "🏁 $titre", "Score $desc · rang $points sur $msg · meilleur $url") } ; tele("Classement : rang $points sur $msg", titre) }
            10, 12 -> ui.post { trackers = trackers + (id to titre) }
            11 -> ui.post { trackers = trackers - id }
            5 -> { ui.post { defis = defis + (id to titre) }; tele("Défi", titre) }
            6 -> ui.post { defis = defis - id }
            7, 9 -> {
                ui.post { progres = "$titre : $msg" }
                val n = System.nanoTime()                        // (build 527 : sur la tele au plus toutes les 4 s)
                if (type == 7 || n - teleProgresT > 4_000_000_000L) { teleProgresT = n; tele("Progression", "$titre : $msg") }
            }
            8 -> ui.post { progres = "" }
            14 -> ModeHardcore.relancer()
            15 -> { noter("jeu terminé à 100 %"); ctx?.let { PlaybackService.notify(it, 9399, "🏆 RetroAchievements", "Tous les succès de ce jeu sont débloqués !") } }
            16 -> noter("erreur du site : $msg")
            17 -> { noter("site injoignable : déblocages en attente"); ui.post { etat = "Site injoignable : les succès seront envoyés plus tard" } }
            18 -> { noter("site joignable : déblocages envoyés"); ui.post { etat = "Succès vérifiés en direct" } }
        }
    }
    // ---------- build 553 (appli privee) : les classements de la partie, avec leur dernier resultat ----------
    class ClassementRA(val id: Int, val titre: String, val desc: String, val etat: String, val score: String, val meilleur: String, val t: Long)
    var classements by mutableStateOf<List<ClassementRA>>(emptyList())
    private fun classement(id: Int, titre: String, etat: String, desc: String = "", score: String = "", meilleur: String = "") {
        ui.post {
            val av = classements.firstOrNull { it.id == id }
            val n = ClassementRA(id, titre.ifEmpty { av?.titre ?: "" }, desc.ifEmpty { av?.desc ?: "" }, etat, score.ifEmpty { if (etat == "en cours") "" else av?.score ?: "" },
                meilleur.ifEmpty { av?.meilleur ?: "" }, System.currentTimeMillis())
            classements = (listOf(n) + classements.filter { it.id != id }).take(20)
        }
    }
    private fun majListe() {
        val t = RetroNatif.nListe()
        val l = t.split('\n').mapNotNull { ln ->
            val f = ln.split('\t'); if (f.size < 7) null
            else SuccesRA(f[0].toIntOrNull() ?: 0, f[1].toIntOrNull() ?: 0, f[2] == "1", f[3], f[4], f[5], f[6])
        }
        val u = RetroNatif.nUtilisateur()
        ui.post { liste = l; if (u != null) points = u[3]; rev++ }
    }

    // ---------- build 494 : notes d'adresses (jauges) ----------
    fun chargerNotes() {
        if (!pret || jeu.isEmpty()) return
        notesEtat = "Chargement des notes…"
        synchronized(verrou) { RetroNatif.nNotes() }
    }
    @Volatile private var pendantes: Set<Int> = emptySet()   // (epingles gardees : remises a l'arrivee des notes)
    fun notesRecues(ok: Boolean, t: String) {
        val l = if (!ok) emptyList() else t.split('\n').mapNotNull { ln ->
            val i = ln.indexOf('\t'); if (i <= 0) null else ln.substring(0, i).toLongOrNull()?.let { NoteRA(it.toInt(), ln.substring(i + 1)) }
        }.filter { it.titre.isNotEmpty() }
        noter("notes d'adresses : " + l.size)
        ui.post {
            notes = l; notesEtat = if (!ok) "Notes non reçues" else if (l.isEmpty()) "Pas de notes pour ce jeu" else ""
            val p = pendantes; pendantes = emptySet()
            for (n in l) if (n.adr in p && epinglees.none { it.adr == n.adr }) epingler(n)
        }
    }
    fun epingler(n: NoteRA) {
        val l = epinglees.toMutableList()
        if (l.any { it.adr == n.adr }) l.removeAll { it.adr == n.adr } else if (l.size < 12) l.add(n)
        epinglees = l
        ctx?.let { c -> pr(c).edit().putString("ra_epi_" + jeuId, l.joinToString(",") { it.adr.toString() }).apply() }
        if (pret) synchronized(verrou) { RetroNatif.nEpingles(l.map { it.adr }.toIntArray(), l.map { it.octets }.toIntArray()) }
    }
    @Volatile private var jeuId = ""
    /** valeur lue (copie de la memoire, mise a jour par Pulse) ; null : pas encore recue */
    fun valeur(n: NoteRA): String? {
        val b = synchronized(verrou) { RetroNatif.nOctets(n.adr, n.octets) } ?: return null
        var v = 0L; for (x in b) v = (v shl 8) or (x.toLong() and 255)
        if (n.flottant) return String.format(java.util.Locale.FRANCE, "%.2f", java.lang.Float.intBitsToFloat(v.toInt()))
        if (n.pointeur) return String.format("0x%08X", v)
        return v.toString()
    }

    // ---------- la partie ----------
    private const val RIEN = 0; private const val CHARGE = 1; private const val RODAGE = 2; private const val ACTIF = 3; private const val ATTENTE = 4
    @Volatile private var phase = RIEN
    @Volatile private var fil: Thread? = null
    @Volatile private var hashEnCours = ""
    // (build 537 : 256 au lieu de 8 : avec l'anneau, les images retenues en attendant une plus ancienne (jusqu'a 32) sortent
    //  toutes d'un coup ; avec 8 places, les plus anciennes etaient jetees : partie du 11 novembre, 95,6 % seulement)
    private val images = java.util.concurrent.LinkedBlockingDeque<Triple<Long, ByteArray, IntArray?>>(256)
    /** build 537 : les dernieres listes envoyees a Pulse (une image en route peut encore venir d'une liste precedente) */
    private val listes = ArrayDeque<IntArray>()
    /** build 539 : quand chaque liste a ete remplacee (nanoTime ; la courante : absente) : une image en route ne vient d'une
     *  liste remplacee que pendant ~1 s (l'anneau garde 32 images au plus). Avant : deux listes de meme taille (4096 octets,
     *  le maximum : frequent) bloquaient toutes les images pour toujours (Wind Waker, 11 novembre : compteur fige a 2000). */
    private val remplacees = java.util.IdentityHashMap<IntArray, Long>()
    @Volatile private var plagesEnvoyees = IntArray(0)

    /** une partie commence (ou finit : g null) ; appele par StationActivity (Live) */
    fun partie(c: Context, g: Live.Game?) {
        demarrer(c)
        if (g == null || g.raHash.isEmpty() || !g.mem) { if (g == null) { arreter(); finDePartie() } else if (g.raHash.isEmpty() && utilisateur.isNotEmpty()) etat = "Pas d'empreinte du jeu : GC Station v393 ou plus, et « Chercheur de codes (appli) » activé"; return }
        if (!pret || utilisateur.isEmpty()) return
        if (hashEnCours == g.raHash && fil?.isAlive == true) return
        arreter()
        hashEnCours = g.raHash; phase = CHARGE; jeu = ""; liste = emptyList()
        etat = "Recherche du jeu sur RetroAchievements…"
        noter("partie : " + g.id + " empreinte " + g.raHash)
        // (build 500 : le mode de la partie, choisi avant de charger le jeu)
        val raison = ModeHardcore.raison(g)
        val hc = ModeHardcore.veut(raison)
        hardcore = hc; raisonNormal = raison; trackers = emptyMap(); defis = emptyMap(); progres = ""
        noter("mode : " + (if (hc) ModeHardcore.NOM else "normal") + (if (raison.isNotEmpty()) " ($raison)" else ""))
        synchronized(verrou) { RetroNatif.nHardcore(hc); RetroNatif.nChargerJeu(g.raHash) }
        images.clear()
        MemoireJeu.fluxRA = { img, d, li ->
            // (build 539 : la liste de l'image, en plages comme plagesEnvoyees : adresse - 0x80000000, longueur)
            val pl = li?.let { l -> IntArray(l.size * 2) { k -> if (k % 2 == 0) (l[k / 2].first - MemoireJeu.BASE).toInt() else l[k / 2].second } }
            val x = Triple(img, d, pl)
            if (!images.offerLast(x)) { images.pollFirst(); images.offerLast(x) } }
        val h = g.raHash
        fil = Thread { boucle(h) }.apply { isDaemon = true; name = "retro"; start() }
        ctx?.let { RetroService.demarrer(it, g.id) }                     // (build 544 : l'appli reste en vie en arriere-plan)
    }
    /** build 544 : appli revenue au premier plan pendant une partie : le service, s'il n'avait pas pu demarrer */
    fun auPremierPlan() { val c = ctx ?: return; if (fil?.isAlive == true) RetroService.demarrer(c, Live.game?.id ?: "") }
    /** build 547 : partie finie : la page des succes repart de zero (le compte reste connecte ; le journal reste) */
    private fun finDePartie() {
        val f: () -> Unit = { etat = ""; jeu = ""; liste = emptyList(); presence = ""; liaison = ""; raisonNormal = ""; jeuImage = ""; classements = emptyList(); rev++; Unit }
        if (android.os.Looper.myLooper() == android.os.Looper.getMainLooper()) f() else ui.post { f() }
    }
    fun arreter() {
        val f = fil; fil = null; hashEnCours = ""
        f?.interrupt()
        if (f != null) ctx?.let { RetroService.arreter(it) }              // (build 544)
        MemoireJeu.fluxRA = null
        if (MemoireJeu.raL.isNotEmpty()) Thread { MemoireJeu.abonnerRA(emptyList()) }.start()
        if (pret && phase != RIEN) synchronized(verrou) { RetroNatif.nFermerJeu() }
        phase = RIEN
        hardcore = false; trackers = emptyMap(); defis = emptyMap(); progres = ""          // (build 500)
    }
    private var dernierTropGros = 0
    private var zonesBrutes = 0; private var octetsBruts = 0
    private val listeRA = ListeRA()                                 // (build 553)
    private fun abonner() {
        // plages lues par rcheevos ; trop de morceaux : plages voisines reunies plus largement (24 morceaux, 4 Ko au plus)
        // (build 546 : console v416+ (compression) : jusqu'a 12 Ko et 32 morceaux (avant : 4 Ko, 24) : Super Mario Sunshine et
        //  Paper Mario lisent plus de 4 Ko : la liste coupee ne couvrait pas tout, le rodage ne finissait jamais et les succes
        //  n'etaient pas verifies. Morceaux de 4 608 octets au plus (limite de Pulse) : une plage plus longue est coupee.)
        // (build 549 : console v416+ : jusqu'a 96 morceaux (la limite de Pulse) ; les plages sont reunies en commencant par les
        //  plus proches (le moins d'octets en plus), au lieu d'un ecart fixe de plus en plus grand : Super Mario Sunshine lisait
        //  ~30 zones eloignees, reunies en gros blocs : plus de 12 Ko, liste coupee, rodage jamais fini)
        val gros = MemoireJeu.compressionVue()
        val maxTot = if (gros) 12288 else 4096; val maxMorc = if (gros) MemoireJeu.morceauxConsole else 24   // (build 550 : 256 avec la console v419)
        var p = IntArray(0)
        if (gros) {
            // (build 551 : zones oubliees apres 5 s sans lecture (300 images) au lieu de 30 s : Super Mario Sunshine change sans
            //  arret d'objets, la liste gardait des centaines de zones qui ne servaient plus et depassait 12 Ko)
            val q = synchronized(verrou) { RetroNatif.nPlages(300, 0) }
            zonesBrutes = q.size / 2; octetsBruts = q.indices.step(2).sumOf { q[it + 1] }   // (build 550 : journal : avant regroupement)
            // (build 553 : marge, pages chaudes et regroupement : ListeRA, essaye sur le PC)
            val (pp, voulu) = listeRA.construire(q, maxTot, maxMorc)
            p = pp
            if (voulu > maxTot && voulu != dernierTropGros) { dernierTropGros = voulu; noter("liste trop grosse : $voulu octets demandés par RetroAchievements, $maxTot au plus : coupée (la vérification peut attendre)") }
        } else for (ecart in intArrayOf(64, 256, 1024, 4096)) {
            p = synchronized(verrou) { RetroNatif.nPlages(1800, ecart) }
            if (p.indices.step(2).sumOf { (p[it + 1] + 4607) / 4608 } <= maxMorc) break
        }
        val mc = ListeRA.morceaux(p, maxTot, maxMorc)
        val l = mc.map { (MemoireJeu.BASE + it.first) to it.second }
        val gard = mc.flatMap { listOf(it.first, it.second) }; val tot = mc.sumOf { it.second }
        val neuf = gard.toIntArray()
        if (!neuf.contentEquals(plagesEnvoyees)) noter("abonnement au direct : " + l.size + " morceaux, " + tot + " octets (" + p.size / 2 + " plages lues)")
        plagesEnvoyees = neuf
        if (listes.lastOrNull()?.contentEquals(neuf) != true) {
            listes.lastOrNull()?.let { remplacees[it] = System.nanoTime() }
            listes.addLast(neuf)
            while (listes.size > 6) remplacees.remove(listes.removeFirst())
        }
        MemoireJeu.abonnerRA(l)
    }
    private fun boucle(h: String) {
        // (build 538 : le telephone n'est pas bride pendant une partie : fil en priorite haute, processeur jamais endormi)
        try { android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_URGENT_AUDIO) } catch (e: Exception) { }
        val reveil = try {
            (ctx?.getSystemService(Context.POWER_SERVICE) as? android.os.PowerManager)?.newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK, "gcs:retro")
                ?.apply { setReferenceCounted(false); acquire(6L * 3600 * 1000) }
        } catch (e: Exception) { null }
        try { boucle2(h) } finally { try { reveil?.takeIf { it.isHeld }?.release() } catch (e: Exception) { } }
    }
    /** build 554 (appli privee : ExtrasRA) : le rapport de test de la partie, a copier tel quel */
    private fun rapportPartie(nom: String, id: String, debutMs: Long, vues: Long, manquees: Long, nbPauses: Int, pauseMax: Long, pauseTot: Long, hcDebut: Boolean, hcFin: Boolean, raison: String): String {
        val f = java.util.Locale.FRANCE
        val dur = (System.currentTimeMillis() - debutMs) / 1000
        val ne = MemoireJeu.nonEnvoyees.coerceIn(0L, manquees)
        val (rf, oc, ob) = MemoireJeu.compression()
        val tot = vues + manquees
        return buildString {
            append("Rapport RetroAchievements · ").append(java.text.SimpleDateFormat("d MMM yyyy HH:mm", f).format(java.util.Date(debutMs))).append('\n')
            append("Jeu : ").append(nom.ifEmpty { "?" }).append(if (id.isNotEmpty()) " ($id)" else "").append('\n')
            append(String.format(f, "Durée : %d h %02d min %02d s", dur / 3600, dur % 3600 / 60, dur % 60)).append('\n')
            append(String.format(f, "Images : %d vérifiées sur %d (%.3f %%) ; %d manquantes dont %d pas envoyées par la console", vues, tot, if (tot > 0) 100.0 * vues / tot else 0.0, manquees, ne)).append('\n')
            append(String.format(f, "Pauses de la vérification : %d, la plus longue %d images (%.2f s), en tout %.2f %% des images", nbPauses, pauseMax, pauseMax / 60.0, if (vues > 0) 100.0 * pauseTot / vues else 0.0)).append('\n')
            if (ob > 0) append(String.format(f, "Compression : %d %% de la taille, %d refusées", oc * 100 / ob, rf)).append('\n')
            if (zonesBrutes > 0) append("Liste : $zonesBrutes zones ($octetsBruts octets), ${MemoireJeu.morceauxConsole} morceaux acceptés par la console" + (if (listeRA.pagesChaudes > 0) ", ${listeRA.pagesChaudes} page(s) d'objets" else "")).append('\n')
            append("Mode : ").append(if (!hcDebut) "normal" + (if (raison.isNotEmpty()) " ($raison)" else "") else if (hcFin) "hardcore gardé jusqu'au bout" else "hardcore au début, puis normal" + (if (raison.isNotEmpty()) " ($raison)" else "")).append('\n')
            append("Appli build ").append(BuildConfig.VERSION_CODE).append(" · Android ").append(android.os.Build.VERSION.RELEASE).append(" · ").append(android.os.Build.MODEL)
        }
    }
    private fun boucle2(h: String) {
        var dernierAbo = 0L; var derniereImage = System.nanoTime(); var dernierIdle = 0L; var derniereSonde = 0L
        var vues = 0L; var manquees = 0L; var imgPrec = -1L; var liaisonT = 0L
        // (build 540 : journal detaille : chaque trou, quand (depuis le debut de la partie), ou (phase), pourquoi (console ou Wi-Fi))
        val debut = System.nanoTime(); var neAvant = 0L; var diagVu = ""; var imagesPause = 0L
        // (build 554 : pauses de la verification : nombre, plus longue, total ; le rapport de fin de partie)
        var pauseCours = 0L; var nbPauses = 0; var pauseMax = 0L; var pauseTot = 0L
        val hcDebut = hardcore; val idJeu = Live.game?.id ?: ""; var nomJeu = jeu; val debutMs = System.currentTimeMillis()
        var hcFin = hardcore; var raisonFin = raisonNormal
        val finPause = { if (pauseCours > 0) { nbPauses++; pauseTot += pauseCours; if (pauseCours > pauseMax) pauseMax = pauseCours
            if (pauseCours > 10) noter(String.format(java.util.Locale.FRANCE, "pause de la vérification : %d images (%.2f s)", pauseCours, pauseCours / 60.0)); pauseCours = 0L } }
        MemoireJeu.diagConsole = null; var bilanT = debut; var vuesBilan = 0L; var manqueesBilan = 0L
        val nomPhase = { ph: Int -> when (ph) { RODAGE -> "rodage"; ACTIF -> "vérification"; ATTENTE -> "attente d'adresse"; else -> "préparation" } }
        noter("journal détaillé : début de la partie (hardcore " + (if (hardcore) "oui" else "non") + ")")
        Thread { MemoireJeu.demande('I', garde = false) }.start()      // (build 550 : combien de morceaux la console accepte)
        MemoireJeu.nonEnvoyees = 0L
        listes.clear(); remplacees.clear(); listes.addLast(plagesEnvoyees); listeRA.raz()
        ui.post { liaison = "" }
        try {
            while (hashEnCours == h && Live.game?.raHash == h && !Thread.currentThread().isInterrupted) {
                val now = System.nanoTime()
                if (jeu.isNotEmpty()) nomJeu = jeu
                hcFin = hardcore; if (raisonNormal.isNotEmpty()) raisonFin = raisonNormal   // (avant que arreter() ne remette tout a zero)
                // (build 492 : la sonde d'abord, tant qu'aucune image n'est arrivee : en 491, l'abonnement passait avant elle avec
                //  une liste vide, et la sonde n'avait jamais lieu : Pulse n'envoyait rien, rapport du 9 novembre, 14 h 07)
                if (phase == RODAGE && vues == 0L && now - derniereSonde > 500_000_000L) {
                    synchronized(verrou) { RetroNatif.nSonder() }    // (memoire encore vide : les adresses directes sont connues)
                    derniereSonde = now; dernierAbo = 0L
                }
                if (phase >= RODAGE && now - dernierAbo > 1_000_000_000L) {
                    abonner(); dernierAbo = now
                    val pz = synchronized(verrou) { RetroNatif.nPresence() }        // (build 494)
                    if (pz != presence) ui.post { presence = pz }
                }
                val t0 = System.nanoTime()
                if (vues > 0 && now - liaisonT > 2_000_000_000L) { liaisonT = now; val t = texteLiaison(vues, manquees); ui.post { liaison = t } }   // (build 527)
                MemoireJeu.ecouter(20)
                if (images.isEmpty() && System.nanoTime() - t0 < 5_000_000L) Thread.sleep(20)   // (console pas trouvee : pas de boucle a vide)
                while (true) {
                    val (img, d, plImg) = images.pollFirst() ?: break
                    derniereImage = System.nanoTime()
                    if (phase < RODAGE) continue
                    // (build 537 : l'image est lue avec la liste qui a servi a l'envoyer, reconnue a sa taille ; si deux listes
                    //  recentes differentes ont cette taille, on ne sait pas : image laissee, comme avant)
                    val tn = System.nanoTime()
                    val ok = listes.filter { l -> (remplacees[l]?.let { tn - it < 1_500_000_000L } ?: true) && l.indices.step(2).sumOf { l[it + 1] } == d.size }
                        .distinctBy { it.toList() }
                    // (build 539 : console v413 : la liste est connue par le numero d'abonnement : plus d'ambiguite)
                    val pl = if (plImg != null && plImg.indices.step(2).sumOf { plImg[it + 1] } == d.size) plImg else if (ok.size == 1) ok[0] else continue
                    if (imgPrec >= 0 && img > imgPrec + 1) {
                        val trou = img - imgPrec - 1
                        manquees += trou
                        val ne = MemoireJeu.nonEnvoyees - neAvant; neAvant = MemoireJeu.nonEnvoyees
                        noter(String.format(java.util.Locale.FRANCE, "trou : images %d à %d (%d) à %.1f s, %s, %s ; liste de %d octets, %s",
                            imgPrec + 1, img - 1, trou, (System.nanoTime() - debut) / 1e9, nomPhase(phase),
                            if (ne >= trou) "pas envoyées par la console (bus pris par le jeu)" else if (ne > 0) "dont $ne pas envoyées par la console, le reste perdu en route" else "perdues en route (Wi-Fi)",
                            d.size, MemoireJeu.etatAnneau()))
                        if (hardcore && phase == ACTIF) ModeHardcore.imagesManquees(trou)   // (build 531)
                    }
                    MemoireJeu.diagConsole?.let { dg ->                     // (build 542 : diagnostic de la console v414, a chaque changement)
                        val t = dg.joinToString(",")
                        if (t != diagVu) {
                            diagVu = t
                            noter(String.format(java.util.Locale.FRANCE, "console (depuis le lancement) : %d gardées sans être envoyées (bus pris), %d renvoyées ensuite, %d sautées sans être gardées, %d envois sans attendre la file, à %.1f s",
                                dg[0], dg[1], dg[2], dg[3], (System.nanoTime() - debut) / 1e9))
                        }
                    }
                    if (System.nanoTime() - bilanT > 60_000_000_000L) {   // (build 540 : une ligne par minute)
                        val (rf, oc, ob) = MemoireJeu.compression()
                        noter(String.format(java.util.Locale.FRANCE, "minute %d : %d images vues, %d manquantes, %s", ((System.nanoTime() - debut) / 60_000_000_000L).toInt(),
                            vues - vuesBilan, manquees - manqueesBilan, nomPhase(phase)) +
                            (if (zonesBrutes > 0) " ; zones lues : $zonesBrutes ($octetsBruts octets), la console en accepte ${MemoireJeu.morceauxConsole}" + (if (listeRA.pagesChaudes > 0) ", ${listeRA.pagesChaudes} page(s) d'objets entière(s)" else "") else "") +
                            (if (vues > vuesBilan) String.format(java.util.Locale.FRANCE, " ; en pause (adresse attendue) : %.1f %% des images", 100.0 * imagesPause / (vues - vuesBilan)) else "") +
                            (if (ob > 0) String.format(java.util.Locale.FRANCE, " ; compression : %d %% de la taille, %d refusées (somme fausse ou référence absente)", oc * 100 / ob, rf) else ""))
                        bilanT = System.nanoTime(); vuesBilan = vues; manqueesBilan = manquees; imagesPause = 0L
                    }
                    imgPrec = img; vues++
                    synchronized(verrou) {
                        RetroNatif.nAppliquer(pl, d, 0)
                        when (phase) {
                            RODAGE -> {
                                val r = RetroNatif.nSonder()
                                if (r[1] > 0) dernierAbo = 0L
                                else if (r[0] == 0) { RetroNatif.nRecommencer(); phase = ACTIF; noter("rodage fini : " + pl.size / 2 + " morceaux suivis"); ui.post { etat = "Succès vérifiés en direct" } }
                            }
                            ACTIF -> {
                                val r = RetroNatif.nImage()
                                if (r[1] > 0) dernierAbo = 0L
                                if (r[0] > 0) { phase = ATTENTE; pauseCours = 1L; noter("adresse nouvelle pas encore reçue : vérification en pause") }
                            }
                            ATTENTE -> {
                                imagesPause++                                       // (build 551 : temps en pause, au bilan)
                                pauseCours++
                                if (hardcore && pauseCours == ModeHardcore.PAUSE_MAX + 1L) ModeHardcore.pauseTropLongue(pauseCours)   // (build 554)
                                val r = RetroNatif.nSonder()
                                if (r[1] > 0) dernierAbo = 0L
                                if (r[0] == 0) { phase = ACTIF; finPause() }
                            }
                        }
                    }
                }
                if (System.nanoTime() - derniereImage > 500_000_000L && System.nanoTime() - dernierIdle > 500_000_000L) {
                    synchronized(verrou) { RetroNatif.nAttente() }; dernierIdle = System.nanoTime()
                    if (phase >= RODAGE && System.nanoTime() - derniereImage > 5_000_000_000L) ui.post { etat = "Pulse n'envoie rien (réseau, ou partie finie ?)" }
                }
            }
        } catch (e: InterruptedException) { }
        finPause()
        if (vues > 0) try { ExtrasRA.rapport(ctx, rapportPartie(nomJeu, idJeu, debutMs, vues, manquees, nbPauses, pauseMax, pauseTot, hcDebut, hcFin, raisonFin)) } catch (e: Exception) { }   // (build 554)
        noter("fin de la partie : $vues images vérifiées, $manquees manquantes (dont ${MemoireJeu.nonEnvoyees} pas envoyées par la console)")
        texteLiaison(vues, manquees).let { t -> ui.post { liaison = if (t.isEmpty()) "" else "Dernière partie · $t" } }   // (build 527 : le bilan reste affiché)
        jeuImage = ""; ui.post { presence = "" }
    }
}

@Composable
fun EcranRetro() {
    val ctx = LocalContext.current
    remember { RetroSucces.demarrer(ctx); 0 }
    var u by remember { mutableStateOf("") }
    var p by remember { mutableStateOf("") }
    val cxR = androidx.compose.ui.platform.LocalContext.current
    var rangee by remember { mutableStateOf(cxR.getSharedPreferences("gcs", 0).getBoolean("ra_rangee", false)) }   // (build 550)
    @Suppress("UNUSED_VARIABLE") val r = RetroSucces.rev
    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        item {
            Carte(Modifier.fillMaxWidth()) {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Titre("RetroAchievements")
                    if (RetroSucces.utilisateur.isEmpty()) {
                        Doux("Connecte ton compte retroachievements.org : pendant une partie lancée par GC Station, l'appli vérifie les succès du jeu avec la mémoire envoyée par Pulse. Mode normal (Softcore) seulement. Le mot de passe n'est pas gardé.", taille = 12)
                        OutlinedTextField(u, { u = it }, Modifier.fillMaxWidth(), singleLine = true, label = { Text("Nom d'utilisateur") })
                        OutlinedTextField(p, { p = it }, Modifier.fillMaxWidth(), singleLine = true, label = { Text("Mot de passe") },
                            visualTransformation = PasswordVisualTransformation(), keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password))
                        Bouton("Se connecter", Modifier.fillMaxWidth(), plein = true, actif = u.isNotBlank() && p.isNotEmpty()) { RetroSucces.connecter(u, p); p = "" }
                    } else {
                        Text(RetroSucces.utilisateur, color = C.Texte, fontSize = 18.sp, fontWeight = FontWeight.Bold)
                        Doux(RetroSucces.points + " points " + (if (RetroSucces.hardcore) ModeHardcore.TEXTE_POINTS else "(mode normal)"), taille = 12)
                        ModeHardcore.Reglage()                              // (build 500 ; 526 : appli privee seulement)
                        Bouton("Se déconnecter", Modifier.fillMaxWidth()) { RetroSucces.deconnecter() }
                    }
                    if (RetroSucces.etat.isNotEmpty()) Doux(RetroSucces.etat, taille = 12, couleur = C.Cyan)
                    if (RetroSucces.liaison.isNotEmpty()) Doux(RetroSucces.liaison, taille = 12)   // (build 527)
                    Doux("Données envoyées à retroachievements.org (et nulle part ailleurs) : ton nom d'utilisateur, le jeton de connexion, le jeu en cours (son empreinte) et les succès débloqués. Le mot de passe sert une fois, à la connexion, et n'est pas gardé.", taille = 11)
                    Doux("Il faut GC Station v393 ou plus et le réglage « Chercheur de codes (appli) » : la console calcule l'empreinte du jeu au lancement.", taille = 11)
                }
            }
        }
        if (RetroSucces.presence.isNotEmpty() || RetroSucces.trackers.isNotEmpty() || RetroSucces.defis.isNotEmpty() || RetroSucces.progres.isNotEmpty()) item {   // (build 494 ; 500 : classements, defis, progression)
            Carte(Modifier.fillMaxWidth(), androidx.compose.foundation.layout.PaddingValues(12.dp)) {
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Titre("En ce moment")
                    if (RetroSucces.presence.isNotEmpty()) Text(RetroSucces.presence, color = C.Texte, fontSize = 14.sp)
                    for ((_, v) in RetroSucces.trackers) Text("🏁 $v", color = C.Cyan, fontSize = 14.sp)
                    for ((_, v) in RetroSucces.defis) Text("⚔ Défi : $v", color = C.Orange, fontSize = 13.sp)
                    if (RetroSucces.progres.isNotEmpty()) Doux("📈 " + RetroSucces.progres, taille = 12)
                }
            }
        }
        if (Prive.ACTIF && RetroSucces.classements.isNotEmpty()) item {      // (build 553 : classements de la partie)
            Carte(Modifier.fillMaxWidth(), androidx.compose.foundation.layout.PaddingValues(12.dp)) {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Titre("Classements de la partie")
                    for (c in RetroSucces.classements) {
                        val enCours = c.etat == "en cours"
                        val val0 = if (enCours) RetroSucces.trackers.values.firstOrNull().orEmpty() else ""
                        Text((if (enCours) "⏱ " else "🏁 ") + c.titre, color = if (enCours) C.Cyan else C.Texte, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                        Doux(listOf(c.etat, if (c.score.isNotEmpty()) "score " + c.score else "", if (val0.isNotEmpty()) "en ce moment $val0" else "",
                            if (c.meilleur.isNotEmpty()) "ton meilleur " + c.meilleur else "").filter { it.isNotEmpty() }.joinToString(" · "), taille = 12)
                        if (c.desc.isNotEmpty() && enCours) Doux(c.desc, taille = 11, couleur = C.Pale)
                    }
                }
            }
        }
        if (RetroSucces.jeu.isNotEmpty()) item { JaugesRA() }
        // (build 550 : la liste des succes se range (appui sur son titre ; retenu) et le journal est juste au-dessus d'elle :
        //  plus besoin de faire defiler des centaines de succes pour l'atteindre)
        if (ExtrasRA.ACTIF) item { ExtrasRA.Rapports() }                  // (build 554, appli privee : rapport de fin de partie)
        item { BlocJournal() }
        if (ExtrasRA.ACTIF) item { ExtrasRA.Historique() }                // (build 554, appli privee : succes debloques, avec la date)
        if (RetroSucces.jeu.isNotEmpty()) item {
            val l = RetroSucces.liste
            val cx = androidx.compose.ui.platform.LocalContext.current
            Row(Modifier.fillMaxWidth().clickable { rangee = !rangee; cx.getSharedPreferences("gcs", 0).edit().putBoolean("ra_rangee", rangee).apply() },
                verticalAlignment = Alignment.CenterVertically) {
                Titre(RetroSucces.jeu, Modifier.weight(1f))
                Doux(l.count { it.obtenu }.toString() + " / " + l.size + if (rangee) "  ▸ voir" else "  ▾ ranger", taille = 12)
            }
        }
        if (!rangee) items(RetroSucces.liste, key = { it.id }) { s ->
            Carte(Modifier.fillMaxWidth(), androidx.compose.foundation.layout.PaddingValues(12.dp)) {
                Column {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text((if (s.obtenu) "🏆 " else "🔒 ") + s.titre, Modifier.weight(1f), color = if (s.obtenu) C.Vert else C.Texte, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                        Doux(s.points.toString() + " pts", taille = 11)
                    }
                    Doux(s.desc, taille = 12)
                    if (s.progres.isNotEmpty() && !s.obtenu) Doux(s.progres, taille = 11, couleur = C.Cyan)
                }
            }
        }
    }
}

/** build 550 : le journal (bouton, copier, texte), au-dessus de la liste des succes */
@Composable
private fun BlocJournal() {
    var journal by remember { mutableStateOf(false) }
    Column {
        run {
            Bouton(if (journal) "Cacher le journal" else "Journal", Modifier.fillMaxWidth()) { journal = !journal }
            if (journal) {
                val cx = androidx.compose.ui.platform.LocalContext.current
                // (build 533 : copier le journal, pour l'envoyer tel quel)
                Bouton("Copier le journal", Modifier.fillMaxWidth().padding(top = 6.dp)) {
                    val cm = cx.getSystemService(Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                    cm.setPrimaryClip(android.content.ClipData.newPlainText("Journal RetroAchievements", RetroSucces.journalTexte()))
                    android.widget.Toast.makeText(cx, "Journal copié", android.widget.Toast.LENGTH_SHORT).show()
                }
                Doux(RetroSucces.journalTexte().ifEmpty { "(vide)" }, Modifier.padding(top = 6.dp), taille = 11)
            }
        }
    }
}

/** build 494 : jauges en direct d'apres les notes d'adresses des developpeurs RetroAchievements */
@Composable
private fun JaugesRA() {
    var ouvert by remember { mutableStateOf(false) }
    var filtre by remember { mutableStateOf("") }
    var tic by remember { mutableIntStateOf(0) }
    androidx.compose.runtime.LaunchedEffect(Unit) { while (true) { kotlinx.coroutines.delay(300); tic++ } }
    Carte(Modifier.fillMaxWidth(), androidx.compose.foundation.layout.PaddingValues(12.dp)) {
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Titre("Jauges en direct")
            @Suppress("UNUSED_VARIABLE") val t = tic
            for (n in RetroSucces.epinglees) Row(verticalAlignment = Alignment.CenterVertically) {
                Text(n.titre, Modifier.weight(1f), color = C.Texte, fontSize = 13.sp, maxLines = 2)
                Text(RetroSucces.valeur(n) ?: "…", color = C.Cyan, fontSize = 15.sp, fontWeight = FontWeight.Bold)
            }
            if (RetroSucces.epinglees.isEmpty()) Doux("Choisis des valeurs à suivre dans les notes des développeurs RetroAchievements (vie, argent, niveau…).", taille = 12)
            Bouton(if (ouvert) "Fermer les notes" else "Choisir dans les notes", Modifier.fillMaxWidth()) {
                ouvert = !ouvert
                if (ouvert && RetroSucces.notes.isEmpty()) RetroSucces.chargerNotes()
            }
            if (ouvert) {
                if (RetroSucces.notesEtat.isNotEmpty()) Doux(RetroSucces.notesEtat, taille = 12, couleur = C.Cyan)
                OutlinedTextField(filtre, { filtre = it }, Modifier.fillMaxWidth(), singleLine = true, label = { Text("Chercher (ex. health, rupee)") })
                val l = RetroSucces.notes.filter { filtre.isBlank() || it.texte.contains(filtre, ignoreCase = true) }.take(80)
                for (n in l) {
                    val epi = RetroSucces.epinglees.any { it.adr == n.adr }
                    Row(Modifier.fillMaxWidth().appui { RetroSucces.epingler(n) }.padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text((if (epi) "📌 " else "") + n.titre, Modifier.weight(1f), color = if (epi) C.Vert else C.Texte, fontSize = 12.sp, maxLines = 2)
                        Doux(String.format("%08X", 0x80000000L + n.adr), taille = 10)
                    }
                }
                Doux("Notes écrites par les développeurs de RetroAchievements. Les adresses avec pointeur montrent l'adresse pointée, pas la valeur finale.", taille = 10)
            }
        }
    }
}
