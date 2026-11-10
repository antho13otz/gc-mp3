package fr.gcstation.telecommande

// Appli privee build 499 : spectateurs 3D avec leur propre camera (Wind Waker). Idee venue de Reddit : regarder quelqu'un
// jouer et pouvoir tourner la camera soi-meme. Au lieu d'une video (Partage.kt, build 495-497), le joueur envoie l'etat du
// jeu tel que sa vue 3D le rejoue : lieu, position et pose de Link (42 matrices), camera du jeu, curseur du Tingle Tuner,
// bateau et voile, objets animes. Chaque spectateur redessine la scene avec SES packs Wind Waker (rien du jeu ne passe par
// le reseau, seulement des nombres) et tourne la camera comme il veut. Plusieurs spectateurs a la fois (8 au plus).
// - Rencontre comme Partage : code de 6 lettres, point de rendez-vous MQTT public (adresses seulement), STUN, percage UDP.
//   L'hote reste au rendez-vous pendant toute la session : un ami peut arriver a tout moment.
// - Trames : ~30 par seconde, compressees (Deflater), coupees en morceaux de 1100 octets ; une trame incomplete est jetee.
// - Le spectateur garde les dernieres trames et rejoue avec ~100 ms de retard, en glissant de l'une a l'autre.

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress
import java.nio.ByteBuffer

/** une trame de l'etat du jeu (repere du jeu pour pos ; repere de l'appli pour curseur) */
class TrameSpect(
    val t: Int,                          // (ms depuis le debut du partage, horloge de l'hote)
    val lieu: String, val cap: Float, val vitesse: Float, val nage: Boolean,
    val pos: FloatArray, val pose: FloatArray, val cam: FloatArray?, val curseur: FloatArray?,
    val vague: FloatArray?, val voile: FloatArray?, val vifs: List<VifPoseWW>, val modeles: IntArray)

object CodecSpect {
    private const val VERSION = 1
    private const val BUDGET_VIFS = 60_000                  // (octets d'objets au plus par trame, avant compression)
    fun ecrire(f: TrameSpect): ByteArray {
        var taille = 64 + f.lieu.length * 3 + 4 * (f.pos.size + f.pose.size + (f.cam?.size ?: 0) + (f.curseur?.size ?: 0) + (f.vague?.size ?: 0) + (f.voile?.size ?: 0)) + 4 * f.modeles.size
        val vifs = ArrayList<VifPoseWW>(); var bv = 0
        for (v in f.vifs) { val n = 7 + 4 * v.m.size; if (bv + n > BUDGET_VIFS) break; vifs.add(v); bv += n }
        taille += bv
        val b = ByteBuffer.allocate(taille)
        b.put(VERSION.toByte()); b.putInt(f.t)
        val l = f.lieu.toByteArray(Charsets.UTF_8).copyOf(minOf(255, f.lieu.toByteArray(Charsets.UTF_8).size))
        b.put(l.size.toByte()); b.put(l)
        b.putFloat(f.cap); b.putFloat(f.vitesse); b.put(if (f.nage) 1 else 0)
        fun tab(a: FloatArray?, court: Boolean) { val n = a?.size ?: 0; if (court) b.put(n.toByte()) else b.putShort(n.toShort()); if (a != null) for (x in a) b.putFloat(x) }
        tab(f.pos, true); tab(f.pose, false); tab(f.cam, true); tab(f.curseur, true); tab(f.vague, true); tab(f.voile, false)
        b.putShort(vifs.size.toShort())
        for (v in vifs) { b.putInt(v.modele); b.put(if (v.plein) 1 else 0); b.putShort(v.m.size.toShort()); for (x in v.m) b.putFloat(x) }
        b.putShort(f.modeles.size.toShort()); for (m in f.modeles) b.putInt(m)
        val brut = b.array().copyOf(b.position())
        val d = java.util.zip.Deflater(1); d.setInput(brut); d.finish()
        val o = java.io.ByteArrayOutputStream(brut.size / 2 + 64); val tmp = ByteArray(16384)
        while (!d.finished()) { val k = d.deflate(tmp); o.write(tmp, 0, k) }
        d.end()
        return o.toByteArray()
    }
    fun lire(z: ByteArray): TrameSpect? = try {
        val inf = java.util.zip.Inflater(); inf.setInput(z)
        val o = java.io.ByteArrayOutputStream(z.size * 3); val tmp = ByteArray(16384)
        while (!inf.finished()) { val k = inf.inflate(tmp); if (k == 0 && (inf.needsInput() || inf.needsDictionary())) break; o.write(tmp, 0, k); if (o.size() > 4_000_000) break }
        inf.end()
        val b = ByteBuffer.wrap(o.toByteArray())
        if (b.get().toInt() != VERSION) null else {
            val t = b.getInt()
            val ll = b.get().toInt() and 255; val lb = ByteArray(ll); b.get(lb)
            val cap = b.getFloat(); val vit = b.getFloat(); val nage = b.get().toInt() == 1
            fun tab(court: Boolean): FloatArray? { val n = if (court) b.get().toInt() and 255 else b.getShort().toInt() and 0xFFFF; return if (n == 0) null else FloatArray(n) { b.getFloat() } }
            val pos = tab(true) ?: FloatArray(3); val pose = tab(false) ?: FloatArray(0)
            val cam = tab(true); val cur = tab(true); val vag = tab(true); val voi = tab(false)
            val nv = b.getShort().toInt() and 0xFFFF
            val vifs = ArrayList<VifPoseWW>(nv)
            repeat(nv) { val mo = b.getInt(); val pl = b.get().toInt() == 1; val n = b.getShort().toInt() and 0xFFFF; vifs.add(VifPoseWW(mo, pl, FloatArray(n) { b.getFloat() })) }
            val nm = b.getShort().toInt() and 0xFFFF
            val mods = IntArray(nm) { b.getInt() }
            TrameSpect(t, String(lb, Charsets.UTF_8), cap, vit, nage, pos, pose, cam, cur, vag, voi, vifs, mods)
        }
    } catch (e: Exception) { null }

