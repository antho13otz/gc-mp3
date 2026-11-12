package fr.gcstation.telecommande

// Appli (build 372) : GBA du telephone, pour le Tingle Tuner de Wind Waker (essai). Avec le reglage « GBA du telephone
// (port 2) » de GC Station, Pulse (le resident du jeu) fait passer le telephone pour une GBA branchee au port 2 : il repond
// lui-meme au jeu (le Wi-Fi est trop lent pour ca) et nous envoie tous les mots que le jeu ecrit a la « GBA » (UDP 51237,
// paquets "GCGB", format dans natres.h). Le jeu commence par envoyer a la GBA son programme (le Tingle Tuner, ~120 Ko),
// chiffre : on le dechiffre (format public du demarrage par le cable : cle de session, generateur "Kawa", somme CRC 0xA1C1),
// on verifie sa somme et on lit son titre ; il est garde sur le telephone (filesDir/gba). Ensuite, sur une vraie GBA, ce
// programme tournerait et parlerait au jeu : build 375, il tourne dans l'emulateur GBA de l'appli (GbaEmu.kt, mGBA).

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
    @Volatile private var diag: LongArray? = null          // (build 377 : detail du port de la GBA, console v335)
    @Volatile private var image: ByteArray? = null         // (build 375 : programme dechiffre, somme juste : pour l'emulateur)
    private var emuLance = false
    private var decode = false
    private var vuPulse = 0L
    private var relaisVu = 0L
    private var dseq = 0L                                  // (build 393 : numero du dernier paquet de Pulse pris)                              // (mots du relais deja montres)
    private var lu = false
    private val journal = ArrayDeque<String>()

    // build 380 : une meme ligne revue dans les 5 s n'est pas recopiee (le jeu qui recommence en boucle remplissait le
    // journal) : elle revient ensuite avec le nombre de fois sautees
    private val vues = HashMap<String, Pair<Long, Int>>()
    private fun note(t: String) { synchronized(journal) {
        val now = System.currentTimeMillis()
        val v = vues[t]
        if (v != null && now - v.first < 5000) { vues[t] = Pair(v.first, v.second + 1); return }
        vues[t] = Pair(now, 0)
        if (vues.size > 50) vues.entries.removeIf { now - it.value.first > 5000 }
        val h = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.FRANCE).format(java.util.Date())
        val x = if (v != null && v.second > 0) "$t (et ${v.second} fois de plus avant)" else t
        journal.addLast("$h $x"); while (journal.size > 60) journal.removeFirst()
    } }
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
        dseq = 0
        marche = true
        note("GBA branchée : on attend Pulse (port choisi dans GC Station)")
        publier()
        fil = Thread({ boucle() }, "gba-tel").apply { isDaemon = true; start() }
    }
    fun debrancher() { marche = false; GbaEmu.arreter(); note("GBA débranchée"); publier() }
    /** build 375 : relancer l'emulateur avec le dernier programme recu */
    fun relancerEmu() { image?.let { emuLance = true; GbaEmu.demarrer(it); note("GBA relancée") } }
    val aProgramme get() = image != null
    /** Pulse remet sa « GBA » en attente (le jeu pourra renvoyer le programme) */
    fun recommencer() { demandeReprise = true; note("Recommencer demandé") }

    private fun raz() {
        mots.clear(); suite = 0; decode = false; programme = null; relaisVu = 0; lu = false
        image = null; emuLance = false; GbaEmu.arreter()
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
                // build 403 (console v365) : + la file des mots de la GBA (Tingle Tuner) : nombre, numero du 1er, mots
                val fl = if (GbaEmu.marche) GbaEmu.fileAEnvoyer() else emptyList()
                val d = ByteArray(if (fl.isEmpty()) 24 else 32 + 4 * fl.size)
                d[0] = 'G'.code.toByte(); d[1] = 'C'.code.toByte(); d[2] = 'G'.code.toByte(); d[3] = 'B'.code.toByte()
                d[4] = 1; d[5] = 1
                var opt = 0L; var trans = 0L
                if (GbaEmu.marche) { val (o, t) = GbaEmu.options(); opt = o; trans = t }   // (build 375 : relais de l'emulateur)
                if (fl.isNotEmpty()) {
                    opt = opt or 0x20L; p32(d, 24, fl.size.toLong()); p32(d, 28, fl[0] ushr 32)
                    for ((i, w) in fl.withIndex()) p32(d, 32 + 4 * i, w and 0xFFFFFFFFL)
                }
                p32(d, 8, seq++); p32(d, 12, suite); p32(d, 16, trans)
                if (lu) { opt = opt or 2; lu = false }
                if (demandeReprise) { opt = opt or 4; demandeReprise = false; raz() }
                p32(d, 20, opt)
                try { so.send(DatagramPacket(d, d.size, h, MemoireJeu.PORT)) } catch (e: Exception) { }
                // (la GBA tourne, ou build 380 : le jeu envoie son programme : accuses toutes les 8 ms, sinon Pulse attend)
                val fin = System.currentTimeMillis() + (if (GbaEmu.marche || etape in 1..2) 8 else 30)
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
                if (vuPulse > 0) lireType()                    // (build 378 : type du port vu par le jeu, s'il est connu)
            }
        } catch (e: Exception) { note("Arrêt : ${e.message}") } finally { try { so.close() } catch (e: Exception) { } }
        publier()
    }

    private fun paquet(b: ByteArray, n: Int) {
        vuPulse = System.currentTimeMillis()
        val st = b[6].toInt() and 255; val js = b[7].toInt() and 255
        val k = g32(b, 8); val gl = g32(b, 12); val gp = g32(b, 16); val first = g32(b, 20); val cnt = g32(b, 24).toInt()
        // build 393 (console v350) : paquet plus ancien que le dernier pris (le Wi-Fi les livre parfois dans le desordre) :
        // seulement ses mots ; son etat (lecture par le jeu, remises a zero) est perime
        if (cnt >= 0 && n >= 40 + cnt * 4 + 56) {
            val sq = g32(b, 40 + cnt * 4 + 52)
            if (dseq > 0 && sq > 16 && sq - dseq <= 0 && dseq - sq < 1000) { for (i in 0 until cnt) mots[first + i] = g32(b, 40 + 4 * i); return }
            dseq = sq
        }
        GbaEmu.tidLuPulse = b[5].toInt() and 255           // (console v333 : dernier JOY_TRANS de la GBA lu par le jeu)
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
        if (n >= 40 + cnt * 4 + 48) diag = LongArray(12) { g32(b, 40 + cnt * 4 + it * 4) }   // (console v335 : diagnostic)
        if (n >= 40 + cnt * 4 + 60) GbaEmu.resetIdx = g32(b, 40 + cnt * 4 + 56)                 // (console v351 : mots d'avant la remise a zero)
        if (n >= 40 + cnt * 4 + 52) GbaEmu.resetsPulse = g32(b, 40 + cnt * 4 + 48).toInt()      // (console v347 : remises a zero)
        while (mots.containsKey(suite)) suite++
        val total = 1 + gl / 4                             // (cle B, puis programme et CRC)
        if (!decode && st >= 3 && gl > 0 && suite >= total) { decode = true; dechiffrer(gl) }
        val img = image
        if (st == 4 && img != null && !emuLance) { emuLance = true; GbaEmu.demarrer(img); note("Programme lancé dans l'émulateur GBA") }
        if (st == 4 && suite > total) {                    // (relais : ce que le jeu ecrit a la GBA)
            var i = maxOf(relaisVu, total)
            while (i < suite) {
                val w = mots.remove(i) ?: 0L
                if (emuLance) GbaEmu.entrees.add((i shl 32) or (w and 0xFFFFFFFFL))   // (build 375 : a la GBA emulee, dans l'ordre ; 394 : avec son numero)
                if (i - total < 30) note("Le jeu écrit à la GBA : %08X".format(w))
                i++
            }
            relaisVu = suite
            if (!emuLance) lu = true                       // (sans emulateur : lu tout de suite, pour que le jeu continue)
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
            if (ok) image = img
            if (ok && romlen >= 0x200) {                   // (build 381 : preuve et code, deja donnes au jeu par Pulse)
                fun le(o: Int) = (img[o].toLong() and 255) or (img[o + 1].toLong() and 255 shl 8) or (img[o + 2].toLong() and 255 shl 16) or (img[o + 3].toLong() and 255 shl 24)
                val e0 = mots[1L + 0x1F8 / 4] ?: 0L; val e1 = mots[1L + 0x1FC / 4] ?: 0L
                val p0 = le(0x1F8); val p1 = le(0x1FC)
                GbaEmu.avaler = setOf((e0 * e1) and M32, (p0 * p1) and M32, (p0 * e1) and M32, le(0xAC))
                GbaEmu.codeJeu = le(0xAC)
            }
            programme = "%s%s · %d Ko · somme %s".format(titre.ifEmpty { "(sans titre)" }, if (code.isNotEmpty()) " ($code)" else "", romlen / 1024, if (ok) "OK" else "FAUSSE")
            note("Programme déchiffré : $programme")
            dir?.let { d -> d.mkdirs(); File(d, "programme-" + (titre.ifEmpty { "gba" }).replace(Regex("[^A-Za-z0-9_-]"), "_") + ".mb").writeBytes(img) }
        } catch (e: Exception) { note("Déchiffrement impossible : ${e.message}") }
    }

    /** build 378 : le rapport detaille du port de la GBA (console v335 ; v338 : interruption, Pulse universel) */
    private fun detail(): String {
        val d = diag ?: return "Détail du port : pas reçu (console d'avant le build 855)\n"
        val port = when { d[10] and 1L != 0L -> 1; d[10] and 2L != 0L -> 2; d[10] and 4L != 0L -> 3; else -> 4 }
        val ch = port - 1
        val sh = (3 - ch) * 8
        val nIrq = (d[10] shr 8) and 0xFFFFFF
        val trapEtat = (d[11] shr 24).toInt(); val trapN = d[11] and 0xFFFFFF
        fun sisr(v: Long) = ((v shr sh) and 0x3F).let { b -> "%02X".format(b) + (if (b == 0L) "" else " (" + listOfNotNull(if (b and 8L != 0L) "pas de réponse" else null, if (b and 4L != 0L) "collision" else null,
            if (b and 2L != 0L) "débordement" else null, if (b and 1L != 0L) "manque" else null, if (b and 0x20L != 0L) "données neuves" else null, if (b and 0x10L != 0L) "écriture" else null).joinToString(", ") + ")") }
        fun comcsr(v: Long) = "%08X".format(v) + " (" + listOf(if (v and 0x80000000L != 0L) "fin de transfert" else "pas de fin", if (v and 0x40000000L != 0L) "interruption permise" else "interruption masquée",
            if (v and 0x20000000L != 0L) "ERREUR" else "pas d'erreur", "port ${((v shr 1) and 3) + 1}", if (v and 1L != 0L) "en cours" else "fini",
            "envoi ${(v shr 16) and 0x7F} / réponse ${(v shr 8) and 0x7F} octets").joinToString(", ") + ")"
        val dt = d[2]
        return buildString {
            append("\n=== Détail du port $port (ce que le jeu voit) ===\n")
            append("Transferts du jeu vers ce port : ${d[8]} réponses lues ; aiguillés vers une vraie manette : ${d[1]}\n")
            append("Dernier transfert : port ${((dt shr 24) and 3) + 1}, commande %02X, ${(dt shr 8) and 0x7F} octet(s) envoyés, ${dt and 0x7F} attendus\n".format((dt shr 16) and 0xFF))
            append("SIPOLL %08X : manettes lues en continu : %s\n".format(d[0], (0..3).filter { (d[0] shr (7 - it)) and 1L != 0L }.joinToString(", ") { "port ${it + 1}" }.ifEmpty { "aucune" }))
            append("SISR, vrai matériel : port $port = ${sisr(d[4])} ; rendu au jeu : ${sisr(d[5])}\n")
            append("COMCSR rendu au jeu (dernière lecture) : ${comcsr(d[6])}\n")
            append("Lectures de COMCSR avec « fin de transfert » passées par Pulse : ${d[7]}\n")
            append("Interruptions des manettes pendant un transfert vers ce port (console 858+) : $nIrq ; vrai COMCSR à ce moment : ${if (nIrq > 0) comcsr(d[3]) else "—"}\n")
            append("Dernière réponse rendue : %08X (%s)\n".format(d[9], if (d[9] shr 16 == 0x0004L) "GBA, JOYSTAT %02X".format((d[9] shr 8) and 0xFF) else "autre"))
            append("Pulse universel : " + when (trapEtat) { 0 -> "pas demandé"; 1 -> "ACTIF ($trapN accès pris)"; 2 -> "refusé (BAT)"; 3 -> "refusé (BAT 2)"; 4 -> "refusé (le jeu a sa propre table des pages)"
                5 -> "refusé (vecteur d'exception du jeu)"; 6 -> "refusé (table des pages pleine)"; 7 -> "perdu en cours de jeu"; else -> "état $trapEtat" } + "\n")
            typeJeu?.let { append(it) }
            append("Lecture : " + when {
                d[8] == 0L -> "le jeu n'envoie rien à ce port"
                nIrq == 0L && trapEtat != 1 -> "aucune interruption vue pendant les transferts vers la GBA (ou console d'avant le build 858)"
                nIrq > 0 && d[3] and 0x20000000L != 0L -> "le vrai matériel signale une ERREUR à la fin du transfert aiguillé"
                d[7] == 0L -> "la fin de transfert n'est pas lue par Pulse (accès du jeu non aiguillé)"
                ((d[5] shr sh) and 0x0F) != 0L -> "erreur encore visible pour le jeu (SISR)"
                d[9] shr 16 != 0x0004L -> "réponse rendue inattendue"
                else -> "réponse « GBA » rendue sans erreur"
            } + "\n")
        }
    }

    // build 378 : le type du port que garde la bibliotheque SI du jeu (adresses lues dans son main.dol), lu dans sa memoire
    // (chercheur de codes) : la vraie raison pour laquelle le jeu reconnait ou non la GBA
    @Volatile private var typeJeu: String? = null
    private var tType = 0L
    /** build 379 : Rayman 3 PAL : son automate de connexion GBA par port (0x803AEA50, 60 octets : etat, type, compteur, port)
     *  et le drapeau du joueur qui le fait tourner (0x803AEB40 + 84 x port + 81) : sur quel port le jeu attend la GBA */
    private fun rayman3(): String {
        val b1 = try { MemoireJeu.bloc(0x803AE800L) } catch (e: Exception) { null } ?: return "Rayman 3 : automate GBA illisible\n"
        val b2 = try { MemoireJeu.bloc(0x803AEC00L) } catch (e: Exception) { null }
        fun u8(a: Long): Int? = when { a in 0x803AE800L until 0x803AEC00L -> b1[(a - 0x803AE800L).toInt()].toInt() and 255
                                       b2 != null && a in 0x803AEC00L until 0x803AF000L -> b2[(a - 0x803AEC00L).toInt()].toInt() and 255; else -> null }
        fun u32(a: Long): Long = if (a in 0x803AE800L until 0x803AEBFDL) g32(b1, (a - 0x803AE800L).toInt()) else -1L
        return "Rayman 3, automate GBA par port : " + (0..3).joinToString(" ; ") { i ->
            val m = 0x803AEA50L + i * 60L
            val f = u8(0x803AEB40L + i * 84L + 81L)
            "port ${i + 1} : drapeau ${f ?: "?"} état ${u32(m)} type %08X compteur ${u32(m + 16)} port ${u8(m + 52) ?: "?"}".format(u32(m + 8))
        } + "\n"
    }
    private val TYPES = mapOf("GRHP41" to 0x80235868L, "G4SP01" to 0x804B5218L)   // (Rayman 3, Four Swords PAL : Type[4] ; Si.chan en -0x14)
    private fun lireType() {
        val id = Prive.jeuEnCours()
        val a = TYPES[id] ?: run { typeJeu = null; return }
        val now = System.currentTimeMillis()
        if (now - tType < 1500) return
        tType = now
        val b = try { MemoireJeu.bloc(a and 0x3FFL.inv()) } catch (e: Exception) { null } ?: run { typeJeu = "Type vu par le jeu ($id) : illisible (chercheur de codes activé ?)\n"; return }
        val o = (a and 0x3FFL).toInt()
        fun w(k: Int) = g32(b, o + k)
        fun lire(t: Long): String = when {
            t and 0x08L != 0L -> "PAS DE RÉPONSE"
            t and 0x47L != 0L -> "ERREUR %02X".format(t and 0xFF)
            t and 0xFFL != 0L -> "OCCUPÉ (demande en cours)"
            t and 0xFFFF0000L == 0x00040000L -> "GBA"
            t and 0xFFFF0000L == 0x09000000L -> "manette"
            t == 0L -> "rien"
            else -> "autre"
        }
        typeJeu = buildString {
            append("Type gardé par le jeu ($id, mémoire %08X) : ".format(a))
            append((0..3).joinToString(" ; ") { "port ${it + 1} = %08X %s".format(w(it * 4), lire(w(it * 4))) })
            append(" ; transfert en cours vers le port ${(w(-0x14) and 3) + 1} (%08X)\n".format(w(-0x14)))
            if (id == "GRHP41") append(rayman3())
        }
    }

    fun rapport(): String {
        val e = etat
        return buildString {
            append("GBA du téléphone (Tingle Tuner, essai)\n")
            append("Pulse vu : ${if (e.vuPulse) "oui" else "non"} ; étape ${e.etape} ; JOYSTAT %02X\n".format(e.joy))
            append("Programme annoncé ${e.annonce} octets, reçus par Pulse ${e.recus} ; commandes du jeu : 00/FF ${e.n00}, 14 ${e.n14}, 15 ${e.n15}\n")
            append("Programme : ${e.programme ?: "pas encore"}\n")
            append(detail())
            append("Émulateur GBA : ${if (GbaEmu.marche) "en marche" else "arrêté"} ${GbaEmu.message} ; mots du jeu donnés ${GbaEmu.nRecus}, mots de la GBA lus par le jeu ${GbaEmu.nEnvoyes} (+ ${GbaEmu.nAvales} déjà donnés par Pulse ; ${GbaEmu.nRedonnes} redonnés), en attente ${GbaEmu.entrees.size}${if (GbaEmu.retenus) " (retenus : la GBA n'a pas encore donné son code)" else ""}\nÉvénements de la GBA (build 392) :\n${GbaEmu.evts.joinToString("\n").ifEmpty { "aucun" }}\nCâble vu par la GBA : ${GbaEmu.regsVus} ; remises à zéro : dites par Pulse ${GbaEmu.resetsPulse}, traitées ${GbaEmu.resetsVus}\nRecherche du curseur (build 410) : ${GbaEmu.chercheTexte.ifEmpty { "pas faite" }}\nMessages du Tingle Tuner (build 415) :\n${GbaEmu.messagesTexte()}\nCurseur dans la vue 3D (build 417) : ${GbaEmu.pilotageDiag()}\nObjets apparus en jeu (build 427) : ${Prive.diagObjetsWW()}\nDerniers échanges avec la GBA (build 401) :\n${GbaEmu.traceTexte.ifEmpty { "aucun" }}\n\nJournal :\n")
            e.journal.forEach { append(it).append('\n') }
        }
    }
}

/** compagnon de Wind Waker : la GBA du telephone (Tingle Tuner, essai) ; build 373 : aussi pour les autres jeux (Four Swords...) */
@Composable
fun TingleTunerWW(ww: Boolean = true) {
    val ctx = LocalContext.current
    val e = GbaTel.etat
    // build 558 : carte d'accessoire (icone, etat) ; le mode d'emploi et la licence de l'emulateur sont replies
    val etatGba = when { GbaEmu.marche -> "En marche"; e.marche && e.vuPulse -> "Branchée"; e.marche -> "En attente"; else -> "Débranchée" }
    val coulGba = when { GbaEmu.marche || (e.marche && e.vuPulse) -> C.Vert; e.marche -> C.Orange; else -> C.Pale }
    CarteAccessoire(if (ww) "📡" else "🎮", if (ww) "Tingle Tuner" else "GBA du téléphone",
                    if (ww) "GBA du téléphone · port 2 · essai" else "Câble GameCube–GBA par le réseau · essai", Color(0xFF9BE15D), etatGba, coulGba) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            AideRepliable(if (ww) "Dans les réglages de GC Station : « GBA du téléphone » sur Port 2, puis lancer Wind Waker par Pulse. " +
                 "Brancher ici la GBA avant d'utiliser le Tingle Tuner dans le jeu."
                 else "Dans les réglages de GC Station : « GBA du téléphone » sur le port voulu (Rayman 3, Pac-Man Vs. : port 2 ; Four Swords seul : port 1), " +
                 "puis lancer le jeu par Pulse. Brancher ici la GBA, puis choisir dans le jeu le mode avec GBA : son programme tourne ici.")
            AideRepliable("Émulateur GBA : mGBA 0.10.5 (Jeffrey Pfau et contributeurs), licence Mozilla Public License 2.0. Code source : github.com/mgba-emu/mgba (fichiers repris tels quels). Aucun BIOS ni jeu fourni.", "Émulateur mGBA · licence")   // (build 457 : licence demandee pour la version publique ; toujours la)
            if (!ww) Cartouche.Choix()                          // (build 556, appli privee : cartouche .gba + .sav dans la GBA)
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
                if (GbaEmu.marche) EcranGba()                  // (build 375 : l'emulateur GBA)
                // build 410 : recherche du curseur du Tingle Tuner (pour le relier a la vue 3D)
                if (GbaEmu.marche) Bouton(if (GbaEmu.chercheEnCours) "Recherche du curseur…" else "Chercher le curseur (écran de la carte)", Modifier.fillMaxWidth(),
                                          actif = !GbaEmu.chercheEnCours) { GbaEmu.chercheDemande = true }
                if (GbaEmu.marche && GbaEmu.curseurTexte.isNotEmpty()) Doux(GbaEmu.curseurTexte, taille = 12, couleur = Color(0xFF9BE15D))
                if (GbaEmu.marche && GbaEmu.pilotageTexte.isNotEmpty()) Doux(GbaEmu.pilotageTexte, taille = 12, couleur = Color(0xFFFFC857))
                if (GbaEmu.marche) Prive.curseurTT()?.let { Doux("Curseur dans le jeu (vue 3D) : %.0f, %.0f".format(it[0], it[2]), taille = 11, couleur = C.Pale) }
                if (GbaEmu.marche && GbaEmu.curseurTexte.isNotEmpty()) Bouton("Curseur posé sur Link : noter ce repère (${GbaEmu.nReperes})", Modifier.fillMaxWidth()) { GbaEmu.noterRepere() }
                if (GbaTel.aProgramme && !GbaEmu.marche) Bouton("Relancer la GBA", Modifier.fillMaxWidth()) { GbaTel.relancerEmu() }
                // build 558 : les details techniques replies
                Repliable("Détails techniques") {
                    Doux("Commandes du jeu : état ${e.n00} · lectures ${e.n14} · écritures ${e.n15}", taille = 11, couleur = C.Pale)
                    if (GbaEmu.message.isNotEmpty()) Doux(GbaEmu.message + " · mots du jeu ${GbaEmu.nRecus} · de la GBA ${GbaEmu.nEnvoyes}", taille = 11, couleur = C.Pale)
                    e.journal.takeLast(12).forEach { Doux(it, taille = 11) }
                }
                Bouton("Envoyer le rapport", Modifier.fillMaxWidth()) {
                    val i = Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, GbaTel.rapport())
                    try { ctx.startActivity(Intent.createChooser(i, "Rapport GBA").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } catch (x: Exception) { }
                }
            }
        }
    }
}
