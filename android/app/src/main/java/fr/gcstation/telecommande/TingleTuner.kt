package fr.gcstation.telecommande

// Appli (build 371) : GBA du telephone, pour le Tingle Tuner de Wind Waker (essai). Avec le reglage « GBA du telephone
// (port 2) » de GC Station, Pulse (le resident du jeu) fait passer le telephone pour une GBA branchee au port 2 : il repond
// lui-meme au jeu (le Wi-Fi est trop lent pour ca) et nous envoie tous les mots que le jeu ecrit a la « GBA » (UDP 51237,
// paquets "GCGB", format dans natres.h). Le jeu commence par envoyer a la GBA son programme (le Tingle Tuner, ~120 Ko),
// chiffre : on le dechiffre (format public du demarrage par le cable : cle de session, generateur "Kawa", somme CRC 0xA1C1),
// on verifie sa somme et on lit son titre ; il est garde sur le telephone (filesDir/gba). Ensuite, sur une vraie GBA, ce
// programme tournerait et parlerait au jeu : il n'y a pas encore d'emulateur GBA dans l'appli, on montre les mots echanges.

import android.content.Context
import android.content.Intent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import java.io.File
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketTimeoutException

object GbaTel {
    private const val KAWA = 0x6177614BL
    private const val SEDO = 0x6F646573L
    private const val M32 = 0xFFFFFFFFL

    /** ce que l'ecran montre (mis a jour par le fil reseau) */
    class Etat(val marche: Boolean, val vuPulse: Boolean, val etape: Int, val joy: Int, val annonce: Long, val recus: Long,
               val n00: Long, val n14: Long, val n15: Long, val programme: String?, val journal: List<String>)
    var etat by mutableStateOf(Etat(false, false, 0, 0, 0, 0, 0, 0, 0, null, emptyList()))
        private set

    @Volatile private var marche = false
    @Volatile private var demandeReprise = false
    private var fil: Thread? = null
    private var dir: File? = null
    private val mots = HashMap<Long, Long>()               // (numero du mot du jeu -> mot, tel que la GBA le lit)
    private var suite = 0L                                 // (mots recus a la suite depuis le 1er : notre accuse)
    private var cle = -1L
    private var etape = 0; private var joy = 0; private var annonce = 0L; private var recus = 0L
    private var n00 = 0L; private var n14 = 0L; private var n15 = 0L
    private var programme: String? = null
    private var decode = false
    private var vuPulse = 0L
    private var relaisVu = 0L                              // (mots du relais deja montres)
    private var lu = false
    private val journal = ArrayDeque<String>()