    /** morceaux UDP : 'G' 'S', trame (4), morceau (2), nombre (2), puis 1100 octets au plus */
    const val MORCEAU = 1100
    fun morceaux(seq: Int, d: ByteArray): List<ByteArray> {
        val n = maxOf(1, (d.size + MORCEAU - 1) / MORCEAU)
        return List(n) { k ->
            val o = k * MORCEAU; val l = minOf(MORCEAU, d.size - o)
            val p = ByteArray(10 + l)
            p[0] = 'G'.code.toByte(); p[1] = 'S'.code.toByte()
            p[2] = (seq shr 24).toByte(); p[3] = (seq shr 16).toByte(); p[4] = (seq shr 8).toByte(); p[5] = seq.toByte()
            p[6] = (k shr 8).toByte(); p[7] = k.toByte(); p[8] = (n shr 8).toByte(); p[9] = n.toByte()
            System.arraycopy(d, o, p, 10, l); p
        }
    }
    /** reassemblage : une trame a la fois ; une plus recente remplace l'incomplete */
    class Assembleur {
        private var cour = -1; private var parts: Array<ByteArray?> = arrayOfNulls(0)
        fun recu(p: ByteArray, n0: Int): ByteArray? {
            if (n0 < 10 || p[0] != 'G'.code.toByte() || p[1] != 'S'.code.toByte()) return null
            fun u16(o: Int) = (p[o].toInt() and 255 shl 8) or (p[o + 1].toInt() and 255)
            val seq = ((p[2].toInt() and 255) shl 24) or ((p[3].toInt() and 255) shl 16) or ((p[4].toInt() and 255) shl 8) or (p[5].toInt() and 255)
            val k = u16(6); val n = u16(8)
            if (n !in 1..4000 || k >= n) return null
            if (seq < cour) return null
            if (seq > cour) { cour = seq; parts = arrayOfNulls(n) }
            if (parts.size != n) return null
            parts[k] = p.copyOfRange(10, n0)
            if (parts.any { it == null }) return null
            val d = ByteArray(parts.sumOf { it!!.size }); var o = 0
            for (q in parts) { System.arraycopy(q!!, 0, d, o, q.size); o += q.size }
            parts = arrayOfNulls(0); cour = seq + 1
            return d
        }
    }
}

