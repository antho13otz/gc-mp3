package fr.gcstation.telecommande

// Appli (build 375) : l'emulateur GBA de l'appli (GBA du telephone). Le programme que le jeu a envoye a la « GBA »
// (TingleTuner.kt : recu par Pulse, dechiffre ici) tourne dans mGBA (licence MPL 2.0, src/main/cpp : pont gba_jni.c).
// Relais avec Pulse (natres.h, paquets "GCGB") :
//  - ce que le jeu ecrit a la GBA (commande 15) arrive ici dans l'ordre : donne a la GBA emulee mot par mot, des qu'elle a
//    lu le precedent ; tout lu -> « JOY_RECV lu » (option 2) pour que Pulse efface le drapeau 0x02 vu par le jeu ;
//  - ce que la GBA ecrit dans JOY_TRANS (drapeau 0x08) part a Pulse, numerote (option 1, numero en bits 16-23) ; quand
//    Pulse dit que le jeu l'a lu (octet 5 de son etat, console v333), la GBA emulee l'apprend (commande 14) ;
//  - les drapeaux 0x30 de JOYSTAT (ecrits par le programme de la GBA) suivent a chaque paquet (option 8).

import android.graphics.Bitmap
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.displayCutoutPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.util.concurrent.ConcurrentLinkedQueue

/** le pont natif (gba_jni.c) ; un seul fil s'en sert (celui de l'emulateur) */
object GbaNatif {
    val pret: Boolean = try { System.loadLibrary("gbapont"); true } catch (e: Throwable) { false }
    @JvmStatic external fun demarrer(prog: ByteArray): Boolean
    @JvmStatic external fun arreter()
    @JvmStatic external fun image(touches: Int, pixels: IntArray, son: ShortArray, mots: IntArray, nmots: Int, res: IntArray): Int
    @JvmStatic external fun joy(cmd: Int, data: ByteArray): Int
    @JvmStatic external fun regs(out: IntArray)
    @JvmStatic external fun mem32(adr: Int): Int
    @JvmStatic external fun ecr8(adr: Int, v: Int)
    @JvmStatic external fun lectureAuto(oui: Boolean)          // (build 405 : mots de la GBA lus pendant l'image meme)
    @JvmStatic external fun sorties(out: IntArray): Int
    // (build 556 ; build 559 : ces deux fonctions natives n'existent que dans l'appli privee : appelees seulement par src/prive/Cartouche.kt)
    @JvmStatic external fun cartouche(rom: ByteArray?, sav: ByteArray?): Boolean   // (cartouche du prochain demarrage)
    @JvmStatic external fun sauvegarde(): ByteArray?                               // (build 556 : sa sauvegarde, maintenant)
}

