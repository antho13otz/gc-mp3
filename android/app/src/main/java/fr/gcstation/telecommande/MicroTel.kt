package fr.gcstation.telecommande

// Appli (build 373) : micro du telephone (essai). Avec le reglage « Micro du telephone (slot B) » de GC Station (console
// v332), Pulse (le resident du jeu) fait passer le slot B pour le micro GameCube (DOL-022 : Mario Party 6 et 7...). Il
// repond lui-meme au jeu et prend le son ici : paquets UDP "GCVO" vers le port 51237 de la console, echantillons 16 bits
// gros-boutistes, deja a la frequence que le jeu a demandee au micro (11025, 22050 ou 44100 Hz). Pulse renvoie son etat
// toutes les 10 images (format dans natres.h) : en marche ou non, frequence, taille des paquets, et ses compteurs.

import android.Manifest
import android.annotation.SuppressLint
import android.content.Intent
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
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
import androidx.core.content.ContextCompat
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketTimeoutException

object MicTel {
    private const val PORT = 51237
    private const val RATE = 44100                          // (enregistrement ; ramene a la frequence du jeu)

    /** ce que l'ecran montre (mis a jour par les fils) */
    class Etat(val marche: Boolean, val vuPulse: Boolean, val mstat: Long, val ncmd: Long, val nirq: Long, val nrd: Long,
               val attente: Long, val nrx: Long, val nlow: Long, val envoyes: Long, val niveau: Float, val journal: List<String>)
    var etat by mutableStateOf(Etat(false, false, 0, 0, 0, 0, 0, 0, 0, 0, 0f, emptyList()))
        private set

    @Volatile private var marche = false
    @Volatile private var mstat = 0L                        // (dernier etat du micro emule, lu dans le retour de Pulse)
    @Volatile private var vuPulse = 0L
    private var w = LongArray(8)                           // (mstat, commandes, interruptions, octets lus, mw, mr, paquets, manques)
    @Volatile private var envoyes = 0L
    @Volatile private var nAllume = 0
    // build 398 : reglages d'essai (le jeu recoit le son mais ne reagit pas) : ordre des octets des echantillons, volume
    var inverse by mutableStateOf(false)
    var gainX by mutableStateOf(1)
    @Volatile private var diag: LongArray? = null          // (build 396, console v352 : selections du slot B, lectures de l'identifiant, drapeaux)
    @Volatile private var niveau = 0f
    private val journal = ArrayDeque<String>()

