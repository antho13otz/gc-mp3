package fr.gcstation.telecommande

// Appli privee build 495 (496 : 60 images/s ; 497 : 30 images/s, ~2 Mbit/s) : partager sa vue 3D en video avec un ami qui a aussi GC Remote, chez lui, par Internet.
// - L'hote filme seulement sa vue 3D (PixelCopy de la surface OpenGL, 15 images/s), la compresse en H.264 (MediaCodec,
//   ~1,2 Mbit/s) et l'envoie en UDP, en paquets de 1100 octets.
// - Rencontre sans serveur a nous : un code de 6 lettres ; les deux telephones se trouvent par un point de rendez-vous
//   public et gratuit (MQTT, broker.hivemq.com : seulement les adresses passent par lui, jamais la video), apprennent leur
//   adresse publique (STUN de Google) et ouvrent un passage direct l'un vers l'autre (« percage » UDP, comme les appels
//   video). Peut echouer derriere certains reseaux mobiles tres fermes : l'appli le dit.
// - Le spectateur reassemble les images, les decode sur sa surface ; une image perdue : il demande une image complete.

import android.graphics.Bitmap
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.view.SurfaceView
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.aspectRatio
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import java.io.InputStream
import java.io.OutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket

object Partage {
    private const val BROKER = "broker.hivemq.com"
    private const val BROKER_PORT = 1883
    private const val STUN = "stun.l.google.com"
    private const val STUN_PORT = 19302
    private const val MORCEAU = 1100
    private const val IPS = 30                            // (build 497 : 30 images par seconde, comme le jeu (Wind Waker) ; 496 : 60)
    private val natif = try { System.loadLibrary("rapont"); true } catch (e: Throwable) { false }
    private external fun nYuv(argb: IntArray, w: Int, h: Int, y: java.nio.ByteBuffer, ys: Int, yp: Int, u: java.nio.ByteBuffer, us: Int, up: Int, v: java.nio.ByteBuffer, vs: Int, vp: Int)

    var etat by mutableStateOf("")
    var code by mutableStateOf("")
    var hote by mutableStateOf(false)                      // (on partage)
    var spectateur by mutableStateOf(false)                // (on regarde)
    @Volatile var vue: java.lang.ref.WeakReference<SurfaceView>? = null   // (la vue 3D affichee : Vue3DGL)
    @Volatile var ecran: android.view.Surface? = null       // (surface du spectateur)
    @Volatile private var actif = false
    @Volatile private var udp: DatagramSocket? = null
    @Volatile private var pair: InetSocketAddress? = null
    @Volatile private var mqtt: MiniMqtt? = null
    private val ui by lazy { android.os.Handler(android.os.Looper.getMainLooper()) }
    private fun dire(t: String) { ui.post { etat = t } }

    private fun nouveauCode(): String { val l = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; val r = java.security.SecureRandom(); return String(CharArray(6) { l[r.nextInt(l.length)] }) }
    private fun sujet(c: String) = "gcremote/partage/v1/" + c

    // ------------------------------------------------------------------ demarrer / arreter
    fun partager() {
        if (actif) return
        actif = true; hote = true; spectateur = false; code = nouveauCode()
        val c = code
        Thread { try { session(c, true) } catch (e: Exception) { dire("Partage arrêté : " + (e.message ?: e.javaClass.simpleName)) } finally { fin() } }.apply { isDaemon = true; name = "partage" }.start()
    }
    fun regarder(c: String) {
        if (actif) return
        actif = true; hote = false; spectateur = true; code = c.trim().uppercase()
        val cc = code
        Thread { try { session(cc, false) } catch (e: Exception) { dire("Connexion arrêtée : " + (e.message ?: e.javaClass.simpleName)) } finally { fin() } }.apply { isDaemon = true; name = "regarder" }.start()
    }
    fun arreter() { actif = false; try { udp?.close() } catch (e: Exception) { }; try { mqtt?.fermer() } catch (e: Exception) { } }
    private fun fin() { actif = false; try { udp?.close() } catch (e: Exception) { }; try { mqtt?.fermer() } catch (e: Exception) { }; udp = null; mqtt = null; pair = null
        ui.post { hote = false; spectateur = false } }