object GbaEmu {
    const val W = 240
    const val H = 160
    // build 382 : simples variables (plus d'etat Compose) : cet objet peut naitre dans le fil reseau pendant que l'ecran se
    // compose (plantage « Reading a state that was created after the snapshot was taken ») ; l'ecran les relit a chaque image
    @Volatile var marche = false
        private set
    @Volatile var images = 0                              // (une de plus a chaque image de la GBA)
        private set
    @Volatile var message = ""
        private set
    val ecran: Bitmap = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888)

    @Volatile var touches = 0
    // build 410 : recherche du curseur du Tingle Tuner dans la memoire de la GBA : l'appli appuie elle-meme 2 fois a droite
    // puis 2 fois en bas, photographie la memoire (IWRAM 32 Ko + EWRAM 256 Ko) entre chaque, et garde ce qui bouge comme un
    // curseur (meme pas a chaque appui, seulement dans un sens). Resultat dans le rapport
    @Volatile var chercheDemande = false
    @Volatile var chercheTexte = ""
    @Volatile var chercheEnCours = false
    fun appui(bit: Int, bas: Boolean) { touches = if (bas) touches or (1 shl bit) else touches and (1 shl bit).inv() }

    // relais (lu par le fil reseau de GbaTel)
    val entrees = ConcurrentLinkedQueue<Long>()           // (mots ecrits par le jeu, a donner a la GBA)
    @Volatile var transVal = 0L                           // (JOY_TRANS de la GBA, en attente d'etre lu par le jeu)
    @Volatile var transId = 0                             // (son numero, 1-255 ; 0 : rien en attente)
    @Volatile var luPret = false                          // (la GBA a lu tout ce que le jeu a ecrit : option 2 a envoyer)
    @Volatile var drapeaux = 0                            // (JOYSTAT & 0x30 de la GBA)
    @Volatile var tidLuPulse = 0                          // (dernier numero lu par le jeu, dit par Pulse)
    @Volatile var nRecus = 0L; @Volatile var nEnvoyes = 0L
    // build 381 : ce que le debut du programme ecrit dans JOY_TRANS juste apres son lancement (la preuve demandee par le
    // demarrage du jeu, puis le code du jeu) : Pulse l'a deja donne au jeu a sa place (console v346) ; ces mots-la sont
    // lus tout de suite ici, une fois chacun, sans passer par Pulse
    @Volatile var avaler: Set<Long> = emptySet()
    // build 383 : code du jeu (mot 0xAC) : le debut du programme l'ecrit, attend que le jeu le lise, puis attend que le jeu le
    // lui renvoie ; les mots du jeu sont retenus jusqu'a ce qu'il l'ait ecrit (sinon il attend pour toujours)
    @Volatile var codeJeu = -1L
    // build 387 (console v347) : remises a zero du jeu pendant le relais, dites par Pulse : donnees a la GBA emulee (son
    // programme y repond par son identifiant) ; resetsVus : celles qu'elle a deja traitees (dit a Pulse, qui ignore ce qui
    // est plus ancien)
    @Volatile var resetsPulse = 0
    @Volatile var resetsVus = 0
    @Volatile var resetIdx = 0L                            // (build 394, console v351 : mots du jeu ecrits avant la derniere remise a zero)
    @Volatile var regsVus = ""
    // build 392 : ce qui arrive a la GBA (diagnostic) : ses remises en place (« La connexion a echoue ») avec le code
    // d'erreur et l'etat de son gestionnaire du cable (Rayman 3 : 0x03000A98, 0x03000AA0), ce qu'elle a recu juste avant
    val evts = java.util.concurrent.ConcurrentLinkedDeque<String>()
    private val derniers = ArrayDeque<String>()             // (fil de l'emulateur : les 12 derniers echanges avec la GBA)
    // build 404 : 60 derniers echanges (avant : 12) ; une meme ligne repetee : comptee (« ×n ») au lieu de remplir la liste
    private var traceDer = ""; private var traceN = 0
    private fun trace(t: String) {
        if (t == traceDer && derniers.isNotEmpty()) { traceN++; derniers.removeLast(); derniers.addLast("$t (×$traceN)") }
        else { traceDer = t; traceN = 1; derniers.addLast(t) }
        while (derniers.size > 60) derniers.removeFirst(); traceTexte = derniers.joinToString("\n")
    }
    @Volatile var traceTexte = ""                          // (build 401 : les derniers echanges, pour le rapport)
    // build 415 : messages du Tingle Tuner relus au passage (FEFEFEFE, numero, etat, longueur en octets, donnees, somme des
    // donnees) : la GBA envoie au jeu le message 4 (deux mots ; le 2e change quand le curseur bouge) ; le jeu envoie le
    // message 9 (trois mots, sans doute la position de Link sur la carte). Notes avec la position de Link dans le jeu (vue 3D
    // en direct), pour faire correspondre la carte du Tingle Tuner et le monde
    private class LecteurMsg(val gba: Boolean) {
        var etape = 0; var idx = 0; var st = 0; var reste = 0; val d = ArrayList<Long>()
        fun mot(w: Long) {
            if (w == 0xFEFEFEFEL) { etape = 1; return }
            when (etape) {
                1 -> { idx = w.toInt(); etape = if (w in 0L..15L) 2 else 0 }
                2 -> { st = w.toInt(); etape = 3 }
                3 -> { d.clear(); reste = if (w in 1L..64L) ((w + 3) / 4).toInt() else 0; etape = if (reste > 0 && (st == 3 || st == 7)) 4 else 0 }
                4 -> { d.add(w); if (--reste == 0) etape = 5 }
                5 -> { if ((d.sum() and 0xFFFFFFFFL) == w) msgRecu(gba, idx, d.toList()); etape = 0 }
            }
        }
    }
    private val msgGba = LecteurMsg(true); private val msgJeu = LecteurMsg(false)
    private val derniersMsg = java.util.concurrent.ConcurrentHashMap<String, String>()
    private val histoTT = java.util.concurrent.ConcurrentLinkedDeque<String>()
    var curseurTexte by androidx.compose.runtime.mutableStateOf("")
    @Volatile var curseurMot = -1L
    // build 416 : le message 9 du jeu n'est pas la position de Link (son 2e mot est un compteur) : noté seulement quand ses
    // autres mots changent ; le curseur et les autres messages du jeu ont chacun leur liste (avant : tout dans une liste de
    // 40, remplie par le message 9). Repères : curseur posé sur Link (bouton), avec sa position dans le jeu
    private val histoCurseur = java.util.concurrent.ConcurrentLinkedDeque<String>()
    val reperes = java.util.concurrent.ConcurrentLinkedDeque<String>()
    var nReperes by androidx.compose.runtime.mutableIntStateOf(0)
    private fun heure() = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.FRANCE).format(java.util.Date())
    private fun posTexte(): String = (try { Prive.posDirectWW() } catch (e: Exception) { null })?.let { "%.0f %.0f %.0f".format(it[0], it[1], it[2]) } ?: "pas de direct"
    private fun ajoute(q: java.util.concurrent.ConcurrentLinkedDeque<String>, t: String, max: Int) { q.addLast(t); while (q.size > max) q.removeFirst() }
    private fun msgRecu(gba: Boolean, idx: Int, d: List<Long>) {
        val m = d.joinToString(" ") { "%08X".format(it) }
        val cle = (if (gba) "GBA " else "jeu ") + idx
        val avant = derniersMsg.put(cle, m)
        if (avant == m) return
        if (gba && idx == 4 && d.size >= 2) {
            curseurMot = d[1]
            curseurTexte = "Curseur (message de la GBA) : %d, %d  · %08X %08X".format(d[1] and 0xFFFF, (d[1] ushr 16) and 0xFFFF, d[0], d[1])
            ajoute(histoCurseur, "  ${heure()} $m ; Link dans le jeu : ${posTexte()}", 30)
            return
        }
        if (gba) return
        if (BuildConfig.PRIVE && idx == 3) Saut.jeu3(d)                // (build 490 : position du curseur sur la carte)
        if (idx == 9 && avant != null) { val a = avant.split(" "); if (a.size == d.size && d.size >= 3 && a[0] == "%08X".format(d[0]) && a[2] == "%08X".format(d[2])) return }
        if (idx == 10 || idx == 11) return
        ajoute(histoTT, "  ${heure()} $cle : $m ; Link dans le jeu : ${posTexte()}", 40)
    }
    // (build 419 : L tenu pendant les impulsions ; retire au build 420, la croix seule fait defiler la carte et le curseur)
    // build 417 : piloter le curseur du Tingle Tuner vers un point du monde (toucher dans la vue 3D) : l'appli appuie sur
    // la croix de la GBA par petites impulsions, regarde ou le curseur du jeu est arrive (direct de la vue 3D, en retard de
    // quelques images), apprend sa vitesse et le sens de chaque touche (inverse si le curseur part a l'oppose), et s'arrete
    // a moins de 3 m, ou au bout de 25 s
    @Volatile private var cible: FloatArray? = null
    var pilotageTexte by androidx.compose.runtime.mutableStateOf("")
    private var pilT0 = 0L; private var pilPhase = 0; private var pilKx = 0; private var pilKz = 0; private var pilAtt = 0
    private var pilRef: FloatArray? = null; private var pilTx = 0; private var pilTz = 0; private var pilSx = 0; private var pilSz = 0
    private var sensX = 1; private var sensZ = 1; private var vitX = 60f; private var vitZ = 60f
    private var pilBloque = 0
    fun allerVers(x: Float, z: Float) {
        if (RetroSucces.hardcore) { pilotageTexte = ModeHardcore.TEXTE_PILOTAGE; return }   // (build 500 : RetroAchievements)
        // build 487 (appli privee) : le curseur pose d'un coup quand l'appli sait ou la GBA le range et comment sa carte
        // correspond au monde ; sinon (ou si le saut n'a pas marche) : le trajet a la croix, qui sert aussi a l'apprendre
        if (BuildConfig.PRIVE && Saut.pret()) { Saut.demande = floatArrayOf(x, z); cible = null; pilotageTexte = "Curseur : saut…"; return }
        cible = floatArrayOf(x, z); pilT0 = System.nanoTime(); pilPhase = 0; pilBloque = 0; pilotageTexte = "Curseur : en route" }
    fun pilotageDiag(): String = "curseur du jeu : " + (Prive.curseurTT()?.let { "%.0f %.0f %.0f".format(it[0], it[1], it[2]) } ?: "pas reçu") +
        " ; pilotage : ${pilotageTexte.ifEmpty { "aucun" }} ; sens appris x $sensX z $sensZ ; vitesse %.0f / %.0f par image".format(vitX, vitZ) +
        (if (BuildConfig.PRIVE) " ; saut (build 490) : " + Saut.texte.ifEmpty { "rien appris" } else "")
    // build 421 : bombe de Tingle depuis la vue 3D : B sur la GBA (le Tingle Tuner : « B 💣 ») tenu 8 images ; si le curseur
    // est en route, la bombe part a son arrivee
    @Volatile private var bombeApres = false; private var appuiB = 0
    fun bombe(): String = if (RetroSucces.hardcore) ModeHardcore.TEXTE_BOMBE else if (cible != null) { bombeApres = true; "Bombe : au bout du trajet" } else { appuiB = 8; "Bombe lâchée" }
    private fun appuis(): Int {
        if (bombeApres && cible == null) { bombeApres = false; if (pilotageTexte == "Curseur arrivé") appuiB = 8 }
        if (appuiB > 0) { appuiB--; return 0x02 }
        return 0
    }
    fun arreterPilotage() { cible = null; pilotageTexte = "" }
    private fun pilotage(): Int {
        val c = cible ?: return 0
        val p = Prive.curseurTT() ?: (if (BuildConfig.PRIVE) Saut.monde() else null)   // (build 490 : sinon d'apres la carte)
        if (p == null) { pilotageTexte = "Curseur du jeu pas reçu (positions en direct de la vue 3D ?)"; cible = null; return 0 }
        if (System.nanoTime() - pilT0 > 25_000_000_000L) { pilotageTexte = "Curseur : arrêté (trop long)"; cible = null; return 0 }
        val dx = c[0] - p[0]; val dz = c[1] - p[2]
        when (pilPhase) {
            0 -> {
                val tol = 300f
                if (Math.abs(dx) <= tol && Math.abs(dz) <= tol) { pilotageTexte = "Curseur arrivé"; cible = null; return 0 }
                pilSx = if (Math.abs(dx) > tol) (if (dx > 0) 1 else -1) * sensX else 0
                pilSz = if (Math.abs(dz) > tol) (if (dz > 0) 1 else -1) * sensZ else 0
                pilKx = if (pilSx != 0) (Math.abs(dx) / vitX).toInt().coerceIn(1, 24) else 0
                pilKz = if (pilSz != 0) (Math.abs(dz) / vitZ).toInt().coerceIn(1, 24) else 0
                pilTx = pilKx; pilTz = pilKz; pilRef = p.copyOf(); pilPhase = 1
                pilotageTexte = "Curseur : encore %.0f m".format(Math.hypot(dx.toDouble(), dz.toDouble()) / 100.0)
            }
            1 -> {
                var k = 0
                if (pilKx > 0) { k = k or (if (pilSx > 0) 0x10 else 0x20); pilKx-- }
                if (pilKz > 0) { k = k or (if (pilSz > 0) 0x80 else 0x40); pilKz-- }
                if (pilKx == 0 && pilKz == 0) { pilPhase = 2; pilAtt = 14 }
                // (build 420 : sans L. Le rapport du build 419 montre la croix seule faisant defiler la carte du Tingle Tuner,
                //  et le curseur du jeu la suit : coin de la carte (message 4) + 61 points de carte, x et z. L vient d'un autre
                //  chemin du jeu. Il faut que la GBA montre la carte : au build 417 elle n'y etait pas, rien ne bougeait)
                return k
            }
            2 -> {
                if (--pilAtt > 0) return 0
                val r = pilRef ?: p
                // (ce qu'ont fait les touches : sens et vitesse par image ; une touche sans effet garde sa vitesse)
                // (build 422 : le curseur ne bouge plus malgre les touches, 3 fois de suite : bord de la carte du Tingle
                //  Tuner (elle ne montre que la case de la mer ou est Link) ou la GBA n'affiche pas la carte : arret, au lieu
                //  d'appuyer jusqu'au bout des 25 s)
                val bouge = Math.abs(p[0] - r[0]) > 20f || Math.abs(p[2] - r[2]) > 20f
                if (bouge) pilBloque = 0 else if (++pilBloque >= 3) {
                    pilotageTexte = "Curseur bloqué : bord de la carte du Tingle Tuner (ou la carte n'est pas affichée)"; cible = null; pilPhase = 0; return 0 }
                if (pilTx > 0) { val m = (p[0] - r[0]) * (if (pilSx > 0) 1 else -1) * sensX
                    if (Math.abs(m) > 20f) { if (m < 0) sensX = -sensX; vitX = (Math.abs(m) / pilTx).coerceIn(10f, 2000f) } }
                if (pilTz > 0) { val m = (p[2] - r[2]) * (if (pilSz > 0) 1 else -1) * sensZ
                    if (Math.abs(m) > 20f) { if (m < 0) sensZ = -sensZ; vitZ = (Math.abs(m) / pilTz).coerceIn(10f, 2000f) } }
                pilPhase = 0
            }
        }
        return 0
    }
    fun noterRepere() { val c = curseurMot; if (c >= 0) ajoute(reperes, "  ${heure()} curseur %d, %d (%08X) ; Link dans le jeu : %s".format(c and 0xFFFF, (c ushr 16) and 0xFFFF, c, posTexte()), 20); nReperes++ }
    fun messagesTexte(): String = derniersMsg.entries.sortedBy { it.key }.joinToString("\n") { "  ${it.key} : ${it.value}" }.ifEmpty { "  aucun" } +
        "\nCurseur (message 4 de la GBA), changements :\n" + histoCurseur.joinToString("\n").ifEmpty { "  aucun" } +
        "\nMessages du jeu, changements (sauf compteur du 9, 10 et 11) :\n" + histoTT.joinToString("\n").ifEmpty { "  aucun" } +
        "\nRepères (curseur posé sur Link, build 416) :\n" + reperes.joinToString("\n").ifEmpty { "  aucun" }
    // build 403 (console v365) : Tingle Tuner connecte : mots de la GBA lus ici, envoyes d'un coup a Pulse qui les garde en
    // file pour le jeu (numero 1-255 << 32 | mot) ; retires quand Pulse dit que le jeu les a lus
    val file = java.util.concurrent.ConcurrentLinkedDeque<Long>()
    fun fileAEnvoyer(): List<Long> = file.take(32)
    private fun evt(t: String) {
        val h = java.text.SimpleDateFormat("HH:mm:ss.SSS", java.util.Locale.FRANCE).format(java.util.Date())
        evts.addLast("$h $t\n      avant : " + derniers.toList().takeLast(12).joinToString(" | ")); while (evts.size > 20) evts.removeFirst()
    }                             // (build 383 : registres du cable vus par la GBA, pour le rapport)
    @Volatile var retenus = false
    @Volatile var nAvales = 0
    @Volatile var nRedonnes = 0                           // (build 395 : mots que la GBA n'avait pas lus, redonnes)

    @Volatile private var fil: Thread? = null
    @Volatile private var stop = false

    /** build 410 : memoire de la GBA en mots de 32 bits : IWRAM (0x03000000, 32 Ko) puis EWRAM (0x02000000, 256 Ko) */
    private fun photoGba(): IntArray {
        val t = IntArray(0x2000 + 0x10000)
        for (i in 0 until 0x2000) t[i] = GbaNatif.mem32(0x03000000 + 4 * i)
        for (i in 0 until 0x10000) t[0x2000 + i] = GbaNatif.mem32(0x02000000 + 4 * i)
        return t
    }
    private fun adrGba(i: Int, demi: Int): Int = (if (i < 0x2000) 0x03000000 + 4 * i else 0x02000000 + 4 * (i - 0x2000)) + 2 * demi
    /** ce qui bouge comme un curseur. build 413 : critere assoupli (avant : 0 candidat) : bouge dans le meme sens aux deux
     *  appuis a droite et pas du tout en bas (x), ou l'inverse (y) ; en 16 bits et en 32 bits (virgule fixe) ; les plus
     *  reguliers d'abord. Aussi : ce qui bouge aux deux (une position en pixels qui suit les deux, ex. un sprite) */
    private fun analyseCurseur(s: Array<IntArray?>): String {
        val a = s.map { it!! }
        class C(val adr: Int, val v: List<Long>, val score: Long)
        val cx = ArrayList<C>(); val cy = ArrayList<C>(); val cxy = ArrayList<C>()
        fun essai(adr: Int, v: List<Long>) {
            val d = (1..4).map { v[it] - v[it - 1] }
            val dr = d[0] != 0L && d[1] != 0L && (d[0] > 0) == (d[1] > 0); val db = d[2] != 0L && d[3] != 0L && (d[2] > 0) == (d[3] > 0)
            val ecart = { x: Long, y: Long -> Math.abs(x - y) }
            if (dr && d[2] == 0L && d[3] == 0L) cx.add(C(adr, v, ecart(d[0], d[1])))
            if (db && d[0] == 0L && d[1] == 0L) cy.add(C(adr, v, ecart(d[2], d[3])))
            if (dr && db) cxy.add(C(adr, v, ecart(d[0], d[1]) + ecart(d[2], d[3])))
        }
        for (i in a[0].indices) {
            for (h in 0..1) essai(adrGba(i, h), (0..4).map { ((a[it][i] ushr (16 * h)) and 0xFFFF).toShort().toLong() })
            essai(adrGba(i, 0) or 0x40000000, (0..4).map { a[it][i].toLong() })      // (bit 30 : valeur de 32 bits)
        }
        fun liste(t: String, l: ArrayList<C>): String {
            val sb = StringBuilder("$t : ${l.size} candidats\n")
            for (c in l.sortedBy { it.score }.take(25)) sb.append("  %s%08X : %s\n".format(if (c.adr and 0x40000000 != 0) "32b " else "", c.adr and 0x3FFFFFFF, c.v.joinToString(" ")))
            return sb.toString()
        }
        return liste("droite seulement (x ?)", cx) + liste("bas seulement (y ?)", cy) + liste("les deux", cxy)
    }

    fun demarrer(programme: ByteArray) {
        Saut.raz()                                          // (build 487)
        arreter()
        if (!GbaNatif.pret) { message = "Émulateur GBA absent de cette version de l'appli"; return }
        entrees.clear(); evts.clear(); file.clear(); resetsVus = 0; transId = 0; transVal = 0; luPret = false; drapeaux = 0; tidLuPulse = 0; nRecus = 0; nEnvoyes = 0; nAvales = 0
        stop = false
        fil = Thread({ boucle(programme) }, "gba-emu").apply { isDaemon = true; start() }
    }
    fun arreter() {
        stop = true
        fil?.let { try { it.join(500) } catch (e: Exception) { } }
        fil = null
        marche = false
    }

    private fun boucle(programme: ByteArray) {
        Cartouche.preparer()                                // (build 556, appli privee : une cartouche dans la GBA ; publique : jamais)
        if (!GbaNatif.demarrer(programme)) { message = "Le programme ne démarre pas dans l'émulateur"; return }
        marche = true; message = "La GBA tourne"
        val px = IntArray(W * H)
        val son = ShortArray(4096)
        val res = IntArray(2)
        val r = IntArray(4)
        val mots = IntArray(512)
        val sortiesBuf = IntArray(96)
        val d = ByteArray(8)
        var son0: AudioTrack? = null
        try {
            son0 = AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(AudioFormat.Builder().setSampleRate(32768).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                .setBufferSizeInBytes(32768).build().apply { play() }
        } catch (e: Exception) { son0 = null }
        var attenteLu = false                              // (un JOY_TRANS de la GBA attend que le jeu le lise)
        var numero = 0                                     // (dernier numero donne a un JOY_TRANS, 1-255)
        var donnesSansLu = false                           // (des mots du jeu donnes, pas encore tous lus par la GBA)
        val aAvaler = avaler.toMutableSet()
        var retenir = codeJeu >= 0 && codeJeu in aAvaler          // (build 383 : mots du jeu retenus jusqu'au code ecrit)
        var nImages = 0
        var resetsFaits = 0
        var apresReset = false                             // (build 388 : le 1er JOY_TRANS apres une remise a zero : l'identifiant)
        var idGba = -1L                                    // (l'identifiant de la GBA, vu a la 1re remise a zero)
        var codeLu = false                                 // (build 384 : le code ecrit par la GBA a ete lu ici)
        var joycnt = 0
        var attenteRaz = 0                                 // (build 401 : images depuis qu'une remise a zero attend la GBA)
        var etatTT = -1                                    // (build 401 : etat du lien du Tingle Tuner, pour le rapport)
        var echoTT = false                                 // (build 402 : le Tingle Tuner vient de se connecter : son echo, deja donne par Pulse)
        var dernierMot = -1L                               // (build 402 : dernier mot du jeu donne a la GBA)
        var ch = -1; var chImg = 0                          // (build 410 : recherche du curseur : phase, images dans la phase)
        val snaps = arrayOfNulls<IntArray>(5)
        var etatsTT = ""                                   // (build 404 : etat des 16 messages du Tingle Tuner (0x03005330))
        val attente8 = IntArray(16)                        // (build 414 : images depuis qu'un message de la GBA attend la reponse du jeu)
        var nRelances = 0
        var nApres = 0
        var imgBoot = -1
        var errPrec = 0; var drapPrec = 0
        var motBloque = 0                                   // (build 391 : image ou le mot renvoye par le jeu a ete donne au debut du programme)                                     // (build 390 : images depuis la remise a zero, identifiant pas encore ecrit)
        var prochain = System.nanoTime()
        try {
            while (!stop) {
                // la GBA apprend que le jeu a lu son JOY_TRANS
                run {                                      // (build 403 : mots de la file lus par le jeu : retires)
                    val lp = tidLuPulse
                    if (lp > 0 && file.any { (it ushr 32).toInt() == lp }) while (true) { val h = file.pollFirst() ?: break; if ((h ushr 32).toInt() == lp) break }
                }
                if (attenteLu && tidLuPulse == numero) { trace("lu par le jeu %08X".format(transVal)); GbaNatif.joy(0x14, d); attenteLu = false; transId = 0; nEnvoyes++ }
                val rp = resetsPulse
                // build 391 : pas de remise a zero pendant le debut du programme (il attend JOYCNT = 2 exactement : avec la
                // remise a zero, 3, il attendait pour toujours, ecran blanc) : donnee une fois ce debut passe
                val bootFini = codeJeu < 0 || (imgBoot >= 0 && nImages - imgBoot >= 10) || nImages > 600
                // build 394 : les mots que le jeu a ecrits avant cette remise a zero passent d'abord (sur une vraie GBA, deja
                // recus) : sinon elle les recevait apres et les refusait (erreurs 7 / E4, ecran fige)
                val ri = resetIdx
                var avant = 0
                // build 482 : compte aussi pendant le debut du programme (avant : seulement apres) : Pac-Man vs. remet a zero juste
                // apres le demarrage, puis ecrit tout de suite l'echo et le port ; la remise a zero attendait la fin du debut, mais
                // ces mots, eux, passaient : la GBA les recevait AVANT la remise a zero, puis recommencait la poignee de main que le
                // jeu avait deja finie (« Erreur de connexion »)
                if (rp != resetsFaits) for (w in entrees) { if ((w ushr 32) < ri && avant < mots.size) avant++ else break }
                // build 401 : la remise a zero attend que le programme ecoute le cable (JOYCNT 0x40) : avant, il l'effacait en se
                // mettant en route (Tingle Tuner : JOYCNT = 0x47) et ne repondait jamais ; au plus 60 images d'attente
                val razAttend = rp != resetsFaits && bootFini && avant == 0
                if (razAttend) attenteRaz++ else attenteRaz = 0
                if (razAttend && (joycnt and 0x40 != 0 || attenteRaz > 60)) {          // (build 387 : remise a zero par le jeu : a la GBA ; son JOY_TRANS d'avant est perime)
                    trace("remise à zéro (JOYCNT %02X)".format(joycnt)); file.clear(); GbaNatif.joy(0xFF, d); resetsFaits = rp; attenteLu = false; transId = 0; apresReset = true; nApres = 0
                }
                val razEnAttente = rp != resetsFaits && bootFini && avant == 0
                // build 393 : le programme GBA de Rayman 3 se deconnecte apres 10 images sans echange sur le cable (son compteur :
                // 0x03000AA2, remis a 0 par chaque echange) ; par le Wi-Fi, un paquet retarde suffit : compteur tenu a 0 ici
                if (bootFini && (codeJeu and 0xFFFFFFL) == 0x485247L) GbaNatif.ecr8(0x03000AA2, 0)
                // build 401 : de meme, le Tingle Tuner (code GZL.) recommence le lien apres 10 images sans echange une fois connecte
                // (son compteur : 0x0300564A, remis a 0 par chaque echange)
                if (bootFini && (codeJeu and 0xFFFFFFL) == 0x4C5A47L) GbaNatif.ecr8(0x0300564A, 0)
                // build 409 : connecte, le Tingle Tuner attend la carte du jeu (son message 0) au plus 2 s (120 images, compte a
                // rebours 0x03005653) puis coupe le lien ; par le Wi-Fi, le jeu la donne parfois plus tard : compte tenu plein
                if (bootFini && (codeJeu and 0xFFFFFFL) == 0x4C5A47L && etatTT >= 0 && etatTT and 0x0E == 0x08) GbaNatif.ecr8(0x03005653, 0x78)
                // une image, en donnant les mots du jeu des que la GBA est prete
                var n = 0
                nImages++
                if (nImages % 300 == 0) Cartouche.garder()   // (build 556 : la sauvegarde de la cartouche, toutes les 5 s ; publique : rien)
                if (retenir && nImages > 180) retenir = false      // (3 s sans le code : on donne quand meme)
                // build 384 : apres le code, la GBA attend JOYCNT = 4 exactement (« lu par le jeu ») et l'efface ; le mot du jeu
                // seulement ensuite (sinon JOYCNT = 6 : elle attendait pour toujours)
                if (retenir && codeLu && joycnt and 4 == 0) retenir = false
                // build 390 : comme sur une vraie GBA, apres une remise a zero : son identifiant, lu par le jeu (et la GBA l'a
                // appris : JOYCNT 4 efface), puis seulement le mot du jeu (sinon elle le refusait : « La connexion a echoue »)
                if (apresReset) nApres++
                // build 485 : Pac-Man connecte : pas d'attente sur JOYCNT 4 ici ; chaque image finit par la lecture d'un mot de la GBA
                // (JOYCNT 4 pas encore acquitte par elle a ce moment) : les mots du jeu n'etaient jamais donnes (4968 en attente).
                // L'emulateur attend de toute facon JOYCNT & 7 == 0 avant chaque mot (gba_jni.c)
                val pacLien = (codeJeu and 0xFFFFFFL) == 0x4A5250L && bootFini && (r[0] and 0x30) == 0x30
                val attendReset = (apresReset && nApres <= 30) || (joycnt and 4 != 0 && !pacLien) || razEnAttente   // (build 401 : mots du jeu apres sa remise a zero : pas avant elle)
                    || (rp != resetsFaits && avant == 0)            // (build 482 : aussi quand la remise a zero attend la fin du debut)
                if (avant > 0) for (w in entrees) { if (n >= avant) break; mots[n++] = w.toInt() }
                else if (!retenir && !attendReset) for (w in entrees) { if (n >= mots.size) break; mots[n++] = w.toInt() }
                // build 405 : Tingle Tuner connecte : ses mots lus pendant l'image meme (toute une suite d'un coup, pour la file)
                // build 482 : Pac-Man vs. (code PRJ.) connecte (drapeaux 0x30) : la GBA envoie ses paquets en continu (en-tete puis
                // jusqu'a 64 mots, lus un par un par le jeu des que JOYSTAT dit 0x08) : meme lecture sur place et meme file que le
                // Tingle Tuner (un aller-retour par le Wi-Fi par mot : bien trop lent)
                val pacman = (codeJeu and 0xFFFFFFL) == 0x4A5250L && bootFini && (r[0] and 0x30) == 0x30
                val rafale = ((codeJeu and 0xFFFFFFL) == 0x4C5A47L && etatTT >= 0 && etatTT and 0x0E == 0x08 || pacman) && !attenteLu && file.size < (if (pacman) 32 else 64)   // (build 484 : Pac-Man : 32, la file de Pulse)
                GbaNatif.lectureAuto(rafale)
                var auto = 0                                // (build 410 : recherche du curseur : touches appuyees par l'appli)
                if (chercheDemande) { chercheDemande = false; ch = 0; chImg = 0; chercheEnCours = true; chercheTexte = "recherche en cours…" }
                if (ch >= 0) {
                    if (ch == 0) { snaps[0] = photoGba(); ch = 1; chImg = 0 }
                    else {
                        chImg++
                        if (chImg <= 20) auto = if (ch <= 2) 0x10 else 0x80            // (20 images : droite, puis bas)
                        else if (chImg >= 40) {
                            snaps[ch] = photoGba(); ch++; chImg = 0
                            if (ch == 5) { chercheTexte = analyseCurseur(snaps); ch = -1; chercheEnCours = false }
                        }
                    }
                }
                if (BuildConfig.PRIVE && ch < 0) Saut.image()?.let { r ->   // (build 487 : curseur instantane)
                    pilotageTexte = r.first
                    r.second?.let { c -> cible = c; pilT0 = System.nanoTime(); pilPhase = 0; pilBloque = 0 }   // (repli : la croix)
                }
                if (ch < 0) auto = auto or pilotage() or appuis()   // (build 417 : curseur envoye depuis la vue 3D ; 421 : bombe)
                GbaNatif.image(touches or auto, px, son, mots, n, res)
                GbaNatif.lectureAuto(false)
                run {
                    val ns = GbaNatif.sorties(sortiesBuf)
                    for (i in 0 until ns) {
                        numero = numero % 255 + 1
                        file.add(numero.toLong() shl 32 or (sortiesBuf[i].toLong() and 0xFFFFFFFFL)); nEnvoyes++
                        trace("GBA écrit %08X (rafale, file : %d)".format(sortiesBuf[i], file.size))
                        msgGba.mot(sortiesBuf[i].toLong() and 0xFFFFFFFFL)
                    }
                }
                repeat(res[1]) { i -> entrees.poll(); nRecus++; trace("mot du jeu %08X".format(mots[i])); msgJeu.mot(mots[i].toLong() and 0xFFFFFFFFL) }
                if (res[1] > 0) dernierMot = mots[res[1] - 1].toLong() and 0xFFFFFFFFL
                if (res[1] > 0 && imgBoot < 0) imgBoot = nImages
                if (res[1] > 0) donnesSansLu = true
                GbaNatif.regs(r)
                val js = r[0]
                joycnt = r[3]
                // build 395 : un mot recu (JOYSTAT 02) dont l'interruption est deja acquittee (JOYCNT sans 02) : la GBA ne le
                // lira jamais ; redonne tel quel apres 3 images (son gestionnaire le lit alors)
                if (js and 0x02 != 0 && joycnt and 0x02 == 0) {
                    if (++motBloque >= 3) {
                        val v = r[2]; d[0] = v.toByte(); d[1] = (v shr 8).toByte(); d[2] = (v shr 16).toByte(); d[3] = (v shr 24).toByte()
                        trace("mot %08X redonné (pas lu)".format(v)); GbaNatif.joy(0x15, d); motBloque = 0; nRedonnes++
                    }
                } else motBloque = 0
                run {                                      // (build 392 : remise en place de la GBA, code d'erreur)
                    val err = GbaNatif.mem32(0x03000A98); val e0 = GbaNatif.mem32(0x03000AA0); val e1 = GbaNatif.mem32(0x03000AA4)
                    val drap = js and 0x30
                    if (err != errPrec || (drapPrec != 0 && drap == 0)) {
                        evt("GBA : %s (code d'erreur %X ; état %08X %08X ; JOYSTAT %02X JOYCNT %02X)".format(
                            if (drapPrec != 0 && drap == 0) "remise en place" else "code d'erreur changé", err, e0, e1, js, joycnt))
                    }
                    errPrec = err; drapPrec = drap
                    if ((codeJeu and 0xFFFFFFL) == 0x4C5A47L) {    // (build 401 : Tingle Tuner : etat de son lien (0x0300564D))
                        val et = GbaNatif.mem32(0x0300564C) ushr 8 and 0xFF
                        if (et and 0x0E == 0x08 && (etatTT < 0 || etatTT and 0x0E != 0x08)) echoTT = true   // (passe a l'etape 4 : connecte)
                        // build 404 : etat de chacun des 16 messages (0 libre, 3 en reception, 4 recu, 6 a envoyer, 7 envoi, 9 somme
                        // fausse) et messages de la GBA en attente d'envoi (0x03005645) : chaque changement dans le rapport
                        val sb = StringBuilder()
                        for (k in 0 until 4) { val w = GbaNatif.mem32(0x03005330 + 4 * k); for (j in 0 until 4) sb.append("%X".format((w ushr (8 * j)) and 0xFF)) }
                        val enAttente = (GbaNatif.mem32(0x03005644) ushr 8) and 0xFF
                        val ets = "messages $sb, à envoyer $enAttente"
                        if (ets != etatsTT) { trace("TT $ets"); etatsTT = ets }
                        // build 414 : un message de la GBA envoye (8) dont la reponse du jeu ne vient pas en 1 s (perdue dans une
                        // reconnexion : la case restait bloquee, le curseur ne bougeait plus) : liberee (0), comme si le jeu avait
                        // repondu « lu » ; le programme y remet son message suivant
                        if (et and 0x0E == 0x08) for (k in 0 until 16) {
                            val st = (GbaNatif.mem32(0x03005330 + 4 * (k / 4)) ushr (8 * (k % 4))) and 0xFF
                            if (st == 8) { if (++attente8[k] > 60) { GbaNatif.ecr8(0x03005330 + k, 0); attente8[k] = 0; nRelances++; trace("TT message %d relancé (sans réponse du jeu)".format(k)) } }
                            else attente8[k] = 0
                        }
                        if (et != etatTT) { evt("Tingle Tuner : lien %02X (étape %d%s%s%s ; JOYSTAT %02X JOYCNT %02X)".format(et, et shr 1 and 7,
                            if (et and 0x20 != 0) ", en route" else "", if (et and 1 != 0) ", ERREUR" else "", if (et and 0x40 != 0) ", déjà connecté" else "", js, joycnt)); etatTT = et }
                    }
                }
                regsVus = "JOYSTAT %02X, JOYCNT %02X, JOY_TRANS %08X, JOY_RECV %08X".format(r[0], r[3], r[1], r[2]); retenus = retenir
                if (donnesSansLu && entrees.isEmpty() && js and 0x02 == 0) { donnesSansLu = false; luPret = true }
                val tv = r[1].toLong() and 0xFFFFFFFFL
                // build 388 : l'identifiant donne apres une remise a zero : des la 2e fois, Pulse l'a deja donne au jeu (console
                // v347) et le jeu l'a lu aussitot : lu ici tout de suite (sinon la GBA refuse la suite : « La connexion a echoue »)
                // build 389 : un JOY_TRANS de la GBA ne compte qu'avec ses drapeaux 0x20 / 0x30 (son programme ecrit 0 en se
                // remettant en place, sans drapeau : ni donne au jeu ni lu ici, sinon il y voit une erreur et recommence) ;
                // l'identifiant : celui ecrit avec 0x20 (par sa reponse a la remise a zero)
                val drap = js and 0x30
                // build 401 (console v360) : Pulse donne d'avance le code du jeu comme identifiant, des la 1re remise a zero
                val idRef = if (idGba >= 0) idGba else codeJeu
                val idDejaDonne = !attenteLu && js and 0x08 != 0 && apresReset && drap == 0x20 && idRef >= 0 && tv == idRef
                if (!attenteLu && js and 0x08 != 0 && apresReset && drap == 0x20) { apresReset = false; if (idGba < 0) idGba = tv }
                if (idDejaDonne) {
                    trace("identifiant %08X lu ici".format(tv)); GbaNatif.joy(0x14, d); nAvales++; GbaNatif.regs(r); joycnt = r[3]
                } else if (!attenteLu && js and 0x08 != 0 && echoTT && drap == 0x30 && tv == dernierMot) {
                    // build 402 (console v363) : l'echo de la cle de partie du Tingle Tuner, deja donne au jeu par Pulse : lu ici
                    trace("écho %08X lu ici".format(tv)); GbaNatif.joy(0x14, d); nAvales++; echoTT = false; GbaNatif.regs(r); joycnt = r[3]
                } else if (!attenteLu && js and 0x08 != 0 && aAvaler.remove(tv)) {   // (build 381 : deja donne au jeu par Pulse)
                    GbaNatif.joy(0x14, d); nAvales++
                    if (tv == codeJeu) { codeLu = true; GbaNatif.regs(r); joycnt = r[3] }   // (build 386 : JOYCNT relu apres la lecture : 4)
                } else if (!attenteLu && js and 0x08 != 0 && drap == 0x30 && ((codeJeu and 0xFFFFFFL) == 0x4C5A47L && etatTT >= 0 && etatTT and 0x0E == 0x08 || pacman) && file.size < 32) {
                    // build 483 : Pac-Man vs. aussi : un mot ecrit juste apres un mot de la rafale (pas lu pendant l'image) passait
                    // par l'autre chemin (JOY_TRANS seul) en meme temps que la file ; Pulse donnait la tete de la file et croyait ce
                    // mot-la deja lu : il ne venait jamais, le jeu attendait (JOYSTAT 30), la GBA aussi (JOYSTAT 38)
                    // build 403 : Tingle Tuner connecte : lu ici tout de suite (la GBA passe au mot suivant), envoye a la file de Pulse
                    numero = numero % 255 + 1
                    file.add(numero.toLong() shl 32 or tv); GbaNatif.joy(0x14, d); nEnvoyes++
                    trace("GBA écrit %08X (file : %d)".format(tv, file.size)); GbaNatif.regs(r); joycnt = r[3]
                    msgGba.mot(tv)
                } else if (pacman && js and 0x08 != 0 && drap == 0x30) {
                    // build 484 : Pac-Man connecte, file pleine (le jeu lit moins vite que la GBA n'ecrit) : le mot attend ici, la GBA
                    // aussi (comme sur le vrai cable) ; avant, il passait par le chemin du JOY_TRANS seul, que Pulse ignorait pendant
                    // la file (numero deja vu) : le jeu ne le recevait jamais et renvoyait son paquet de 12 mots sans fin
                } else if (!attenteLu && js and 0x08 != 0 && drap != 0) {      // (la GBA a ecrit JOY_TRANS : pour le jeu)
                    transVal = tv
                    numero = numero % 255 + 1; transId = numero
                    trace("GBA écrit %08X (JOYSTAT %02X)".format(tv, js))
                    msgGba.mot(tv)
                    attenteLu = true
                }
                drapeaux = js and 0x30
                resetsVus = resetsFaits
                synchronized(ecran) { ecran.setPixels(px, 0, W, 0, 0, W, H) }
                images++
                if (res[0] > 0) try { son0?.write(son, 0, res[0] * 2, AudioTrack.WRITE_NON_BLOCKING) } catch (e: Exception) { }
                prochain += 16_742_706L                    // (59,73 images par seconde, comme la GBA)
                val att = prochain - System.nanoTime()
                if (att > 0) Thread.sleep(att / 1_000_000, (att % 1_000_000).toInt()) else if (att < -100_000_000L) prochain = System.nanoTime()
            }
        } catch (e: Exception) {
            message = "Émulateur arrêté : ${e.message}"
        } finally {
            try { son0?.stop(); son0?.release() } catch (e: Exception) { }
            try { Cartouche.garder() } catch (e: Throwable) { }   // (build 556)
            GbaNatif.arreter()
            marche = false
        }
    }

    /** option et JOY_TRANS a mettre dans notre prochain paquet "GCGB" (fil reseau) */
    fun options(): Pair<Long, Long> {
        var o = 0L
        val id = transId
        if (id > 0) o = o or 1L or (id.toLong() shl 16)
        if (luPret) { o = o or 2L; luPret = false }
        if (marche) o = o or 8L or (drapeaux.toLong() shl 8) or 0x10L or ((resetsVus.toLong() and 0xFF) shl 24)
        return Pair(o, transVal)
    }
}

