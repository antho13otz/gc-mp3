package fr.gcstation.telecommande

// Appli (v313, GC Station v313) : la vraie carte marine de The Wind Waker, et le plan des interieurs.
//  - Carte marine : le jeu dessine chaque case de sa carte avec une image de son disque, /res/Fmap/Fmap.arc
//    ("R%02X_col_%d.bti" : numero de la case en hexadecimal, puis la variante du fichier de sauvegarde,
//    dSv_player_info_c.mRandomSalvagePoint ; d_menu_fmap2.cpp). Pendant la partie, cette archive est dans l'ARAM (que
//    Pulse ne lit pas) : l'appli la demande donc a GC Station hors partie (/api/discfile, lue dans le jeu de la carte
//    SD), la garde (files/jeux/GZLP01/Fmap.arc), puis la decode ici : Yaz0, archive RARC, textures BTI (formats GX).
//    Rien de Nintendo n'est dans l'appli ni dans le depot : les images viennent du jeu de l'utilisateur.
//  - Plan des interieurs (donjons, maisons, iles) : le chemin de Link, garde par scene (files/plans_ww/<scene>.bin :
//    x, y, z, salle en float), dessine vu du dessus ; l'etage en cours plus net (hauteur proche).

import android.content.Context
import android.graphics.Bitmap
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import java.io.File

/** decodage des fichiers du disque GameCube : Yaz0, RARC, BTI */
object FichiersGC {
    private fun be16(b: ByteArray, o: Int) = ((b[o].toInt() and 255) shl 8) or (b[o + 1].toInt() and 255)
    private fun be32(b: ByteArray, o: Int) = (be16(b, o) shl 16) or be16(b, o + 2)

    fun yaz0(b: ByteArray): ByteArray {
        if (b.size < 16 || b[0] != 'Y'.code.toByte() || b[1] != 'a'.code.toByte() || b[2] != 'z'.code.toByte() || b[3] != '0'.code.toByte()) return b
        val n = be32(b, 4)
        if (n <= 0 || n > (64 shl 20)) return ByteArray(0)
        val out = ByteArray(n)
        var s = 16; var d = 0; var code = 0; var bits = 0
        while (d < n && s < b.size) {
            if (bits == 0) { code = b[s++].toInt() and 255; bits = 8 }
            if (code and 0x80 != 0) { out[d++] = b[s++] }
            else {
                if (s + 1 >= b.size) break
                val b1 = b[s++].toInt() and 255; val b2 = b[s++].toInt() and 255
                val dist = ((b1 and 15) shl 8 or b2) + 1
                var len = b1 shr 4
                len = if (len == 0) { if (s >= b.size) break; (b[s++].toInt() and 255) + 0x12 } else len + 2
                var c = d - dist
                if (c < 0) break
                repeat(len) { if (d < n) out[d++] = out[c++] }
            }
            code = code shl 1; bits--
        }
        return out
    }

    /** les fichiers d'une archive RARC : nom (en minuscules) -> contenu (decompresse) */
    fun rarc(a0: ByteArray): Map<String, ByteArray> {
        val a = yaz0(a0)
        val r = HashMap<String, ByteArray>()
        if (a.size < 0x40 || String(a, 0, 4, Charsets.ISO_8859_1) != "RARC") return r
        val data = 0x20 + be32(a, 0x0C)
        val info = 0x20
        val nEnt = be32(a, info + 0x08)
        val ent = info + be32(a, info + 0x0C)
        val str = info + be32(a, info + 0x14)
        for (i in 0 until nEnt) {
            val e = ent + i * 0x14
            if (e + 0x14 > a.size) break
            val type = a[e + 4].toInt() and 255
            if (type and 0x02 != 0) continue                          // (dossier)
            val no = str + be16(a, e + 6)
            var k = no
            while (k < a.size && a[k] != 0.toByte()) k++
            if (no >= a.size) continue
            val nom = String(a, no, k - no, Charsets.ISO_8859_1).lowercase()
            val off = data + be32(a, e + 8); val sz = be32(a, e + 12)
            if (sz <= 0 || off < 0 || off + sz > a.size) continue
            r[nom] = yaz0(a.copyOfRange(off, off + sz))
        }
        return r
    }

