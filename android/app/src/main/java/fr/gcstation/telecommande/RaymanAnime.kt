package fr.gcstation.telecommande

// Appli build 272 : Rayman anime en direct dans la vue 3D. Le pack (fichiers communs, fabriques a part depuis fix.lvl, jamais
// dans l'appli) donne r3_rayman_anim.txt : les modeles deformables de Rayman (corps, mains, mains et corps des pouvoirs,
// helice des cheveux), leurs os et la correspondance os -> noeud du squelette en memoire, plus les textures r3_tex_*.png.
// En jeu, on relit a chaque pose : les 25 matrices des noeuds de Rayman (un tableau contigu de 0x54 octets par noeud),
// l'objet affiche par chaque noeud (le corps et les mains changent avec les pouvoirs) et, plus rarement, la texture
// courante de chaque morceau (le corps change de texture avec les pouvoirs).
// Adresses trouvees sur quatre photos de la memoire (Moor_00 et Tower_10, GRHP41) : super-objet de Rayman 0x80C22120
// (type 2, donnee = son perso, au decalage 0x3D9A0 du fichier commun) ; +8 : premier noeud ; noeuds de 0x80 octets a la
// suite (+0 type, +4 objet, +0x24 matrice globale : 3 lignes de 4 flottants puis la translation a +0x34).

import java.io.File

class ModeleAnime(val geos: Map<Int, Geo>, val po: Map<Long, Int>, val tex: Map<Long, String>, val ray: Long, val perso: Long) {
    class Os(val noeud: Int, val r: FloatArray, val t: FloatArray)                       // l = r v + t (r par lignes)
    class El(val eo: Long, val png: String, val coins: IntArray, val uv: FloatArray)
    class Geo(val v: FloatArray, val n: FloatArray, val os: Array<Os?>, val wb: Array<IntArray>, val ww: Array<FloatArray>, val el: List<El>) {
        val nv get() = v.size / 3
        val deforme get() = os.any { it != null }
    }
    companion object {
        private val cache = HashMap<String, ModeleAnime?>()
        fun pour(dossier: File?): ModeleAnime? {
            val f = File(dossier ?: return null, "r3_rayman_anim.txt")
            synchronized(cache) {
                val cle = f.path + ":" + f.lastModified()
                if (cache.containsKey(cle)) return cache[cle]
                val m = try { if (f.exists()) lire(f) else null } catch (e: Throwable) { null }
                cache[cle] = m; return m
            }
        }
        private fun lire(f: File): ModeleAnime? {
            val geos = HashMap<Int, Geo>(); val po = HashMap<Long, Int>(); val tex = HashMap<Long, String>()
            var ray = 0x80C22120L; var perso = 0x3D9A0L
            var id = -1; var v = ArrayList<Float>(); var n = ArrayList<Float>(); var os = arrayOfNulls<Os>(32)
            var wb = ArrayList<IntArray>(); var ww = ArrayList<FloatArray>(); var el = ArrayList<El>()
            var eo = 0L; var png = ""; var ci = ArrayList<Int>(); var cu = ArrayList<Float>()
            fun finEl() { if (png.isNotEmpty()) el.add(El(eo, png, ci.toIntArray(), cu.toFloatArray())); png = ""; ci = ArrayList(); cu = ArrayList() }
            fun finGeo() {
                finEl()
                if (id >= 0) geos[id] = Geo(v.toFloatArray(), n.toFloatArray(), os, wb.toTypedArray(), ww.toTypedArray(), el)
                v = ArrayList(); n = ArrayList(); os = arrayOfNulls(32); wb = ArrayList(); ww = ArrayList(); el = ArrayList()
            }
            f.forEachLine { l ->
                val t = l.trim().split(' ')
                when (t[0]) {
                    "ray" -> { ray = t[1].toLong(16); perso = t[2].toLong(16) }
                    "tex" -> tex[t[1].toLong(16)] = t[2]
                    "geo" -> { finGeo(); id = t[1].toInt() }
                    "os" -> { val b = t[1].toInt(); val q = t.drop(3).map { it.toFloat() }
                        if (b in 0..31 && q.size >= 12) os[b] = Os(t[2].toInt(), q.subList(0, 9).toFloatArray(), q.subList(9, 12).toFloatArray()) }
                    "v" -> { val q = t.drop(1).map { it.toFloat() }
                        for (k in 0..2) v.add(q[k]); for (k in 3..5) n.add(q[k])
                        val nb = (q.size - 6) / 2
                        wb.add(IntArray(nb) { q[6 + 2 * it].toInt() }); ww.add(FloatArray(nb) { q[7 + 2 * it] }) }
                    "el" -> { finEl(); eo = t[1].toLong(16); png = t[2] }
                    "c" -> { ci.add(t[1].toInt()); cu.add(t[2].toFloat()); cu.add(t[3].toFloat()) }
                    "po" -> po[t[1].toLong(16)] = t[2].toInt()
                }
            }
            finGeo()
            return if (geos.isEmpty()) null else ModeleAnime(geos, po, tex, ray, perso)
        }
    }