/** l'ecran de la GBA et ses boutons (toucher : appuyer, lever : relacher) ; build 406 : plein ecran */
@Composable
fun EcranGba() {
    var plein by androidx.compose.runtime.remember { mutableStateOf(false) }
    val demGba = BasculeGba3D.versGba                          // (build 412 : demande venue du plein ecran de la vue 3D)
    val depGba = androidx.compose.runtime.remember { BasculeGba3D.versGba }
    androidx.compose.runtime.LaunchedEffect(demGba) { if (demGba > depGba) plein = true }
    Column(verticalArrangement = Arrangement.spacedBy(10.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        ImageGba(Modifier.fillMaxWidth().aspectRatio(1.5f).clip(RoundedCornerShape(8.dp)).background(Color.Black))
        Box(Modifier.clip(RoundedCornerShape(10.dp)).background(C.Verre).pointerInput(Unit) { detectTapGestures { plein = true } }
                .padding(horizontal = 14.dp, vertical = 6.dp)) { Text("⛶ Plein écran", color = C.Texte, fontSize = 13.sp) }
        BoutonsHautGba()
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            CroixGba(); BoutonsDroiteGba()
        }
    }
    if (plein) PleinEcranGba { plein = false }
}

/** l'image de la GBA, redessinee a chaque image de l'ecran du telephone (build 382) */
@Composable
private fun ImageGba(modifier: Modifier) {
    var n by androidx.compose.runtime.remember { mutableIntStateOf(0) }
    androidx.compose.runtime.LaunchedEffect(Unit) {
        while (true) { androidx.compose.runtime.withFrameNanos { }; if (GbaEmu.images != n) n = GbaEmu.images }
    }
    Box(modifier, contentAlignment = Alignment.Center) {
        val bmp = synchronized(GbaEmu.ecran) { GbaEmu.ecran.copy(Bitmap.Config.ARGB_8888, false) }
        Image(bmp.asImageBitmap(), contentDescription = "Écran de la GBA ($n)", modifier = Modifier.fillMaxSize(),
              contentScale = ContentScale.Fit, filterQuality = FilterQuality.None)
    }
}