    private fun argb(a: Int, r: Int, g: Int, b: Int) = (a shl 24) or (r shl 16) or (g shl 8) or b
    private fun c565(v: Int) = argb(255, (v shr 11 and 31) * 255 / 31, (v shr 5 and 63) * 255 / 63, (v and 31) * 255 / 31)
    private fun c5a3(v: Int) = if (v and 0x8000 != 0) argb(255, (v shr 10 and 31) * 255 / 31, (v shr 5 and 31) * 255 / 31, (v and 31) * 255 / 31)
                               else argb((v shr 12 and 7) * 255 / 7, (v shr 8 and 15) * 17, (v shr 4 and 15) * 17, (v and 15) * 17)
    private fun cIA8(v: Int) = argb(v shr 8 and 255, v and 255, v and 255, v and 255)

    /** une texture BTI (en-tete de 0x20 octets) -> image ; null : format inconnu ou abime */
    fun bti(b: ByteArray): Bitmap? {
        if (b.size < 0x20) return null
        val fmt = b[0].toInt() and 255
        val w = be16(b, 2); val h = be16(b, 4)
        if (w !in 1..1024 || h !in 1..1024) return null
        val palFmt = b[9].toInt() and 255
        val palN = be16(b, 0x0A)
        val palOff = be32(b, 0x0C)
        val off = be32(b, 0x1C).let { if (it == 0) 0x20 else it }
        val pal = IntArray(maxOf(palN, 1))
        for (i in 0 until palN) {
            val o = palOff + i * 2
            if (o + 2 > b.size) break
            val v = be16(b, o)
            pal[i] = when (palFmt) { 0 -> cIA8(v); 1 -> c565(v); else -> c5a3(v) }
        }
        val px = IntArray(w * h)
        val (bw, bh) = when (fmt) { 0, 8, 14 -> 8 to 8; 1, 2, 9 -> 8 to 4; else -> 4 to 4 }
        val bloc = if (fmt == 6) 64 else 32
        var o = off
        fun mets(x: Int, y: Int, c: Int) { if (x < w && y < h) px[y * w + x] = c }
        try {
            var ty = 0
            while (ty < h) {
                var tx = 0
                while (tx < w) {
                    when (fmt) {
                        0 -> for (i in 0 until 64) { val v = (b[o + i / 2].toInt() shr (if (i % 2 == 0) 4 else 0) and 15) * 17; mets(tx + i % 8, ty + i / 8, argb(255, v, v, v)) }
                        1 -> for (i in 0 until 32) { val v = b[o + i].toInt() and 255; mets(tx + i % 8, ty + i / 8, argb(255, v, v, v)) }
                        2 -> for (i in 0 until 32) { val v = b[o + i].toInt() and 255; val l = (v and 15) * 17; mets(tx + i % 8, ty + i / 8, argb((v shr 4) * 17, l, l, l)) }
                        3 -> for (i in 0 until 16) mets(tx + i % 4, ty + i / 4, cIA8(be16(b, o + i * 2)))
                        4 -> for (i in 0 until 16) mets(tx + i % 4, ty + i / 4, c565(be16(b, o + i * 2)))
                        5 -> for (i in 0 until 16) mets(tx + i % 4, ty + i / 4, c5a3(be16(b, o + i * 2)))
                        6 -> for (i in 0 until 16) mets(tx + i % 4, ty + i / 4, argb(b[o + i * 2].toInt() and 255, b[o + i * 2 + 1].toInt() and 255,
                                 b[o + 32 + i * 2].toInt() and 255, b[o + 32 + i * 2 + 1].toInt() and 255))
                        8 -> for (i in 0 until 64) { val v = b[o + i / 2].toInt() shr (if (i % 2 == 0) 4 else 0) and 15; mets(tx + i % 8, ty + i / 8, pal[v.coerceAtMost(pal.size - 1)]) }
                        9 -> for (i in 0 until 32) { val v = b[o + i].toInt() and 255; mets(tx + i % 8, ty + i / 8, pal[v.coerceAtMost(pal.size - 1)]) }
                        10 -> for (i in 0 until 16) { val v = be16(b, o + i * 2) and 0x3FFF; mets(tx + i % 4, ty + i / 4, pal[v.coerceAtMost(pal.size - 1)]) }
                        14 -> for (s in 0 until 4) {                     // CMPR : 4 blocs DXT1 de 4 x 4
                            val so = o + s * 8
                            val c0 = be16(b, so); val c1 = be16(b, so + 2)
                            val p = IntArray(4)
                            p[0] = c565(c0); p[1] = c565(c1)
                            fun mix(a: Int, bb: Int, fa: Int, fb: Int, d: Int) = argb(255,
                                ((a shr 16 and 255) * fa + (bb shr 16 and 255) * fb) / d, ((a shr 8 and 255) * fa + (bb shr 8 and 255) * fb) / d, ((a and 255) * fa + (bb and 255) * fb) / d)
                            if (c0 > c1) { p[2] = mix(p[0], p[1], 2, 1, 3); p[3] = mix(p[0], p[1], 1, 2, 3) }
                            else { p[2] = mix(p[0], p[1], 1, 1, 2); p[3] = 0 }
                            for (i in 0 until 16) {
                                val v = (b[so + 4 + i / 4].toInt() shr (6 - (i % 4) * 2)) and 3
                                mets(tx + (s % 2) * 4 + i % 4, ty + (s / 2) * 4 + i / 4, p[v])
                            }
                        }
                        else -> return null
                    }
                    o += bloc
                    tx += bw
                }
                ty += bh
            }
        } catch (e: IndexOutOfBoundsException) { return null }
        return Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888)
    }
}