object Spect3D {
    private const val BROKER = "broker.hivemq.com"
    private const val BROKER_PORT = 1883
    private const val MAX = 8
    var etat by mutableStateOf("")
    var code by mutableStateOf("")
    var hote by mutableStateOf(false)
    var spectateur by mutableStateOf(false)
    var nbSpect by mutableIntStateOf(0)
    var lieu by mutableStateOf("")                           // (spectateur : lieu de la derniere trame)
    @Volatile private var actif = false
    @Volatile private var udp: DatagramSocket? = null
    private val ui by lazy { android.os.Handler(android.os.Looper.getMainLooper()) }
    private fun dire(t: String) { ui.post { etat = t } }
    private fun sujet(c: String) = "gcremote/spect3d/v1/" + c
    private fun nouveauCode(): String { val l = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; val r = java.security.SecureRandom(); return String(CharArray(6) { l[r.nextInt(l.length)] }) }

    fun partager() {
        if (actif || !BuildConfig.PRIVE) return
        actif = true; hote = true; spectateur = false; nbSpect = 0; code = nouveauCode()
        val c = code; t0 = System.nanoTime()
        Thread { try { sessionHote(c) } catch (e: Exception) { dire("Spectateurs arrêtés : " + (e.message ?: e.javaClass.simpleName)) } finally { fin() } }.apply { isDaemon = true; name = "spect3d-hote" }.start()
    }
    fun regarder(c: String) {
        if (actif || !BuildConfig.PRIVE) return
        actif = true; hote = false; spectateur = true; code = c.trim().uppercase()
        synchronized(trames) { trames.clear() }; decal = Long.MAX_VALUE
        val cc = code
        Thread { try { sessionSpect(cc) } catch (e: Exception) { dire("Connexion arrêtée : " + (e.message ?: e.javaClass.simpleName)) } finally { fin() } }.apply { isDaemon = true; name = "spect3d-vue" }.start()
    }
    fun arreter() { actif = false; try { udp?.close() } catch (e: Exception) { } }
    private fun fin() { actif = false; try { udp?.close() } catch (e: Exception) { }; udp = null; ui.post { hote = false; spectateur = false; nbSpect = 0 } }

    private fun cibles(s: String): List<InetSocketAddress> = s.split('|').filter { it.contains(':') }.mapNotNull { a -> val i = a.lastIndexOf(':'); a.substring(i + 1).toIntOrNull()?.let { p -> try { InetSocketAddress(a.substring(0, i), p) } catch (e: Exception) { null } } }
    private fun maAdresse(s: DatagramSocket): String {
        val pub = Partage.stun(s) ?: throw Exception("adresse publique introuvable (pas d'Internet ?)")
        val loc = Partage.adresseLocale()?.let { InetSocketAddress(it, s.localPort) }
        return pub.address.hostAddress + ":" + pub.port + "|" + (loc?.let { it.address.hostAddress + ":" + it.port } ?: "")
    }

    // ------------------------------------------------------------------ l'hote
    @Volatile private var t0 = 0L
    @Volatile private var aEnvoyer: ByteArray? = null
    private var dernCapture = 0L
    private class Pair1(val adr: InetSocketAddress) { @Volatile var vu = System.currentTimeMillis() }
    private class APercer(val cibles: List<InetSocketAddress>, val depuis: Long = System.currentTimeMillis())