    // ------------------------------------------------------------------ la rencontre
    private fun session(c: String, estHote: Boolean) {
        val s = DatagramSocket(); s.soTimeout = 300; udp = s
        dire("Recherche de ton adresse sur Internet…")
        val pub = stun(s) ?: throw Exception("adresse publique introuvable (pas d'Internet ?)")
        val loc = adresseLocale()?.let { InetSocketAddress(it, s.localPort) }
        val moi = pub.address.hostAddress + ":" + pub.port + "|" + (loc?.let { it.address.hostAddress + ":" + it.port } ?: "")
        dire(if (estHote) "Code : $c  ·  en attente de ton ami…" else "Recherche de l'ami $c…")
        var autre: String? = null
        val m = MiniMqtt(BROKER, BROKER_PORT, "gcr" + System.nanoTime().toString(36)) { _, data -> autre = String(data, Charsets.UTF_8) }
        mqtt = m
        m.abonner(sujet(c) + if (estHote) "/spectateur" else "/hote")
        if (estHote) m.publier(sujet(c) + "/hote", moi.toByteArray(), retenu = true)
        var dernierEnvoi = 0L
        val fin = System.currentTimeMillis() + if (estHote) 15 * 60_000L else 60_000L
        while (actif && autre == null) {
            if (System.currentTimeMillis() > fin) throw Exception(if (estHote) "personne n'est venu en 15 min" else "aucun partage avec ce code")
            if (!estHote && System.currentTimeMillis() - dernierEnvoi > 3000) { m.publier(sujet(c) + "/spectateur", moi.toByteArray(), retenu = false); dernierEnvoi = System.currentTimeMillis() }
            m.lire(500)
        }
        if (!actif) return
        if (estHote) m.publier(sujet(c) + "/spectateur", moi.toByteArray(), retenu = false)   // (le spectateur apprend aussi l'hote)
        // percage : des petits paquets vers son adresse publique et locale, jusqu'a recevoir les siens
        val cibles = autre!!.split('|').filter { it.contains(':') }.mapNotNull { a -> val i = a.lastIndexOf(':'); a.substring(i + 1).toIntOrNull()?.let { InetSocketAddress(a.substring(0, i), it) } }
        dire("Connexion directe…")
        val buf = ByteArray(2048); val p = DatagramPacket(buf, buf.size)
        val t0 = System.currentTimeMillis()
        while (actif && pair == null) {
            if (System.currentTimeMillis() - t0 > 20_000) throw Exception("la connexion directe n'a pas pu se faire (réseau trop fermé, souvent en 4G/5G)")
            for (a in cibles) try { s.send(DatagramPacket(byteArrayOf('G'.code.toByte(), 'P'.code.toByte()), 2, a)) } catch (e: Exception) { }
            try { s.receive(p); if (p.length >= 2 && buf[0] == 'G'.code.toByte()) pair = InetSocketAddress(p.address, p.port) } catch (e: java.net.SocketTimeoutException) { }
            m.lire(1)
        }
        if (!actif) return
        repeat(5) { try { s.send(DatagramPacket(byteArrayOf('G'.code.toByte(), 'P'.code.toByte()), 2, pair)) } catch (e: Exception) { } }
        if (estHote) m.publier(sujet(c) + "/hote", ByteArray(0), retenu = true)            // (rendez-vous efface)
        m.fermer(); mqtt = null
        if (estHote) emettre(s) else recevoir(s)
    }

    /** STUN (RFC 5389) : l'adresse et le port vus depuis Internet, par la meme prise UDP */
    internal fun stun(s: DatagramSocket): InetSocketAddress? {
        val srv = InetSocketAddress(InetAddress.getByName(STUN), STUN_PORT)
        val rq = ByteArray(20); rq[1] = 1; rq[4] = 0x21; rq[5] = 0x12; rq[6] = 0xA4.toByte(); rq[7] = 0x42
        val tid = ByteArray(12); java.security.SecureRandom().nextBytes(tid); System.arraycopy(tid, 0, rq, 8, 12)
        val buf = ByteArray(512); val p = DatagramPacket(buf, buf.size)
        repeat(4) {
            s.send(DatagramPacket(rq, rq.size, srv))
            try {
                s.receive(p)
                var o = 20
                while (o + 4 <= p.length) {
                    val t = (buf[o].toInt() and 255 shl 8) or (buf[o + 1].toInt() and 255); val l = (buf[o + 2].toInt() and 255 shl 8) or (buf[o + 3].toInt() and 255)
                    if ((t == 0x0020 || t == 0x0001) && l >= 8 && buf[o + 5].toInt() == 1) {
                        var port = (buf[o + 6].toInt() and 255 shl 8) or (buf[o + 7].toInt() and 255)
                        val ip = ByteArray(4) { buf[o + 8 + it] }
                        if (t == 0x0020) { port = port xor 0x2112; val k = byteArrayOf(0x21, 0x12, 0xA4.toByte(), 0x42); for (i in 0..3) ip[i] = (ip[i].toInt() xor k[i].toInt()).toByte() }
                        return InetSocketAddress(InetAddress.getByAddress(ip), port)
                    }
                    o += 4 + ((l + 3) and 3.inv())
                }
            } catch (e: java.net.SocketTimeoutException) { }
        }
        return null
    }
    internal fun adresseLocale(): InetAddress? = try {
        java.net.NetworkInterface.getNetworkInterfaces().toList().flatMap { it.inetAddresses.toList() }
            .firstOrNull { !it.isLoopbackAddress && it is java.net.Inet4Address && it.isSiteLocalAddress }
    } catch (e: Exception) { null }