/** la vraie carte marine de Wind Waker (images du disque de l'utilisateur) */
object CarteMarineWW {
    const val CHEMIN = "/res/Fmap/Fmap.arc"
    var tuiles by mutableStateOf<Map<String, ImageBitmap>>(emptyMap())     // ("r2c_col_0" -> image)
    var etat by mutableStateOf("")                                          // (message pour l'ecran)
    var noms: List<String> = emptyList()                                    // (tous les fichiers de l'archive)
    @Volatile private var essai = 0L
    @Volatile private var charge = false

    private fun fichier(c: Context) = File(c.filesDir, "jeux/" + JeuWW.ID + "/Fmap.arc")   // (meme carte pour les versions P et E)

    /** hors partie (GC Station a l'ecran) : l'archive demandee une fois a la console, puis gardee */
    fun telecharger(c: Context) {
        val f = fichier(c)
        if (f.length() > 64 || St.jeu != null || Api.host == null) return
        val t = System.currentTimeMillis()
        if (t - essai < 10 * 60 * 1000L) return
        essai = t
        Thread {
            val b = Api.getBytes("/api/discfile?id=" + JeuWW.ID + "&f=" + CHEMIN, 20000)
                ?: Api.getBytes("/api/discfile?id=GZLE01&f=" + CHEMIN, 20000)          // (v314 : ou la version americaine)
            if (b != null && b.size > 64) {
                try { f.parentFile?.mkdirs(); val tmp = File(f.path + ".tmp"); tmp.writeBytes(b); tmp.renameTo(f) } catch (e: Exception) { }
                charge = false; charger(c)                                   // (l'ecran se met a jour tout seul)
            }
        }.start()
    }

    /** decode les images des cases (une fois) ; a appeler hors du fil de l'ecran */
    fun charger(c: Context) {
        if (charge) return
        val f = fichier(c)
        if (f.length() <= 64) { etat = "La vraie carte du jeu arrive à la prochaine connexion hors partie (GC Station v313, Wind Waker européen sur la carte SD)."; return }
        charge = true
        try {
            val arc = FichiersGC.rarc(f.readBytes())
            noms = arc.keys.sorted()
            val m = HashMap<String, ImageBitmap>()
            for ((nom, d) in arc) if (nom.length >= 9 && nom[0] == 'r' && nom.contains("_col_") && nom.endsWith(".bti")) {
                FichiersGC.bti(d)?.let { m[nom.removeSuffix(".bti")] = it.asImageBitmap() }
            }
            tuiles = m
            etat = if (m.isEmpty()) "Carte du jeu : " + arc.size + " fichiers lus, aucune image de case reconnue." else ""
        } catch (e: Throwable) { etat = "Carte du jeu illisible (" + e.javaClass.simpleName + ")" }
    }