    /** un morceau a dessiner : la geometrie et le noeud qui la porte */
    fun morceaux(p: PoseRayman): List<Pair<Int, Int>> {
        val r = ArrayList<Pair<Int, Int>>()
        for (s in 0 until PoseRayman.N) if (p.types[s] == 8) po[p.data[s] - p.fix4]?.let { r.add(it to s) }
        return r
    }

    /** sommets deformes (x y z nx ny nz), relatifs a la position de Rayman */
    fun peau(g: Geo, p: PoseRayman, s: Int, out: FloatArray) {
        val m = p.mat
        // matrices combinees par os : C = G r, c = G t + T(noeud)
        val cm = Array(g.os.size) { b ->
            val o = g.os[b] ?: return@Array null
            val k = o.noeud * 15
            if (o.noeud !in 0 until PoseRayman.N) return@Array null
            val c = FloatArray(12)
            for (i in 0..2) {
                for (j in 0..2) { var a = 0f; for (q in 0..2) a += m[k + 4 * q + i] * o.r[3 * q + j]; c[3 * i + j] = a }
                var a = 0f; for (q in 0..2) a += m[k + 4 * q + i] * o.t[q]; c[9 + i] = a + m[k + 12 + i]
            }
            c
        }
        val ks = s * 15
        val px = p.pos[0]; val py = p.pos[1]; val pz = p.pos[2]
        for (i in 0 until g.nv) {
            val vx = g.v[3 * i]; val vy = g.v[3 * i + 1]; val vz = g.v[3 * i + 2]
            val nx = g.n[3 * i]; val ny = g.n[3 * i + 1]; val nz = g.n[3 * i + 2]
            var x = 0f; var y = 0f; var z = 0f; var a = 0f; var b = 0f; var c = 0f; var tw = 0f
            val bs = g.wb[i]; val ws = g.ww[i]
            for (k in bs.indices) {
                val cc = cm.getOrNull(bs[k]) ?: continue; val w = ws[k]
                x += w * (cc[0] * vx + cc[1] * vy + cc[2] * vz + cc[9]); y += w * (cc[3] * vx + cc[4] * vy + cc[5] * vz + cc[10]); z += w * (cc[6] * vx + cc[7] * vy + cc[8] * vz + cc[11])
                a += w * (cc[0] * nx + cc[1] * ny + cc[2] * nz); b += w * (cc[3] * nx + cc[4] * ny + cc[5] * nz); c += w * (cc[6] * nx + cc[7] * ny + cc[8] * nz)
                tw += w
            }
            if (tw > 1e-6f) { x /= tw; y /= tw; z /= tw }
            else {                                    // (sans poids : porte par le noeud de l'objet, comme une piece rigide)
                x = m[ks] * vx + m[ks + 4] * vy + m[ks + 8] * vz + m[ks + 12]; y = m[ks + 1] * vx + m[ks + 5] * vy + m[ks + 9] * vz + m[ks + 13]; z = m[ks + 2] * vx + m[ks + 6] * vy + m[ks + 10] * vz + m[ks + 14]
                a = m[ks] * nx + m[ks + 4] * ny + m[ks + 8] * nz; b = m[ks + 1] * nx + m[ks + 5] * ny + m[ks + 9] * nz; c = m[ks + 2] * nx + m[ks + 6] * ny + m[ks + 10] * nz
            }
            val ln = Math.sqrt((a * a + b * b + c * c).toDouble()).toFloat().coerceAtLeast(1e-6f)
            out[6 * i] = x - px; out[6 * i + 1] = y - py; out[6 * i + 2] = z - pz
            out[6 * i + 3] = a / ln; out[6 * i + 4] = b / ln; out[6 * i + 5] = c / ln
        }
    }
}

/** une pose lue dans la memoire : 25 noeuds (matrice brute : 12 flottants par lignes + translation), l'objet de chaque noeud */
class PoseRayman(val mat: FloatArray, val types: IntArray, val data: LongArray, val fix4: Long, val pos: FloatArray,
                 val textures: Map<Long, String>, val t: Long = System.nanoTime()) {
    companion object { const val N = 25 }
}