@Composable private fun BoutonsHautGba() {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        ToucheGba("L", 9, Modifier.width(90.dp)); ToucheGba("R", 8, Modifier.width(90.dp))
    }
}
@Composable private fun CroixGba() {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        ToucheGba("▲", 6)
        Row { ToucheGba("◀", 5); Spacer(Modifier.size(52.dp)); ToucheGba("▶", 4) }
        ToucheGba("▼", 7)
    }
}
@Composable private fun BoutonsDroiteGba() {
    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) { ToucheGba("B", 1, Modifier.size(56.dp), rond = true); ToucheGba("A", 0, Modifier.size(56.dp), rond = true) }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) { ToucheGba("SELECT", 2, Modifier.width(76.dp)); ToucheGba("START", 3, Modifier.width(76.dp)) }
    }
}

/** build 406 : l'ecran de la GBA en plein ecran (barres d'Android cachees, comme la vue 3D) ; build 408 : l'image remplit l'ecran
 *  sans deformation, boutons transparents par-dessus (en largeur : croix et L a gauche, A / B / SELECT / START et R a droite).
 *  ✕ ou retour : sortir */
@Composable
private fun PleinEcranGba(fermer: () -> Unit) {
    PleinEcranCouche(fermer) {                            // (build 411 : couche de l'activite, plus une fenetre de dialogue)
        androidx.compose.foundation.layout.BoxWithConstraints(Modifier.fillMaxSize().background(Color.Black)) {
            val largeur = maxWidth > maxHeight
            // build 408 : l'image prend tout l'ecran sans etre deformee (bandes noires si besoin), boutons par-dessus, transparents
            ImageGba(Modifier.fillMaxSize())
            Box(Modifier.fillMaxSize().displayCutoutPadding().padding(horizontal = 16.dp, vertical = 12.dp).alpha(0.55f)) {
                if (largeur) {
                    Box(Modifier.align(Alignment.TopStart)) { ToucheGba("L", 9, Modifier.width(90.dp)) }
                    Box(Modifier.align(Alignment.TopEnd).padding(end = 56.dp)) { ToucheGba("R", 8, Modifier.width(90.dp)) }   // (a cote de ✕)
                    Box(Modifier.align(Alignment.BottomStart)) { CroixGba() }
                    Box(Modifier.align(Alignment.BottomEnd)) { BoutonsDroiteGba() }
                } else {
                    Column(Modifier.align(Alignment.BottomCenter).padding(bottom = 24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                        BoutonsHautGba()
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                            CroixGba(); BoutonsDroiteGba()
                        }
                    }
                }
            }
            // build 412 : passer a la vue 3D (si elle est ouverte dans le compagnon), et revenir ici par son bouton « 🎮 GBA »
            if (BasculeGba3D.vue3DOuverte) Text("🗺 Vue 3D", color = Color.White, fontSize = 15.sp, modifier = Modifier.align(Alignment.TopCenter).displayCutoutPadding()
                .padding(top = 8.dp).clip(RoundedCornerShape(16.dp)).background(Color(0x55FFFFFF))
                .pointerInput(Unit) { detectTapGestures { fermer(); BasculeGba3D.vers3D++ } }.padding(horizontal = 14.dp, vertical = 6.dp))
            Text("✕", color = Color.White, fontSize = 20.sp, modifier = Modifier.align(Alignment.TopEnd).displayCutoutPadding()
                .padding(top = 8.dp, end = 12.dp).clip(CircleShape).background(Color(0x55FFFFFF))
                .pointerInput(Unit) { detectTapGestures { fermer() } }.padding(horizontal = 12.dp, vertical = 4.dp))
        }
    }
}