    /** appele par la vue 3D de Wind Waker a chaque image rejouee (fil OpenGL) : 30 trames par seconde au plus */
    fun capturer(lieu: String, cap: Float, vitesse: Float, nage: Boolean, pos: FloatArray, pose: FloatArray, cam: FloatArray?,
                 curseur: FloatArray?, vague: FloatArray?, voile: FloatArray?, vifs: List<VifPoseWW>?, modeles: Set<Int>) {
        if (!hote || !actif || nbSpect == 0) return
        val n = System.nanoTime()
        if (n - dernCapture < 33_000_000L) return
        dernCapture = n
        try {
            aEnvoyer = CodecSpect.ecrire(TrameSpect(((n - t0) / 1_000_000L).toInt(), lieu, cap, vitesse, nage, pos.copyOf(), pose.copyOf(), cam?.copyOf(),
                curseur?.copyOf(), vague?.copyOf(), voile?.copyOf(), vifs ?: emptyList(), modeles.toIntArray()))
        } catch (e: Exception) { }
    }

    private fun sessionHote(c: String) {
        val s = DatagramSocket(); s.soTimeout = 2; udp = s
        dire("Recherche de ton adresse sur Internet…")
        val moi = maAdresse(s)
        val arrivees = java.util.concurrent.ConcurrentLinkedQueue<String>()
        val m = MiniMqtt(BROKER, BROKER_PORT, "gcs" + System.nanoTime().toString(36)) { _, data -> arrivees.add(String(data, Charsets.UTF_8)) }
        m.abonner(sujet(c) + "/spectateur")
        m.publier(sujet(c) + "/hote", moi.toByteArray(), retenu = true)
        dire("Code : $c  ·  en attente de spectateurs…")
        val pairs = ArrayList<Pair1>(); val percages = ArrayList<APercer>()
        val buf = ByteArray(2048); val p = DatagramPacket(buf, buf.size)
        var seq = 0; var dernPercage = 0L; var dernSigne = 0L; var dernMqtt = 0L
        val fin = System.currentTimeMillis() + 3 * 3600_000L
        try {
            while (actif && System.currentTimeMillis() < fin) {
                val now = System.currentTimeMillis()
                // 1. le rendez-vous : de nouveaux spectateurs
                if (now - dernMqtt > 200) { dernMqtt = now; m.lire(1) }
                while (true) {
                    val a = arrivees.poll() ?: break
                    val cs = cibles(a)
                    if (cs.isNotEmpty() && percages.none { it.cibles == cs } && pairs.size < MAX) { percages.add(APercer(cs)); m.publier(sujet(c) + "/hote", moi.toByteArray(), retenu = true) }
                }
                // 2. percage vers ceux qui arrivent
                percages.removeAll { now - it.depuis > 20_000 }
                if (now - dernPercage > 150 && percages.isNotEmpty()) { dernPercage = now
                    for (q in percages) for (a in q.cibles) try { s.send(DatagramPacket(byteArrayOf('G'.code.toByte(), 'P'.code.toByte()), 2, a)) } catch (e: Exception) { } }
                // 3. ce qui arrive : signes de vie, nouveaux pairs
                try {
                    while (true) {
                        p.length = buf.size; s.receive(p)
                        if (p.length < 2 || buf[0] != 'G'.code.toByte()) continue
                        val a = InetSocketAddress(p.address, p.port)
                        val ex = pairs.firstOrNull { it.adr == a }
                        if (ex != null) ex.vu = now
                        else if (pairs.size < MAX) {
                            pairs.add(Pair1(a)); percages.removeAll { q -> q.cibles.any { it.port == a.port && it.address == a.address } }
                            repeat(3) { try { s.send(DatagramPacket(byteArrayOf('G'.code.toByte(), 'P'.code.toByte()), 2, a)) } catch (e: Exception) { } }
                        }
                    }
                } catch (e: java.net.SocketTimeoutException) { }
                if (pairs.removeAll { now - it.vu > 10_000 } || pairs.size != nbSpect) {
                    val n = pairs.size; ui.post { nbSpect = n }
                    dire(if (n == 0) "Code : $c  ·  en attente de spectateurs…" else "Code : $c  ·  $n spectateur" + (if (n > 1) "s" else ""))
                }
                if (now - dernSigne > 1000) { dernSigne = now
                    for (q in pairs) try { s.send(DatagramPacket(byteArrayOf('G'.code.toByte(), 'A'.code.toByte()), 2, q.adr)) } catch (e: Exception) { } }
                // 4. la derniere trame, a tous
                val d = aEnvoyer
                if (d != null && pairs.isNotEmpty()) {
                    aEnvoyer = null
                    val ms = CodecSpect.morceaux(++seq, d)
                    for (q in pairs) for (x in ms) try { s.send(DatagramPacket(x, x.size, q.adr)) } catch (e: Exception) { }
                }
            }
        } finally {
            try { m.publier(sujet(c) + "/hote", ByteArray(0), retenu = true) } catch (e: Exception) { }
            m.fermer()
        }
    }