    // ------------------------------------------------------------------ l'hote : capture, compression, envoi
    private fun emettre(s: DatagramSocket) {
        dire("Ton ami regarde ta vue 3D")
        val v0 = vue?.get()
        val ratio = if (v0 != null && v0.width > 0 && v0.height > 0) v0.height.toFloat() / v0.width else 0.9f
        val w = 640; val h = ((w * ratio).toInt() / 16 * 16).coerceIn(256, 720)
        val f = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, w, h)
        f.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible)
        f.setInteger(MediaFormat.KEY_BIT_RATE, 2_000_000); f.setInteger(MediaFormat.KEY_FRAME_RATE, IPS); f.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 2)
        val enc = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        enc.configure(f, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE); enc.start()
        val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        val px = IntArray(w * h)
        val fil = android.os.HandlerThread("capture").apply { start() }
        val hd = android.os.Handler(fil.looper)
        var config: ByteArray? = null; var seq = 0L; val info = MediaCodec.BufferInfo()
        var dernierSigne = System.currentTimeMillis(); var derniereCle = 0L
        val t0 = System.nanoTime()
        val buf = ByteArray(256); val p = DatagramPacket(buf, buf.size); s.soTimeout = 1
        try {
            while (actif) {
                val tImg = System.nanoTime()
                // 1. capture de la vue 3D (si elle est affichee)
                val v = vue?.get()
                if (v != null && v.holder.surface?.isValid == true && v.width > 0) {
                    val lock = Object(); var ok = false
                    synchronized(lock) {
                        try {
                            android.view.PixelCopy.request(v, bmp, { r -> synchronized(lock) { ok = r == android.view.PixelCopy.SUCCESS; lock.notifyAll() } }, hd)
                            lock.wait(200)
                        } catch (e: Exception) { }
                    }
                    if (ok) {
                        val i = enc.dequeueInputBuffer(10_000)
                        if (i >= 0) {
                            val img = enc.getInputImage(i)
                            if (img != null) { bmp.getPixels(px, 0, w, 0, 0, w, h); versYuv(px, w, h, img) }
                            enc.queueInputBuffer(i, 0, w * h * 3 / 2, (System.nanoTime() - t0) / 1000, 0)
                        }
                    }
                }
                // 2. images compressees : en morceaux vers l'ami (la configuration repart avant chaque image cle)
                while (true) {
                    val o = enc.dequeueOutputBuffer(info, 0)
                    if (o < 0) break
                    val ob = enc.getOutputBuffer(o)
                    if (ob == null) { enc.releaseOutputBuffer(o, false); continue }
                    val d = ByteArray(info.size); ob.position(info.offset); ob.get(d)
                    enc.releaseOutputBuffer(o, false)
                    if (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0) { config = d; continue }
                    val cle = info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME != 0
                    val tout = if (cle && config != null) config!! + d else d
                    envoyerImage(s, ++seq, w, h, cle, tout)
                }
                // 3. ce que dit l'ami : toujours la (signe), image cle demandee
                try { while (true) { p.length = buf.size; s.receive(p); if (p.length >= 2 && buf[0] == 'G'.code.toByte()) { dernierSigne = System.currentTimeMillis()
                    if (buf[1] == 'K'.code.toByte() && System.currentTimeMillis() - derniereCle > 1000) { derniereCle = System.currentTimeMillis()
                        enc.setParameters(android.os.Bundle().apply { putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0) }) } } } } catch (e: java.net.SocketTimeoutException) { }
                if (System.currentTimeMillis() - dernierSigne > 10_000) { dire("Ton ami est parti"); break }
                val reste = 1_000_000_000L / IPS - (System.nanoTime() - tImg)
                if (reste > 0) Thread.sleep(reste / 1_000_000, (reste % 1_000_000).toInt())
            }
        } finally { try { enc.stop(); enc.release() } catch (e: Exception) { }; fil.quitSafely() }
    }
    private fun envoyerImage(s: DatagramSocket, seq: Long, w: Int, h: Int, cle: Boolean, d: ByteArray) {
        val n = (d.size + MORCEAU - 1) / MORCEAU
        for (k in 0 until n) {
            val o = k * MORCEAU; val l = minOf(MORCEAU, d.size - o)
            val pq = ByteArray(16 + l)
            pq[0] = 'G'.code.toByte(); pq[1] = 'V'.code.toByte()
            pq[2] = (seq shr 24).toByte(); pq[3] = (seq shr 16).toByte(); pq[4] = (seq shr 8).toByte(); pq[5] = seq.toByte()
            pq[6] = (k shr 8).toByte(); pq[7] = k.toByte(); pq[8] = (n shr 8).toByte(); pq[9] = n.toByte()
            pq[10] = (w shr 8).toByte(); pq[11] = w.toByte(); pq[12] = (h shr 8).toByte(); pq[13] = h.toByte(); pq[14] = if (cle) 1 else 0
            System.arraycopy(d, o, pq, 16, l)
            try { s.send(DatagramPacket(pq, pq.size, pair)) } catch (e: Exception) { }
        }
    }
    /** ARGB -> YUV 4:2:0 (BT.601) dans les plans de l'image d'entree du codeur (quelle que soit leur disposition) */
    private fun versYuv(px: IntArray, w: Int, h: Int, img: android.media.Image) {
        val pl = img.planes
        if (natif && pl.all { it.buffer.isDirect }) {               // (build 496 : en C, ~1 ms)
            nYuv(px, w, h, pl[0].buffer, pl[0].rowStride, pl[0].pixelStride, pl[1].buffer, pl[1].rowStride, pl[1].pixelStride, pl[2].buffer, pl[2].rowStride, pl[2].pixelStride)
            return
        }
        val yb = pl[0].buffer; val ys = pl[0].rowStride; val yp = pl[0].pixelStride
        val ub = pl[1].buffer; val us = pl[1].rowStride; val up = pl[1].pixelStride
        val vb = pl[2].buffer; val vs = pl[2].rowStride; val vp = pl[2].pixelStride
        for (y in 0 until h) for (x in 0 until w) {
            val c = px[y * w + x]; val r = c shr 16 and 255; val g = c shr 8 and 255; val b = c and 255
            yb.put(y * ys + x * yp, (((66 * r + 129 * g + 25 * b + 128) shr 8) + 16).toByte())
            if (y and 1 == 0 && x and 1 == 0) {
                ub.put((y / 2) * us + (x / 2) * up, (((-38 * r - 74 * g + 112 * b + 128) shr 8) + 128).toByte())
                vb.put((y / 2) * vs + (x / 2) * vp, (((112 * r - 94 * g - 18 * b + 128) shr 8) + 128).toByte())
            }
        }
    }

    // ------------------------------------------------------------------ le spectateur : reception, decodage
    private fun recevoir(s: DatagramSocket) {
        dire("Connecté · en attente de l'image…")
        val buf = ByteArray(2048); val p = DatagramPacket(buf, buf.size); s.soTimeout = 50
        var dec: MediaCodec? = null; var tailleDec = 0
        var cour = -1L; var parts: Array<ByteArray?> = arrayOfNulls(0); var attendCle = true
        var dernierSigne = 0L; var dernierRecu = System.currentTimeMillis(); var derniereDemande = 0L; var nImages = 0
        val info = MediaCodec.BufferInfo()
        fun demanderCle() { if (System.currentTimeMillis() - derniereDemande > 800) { derniereDemande = System.currentTimeMillis()
            try { s.send(DatagramPacket(byteArrayOf('G'.code.toByte(), 'K'.code.toByte()), 2, pair)) } catch (e: Exception) { } } }
        try {
            while (actif) {
                if (System.currentTimeMillis() - dernierSigne > 1000) { dernierSigne = System.currentTimeMillis()
                    try { s.send(DatagramPacket(byteArrayOf('G'.code.toByte(), 'A'.code.toByte()), 2, pair)) } catch (e: Exception) { } }
                if (System.currentTimeMillis() - dernierRecu > 10_000) { dire("Plus d'image : le partage s'est arrêté"); break }
                try { s.receive(p) } catch (e: java.net.SocketTimeoutException) { continue }
                if (p.length < 16 || buf[0] != 'G'.code.toByte() || buf[1] != 'V'.code.toByte()) continue
                dernierRecu = System.currentTimeMillis()
                fun u16(o: Int) = (buf[o].toInt() and 255 shl 8) or (buf[o + 1].toInt() and 255)
                val seq = ((buf[2].toLong() and 255) shl 24) or ((buf[3].toLong() and 255) shl 16) or ((buf[4].toLong() and 255) shl 8) or (buf[5].toLong() and 255)
                val k = u16(6); val n = u16(8); val w = u16(10); val h = u16(12); val cle = buf[14].toInt() == 1
                if (n !in 1..2000 || k >= n) continue
                if (seq < cour) continue
                if (seq > cour) {
                    if (cour >= 0 && parts.any { it == null }) { attendCle = true; demanderCle() }   // (image precedente incomplete)
                    cour = seq; parts = arrayOfNulls(n)
                }
                parts[k] = buf.copyOfRange(16, p.length)
                if (parts.any { it == null }) continue
                val d = ByteArray(parts.sumOf { it!!.size }); var o = 0; for (q in parts) { System.arraycopy(q!!, 0, d, o, q.size); o += q.size }
                parts = arrayOfNulls(0); cour = seq + 1
                if (attendCle && !cle) { demanderCle(); continue }
                attendCle = false
                val surf = ecran ?: continue
                if (dec == null || tailleDec != (w shl 16 or h)) {
                    try { dec?.stop(); dec?.release() } catch (e: Exception) { }
                    dec = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
                    dec.configure(MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, w, h), surf, null, 0); dec.start(); tailleDec = w shl 16 or h
                }
                val i = dec.dequeueInputBuffer(20_000)
                if (i >= 0) { dec.getInputBuffer(i)?.apply { clear(); put(d) }; dec.queueInputBuffer(i, 0, d.size, seq * (1_000_000L / IPS), 0) }
                while (true) { val oo = dec.dequeueOutputBuffer(info, 0); if (oo < 0) break; dec.releaseOutputBuffer(oo, true) }
                if (++nImages == 1) dire("En direct")
            }
        } finally { try { dec?.stop(); dec?.release() } catch (e: Exception) { } }
    }
}