object LecturePose {
    private var so0 = 0L; private var l0 = 0L; private var mr = 0L; private var fix4 = 0L; private var verifie = 0L
    private var objets: Triple<IntArray, LongArray, IntArray>? = null; private var tObjets = 0L
    private var textures: Map<Long, String> = emptyMap(); private var tTex = 0L
    /** build 276 : les matrices globales du jeu sont lues bloc par bloc, donc a des images differentes : quand Rayman va
     *  vite, les morceaux se decalaient. On lit donc les matrices LOCALES (chaque noeud par rapport a son parent, tableau
     *  contigu pointe par +0x20 des noeuds) et la rotation de Rayman, et on recompose le squelette ici, autour de Rayman
     *  (verifie sur les quatre photos : la recomposition redonne les matrices globales du jeu). */
    fun lire(mod: ModeleAnime?): PoseRayman? {
        mod ?: return null
        val now = System.currentTimeMillis()
        val l = Lecteur(24, 0)
        if (now - verifie > 1500 || so0 == 0L) {                 // (les adresses du squelette changent avec le niveau)
            so0 = 0L
            if (l.u32(mod.ray) != 2L) return null
            val perso = l.ptr(mod.ray + 4) ?: return null
            val s = l.ptr(mod.ray + 8) ?: return null
            if (l.u32(s) != 8L) return null
            l0 = l.ptr(s + 0x20) ?: return null
            mr = l.ptr(mod.ray + 0x24) ?: return null
            so0 = s; fix4 = perso - mod.perso; verifie = now; tObjets = 0L
        }
        if (now - tObjets > 400 || objets == null) {              // (l'objet de chaque noeud : corps, mains, pouvoirs ; et son parent)
            val ty = IntArray(PoseRayman.N); val da = LongArray(PoseRayman.N); val pa = IntArray(PoseRayman.N)
            for (k in 0 until PoseRayman.N) {
                val so = so0 + 0x80L * k
                ty[k] = l.u32(so)?.toInt() ?: return null; da[k] = l.u32(so + 4) ?: return null
                val p = l.u32(so + 0x1C) ?: return null
                pa[k] = if (p == mod.ray) -1 else ((p - so0) / 0x80).toInt().takeIf { (p - so0) % 0x80 == 0L && it in 0 until PoseRayman.N } ?: -1
            }
            objets = Triple(ty, da, pa); tObjets = now
        }
        val (ty, da, pa) = objets ?: return null
        // rotation de Rayman (sa position, elle, vient de la lecture rapide : le squelette est dessine autour)
        val rr = FloatArray(12)
        for (j in 0 until 12) rr[j] = l.f32(mr + 4 + 4 * j) ?: return null
        val loc = FloatArray(PoseRayman.N * 15)
        for (k in 0 until PoseRayman.N) {
            val a = l0 + 0x54L * k
            for (j in 0 until 12) loc[k * 15 + j] = l.f32(a + 4 + 4 * j) ?: return null
            for (j in 0..2) loc[k * 15 + 12 + j] = l.f32(a + 0x34 + 4 * j) ?: return null
        }
        if (loc.any { !it.isFinite() || Math.abs(it) > 1e5f } || rr.any { !it.isFinite() || Math.abs(it) > 1e3f }) { so0 = 0L; return null }
        // recomposition : G = P L (rangement du jeu : element [4j + i] = ligne i, colonne j de la rotation ; translation a 12..14)
        val mat = FloatArray(PoseRayman.N * 15); val fait = BooleanArray(PoseRayman.N)
        fun calc(k: Int, prof: Int) {
            if (fait[k]) return
            val p = pa[k]
            if (p >= 0 && prof < 30) calc(p, prof + 1)
            val P = if (p >= 0 && fait[p]) mat else rr; val po = if (p >= 0 && fait[p]) p * 15 else 0
            val pt = if (p >= 0 && fait[p]) floatArrayOf(mat[po + 12], mat[po + 13], mat[po + 14]) else floatArrayOf(0f, 0f, 0f)
            val o = k * 15
            for (i in 0..2) {
                for (j in 0..2) { var s = 0f; for (q in 0..2) s += P[po + 4 * q + i] * loc[o + 4 * j + q]; mat[o + 4 * j + i] = s }
                var s = 0f; for (q in 0..2) s += P[po + 4 * q + i] * loc[o + 12 + q]; mat[o + 12 + i] = s + pt[i]
            }
            fait[k] = true
        }
        for (k in 0 until PoseRayman.N) calc(k, 0)
        val pos = FloatArray(3)
        if (now - tTex > 1000) {                                   // (la texture de chaque morceau affiche : change avec les pouvoirs)
            val r = HashMap<Long, String>()
            for ((g, _) in mod.morceaux(PoseRayman(mat, ty, da, fix4, pos, emptyMap()))) for (e in mod.geos[g]?.el ?: emptyList()) {
                val mt = l.ptr(fix4 + e.eo) ?: continue
                val ti = l.u32(mt + 0x68) ?: continue
                mod.tex[ti - fix4]?.let { r[e.eo] = it }
            }
            textures = r; tTex = now
        }
        return PoseRayman(mat, ty, da, fix4, pos, textures)
    }
}