    /** l'image de la case i (0..48) pour la variante v, sinon une autre variante */
    fun tuile(i: Int, v: Int): ImageBitmap? {
        val t = tuiles
        if (t.isEmpty()) return null
        val base = "r" + String.format(java.util.Locale.ROOT, "%02x", i + 1) + "_col_"
        return t[base + v] ?: t[base + "0"] ?: t.entries.firstOrNull { it.key.startsWith(base) }?.value
    }
}

/** le plan des interieurs de Wind Waker : le chemin de Link par scene, garde sur le telephone */
object PlanWW {
    var version by mutableIntStateOf(0)
    private val plans = HashMap<String, ArrayList<FloatArray>>()           // scene -> (x, y, z, salle)
    private val modifies = HashSet<String>()
    private var dernierEnr = 0L

    private fun dossier(c: Context) = File(c.filesDir, "plans_ww")
    private fun nomFichier(s: String) = s.filter { it.isLetterOrDigit() || it == '_' }.ifEmpty { "x" } + ".bin"

    @Synchronized fun points(c: Context?, scene: String): List<FloatArray> = (plans[scene] ?: c?.let { charger(it, scene) } ?: emptyList()).toList()

    @Synchronized private fun charger(c: Context, scene: String): ArrayList<FloatArray> {
        val l = ArrayList<FloatArray>()
        try {
            val f = File(dossier(c), nomFichier(scene))
            if (f.exists()) {
                val bb = java.nio.ByteBuffer.wrap(f.readBytes())
                while (bb.remaining() >= 16) l.add(floatArrayOf(bb.float, bb.float, bb.float, bb.float))
            }
        } catch (e: Exception) { }
        plans[scene] = l
        return l
    }

    /** Link est la : ajoute le point s'il s'est assez deplace (pas en mer, pas a l'ecran titre) */
    @Synchronized fun ajouter(c: Context?, scene: String, x: Float, y: Float, z: Float, salle: Int) {
        if (scene.isEmpty() || scene == "sea" || scene == "sea_T" || c == null) return
        val l = plans[scene] ?: charger(c, scene)
        val d = l.lastOrNull()
        if (d != null && (d[0] - x) * (d[0] - x) + (d[2] - z) * (d[2] - z) < 60f * 60f && Math.abs(d[1] - y) < 100f) return
        // (deja un point tout pres : pas de doublon, seulement le passage)
        if (l.size > 200 && l.any { Math.abs(it[0] - x) < 45f && Math.abs(it[2] - z) < 45f && Math.abs(it[1] - y) < 100f }) return
        if (l.size >= 6000) l.removeAt(0)
        l.add(floatArrayOf(x, y, z, salle.toFloat()))
        modifies.add(scene)
        version++
        val t = System.currentTimeMillis()
        if (t - dernierEnr > 10000) enregistrer(c)
    }

    @Synchronized fun enregistrer(c: Context?) {
        if (c == null || modifies.isEmpty()) return
        dernierEnr = System.currentTimeMillis()
        val d = dossier(c); d.mkdirs()
        for (s in modifies) {
            val l = plans[s] ?: continue
            try {
                val bb = java.nio.ByteBuffer.allocate(l.size * 16)
                for (p in l) { bb.putFloat(p[0]); bb.putFloat(p[1]); bb.putFloat(p[2]); bb.putFloat(p[3]) }
                File(d, nomFichier(s)).writeBytes(bb.array())
            } catch (e: Exception) { }
        }
        modifies.clear()
    }

    @Synchronized fun effacer(c: Context, scene: String) {
        plans[scene] = ArrayList(); modifies.remove(scene)
        File(dossier(c), nomFichier(scene)).delete()
        version++
    }
}