@Composable
private fun ToucheGba(nom: String, bit: Int, modifier: Modifier = Modifier.size(52.dp), rond: Boolean = false) {
    var bas by androidx.compose.runtime.remember { mutableStateOf(false) }
    Box(modifier.clip(if (rond) CircleShape else RoundedCornerShape(10.dp)).background(if (bas) C.Rose else C.Verre)
            .pointerInput(bit) {
                detectTapGestures(onPress = {
                    bas = true; GbaEmu.appui(bit, true)
                    try { tryAwaitRelease() } finally { bas = false; GbaEmu.appui(bit, false) }
                })
            }, contentAlignment = Alignment.Center) {
        Text(nom, color = C.Texte, fontSize = if (nom.length > 2) 11.sp else 18.sp)
    }
}

/** build 487 (appli privee seulement : BuildConfig.PRIVE) : curseur du Tingle Tuner pose d'un coup a l'endroit touche dans la
 *  vue 3D. Builds 487-489 : appris sur le message 4 de la GBA ; les rapports du 9 novembre (2 h 33, 2 h 38) montrent que ce
 *  message ne donne pas la position sur la carte (la carte defile : une moitie reste a 0 pendant que le curseur du jeu
 *  avance de 3000 unites). Build 490 : la position vient du message 3 du JEU (ce qu'il renvoie a la GBA pour l'affichage) :
 *  l'entree dont le 2e mot commence par 0x0380 (x = moitie haute du mot d'avant, y = moitie basse) ; elle a bouge de ~40
 *  sur les deux axes pendant les trajets en diagonale et sur un seul axe pendant le trajet droit. L'appli apprend
 *  carte -> monde (droite par axe, d'apres des reperes pris curseur immobile), cherche dans la memoire de la GBA les
 *  demi-mots egaux a x et a y (gardes s'ils suivent chaque arret du curseur), et le saut les ecrit ; verifie ~0,5 s plus
 *  tard dans le message 3 ; sinon trajet a la croix ; 2 echecs de suite : saut coupe pour la partie. */