    // ------------------------------------------------------------------ le spectateur
    private val trames = ArrayDeque<Pair2>()
    private class Pair2(val f: TrameSpect, val recu: Long)
    @Volatile private var decal = Long.MAX_VALUE              // (ms : heure locale - heure de l'hote, la plus petite vue)
    private const val RETARD = 110L

    private fun sessionSpect(c: String) {
        val s = DatagramSocket(); s.soTimeout = 300; udp = s
        dire("Recherche de ton adresse sur Internet…")
        val moi = maAdresse(s)
        var hoteAdr: String? = null
        val m = MiniMqtt(BROKER, BROKER_PORT, "gcv" + System.nanoTime().toString(36)) { _, data -> if (data.isNotEmpty()) hoteAdr = String(data, Charsets.UTF_8) }
        var pair: InetSocketAddress? = null
        try {
            m.abonner(sujet(c) + "/hote")
            dire("Recherche du partage $c…")
            val buf = ByteArray(2048); val p = DatagramPacket(buf, buf.size)
            var dernPub = 0L; val t0c = System.currentTimeMillis()
            while (actif && pair == null) {
                val now = System.currentTimeMillis()
                if (now - t0c > 60_000) throw Exception(if (hoteAdr == null) "aucun partage avec ce code" else "la connexion directe n'a pas pu se faire (réseau trop fermé, souvent en 4G/5G)")
                if (now - dernPub > 3000) { dernPub = now; m.publier(sujet(c) + "/spectateur", moi.toByteArray(), retenu = false) }
                m.lire(100)
                val h = hoteAdr
                if (h != null) {
                    if (etat.startsWith("Recherche du")) dire("Connexion directe…")
                    for (a in cibles(h)) try { s.send(DatagramPacket(byteArrayOf('G'.code.toByte(), 'P'.code.toByte()), 2, a)) } catch (e: Exception) { }
                    try { s.receive(p); if (p.length >= 2 && buf[0] == 'G'.code.toByte()) pair = InetSocketAddress(p.address, p.port) } catch (e: java.net.SocketTimeoutException) { }
                }
            }
        } finally { m.fermer() }
        val hp = pair ?: return
        repeat(5) { try { s.send(DatagramPacket(byteArrayOf('G'.code.toByte(), 'A'.code.toByte()), 2, hp)) } catch (e: Exception) { } }
        dire("Connecté · en attente du jeu (le joueur doit avoir sa vue 3D ouverte)…")
        val buf = ByteArray(2048); val p = DatagramPacket(buf, buf.size); s.soTimeout = 50
        val asm = CodecSpect.Assembleur()
        var dernSigne = 0L; var dernRecu = System.currentTimeMillis(); var n = 0
        while (actif) {
            val now = System.currentTimeMillis()
            if (now - dernSigne > 1000) { dernSigne = now; try { s.send(DatagramPacket(byteArrayOf('G'.code.toByte(), 'A'.code.toByte()), 2, hp)) } catch (e: Exception) { } }
            if (now - dernRecu > 15_000) { dire("Plus rien ne vient : le partage s'est arrêté"); break }
            try { p.length = buf.size; s.receive(p) } catch (e: java.net.SocketTimeoutException) { continue }
            if (p.length >= 2 && buf[0] == 'G'.code.toByte() && buf[1] == 'A'.code.toByte()) { dernRecu = System.currentTimeMillis(); continue }
            val d = asm.recu(buf, p.length) ?: continue
            val f = CodecSpect.lire(d) ?: continue
            dernRecu = System.currentTimeMillis()
            val ici = System.currentTimeMillis()
            decal = minOf(decal, ici - f.t)
            synchronized(trames) { trames.addLast(Pair2(f, ici)); while (trames.size > 12) trames.removeFirst() }
            if (f.lieu != lieu) { val l = f.lieu; ui.post { lieu = l } }
            if (++n == 1) dire("En direct · tourne la caméra comme tu veux")
        }
    }