/** v314 : un fichier du disque d'un jeu (main.dol, un .rel...) enregistre dans Telechargements/GC Remote, puis partage
 *  (GC Station v314 : /api/discfile ; le jeu doit etre sur la carte SD, et GC Station a l'ecran, hors partie) */
object ExtraireFichier {
    @Volatile var enCours = false
    /** build 199 : le fichier extrait enregistre la ou tu l'as choisi (dossier de tes fichiers, Drive...) */
    fun versUri(a: android.app.Activity, id: String, chemin0: String, u: android.net.Uri) {
        if (enCours) return
        val chemin = "/" + chemin0.trim().trimStart('/')
        if (St.jeu != null) { Lang.toast(a, "Hors partie seulement : reviens à GC Station (A + Z + START)", android.widget.Toast.LENGTH_LONG).show(); return }
        enCours = true
        Lang.toast(a, "Extraction de $chemin…", android.widget.Toast.LENGTH_SHORT).show()
        Thread {
            val b = Api.getBytes("/api/discfile?id=$id&f=" + Api.enc(chemin), 30000)
            var ok = false
            if (b != null && b.isNotEmpty()) try { a.contentResolver.openOutputStream(u, "w")?.use { it.write(b) }; ok = true } catch (e: Exception) { }
            a.runOnUiThread {
                enCours = false
                Lang.toast(a, when { b == null -> "Pas reçu : GC Station à l'écran, le jeu sur la carte SD, et le bon chemin ?"
                    !ok -> "Impossible d'écrire le fichier à cet endroit"; else -> "Enregistré (" + (b.size / 1024) + " Ko)" }, android.widget.Toast.LENGTH_LONG).show()
            }
        }.start()
    }
    fun lancer(a: android.app.Activity, id: String, chemin0: String) {
        if (enCours) return
        val chemin = "/" + chemin0.trim().trimStart('/')
        if (St.jeu != null) { Lang.toast(a, "Hors partie seulement : reviens à GC Station (A + Z + START)", android.widget.Toast.LENGTH_LONG).show(); return }
        if (android.os.Build.VERSION.SDK_INT < 29) { Lang.toast(a, "Android 10 ou plus est nécessaire", android.widget.Toast.LENGTH_LONG).show(); return }
        enCours = true
        Lang.toast(a, "Extraction de $chemin…", android.widget.Toast.LENGTH_SHORT).show()
        Thread {
            val b = Api.getBytes("/api/discfile?id=$id&f=" + Api.enc(chemin), 30000)
            var u: android.net.Uri? = null
            val nom = id + "-" + chemin.substringAfterLast('/')
            if (b != null && b.isNotEmpty()) try {
                val cv = android.content.ContentValues()
                cv.put(android.provider.MediaStore.Downloads.DISPLAY_NAME, nom)
                cv.put(android.provider.MediaStore.Downloads.MIME_TYPE, "application/octet-stream")
                cv.put(android.provider.MediaStore.Downloads.RELATIVE_PATH, android.os.Environment.DIRECTORY_DOWNLOADS + "/GC Remote")
                u = a.contentResolver.insert(android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv)
                if (u != null) a.contentResolver.openOutputStream(u)?.use { it.write(b) }
            } catch (e: Exception) { u = null }
            a.runOnUiThread {
                enCours = false
                if (b == null) { Lang.toast(a, "Pas reçu : GC Station v314 à l'écran, le jeu sur la carte SD, et le bon chemin ?", android.widget.Toast.LENGTH_LONG).show(); return@runOnUiThread }
                if (u == null) { Lang.toast(a, "Impossible d'enregistrer le fichier", android.widget.Toast.LENGTH_LONG).show(); return@runOnUiThread }
                Lang.toast(a, "Enregistré : Téléchargements/GC Remote/$nom (" + (b.size / 1024) + " Ko)", android.widget.Toast.LENGTH_LONG).show()
                val i = android.content.Intent(android.content.Intent.ACTION_SEND).setType("application/octet-stream")
                    .putExtra(android.content.Intent.EXTRA_STREAM, u).addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
                try { a.startActivity(android.content.Intent.createChooser(i, "Envoyer $nom")) } catch (e: Exception) { }
            }
        }.start()
    }
}