    private fun note(t: String) {
        val h = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.FRANCE).format(java.util.Date())
        synchronized(journal) { journal.addLast("$h $t"); while (journal.size > 60) journal.removeFirst() }
    }
    private fun publier() {
        etat = Etat(marche, System.currentTimeMillis() - vuPulse < 2000, etape, joy, annonce, recus, n00, n14, n15, programme,
                    synchronized(journal) { journal.toList() })
    }
    private fun hote(): InetAddress? = try { Api.host?.substringBefore(':')?.let { InetAddress.getByName(it) } } catch (e: Exception) { null }
    private fun g32(b: ByteArray, o: Int) = (b[o].toLong() and 255 shl 24) or (b[o + 1].toLong() and 255 shl 16) or (b[o + 2].toLong() and 255 shl 8) or (b[o + 3].toLong() and 255)
    private fun p32(b: ByteArray, o: Int, v: Long) { b[o] = (v shr 24).toByte(); b[o + 1] = (v shr 16).toByte(); b[o + 2] = (v shr 8).toByte(); b[o + 3] = v.toByte() }

    fun brancher(ctx: Context) {
        if (marche) return
        dir = File(ctx.filesDir, "gba")
        marche = true
        note("GBA branchée : on attend Pulse (port 2)")
        publier()
        fil = Thread({ boucle() }, "gba-tel").apply { isDaemon = true; start() }
    }
    fun debrancher() { marche = false; note("GBA débranchée"); publier() }
    /** Pulse remet sa « GBA » en attente (le jeu pourra renvoyer le programme) */
    fun recommencer() { demandeReprise = true; note("Recommencer demandé") }

    private fun raz() {
        mots.clear(); suite = 0; decode = false; programme = null; relaisVu = 0; lu = false
    }

    private fun boucle() {
        val so = try { DatagramSocket().apply { soTimeout = 30; try { receiveBufferSize = 1 shl 20 } catch (e: Exception) { } } } catch (e: Exception) { note("Réseau indisponible : ${e.message}"); marche = false; publier(); return }
        val b = ByteArray(2048)
        var seq = 1L
        var tPub = 0L
        try {
            while (marche) {
                val h = hote()
                if (h == null) { Thread.sleep(500); continue }
                // notre paquet (toutes les ~30 ms) : accuse, lecture du drapeau 0x02 (relais), recommencer
                val d = ByteArray(24)
                d[0] = 'G'.code.toByte(); d[1] = 'C'.code.toByte(); d[2] = 'G'.code.toByte(); d[3] = 'B'.code.toByte()
                d[4] = 1; d[5] = 1
                p32(d, 8, seq++); p32(d, 12, suite); p32(d, 16, 0)
                var opt = 0L
                if (lu) { opt = opt or 2; lu = false }
                if (demandeReprise) { opt = opt or 4; demandeReprise = false; raz() }
                p32(d, 20, opt)
                try { so.send(DatagramPacket(d, 24, h, MemoireJeu.PORT)) } catch (e: Exception) { }
                val fin = System.currentTimeMillis() + 30
                while (true) {
                    val reste = (fin - System.currentTimeMillis()).toInt()
                    if (reste <= 0) break
                    so.soTimeout = reste
                    val p = DatagramPacket(b, b.size)
                    try { so.receive(p) } catch (e: SocketTimeoutException) { break }
                    if (p.length >= 40 && b[0] == 'G'.code.toByte() && b[1] == 'C'.code.toByte() && b[2] == 'G'.code.toByte() && b[3] == 'B'.code.toByte() && b[4].toInt() == 2)
                        paquet(b, p.length)
                }
                val now = System.currentTimeMillis()
                if (now - tPub > 200) { tPub = now; publier() }
            }
        } catch (e: Exception) { note("Arrêt : ${e.message}") } finally { try { so.close() } catch (e: Exception) { } }
        publier()
    }

    private fun paquet(b: ByteArray, n: Int) {
        vuPulse = System.currentTimeMillis()
        val st = b[6].toInt() and 255; val js = b[7].toInt() and 255
        val k = g32(b, 8); val gl = g32(b, 12); val gp = g32(b, 16); val first = g32(b, 20); val cnt = g32(b, 24).toInt()
        n00 = g32(b, 28); n14 = g32(b, 32); n15 = g32(b, 36)
        if (cnt < 0 || 40 + cnt * 4 > n) return
        if (k != cle && st in 1..3) {                      // (nouvelle remise a zero du jeu : nouvelle cle de session)
            if (cle != -1L) note("Nouvelle remise à zéro par le jeu")
            cle = k; raz()
        }
        if (st != etape) {
            note(when (st) { 0 -> "Pulse : en attente du jeu"; 1 -> "Le jeu a lu la clé de session"; 2 -> "Le jeu envoie son programme (${gl / 1024} Ko)"
                             3 -> "Programme reçu par Pulse"; 4 -> "Programme lancé : relais"; else -> "État $st" })
            if (st == 0) { cle = -1L; raz() }
        }
        etape = st; joy = js; annonce = gl; recus = gp
        for (i in 0 until cnt) mots[first + i] = g32(b, 40 + 4 * i)
        while (mots.containsKey(suite)) suite++
        val total = 1 + gl / 4                             // (cle B, puis programme et CRC)
        if (!decode && st >= 3 && gl > 0 && suite >= total) { decode = true; dechiffrer(gl) }
        if (st == 4 && suite > total) {                    // (relais : ce que le jeu ecrit a la GBA)
            var i = maxOf(relaisVu, total)
            while (i < suite) { if (i - total < 200) note("Le jeu écrit à la GBA : %08X".format(mots[i] ?: 0L)); i++ }
            relaisVu = suite; lu = true
        }
    }

    /** le programme envoye par le jeu, comme le dechiffre la GBA (format public) ; garde dans filesDir/gba */
    private fun dechiffrer(gl: Long) {
        try {
            val romlen = (gl - 4).toInt()
            if (romlen <= 0xC0 || romlen > 0x40000) { note("Taille annoncée impossible : $gl"); return }
            val img = ByteArray(romlen)
            var key = cle; var crc = 0x15A0L; var fin = 0L
            for (i in 0..romlen / 4) {
                val a = i * 4L
                var w = mots[1L + i] ?: 0L
                if (a >= 0xC0) {
                    key = (key * KAWA + 1) and M32
                    w = w xor key xor ((-(0x02000000L + a)) and M32) xor 0x20796220L
                    if (a < romlen) { var dd = w; repeat(32) { crc = if ((crc xor dd) and 1L != 0L) (crc shr 1) xor 0xA1C1L else crc shr 1; dd = dd shr 1 } }
                }
                if (a < romlen) { val o = a.toInt(); img[o] = w.toByte(); img[o + 1] = (w shr 8).toByte(); img[o + 2] = (w shr 16).toByte(); img[o + 3] = (w shr 24).toByte() }
                else fin = w
            }
            val ok = fin == ((crc or (romlen.toLong() shl 16)) and M32)
            val titre = String(img, 0xA0, 12, Charsets.ISO_8859_1).trim { it <= ' ' || it == '\u0000' }
            val code = String(img, 0xAC, 4, Charsets.ISO_8859_1).trim { it <= ' ' || it == '\u0000' }
            programme = "%s%s · %d Ko · somme %s".format(titre.ifEmpty { "(sans titre)" }, if (code.isNotEmpty()) " ($code)" else "", romlen / 1024, if (ok) "OK" else "FAUSSE")
            note("Programme déchiffré : $programme")
            dir?.let { d -> d.mkdirs(); File(d, "programme-" + (titre.ifEmpty { "gba" }).replace(Regex("[^A-Za-z0-9_-]"), "_") + ".mb").writeBytes(img) }
        } catch (e: Exception) { note("Déchiffrement impossible : ${e.message}") }
    }

    fun rapport(): String {
        val e = etat
        return buildString {
            append("GBA du téléphone (Tingle Tuner, essai)\n")
            append("Pulse vu : ${if (e.vuPulse) "oui" else "non"} ; étape ${e.etape} ; JOYSTAT %02X\n".format(e.joy))
            append("Programme annoncé ${e.annonce} octets, reçus par Pulse ${e.recus} ; commandes du jeu : 00/FF ${e.n00}, 14 ${e.n14}, 15 ${e.n15}\n")
            append("Programme : ${e.programme ?: "pas encore"}\n\nJournal :\n")
            e.journal.forEach { append(it).append('\n') }
        }
    }
}

