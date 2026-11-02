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
    private const val POS = 0x804AAC94L
    private var so0 = 0L; private var m0 = 0L; private var fix4 = 0L; private var verifie = 0L
    private var objets: Pair<IntArray, LongArray>? = null; private var tObjets = 0L
    private var textures: Map<Long, String> = emptyMap(); private var tTex = 0L
    /** une pose, ou null (pas Rayman 3, pas de pack, structures pas a leur place) */
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
            val m = l.ptr(s + 0x24) ?: return null
            so0 = s; m0 = m; fix4 = perso - mod.perso; verifie = now; tObjets = 0L
        }
        if (now - tObjets > 400 || objets == null) {              // (l'objet de chaque noeud : corps, mains, pouvoirs)
            val ty = IntArray(PoseRayman.N); val da = LongArray(PoseRayman.N)
            for (k in 0 until PoseRayman.N) { ty[k] = l.u32(so0 + 0x80L * k)?.toInt() ?: return null; da[k] = l.u32(so0 + 0x80L * k + 4) ?: return null }
            objets = ty to da; tObjets = now
        }
        val (ty, da) = objets ?: return null
        val mat = FloatArray(PoseRayman.N * 15)
        for (k in 0 until PoseRayman.N) {
            val a = m0 + 0x54L * k
            for (j in 0 until 12) mat[k * 15 + j] = l.f32(a + 4 + 4 * j) ?: return null
            for (j in 0..2) mat[k * 15 + 12 + j] = l.f32(a + 0x34 + 4 * j) ?: return null
        }
        if (mat.any { !it.isFinite() || Math.abs(it) > 1e5f }) { so0 = 0L; return null }
        val pos = FloatArray(3)
        for (j in 0..2) pos[j] = l.f32(POS + 4 * j) ?: return null
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