    private fun note(t: String) {
        val h = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.FRANCE).format(java.util.Date())
        synchronized(journal) { journal.addLast("$h $t"); while (journal.size > 40) journal.removeFirst() }
    }
    private fun publier() {
        val x = w
        etat = Etat(marche, System.currentTimeMillis() - vuPulse < 2000, mstat, x[1], x[2], x[3], (x[4] - x[5]) and 0xFFFFFFFFL, x[6], x[7],
                    envoyes, niveau, synchronized(journal) { journal.toList() })
    }
    private fun hote(): InetAddress? = try { Api.host?.substringBefore(':')?.let { InetAddress.getByName(it) } } catch (e: Exception) { null }
    private fun g32(b: ByteArray, o: Int) = (b[o].toLong() and 255 shl 24) or (b[o + 1].toLong() and 255 shl 16) or (b[o + 2].toLong() and 255 shl 8) or (b[o + 3].toLong() and 255)
    private fun p32(b: ByteArray, o: Int, v: Long) { b[o] = (v shr 24).toByte(); b[o + 1] = (v shr 16).toByte(); b[o + 2] = (v shr 8).toByte(); b[o + 3] = v.toByte() }

    /** frequence demandee par le jeu (bits 11-12 de l'etat) */
    fun hz(st: Long) = when ((st shr 11) and 3) { 0L -> 11025; 1L -> 22050; else -> 44100 }
    fun paquet(st: Long) = when ((st shr 13) and 3) { 0L -> 32; 1L -> 64; else -> 128 }

    fun brancher() {
        if (marche) return
        marche = true; w = LongArray(8); envoyes = 0; mstat = 0; diag = null; nAllume = 0
        note("Micro branché : on attend Pulse (slot B)")
        publier()
        Thread({ boucle() }, "micro-tel").apply { isDaemon = true; start() }
    }
    fun debrancher() { marche = false; note("Micro débranché"); publier() }

    @SuppressLint("MissingPermission")
    private fun boucle() {
        var sock: DatagramSocket? = null
        var rec: AudioRecord? = null
        try {
            sock = DatagramSocket().apply { soTimeout = 1 }
            val min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
            rec = AudioRecord(MediaRecorder.AudioSource.VOICE_RECOGNITION, RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, maxOf(min, RATE / 5 * 2))
            if (rec.state != AudioRecord.STATE_INITIALIZED) { note("Micro du téléphone indisponible (autorisation ? autre appli ?)"); marche = false; publier(); return }
            rec.startRecording()
            val bloc = ShortArray(RATE / 100)                  // (10 ms)
            val sortie = java.io.ByteArrayOutputStream()
            val rx = ByteArray(256)
            var seq = 0L; var tPub = 0L; var tVide = 0L; var vuAvant = false; var stAvant = -1L
            var acc = 0; var nacc = 0                          // (moyenne de 1, 2 ou 4 echantillons : 44100 -> frequence du jeu)
            while (marche) {
                val n = rec.read(bloc, 0, bloc.size)
                if (n <= 0) continue
                val dst = hote()
                // retour de Pulse (etat du micro emule)
                while (true) {
                    val p = DatagramPacket(rx, rx.size)
                    try { sock.receive(p) } catch (e: SocketTimeoutException) { break }
                    if (p.length >= 40 && g32(rx, 0) == 0x4743564FL && rx[4].toInt() == 2) {
                        val x = LongArray(8) { g32(rx, 8 + it * 4) }
                        if (p.length >= 52) diag = LongArray(4) { if (it < 3 || p.length >= 56) g32(rx, 40 + it * 4) else -1L }
                        w = x; vuPulse = System.currentTimeMillis()
                        if (!vuAvant) { vuAvant = true; note("Pulse répond (slot B)") }
                        if (x[0] != stAvant) {
                            stAvant = x[0]; mstat = x[0]
                            if (x[0] and 0x8000L != 0L) nAllume++
                            note(if (x[0] and 0x8000L != 0L) "Le jeu allume le micro : ${hz(x[0])} Hz, paquets de ${paquet(x[0])} octets" else "Le jeu éteint le micro")
                            sortie.reset(); acc = 0; nacc = 0
                        }
                    }
                }
                // niveau (pour l'ecran)
                var m = 0
                for (i in 0 until n) { val a = kotlin.math.abs(bloc[i].toInt()); if (a > m) m = a }
                niveau = m / 32768f
                val st = mstat
                if (dst != null && st and 0x8000L != 0L) {
                    val f = RATE / hz(st)
                    for (i in 0 until n) {
                        acc += bloc[i]; nacc++
                        if (nacc == f) {
                            val v = (acc / f * gainX).coerceIn(-32768, 32767)
                            if (inverse) { sortie.write(v); sortie.write(v shr 8) } else { sortie.write(v shr 8); sortie.write(v) }
                            acc = 0; nacc = 0
                        }
                    }
                    if (sortie.size() >= 2 * hz(st) / 50) {           // (20 ms par paquet ; 1400 octets au plus)
                        val d = sortie.toByteArray(); sortie.reset()
                        var o = 0
                        while (o < d.size) {
                            val k = minOf(1400, d.size - o)
                            val b = ByteArray(12 + k)
                            b[0] = 'G'.code.toByte(); b[1] = 'C'.code.toByte(); b[2] = 'V'.code.toByte(); b[3] = 'O'.code.toByte()
                            p32(b, 4, ++seq); p32(b, 8, k.toLong())
                            System.arraycopy(d, o, b, 12, k)
                            try { sock.send(DatagramPacket(b, b.size, dst, PORT)); envoyes++ } catch (e: Exception) { }
                            o += k
                        }
                    }
                } else if (dst != null && System.currentTimeMillis() - tVide > 300) {
                    // micro du jeu eteint : un paquet vide de temps en temps, pour que Pulse connaisse notre adresse (son etat) ;
                    // 20 octets (n = 0) : Pulse refuse les paquets plus courts
                    tVide = System.currentTimeMillis()
                    val b = ByteArray(20); b[0] = 'G'.code.toByte(); b[1] = 'C'.code.toByte(); b[2] = 'V'.code.toByte(); b[3] = 'O'.code.toByte()
                    p32(b, 4, ++seq)
                    try { sock.send(DatagramPacket(b, b.size, dst, PORT)) } catch (e: Exception) { }
                }
                if (System.currentTimeMillis() - tPub > 250) { tPub = System.currentTimeMillis(); publier() }
            }
        } catch (e: Exception) {
            note("Erreur : ${e.message}")
        } finally {
            try { rec?.stop() } catch (e: Exception) { }
            rec?.release(); sock?.close()
            marche = false; niveau = 0f; publier()
        }
    }

    /** build 396 : ce que montre l'etat, en une phrase */
    fun lecture(e: Etat = etat): String {
        val d = diag
        return when {
            !e.marche -> "micro pas branché dans l'appli"
            Api.host == null -> "l'appli n'est pas reliée à la console"
            !e.vuPulse -> "aucune réponse de la console : jeu pas lancé par Pulse, ou console d'avant le build 875 avec le réglage « Micro du téléphone » sur Non, ou téléphone pas sur le même Wi-Fi"
            d != null && d[2] and 1L == 0L -> "le réglage « Micro du téléphone (slot B) » est sur Non dans GC Station : le mettre sur Oui et relancer le jeu"
            d != null && d[2] and 2L == 0L -> "le micro est activé mais le slot B n'est pas devenu le micro (relancer le jeu par Pulse)"
            d != null && d[0] == 0L -> "le jeu n'a jamais touché le slot B par Pulse (ses accès ne sont pas aiguillés : à me signaler)"
            d != null && d[1] == 0L -> "le jeu touche le slot B mais n'a pas encore lu l'identifiant du micro (aller dans un menu ou un mini-jeu à micro)"
            e.mstat and 0x8000L == 0L && nAllume > 0 && e.nrd == 0L -> "le jeu a allumé le micro $nAllume fois puis l'a éteint sans lire de son (paquets pas arrivés à temps)"
            e.mstat and 0x8000L == 0L && nAllume > 0 -> "le jeu a allumé le micro $nAllume fois et lu du son ; il est éteint pour l'instant"
            e.mstat and 0x8000L == 0L -> "le jeu a vu le micro mais ne l'a pas allumé (lancer un mini-jeu à micro)"
            e.nrx == 0L -> "micro allumé par le jeu mais aucun son du téléphone n'arrive à la console"
            e.nlow > e.nrd / 4 -> "le son arrive trop lentement (beaucoup d'octets sans son) : Wi-Fi lent ?"
            else -> "le jeu reçoit le son du téléphone"
        }
    }

    fun rapport(): String {
        val e = etat
        return buildString {
            append("Micro du téléphone (slot B, essai)\n")
            append("Pulse vu : ${if (e.vuPulse) "oui" else "non"} ; état du micro %04X".format(e.mstat))
            append(if (e.mstat and 0x8000L != 0L) " (en marche, ${hz(e.mstat)} Hz, paquets de ${paquet(e.mstat)} octets)\n" else " (éteint)\n")
            append("Commandes du jeu ${e.ncmd} ; interruptions ${e.nirq} ; octets lus par le jeu ${e.nrd} ; en attente ${e.attente}\n")
            append("Paquets reçus par Pulse ${e.nrx} ; octets sans son (retard) ${e.nlow} ; paquets envoyés ${e.envoyes}\n")
            // build 396 : le detail cote console (v352) et ce qu'on en conclut
            append("Appli GC Remote ${BuildConfig.VERSION_NAME} ; console visée : ${Api.host ?: "aucune (appli pas reliée)"} ; octets ${if (inverse) "inversés" else "normaux"}, volume ×$gainX\n")
            val d = diag
            if (d != null) {
                append("Côté console : micro activé dans GC Station : ${if (d[2] and 1L != 0L) "oui" else "NON"} ; slot B = micro : ${if (d[2] and 2L != 0L) "oui" else "non"} ; slot B émulé par Pulse : ${if (d[2] and 4L != 0L) "oui" else "non"}\n")
                append("Le jeu : slot B sélectionné ${d[0]} fois ; identifiant du micro lu ${d[1]} fois\n")
                if (d[3] >= 0) append("Coupures du son (réserve refaite, console 877+) : ${d[3]}\n")
            } else if (e.vuPulse) append("Côté console : pas de détail (console d'avant le build 875)\n")
            append("Lecture : " + lecture(e) + "\n\nJournal :\n")
            e.journal.forEach { append(it).append('\n') }
        }
    }
}