/** compagnon de Wind Waker : la GBA du telephone (Tingle Tuner, essai) */
@Composable
fun TingleTunerWW() {
    val ctx = LocalContext.current
    val e = GbaTel.etat
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Titre("Tingle Tuner · GBA du téléphone (essai)", couleur = Color(0xFF9BE15D))
            Doux("Dans les réglages de GC Station : « GBA du téléphone (port 2) » sur Oui, puis lancer Wind Waker par Pulse. " +
                 "Brancher ici la GBA avant d'utiliser le Tingle Tuner dans le jeu.", taille = 12)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Bouton(if (e.marche) "Débrancher" else "Brancher la GBA", Modifier.weight(1f), plein = !e.marche) {
                    if (e.marche) GbaTel.debrancher() else GbaTel.brancher(ctx)
                }
                Bouton("Recommencer", Modifier.weight(1f), actif = e.marche) { if (e.marche) GbaTel.recommencer() }
            }
            if (e.marche) {
                val et = when (e.etape) { 0 -> "en attente du jeu"; 1 -> "clé de session lue par le jeu"; 2 -> "le jeu envoie son programme"
                                          3 -> "programme reçu"; 4 -> "programme lancé (relais)"; else -> "?" }
                Doux("Pulse : " + (if (e.vuPulse) "répond" else "pas de réponse (réglage activé ? jeu lancé par Pulse ?)") + " · " + et + " · JOYSTAT %02X".format(e.joy), taille = 12, couleur = C.Texte)
                if (e.etape == 2 && e.annonce > 0) {
                    LinearProgressIndicator(progress = { (e.recus.toFloat() / e.annonce).coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth().height(6.dp), color = C.Rose)
                    Doux("Programme : ${e.recus / 1024} / ${e.annonce / 1024} Ko", taille = 12)
                }
                e.programme?.let { Doux("Programme du jeu : $it", taille = 12, couleur = Color(0xFF9BE15D)) }
                Doux("Commandes du jeu : état ${e.n00} · lectures ${e.n14} · écritures ${e.n15}", taille = 11, couleur = C.Pale)
                if (e.etape == 4) Doux("Le programme du Tingle Tuner tournerait maintenant sur la GBA : l'émulateur GBA n'est pas encore dans l'appli (prochaine étape). Les mots que le jeu envoie s'affichent ci-dessous.", taille = 11, couleur = C.Pale)
                e.journal.takeLast(12).forEach { Doux(it, taille = 11) }
                Bouton("Envoyer le rapport", Modifier.fillMaxWidth()) {
                    val i = Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, GbaTel.rapport())
                    try { ctx.startActivity(Intent.createChooser(i, "Rapport GBA").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (x: Exception) { }
                }
            }
        }
    }
}
