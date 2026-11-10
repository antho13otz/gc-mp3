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
    // ---------- build 500 (appli privee) : mode hardcore, prepare pour la validation par RetroAchievements ----------
    // Regles (exigences de RetroAchievements, « Hardcore Compliance Requirements ») : le mode se choisit avant le lancement
    // d'un jeu (jamais active en cours de partie : il faut relancer le jeu) ; il peut etre coupe a tout moment ; pendant
    // une partie hardcore, l'appli refuse tout ce qui aide ou change le jeu : ecriture et blocage de valeurs, pause et photos
    // de la memoire (chercheur de codes), saut et pilotage du curseur du Tingle Tuner ; avec des cheats appliques par la
    // console, ou une partie commencee avant que l'appli la voie, la partie reste en mode normal. Tant que RetroAchievements
    // n'a pas valide GC Remote, le site compte les succes en mode normal de toute facon.
    var hardcoreVoulu by mutableStateOf(false)               // (reglage : « Mode hardcore »)
    var hardcore by mutableStateOf(false)                    // (la partie en cours est en hardcore)
    var raisonNormal by mutableStateOf("")                   // (pourquoi cette partie reste en mode normal)
    var trackers by mutableStateOf<Map<Int, String>>(emptyMap())     // (compteurs des classements en cours)
    var defis by mutableStateOf<Map<Int, String>>(emptyMap())        // (succes « defi » en cours)
    var progres by mutableStateOf("")                                // (progression d'un succes mesure)
    fun choisirHardcore(v: Boolean) {
        if (!BuildConfig.PRIVE) return
        hardcoreVoulu = v
        ctx?.let { pr(it).edit().putBoolean("ra_hardcore", v).apply() }
        if (!v && hardcore) { synchronized(verrou) { RetroNatif.nHardcore(false) }; hardcore = false; trackers = emptyMap()
            noter("hardcore coupé en cours de partie"); etat = "Mode normal pour la suite de la partie" }
        else if (v && phase != RIEN && !hardcore) etat = "Hardcore : s'active au prochain lancement d'un jeu (relance le jeu)"
    }
    /** pendant une partie hardcore : une aide est refusee (avec un message) ; rend true si c'est refuse */
    fun refuseEnHardcore(c: Context?, quoi: String): Boolean {
        if (!hardcore) return false
        noter("refusé en hardcore : $quoi")
        c?.let { cc -> ui.post { Lang.toast(cc, "$quoi : coupé pendant une partie hardcore (RetroAchievements)", android.widget.Toast.LENGTH_SHORT).show() } }
        return true
    }
    var notes by mutableStateOf<List<NoteRA>>(emptyList())
    var notesEtat by mutableStateOf("")
    var epinglees by mutableStateOf<List<NoteRA>>(emptyList())
    private val lignesJournal = ArrayDeque<String>()
    fun journalTexte(): String = synchronized(lignesJournal) { lignesJournal.joinToString("\n") }
    fun noter(m: String) {
        val l = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.FRANCE).format(java.util.Date()) + " " + m
        synchronized(lignesJournal) { lignesJournal.addLast(l); while (lignesJournal.size > 60) lignesJournal.removeFirst() }
        android.util.Log.i("GCS-RA", m)
    }
    private fun pr(c: Context) = c.getSharedPreferences("gcs", 0)
    private val ui by lazy { android.os.Handler(android.os.Looper.getMainLooper()) }

    /** au demarrage de l'appli : reconnexion avec le jeton garde */
    fun demarrer(c: Context) {
        ctx = c.applicationContext
        if (pret) return
        if (!RetroNatif.ok) { etat = "Module RetroAchievements absent de cette version de l'appli"; return }
        synchronized(verrou) {
            if (!RetroNatif.nInit()) { etat = "RetroAchievements : mémoire insuffisante"; return }
            RetroNatif.agent = "GCRemote/1." + BuildConfig.VERSION_CODE + ".0 (Android " + android.os.Build.VERSION.RELEASE + ") " + RetroNatif.nClauseAgent()
            pret = true
            hardcoreVoulu = BuildConfig.PRIVE && pr(c).getBoolean("ra_hardcore", false)   // (build 500)
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
                    if (hardcore) "Mode hardcore : aides du téléphone coupées" else "Mode normal" + (if (raisonNormal.isNotEmpty()) " ($raisonNormal)" else "")) }
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
    fun evenement(type: Int, id: Int, titre: String, desc: String, points: Int, url: String, msg: String) {
        when (type) {
            1 -> {                                             // RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED
                noter("succès débloqué : $titre ($id)")
                ctx?.let { PlaybackService.notify(it, 9400 + id % 64, "🏆 RetroAchievements : $titre", "$desc · $points points") }
                // (build 509, appli privee : aussi sur la tele, dans le bandeau de la console v397)
                ctx?.let { c -> if (Bandeau.raSurTele(c))   /* (build 521 : publique aussi) */ Api.pool { Bandeau.envoyer(2, titre)?.let { noter("bandeau de la télé : $it") } } }
                majListe()
            }
            2 -> { noter("classement commencé : $titre"); ctx?.let { PlaybackService.notify(it, 9300 + id % 64, "🏁 Classement : $titre", desc) } }
            3 -> { noter("classement raté : $titre"); ctx?.let { PlaybackService.notify(it, 9300 + id % 64, "🏁 Classement raté : $titre", desc) } }
            4 -> { noter("classement envoyé : $titre ($url)"); ctx?.let { PlaybackService.notify(it, 9300 + id % 64, "🏁 Score envoyé : $titre", url) } }
            13 -> { noter("classement : rang $points sur $msg ($titre)"); ctx?.let { PlaybackService.notify(it, 9300 + id % 64, "🏁 $titre", "Score $desc · rang $points sur $msg · meilleur $url") } }
            10, 12 -> ui.post { trackers = trackers + (id to titre) }
            11 -> ui.post { trackers = trackers - id }
            5 -> ui.post { defis = defis + (id to titre) }
            6 -> ui.post { defis = defis - id }
            7, 9 -> ui.post { progres = "$titre : $msg" }
            8 -> ui.post { progres = "" }
            14 -> noter("le site demande de relancer le jeu (hardcore)")
            15 -> { noter("jeu terminé à 100 %"); ctx?.let { PlaybackService.notify(it, 9399, "🏆 RetroAchievements", "Tous les succès de ce jeu sont débloqués !") } }
            16 -> noter("erreur du site : $msg")
            17 -> { noter("site injoignable : déblocages en attente"); ui.post { etat = "Site injoignable : les succès seront envoyés plus tard" } }
            18 -> { noter("site joignable : déblocages envoyés"); ui.post { etat = "Succès vérifiés en direct" } }
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
    private val images = java.util.concurrent.LinkedBlockingDeque<Pair<Long, ByteArray>>(8)
    @Volatile private var plagesEnvoyees = IntArray(0)

    /** une partie commence (ou finit : g null) ; appele par StationActivity (Live) */
    fun partie(c: Context, g: Live.Game?) {
        demarrer(c)
        if (g == null || g.raHash.isEmpty() || !g.mem) { if (g == null) arreter(); else if (g.raHash.isEmpty() && utilisateur.isNotEmpty()) etat = "Pas d'empreinte du jeu : GC Station v393 ou plus, et « Chercheur de codes (appli) » activé"; return }
        if (!pret || utilisateur.isEmpty()) return
        if (hashEnCours == g.raHash && fil?.isAlive == true) return
        arreter()
        hashEnCours = g.raHash; phase = CHARGE; jeu = ""; liste = emptyList()
        etat = "Recherche du jeu sur RetroAchievements…"
        noter("partie : " + g.id + " empreinte " + g.raHash)
        // (build 500 : le mode de la partie, choisi avant de charger le jeu)
        val raison = when {
            !(BuildConfig.PRIVE && hardcoreVoulu) -> ""
            g.cheats < 0 -> "console trop ancienne pour le hardcore (GC Station v395 ou plus)"
            g.cheats == 1 -> "des cheats sont appliqués à ce jeu"
            !g.enDirect || g.playedSec() > 90 -> "partie commencée avant que l'appli la voie"
            else -> ""
        }
        val hc = BuildConfig.PRIVE && hardcoreVoulu && raison.isEmpty()
        hardcore = hc; raisonNormal = raison; trackers = emptyMap(); defis = emptyMap(); progres = ""
        noter("mode : " + (if (hc) "hardcore" else "normal") + (if (raison.isNotEmpty()) " ($raison)" else ""))
        synchronized(verrou) { RetroNatif.nHardcore(hc); RetroNatif.nChargerJeu(g.raHash) }
        images.clear()
        MemoireJeu.fluxRA = { img, d -> if (!images.offerLast(img to d)) { images.pollFirst(); images.offerLast(img to d) } }
        val h = g.raHash
        fil = Thread { boucle(h) }.apply { isDaemon = true; name = "retro"; start() }
    }
    fun arreter() {
        val f = fil; fil = null; hashEnCours = ""
        f?.interrupt()
        MemoireJeu.fluxRA = null
        if (MemoireJeu.raL.isNotEmpty()) Thread { MemoireJeu.abonnerRA(emptyList()) }.start()
        if (pret && phase != RIEN) synchronized(verrou) { RetroNatif.nFermerJeu() }
        phase = RIEN
        hardcore = false; trackers = emptyMap(); defis = emptyMap(); progres = ""          // (build 500)
    }
    private fun abonner() {
        // plages lues par rcheevos ; trop de morceaux : plages voisines reunies plus largement (24 morceaux, 4 Ko au plus)
        var p = IntArray(0)
        for (ecart in intArrayOf(64, 256, 1024, 4096)) {
            p = synchronized(verrou) { RetroNatif.nPlages(1800, ecart) }
            if (p.size / 2 <= 24) break
        }
        val l = ArrayList<Pair<Long, Int>>(); var tot = 0; val gard = ArrayList<Int>()
        var i = 0
        while (i + 1 < p.size && l.size < 24) {
            val n = minOf(p[i + 1], 4096 - tot); if (n <= 0) break
            l.add((MemoireJeu.BASE + p[i]) to n); gard.add(p[i]); gard.add(n); tot += n; i += 2
        }
        val neuf = gard.toIntArray()
        if (!neuf.contentEquals(plagesEnvoyees)) noter("abonnement au direct : " + l.size + " morceaux, " + tot + " octets (" + p.size / 2 + " plages lues)")
        plagesEnvoyees = neuf
        MemoireJeu.abonnerRA(l)
    }
    private fun boucle(h: String) {
        var dernierAbo = 0L; var derniereImage = System.nanoTime(); var dernierIdle = 0L; var derniereSonde = 0L
        var vues = 0L; var manquees = 0L; var imgPrec = -1L
        try {
            while (hashEnCours == h && Live.game?.raHash == h && !Thread.currentThread().isInterrupted) {
                val now = System.nanoTime()
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
                MemoireJeu.ecouter(20)
                if (images.isEmpty() && System.nanoTime() - t0 < 5_000_000L) Thread.sleep(20)   // (console pas trouvee : pas de boucle a vide)
                while (true) {
                    val (img, d) = images.pollFirst() ?: break
                    derniereImage = System.nanoTime()
                    if (phase < RODAGE) continue
                    val pl = plagesEnvoyees
                    if (d.size != pl.indices.step(2).sumOf { pl[it + 1] }) continue   // (image d'une autre liste)
                    if (imgPrec >= 0 && img > imgPrec + 1) manquees += img - imgPrec - 1
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
                                if (r[0] > 0) { phase = ATTENTE; noter("adresse nouvelle pas encore reçue : vérification en pause") }
                            }
                            ATTENTE -> {
                                val r = RetroNatif.nSonder()
                                if (r[1] > 0) dernierAbo = 0L
                                if (r[0] == 0) phase = ACTIF
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
        noter("fin de la partie : $vues images vérifiées, $manquees perdues en route")
        jeuImage = ""; ui.post { presence = "" }
    }
}

@Composable
fun EcranRetro() {
    val ctx = LocalContext.current
    remember { RetroSucces.demarrer(ctx); 0 }
    var u by remember { mutableStateOf("") }
    var p by remember { mutableStateOf("") }
    var journal by remember { mutableStateOf(false) }
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
                        Doux(RetroSucces.points + " points " + (if (RetroSucces.hardcore) "(partie en hardcore)" else "(mode normal)"), taille = 12)
                        if (BuildConfig.PRIVE) {                            // (build 500 : mode hardcore, appli privee)
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text("Mode hardcore", Modifier.weight(1f), color = C.Texte, fontSize = 14.sp)
                                androidx.compose.material3.Switch(RetroSucces.hardcoreVoulu, { RetroSucces.choisirHardcore(it) })
                            }
                            Doux("S'active au lancement d'un jeu. Pendant une partie hardcore, le téléphone coupe ses aides : chercheur de codes (écrire, bloquer, photos de la mémoire), saut et pilotage du curseur de Tingle. Pas de hardcore avec des cheats ni pour une partie commencée avant l'appli. En attendant l'accord de RetroAchievements, le site compte encore les succès en mode normal.", taille = 11)
                            if (RetroSucces.raisonNormal.isNotEmpty()) Doux("Cette partie : mode normal (" + RetroSucces.raisonNormal + ")", taille = 11, couleur = C.Orange)
                        }
                        Bouton("Se déconnecter", Modifier.fillMaxWidth()) { RetroSucces.deconnecter() }
                    }
                    if (RetroSucces.etat.isNotEmpty()) Doux(RetroSucces.etat, taille = 12, couleur = C.Cyan)
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
        if (RetroSucces.jeu.isNotEmpty()) item { JaugesRA() }
        if (RetroSucces.jeu.isNotEmpty()) item {
            val l = RetroSucces.liste
            Row(verticalAlignment = Alignment.CenterVertically) {
                Titre(RetroSucces.jeu, Modifier.weight(1f))
                Doux(l.count { it.obtenu }.toString() + " / " + l.size, taille = 12)
            }
        }
        items(RetroSucces.liste, key = { it.id }) { s ->
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
        item {
            Bouton(if (journal) "Cacher le journal" else "Journal", Modifier.fillMaxWidth()) { journal = !journal }
            if (journal) Doux(RetroSucces.journalTexte().ifEmpty { "(vide)" }, Modifier.padding(top = 6.dp), taille = 11)
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