    /** la trame a montrer maintenant (~110 ms de retard), glissee entre les deux qui l'entourent */
    fun maintenant(): TrameSpect? {
        val (a, b) = synchronized(trames) {
            if (trames.isEmpty() || decal == Long.MAX_VALUE) return null
            val th = System.currentTimeMillis() - decal - RETARD
            val i = trames.indexOfFirst { it.f.t >= th }
            when { i < 0 -> trames.last().f to trames.last().f; i == 0 -> trames.first().f to trames.first().f; else -> trames[i - 1].f to trames[i].f }
        }.let { it.first to it.second }
        if (a === b || a.lieu != b.lieu) return b
        val th = System.currentTimeMillis() - decal - RETARD
        val k = ((th - a.t).toFloat() / maxOf(1, b.t - a.t)).coerceIn(0f, 1f)
        fun mix(x: FloatArray?, y: FloatArray?): FloatArray? = if (x == null || y == null || x.size != y.size) y ?: x else FloatArray(x.size) { x[it] + (y[it] - x[it]) * k }
        fun normer(m: FloatArray, n: Int) { for (j in 0 until n) for (r in 0..2) { val o = j * 12 + r * 4; if (o + 2 >= m.size) return
            val l = Math.sqrt((m[o] * m[o] + m[o + 1] * m[o + 1] + m[o + 2] * m[o + 2]).toDouble()).toFloat()
            if (l > 1e-4f && Math.abs(l - 1f) < 0.2f) { m[o] /= l; m[o + 1] /= l; m[o + 2] /= l } } }
        val pose = mix(a.pose, b.pose)!!.also { normer(it, it.size / 12) }
        val vifs = if (a.vifs.size == b.vifs.size && a.vifs.indices.all { a.vifs[it].modele == b.vifs[it].modele && a.vifs[it].m.size == b.vifs[it].m.size })
            a.vifs.indices.map { i -> val m = mix(a.vifs[i].m, b.vifs[i].m)!!; normer(m, m.size / 12); VifPoseWW(b.vifs[i].modele, b.vifs[i].plein, m) } else b.vifs
        var dc = b.cap - a.cap; while (dc > Math.PI) dc -= (2 * Math.PI).toFloat(); while (dc < -Math.PI) dc += (2 * Math.PI).toFloat()
        return TrameSpect(th.toInt(), b.lieu, a.cap + dc * k, a.vitesse + (b.vitesse - a.vitesse) * k, b.nage, mix(a.pos, b.pos)!!, pose,
            mix(a.cam, b.cam), mix(a.curseur, b.curseur), mix(a.vague, b.vague), mix(a.voile, b.voile), vifs, b.modeles)
    }

    /** spectateur : applique la trame du moment a la vue 3D (appele avant chaque image) */
    fun appliquer(et: Etat3D) {
        val f = maintenant() ?: run { et.directRejeu = false; return }
        val t = System.nanoTime()
        et.directRejeu = true
        if (f.pose.size == 42 * 12) { et.poseWW = f.pose; et.tPoseWW = t }
        et.cible = CartesWW.pos(f.pos[0], f.pos[1], f.pos[2]); et.direct = true
        f.cam?.let { et.camJeu = it; et.tCamJeu = t }
        et.linkWW = floatArrayOf(f.cap, f.vitesse, if (f.nage) 1f else 0f)
        et.capPerso = Math.atan2(-Math.cos(f.cap.toDouble()), Math.sin(f.cap.toDouble())).toFloat()
        et.curseurTT = f.curseur
        et.vagueWW = f.vague
        et.voileWW = f.voile
        et.vifsWW = f.vifs.takeIf { it.isNotEmpty() }
        et.modelesVifs = if (f.vifs.isEmpty()) emptySet() else f.modeles.toHashSet().also { s -> f.vifs.forEach { s.add(it.modele) } }
    }
    fun derniere(): TrameSpect? = synchronized(trames) { trames.lastOrNull()?.f }
}