/** MQTT 3.1.1 minimal (sans chiffrement, QoS 0) : pour le seul rendez-vous des deux telephones */
class MiniMqtt(hote: String, port: Int, id: String, val recu: (String, ByteArray) -> Unit) {
    private val so = Socket().apply { connect(InetSocketAddress(hote, port), 8000); soTimeout = 8000 }
    private val ent: InputStream = so.getInputStream(); private val sor: OutputStream = so.getOutputStream()
    private var pid = 1; private var dernierPing = System.currentTimeMillis()
    init {
        val v = chaine("MQTT") + byteArrayOf(4, 2, 0, 60) + chaine(id)
        paquet(0x10, v)
        val t = ent.read(); val l = ent.read(); val r = ByteArray(l); lireTout(r)
        if (t != 0x20 || r.size < 2 || r[1].toInt() != 0) throw Exception("point de rendez-vous refusé")
    }
    private fun chaine(s: String): ByteArray { val b = s.toByteArray(Charsets.UTF_8); return byteArrayOf((b.size shr 8).toByte(), b.size.toByte()) + b }
    @Synchronized private fun paquet(t: Int, v: ByteArray) {
        val o = java.io.ByteArrayOutputStream(); o.write(t); var x = v.size
        do { var b = x and 127; x = x shr 7; if (x > 0) b = b or 128; o.write(b) } while (x > 0)
        o.write(v); sor.write(o.toByteArray()); sor.flush()
    }
    private fun lireTout(b: ByteArray) { var o = 0; while (o < b.size) { val k = ent.read(b, o, b.size - o); if (k < 0) throw Exception("rendez-vous coupé"); o += k } }
    fun abonner(sujet: String) { val p = pid++; paquet(0x82, byteArrayOf((p shr 8).toByte(), p.toByte()) + chaine(sujet) + byteArrayOf(0)) }
    fun publier(sujet: String, d: ByteArray, retenu: Boolean) = paquet(0x30 or (if (retenu) 1 else 0), chaine(sujet) + d)
    /** lit ce qui arrive pendant au plus ms millisecondes (messages -> recu) ; ping de temps en temps */
    fun lire(ms: Int) {
        if (System.currentTimeMillis() - dernierPing > 30_000) { paquet(0xC0, ByteArray(0)); dernierPing = System.currentTimeMillis() }
        so.soTimeout = ms.coerceAtLeast(1)
        try {
            while (true) {
                val t = ent.read(); if (t < 0) throw Exception("rendez-vous coupé")
                so.soTimeout = 3000
                var l = 0; var m = 1
                do { val b = ent.read(); if (b < 0) throw Exception("rendez-vous coupé"); l += (b and 127) * m; m *= 128 } while (b and 128 != 0)
                val v = ByteArray(l); lireTout(v)
                if (t and 0xF0 == 0x30 && l >= 2) {
                    val sl = (v[0].toInt() and 255 shl 8) or (v[1].toInt() and 255)
                    val su = String(v, 2, sl, Charsets.UTF_8)
                    val deb = 2 + sl + if (t and 6 != 0) 2 else 0
                    if (deb <= v.size && v.size - deb > 0) recu(su, v.copyOfRange(deb, v.size))
                }
                so.soTimeout = 1
            }
        } catch (e: java.net.SocketTimeoutException) { }
    }
    fun fermer() { try { paquet(0xE0, ByteArray(0)) } catch (e: Exception) { }; try { so.close() } catch (e: Exception) { } }
}