object Saut {
    @Volatile var demande: FloatArray? = null
    @Volatile var texte = ""
    @Volatile private var carte: IntArray? = null                 // x, y (message 3 du jeu, entree 0x0380)
    @Volatile private var carteVue = false; private var tCarte = 0L
    private val cands = arrayOfNulls<IntArray>(2)                 // par axe (0 : x, 1 : y) : adresses de demi-mots
    private val nSuivi = IntArray(2); private val tScan = LongArray(2); private val valSuivie = intArrayOf(-1, -1)
    private val paires = ArrayList<FloatArray>()                   // cx, cy, monde x, monde z
    private var echecs = 0; private var nSauts = 0; private var nReussis = 0
    private var verif: IntArray? = null; private var verifCible: FloatArray? = null; private var verifImg = 0
    @Volatile private var fit = false
    private var kx = 0; private var ax = 0f; private var bx = 0f; private var kz = 1; private var az = 0f; private var bz = 0f

    fun raz() { for (k in 0..1) { cands[k] = null; nSuivi[k] = 0; tScan[k] = 0L; valSuivie[k] = -1 }; paires.clear(); echecs = 0
        nSauts = 0; nReussis = 0; verif = null; verifCible = null; fit = false; demande = null; carte = null; carteVue = false; texte = "" }
    fun pret(): Boolean {
        if (!fit || echecs >= 2 || carte == null) return false
        for (k in 0..1) { val c = cands[k] ?: return false; if (c.size !in 1..16 || nSuivi[k] < 3) return false }
        return true
    }
    /** (fil de l'emulateur) le message 3 du jeu a change */
    fun jeu3(d: List<Long>) {
        if (RetroSucces.hardcore) return                              // (build 500 : pas d'ecriture dans la GBA en hardcore)
        carteVue = true
        var p: IntArray? = null
        for (i in 1 until d.size) if (((d[i] ushr 16) and 0xFFFF) == 0x0380L) { p = intArrayOf(((d[i - 1] ushr 16) and 0xFFFF).toInt(), (d[i] and 0xFFFF).toInt()); break }
        val a = carte
        if (p == null || a == null || a[0] != p[0] || a[1] != p[1]) tCarte = System.nanoTime()
        carte = p
        majTexte()
    }
    /** position du curseur dans le monde d'apres la carte (quand la vue 3D ne la donne pas) */
    fun monde(): FloatArray? { val c = carte ?: return null; if (!fit) return null
        return floatArrayOf(ax * c[kx] + bx, 0f, az * c[kz] + bz) }
    private fun zones() = sequenceOf(0x03000000 until 0x03008000 step 4, 0x02000000 until 0x02040000 step 4).flatten()
    private fun lire16(a: Int): Int { val w = GbaNatif.mem32(a and 3.inv()); return if (a and 2 != 0) (w ushr 16) and 0xFFFF else w and 0xFFFF }
    /** curseur immobile sur la valeur v (axe k) : on garde les demi-mots de la GBA qui la contiennent */
    private fun suivre(k: Int, v: Int) {
        if (v == valSuivie[k]) return
        val c = cands[k]
        if (c == null) {
            val t = System.nanoTime(); if (t - tScan[k] < 1_000_000_000L) return; tScan[k] = t
            val l = ArrayList<Int>()
            for (a in zones()) { val w = GbaNatif.mem32(a); if (w and 0xFFFF == v) l.add(a); if ((w ushr 16) and 0xFFFF == v) l.add(a + 2); if (l.size > 4096) break }
            cands[k] = if (l.size in 1..4096) l.toIntArray() else null; nSuivi[k] = 0
        } else {
            val f = c.filter { lire16(it) == v }.toIntArray()
            if (f.isEmpty()) { cands[k] = null; nSuivi[k] = 0 } else { cands[k] = f; nSuivi[k]++ }
        }
        valSuivie[k] = v
    }
    private fun majTexte() {
        val c = carte
        val n = arrayOf("x", "y")
        texte = (if (!carteVue) "message 3 du jeu pas encore vu" else if (c == null) "curseur absent du message 3 (carte pas affichée ?)" else "carte %d, %d".format(c[0], c[1])) +
            ", emplacements ${cands[0]?.size ?: 0} / ${cands[1]?.size ?: 0} (suivis ${nSuivi[0]} / ${nSuivi[1]} fois), ${paires.size} repère(s)" +
            (if (fit) ", carte apprise (monde x : carte ${n[kx]} × %.1f, monde z : carte ${n[kz]} × %.1f)".format(ax, az) else "") +
            (if (nSauts > 0) ", sauts $nReussis / $nSauts" else "") + (if (echecs >= 2) ", saut coupé" else "")
    }
    /** (fil de l'emulateur, chaque image) : reperes, saut demande, verification. Rend (texte, repli) ou null */
    fun image(): Pair<String, FloatArray?>? {
        val c = carte
        val immobile = c != null && System.nanoTime() - tCarte > 500_000_000L
        val v = verif
        if (v != null) {
            if (++verifImg < 30 && !(c != null && Math.abs(c[0] - v[0]) <= 2 && Math.abs(c[1] - v[1]) <= 2)) return null
            verif = null; val cib = verifCible; verifCible = null
            val ok = c != null && Math.abs(c[0] - v[0]) <= 2 && Math.abs(c[1] - v[1]) <= 2
            if (ok) { echecs = 0; nReussis++; majTexte(); return "Curseur posé (saut)" to null }
            echecs++; majTexte()
            return (if (echecs >= 2) "Saut coupé (2 échecs) : trajet à la croix" else "Saut pas suivi par le jeu : trajet à la croix") to cib
        }
        if (immobile && c != null) {
            suivre(0, c[0]); suivre(1, c[1])
            val jeu = Prive.curseurTT()
            if (jeu != null) {
                val der = paires.lastOrNull()
                if (der == null || der[0].toInt() != c[0] || der[1].toInt() != c[1]) {
                    paires.add(floatArrayOf(c[0].toFloat(), c[1].toFloat(), jeu[0], jeu[2])); if (paires.size > 16) paires.removeAt(0); ajuster()
                }
            }
            majTexte()
        }
        val d = demande ?: return null
        demande = null
        if (!pret() || c == null) return "Curseur : saut pas prêt" to d
        val tx = Math.round((d[0] - bx) / ax); val tz = Math.round((d[1] - bz) / az)
        val cible = IntArray(2); cible[kx] = tx; cible[kz] = tz
        if (cible[0] !in 0..0xFFFF || cible[1] !in 0..0xFFFF) return "Curseur : hors de la carte, trajet à la croix" to d
        for (k in 0..1) ecrire(k, cible[k])
        verif = cible; verifCible = d; verifImg = 0; nSauts++; majTexte()
        return "Curseur : saut (carte ${cible[0]}, ${cible[1]})" to null
    }
    private fun ecrire(k: Int, v: Int) {
        val c = cands[k] ?: return
        for (a in c) { GbaNatif.ecr8(a, v and 0xFF); GbaNatif.ecr8(a + 1, (v ushr 8) and 0xFF) }
        valSuivie[k] = -1
    }
    /** monde x puis monde z : l'axe de la carte qui suit le mieux (droite, ecart faible par rapport a l'etendue) */
    private fun ajuster() {
        if (paires.size < 3) return
        fun droite(k: Int, w: Int): Triple<Float, Float, Float>? {
            val xs = paires.map { it[k] }; val ys = paires.map { it[2 + w] }
            val n = xs.size; val mx = xs.average().toFloat(); val my = ys.average().toFloat()
            var sxx = 0f; var sxy = 0f; var syy = 0f
            for (i in 0 until n) { sxx += (xs[i] - mx) * (xs[i] - mx); sxy += (xs[i] - mx) * (ys[i] - my); syy += (ys[i] - my) * (ys[i] - my) }
            if (sxx < 16f || syy < 1e4f) return null
            val a = sxy / sxx; val b = my - a * mx
            var err = 0f; for (i in 0 until n) { val e = ys[i] - (a * xs[i] + b); err += e * e }
            return Triple(a, b, err / syy)
        }
        var best: Triple<Int, Triple<Float, Float, Float>, Triple<Float, Float, Float>>? = null; var bestErr = 1e9f
        for (k in 0..1) {                                              // (x de la carte -> monde x, ou bien -> monde z)
            val tx = droite(k, 0) ?: continue; val tz = droite(1 - k, 1) ?: continue
            val e = tx.third + tz.third
            if (tx.third < 0.05f && tz.third < 0.05f && e < bestErr) { bestErr = e; best = Triple(k, tx, tz) }
        }
        val b0 = best ?: run { fit = false; return }
        kx = b0.first; ax = b0.second.first; bx = b0.second.second
        kz = 1 - b0.first; az = b0.third.first; bz = b0.third.second
        fit = Math.abs(ax) > 1e-3f && Math.abs(az) > 1e-3f
    }
}