/** le bouton de l'hote, par-dessus la vue 3D (appli privee), sous celui du partage video */
@Composable
fun BoutonSpect3D() {
    if (Spect3D.spectateur) return
    if (Spect3D.hote) {
        Text("👥 Code : " + Spect3D.code, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.Bold)
        Doux(if (Spect3D.nbSpect > 0) "${Spect3D.nbSpect} spectateur" + (if (Spect3D.nbSpect > 1) "s" else "") + " · caméra libre" else Spect3D.etat, taille = 11, couleur = C.Cyan)
        Bouton("Arrêter les spectateurs", hauteur = 34.dp) { Spect3D.arreter() }
    } else Bouton("👥 Spectateurs 3D", hauteur = 34.dp, actif = !Partage.spectateur) { Spect3D.partager() }
}

/** l'ecran du spectateur (Plus > En jeu > Regarder en 3D) : le code, puis la scene en 3D avec sa propre camera */
@Composable
fun EcranRegarder3D() {
    val ctx = LocalContext.current
    if (CartesWW.dossier == null) CartesWW.dossier = java.io.File(ctx.filesDir, "cartes-ww")
    var c by remember { mutableStateOf("") }
    Column(Modifier.fillMaxSize().padding(horizontal = 14.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Carte(Modifier.fillMaxWidth()) {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Titre("Regarder un ami en 3D, ta caméra")
                Doux("Ton ami ouvre sa vue 3D de Wind Waker et touche « 👥 Spectateurs 3D » : un code s'affiche. Ici, ce n'est pas une vidéo : ton téléphone redessine le jeu avec tes packs Wind Waker, et tu tournes la caméra comme tu veux. Plusieurs amis peuvent regarder en même temps.", taille = 12)
                if (!Spect3D.spectateur) {
                    OutlinedTextField(c, { c = it.uppercase().filter { ch -> ch.isLetterOrDigit() }.take(6) }, Modifier.fillMaxWidth(), singleLine = true, label = { Text("Code de l'ami") })
                    Bouton("Regarder", Modifier.fillMaxWidth(), plein = true, actif = c.length == 6 && !Spect3D.hote) { Spect3D.regarder(c) }
                } else Bouton("Arrêter", Modifier.fillMaxWidth()) { Spect3D.arreter() }
                if (Spect3D.etat.isNotEmpty()) Doux(Spect3D.etat, taille = 12, couleur = C.Cyan)
            }
        }
        val l = Spect3D.lieu
        if (Spect3D.spectateur && l.isNotEmpty()) {
            val sc = remember(l) { CartesWW.scene(l) }
            if (sc == null) Doux("Il te faut le pack Wind Waker de ce lieu (" + CartesWW.nom(l) + ") : installe-le depuis la fiche du jeu.", taille = 12, couleur = C.Orange)
            else {
                Titre("Chez ton ami : " + CartesWW.nom(l))
                val f0 = remember(l) { Spect3D.derniere() }
                val p0 = f0?.let { CartesWW.pos(it.pos[0], it.pos[1], it.pos[2]) } ?: sc.centre
                DessinVue3DGL(sc, p0, null, emptyList(), montrerRayman = true, distance = 22f,
                    linkWW = f0?.let { floatArrayOf(it.cap, it.vitesse, if (it.nage) 1f else 0f) },
                    camJeu = { null }, rejeu = { et -> Spect3D.appliquer(et) }, spectateur = true)
            }
        }
    }
    DisposableEffect(Unit) { onDispose { if (Spect3D.spectateur) Spect3D.arreter() } }
}