/** compagnon de jeu : le micro du telephone (slot B, essai) */
@Composable
fun MicroTelephone() {
    val ctx = LocalContext.current
    val e = MicTel.etat
    var refus by androidx.compose.runtime.remember { mutableStateOf(false) }
    val demande = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { ok -> if (ok) MicTel.brancher() else refus = true }
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Titre("🎤 Micro du téléphone · slot B (essai)", couleur = Color(0xFFFFC857))
            if (!e.marche) Doux("Pour les jeux à micro (Mario Party 6 et 7…). Dans les réglages de GC Station : « Micro du téléphone (slot B) » sur Oui, puis lancer le jeu par Pulse. Garde l'appli ouverte pendant la partie.", taille = 12)
            Bouton(if (e.marche) "Débrancher le micro" else "Brancher le micro", Modifier.fillMaxWidth(), plein = !e.marche) {
                if (e.marche) MicTel.debrancher()
                else if (ContextCompat.checkSelfPermission(ctx, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) MicTel.brancher()
                else demande.launch(Manifest.permission.RECORD_AUDIO)
            }
            if (refus && !e.marche) Doux("Sans l'autorisation du micro, l'appli ne peut pas l'utiliser (Réglages Android > Applis > GC Remote > Autorisations).", taille = 11, couleur = C.Orange)
            if (e.marche) {
                Doux("Pulse : " + (if (e.vuPulse) "répond" else "pas de réponse (réglage activé ? jeu lancé par Pulse ?)") + " · micro du jeu : " +
                     (if (e.mstat and 0x8000L != 0L) "allumé (${MicTel.hz(e.mstat)} Hz)" else "éteint"), taille = 12, couleur = C.Texte)
                LinearProgressIndicator(progress = { e.niveau.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth().height(6.dp), color = Color(0xFFFFC857))
                // build 398 : essais si le jeu ne reagit pas a la voix
                androidx.compose.foundation.layout.Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Bouton(if (MicTel.inverse) "Octets : inversés" else "Octets : normaux", Modifier.weight(1f), plein = MicTel.inverse) { MicTel.inverse = !MicTel.inverse }
                    Bouton("Volume ×${MicTel.gainX}", Modifier.weight(1f), plein = MicTel.gainX > 1) { MicTel.gainX = when (MicTel.gainX) { 1 -> 2; 2 -> 4; 4 -> 8; else -> 1 } }
                }
                Doux("Le jeu : ${e.ncmd} commandes · ${e.nirq} paquets annoncés · ${e.nrd / 1024} Ko lus · retard ${e.nlow} octets", taille = 11, couleur = C.Pale)
                Doux("→ " + MicTel.lecture(e), taille = 11, couleur = Color(0xFFFFC857))
                e.journal.takeLast(6).forEach { Doux(it, taille = 11) }
                Bouton("Envoyer le rapport", Modifier.fillMaxWidth()) {
                    val i = Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, MicTel.rapport())
                    try { ctx.startActivity(Intent.createChooser(i, "Rapport du micro").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (x: Exception) { }
                }
            }
        }
    }
}
