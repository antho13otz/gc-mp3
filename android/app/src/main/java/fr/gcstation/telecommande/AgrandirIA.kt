package fr.gcstation.telecommande

// Appli : agrandissement par IA de l'image du disque (GameTDB, ~160 px). Modele Real-ESRGAN « realesr-general-x4v3 »
// (reseau compact SRVGG, licence BSD-3 : assets/LICENCE-Real-ESRGAN.txt), au format ONNX (assets/srvgg.onnx), execute
// sur le telephone par ONNX Runtime : 4 fois plus grand, contours et textes nets (il « redessine » au lieu d'etirer).
// Une seule fois par jeu (le resultat est garde). En cas d'echec : null, et l'appli garde l'agrandissement classique.

import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import android.content.Context
import android.graphics.Bitmap
import java.nio.FloatBuffer

object AgrandirIA {
    @Volatile private var session: OrtSession? = null
    @Volatile private var rate = false

    private fun session(c: Context): OrtSession? {
        session?.let { return it }
        if (rate) return null
        return synchronized(this) {
            session ?: try {
                val env = OrtEnvironment.getEnvironment()
                val o = OrtSession.SessionOptions().apply { setIntraOpNumThreads(4) }
                env.createSession(c.assets.open("srvgg.onnx").use { it.readBytes() }, o).also { session = it }
            } catch (e: Throwable) { rate = true; null }
        }
    }

    /** w x h -> 4w x 4h (pixels ARGB opaques ; les transparents prennent d'abord la couleur de leurs voisins) */
    fun x4(c: Context, px0: IntArray, w: Int, h: Int): Bitmap? {
        val s = session(c) ?: return null
        return try {
            val px = Agrandir.remplir(px0, w, h)
            val n = w * h
            val fb = FloatBuffer.allocate(3 * n)
            for (k in 0 until n) { val p = px[k]; fb.put(k, (p shr 16 and 255) / 255f); fb.put(n + k, (p shr 8 and 255) / 255f); fb.put(2 * n + k, (p and 255) / 255f) }
            val env = OrtEnvironment.getEnvironment()
            OnnxTensor.createTensor(env, fb, longArrayOf(1, 3, h.toLong(), w.toLong())).use { t ->
                s.run(mapOf("x" to t)).use { r ->
                    val o = (r[0] as OnnxTensor).floatBuffer
                    val W = w * 4; val H = h * 4; val m = W * H
                    val out = IntArray(m)
                    fun c8(v: Float) = (v * 255f + 0.5f).toInt().coerceIn(0, 255)
                    for (k in 0 until m) out[k] = (0xFF shl 24) or (c8(o.get(k)) shl 16) or (c8(o.get(m + k)) shl 8) or c8(o.get(2 * m + k))
                    Bitmap.createBitmap(out, W, H, Bitmap.Config.ARGB_8888)
                }
            }
        } catch (e: Throwable) { null }
    }
}