/** l'ecran du spectateur : le code de l'ami, puis sa vue 3D en video */
@Composable
fun EcranRegarder() {
    var c by remember { mutableStateOf("") }
    Column(Modifier.fillMaxSize().padding(horizontal = 14.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Carte(Modifier.fillMaxWidth()) {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Titre("Regarder la vue 3D d'un ami")
                Doux("Ton ami ouvre sa vue 3D et touche « Partager » : un code de 6 lettres s'affiche. Tape-le ici. La vidéo passe directement de son téléphone au tien, par Internet.", taille = 12)
                if (!Partage.spectateur) {
                    OutlinedTextField(c, { c = it.uppercase().filter { ch -> ch.isLetterOrDigit() }.take(6) }, Modifier.fillMaxWidth(), singleLine = true, label = { Text("Code de l'ami") })
                    Bouton("Regarder", Modifier.fillMaxWidth(), plein = true, actif = c.length == 6 && !Partage.hote) { Partage.regarder(c) }
                } else Bouton("Arrêter", Modifier.fillMaxWidth()) { Partage.arreter() }
                if (Partage.etat.isNotEmpty()) Doux(Partage.etat, taille = 12, couleur = C.Cyan)
            }
        }
        AndroidView({ ctx -> SurfaceView(ctx).apply {
            holder.addCallback(object : android.view.SurfaceHolder.Callback {
                override fun surfaceCreated(h: android.view.SurfaceHolder) { Partage.ecran = h.surface }
                override fun surfaceChanged(h: android.view.SurfaceHolder, f: Int, w: Int, hh: Int) { Partage.ecran = h.surface }
                override fun surfaceDestroyed(h: android.view.SurfaceHolder) { Partage.ecran = null }
            }) } }, Modifier.fillMaxWidth().aspectRatio(0.9f))
    }
    DisposableEffect(Unit) { onDispose { if (Partage.spectateur) Partage.arreter() } }
}

/** le bouton de partage, par-dessus la vue 3D (appli privee) */
@Composable
fun BoutonPartage(modifier: Modifier) {
    if (Spect3D.spectateur) return                                 // (build 499 : pas sur la vue d'un spectateur 3D)
    Column(modifier, verticalArrangement = Arrangement.spacedBy(4.dp)) {
        if (Partage.hote) {
            Text("Code : " + Partage.code, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.Bold)
            if (Partage.etat.isNotEmpty()) Doux(Partage.etat, taille = 11, couleur = C.Cyan)
            Bouton("Arrêter le partage", hauteur = 34.dp) { Partage.arreter() }
        } else Bouton("📡 Partager", hauteur = 34.dp, actif = !Partage.spectateur) { Partage.partager() }
        BoutonSpect3D()                                            // (build 499 : spectateurs avec leur propre camera)
    }
}
