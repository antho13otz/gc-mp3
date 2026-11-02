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

/** une pose lue dans la memoire : 25 noeuds (matrice brute : 12 flottants par lignes + translation), l'objet de chaque noeud.
 *  Build 284 : avec [anim], l'appli rejoue elle-meme l'animation du jeu a partir de [image] (et de sa vitesse). */
class PoseRayman(val mat: FloatArray, val types: IntArray, val data: LongArray, val fix4: Long, val pos: FloatArray,
                 val textures: Map<Long, String>, val t: Long = System.nanoTime(),
                 val ou: FloatArray? = null,       // (build 278 : position de Rayman lue avec sa rotation, a la meme image que la pose)
                 val anim: AnimA3D? = null, val image: Float = 0f, val parents: IntArray? = null, val rot: FloatArray? = null,
                 val base: FloatArray? = null) {
    companion object {
        const val N = 25
        /** recomposition : G = P L (rangement du jeu : element [4j + i] = ligne i, colonne j de la rotation ; translation a 12..14) */
        fun composer(loc: FloatArray, pa: IntArray, rr: FloatArray, mat: FloatArray = FloatArray(N * 15)): FloatArray {
            val fait = BooleanArray(N)
            fun calc(k: Int, prof: Int) {
                if (fait[k]) return
                val p = pa[k]
                if (p >= 0 && prof < 30) calc(p, prof + 1)
                val P = if (p >= 0 && fait[p]) mat else rr; val po = if (p >= 0 && fait[p]) p * 15 else 0
                val px = if (p >= 0 && fait[p]) mat[po + 12] else 0f; val py = if (p >= 0 && fait[p]) mat[po + 13] else 0f; val pz = if (p >= 0 && fait[p]) mat[po + 14] else 0f
                val o = k * 15
                for (i in 0..2) {
                    for (j in 0..2) { var s = 0f; for (q in 0..2) s += P[po + 4 * q + i] * loc[o + 4 * j + q]; mat[o + 4 * j + i] = s }
                    var s = 0f; for (q in 0..2) s += P[po + 4 * q + i] * loc[o + 12 + q]; mat[o + 12 + i] = s + (if (i == 0) px else if (i == 1) py else pz)
                }
                fait[k] = true
            }
            for (k in 0 until N) calc(k, 0)
            return mat
        }
    }
}

/** build 284 : une animation du jeu (format A3D de Rayman 3 GameCube), lue une fois dans la memoire puis rejouee par l'appli.
 *  Format trouve sur quatre photos de la memoire (attente, course, saut, poing) : en-tete de 30 u16 (vitesse, vecteurs,
 *  quaternions, hierarchie, NTTO, numNTTO, ?, canaux, images, 0, 0, cles...), puis vecteurs (3 flottants), quaternions
 *  (4 s16 / 32767 : x y z w), hierarchie (2 u16), NTTO (6 octets), une fiche de 14 octets par image, (alignement 4),
 *  canaux (16 octets : nombre de cles, numero, NTTO, debut dans la table image->cle, premiere cle), numNTTO (u16),
 *  (alignement 4), table image -> cle (u32 par canal et par image), cles (14 octets : image, drapeaux, quaternion, 0,
 *  echelle, translation, ?). Rotation locale du noeud = transposee de celle du quaternion, colonnes multipliees par
 *  l'echelle. Image affichee : +0xD4 des donnees 3D du perso (verifie : ecart < 0,002 sur la pose d'attente). */
class AnimA3D(b: ByteArray, h: Int) {
    val vitesse: Int; val nf: Int
    private val vec: FloatArray; private val quat: FloatArray
    private val chNkf: IntArray; private val chId: IntArray; private val chFs: IntArray; private val chKs: IntArray
    private val fkf: IntArray
    private val kImg: IntArray; private val kQ: IntArray; private val kE: IntArray; private val kT: IntArray
    val octets: Int
    init {
        fun u16(o: Int) = ((b[o].toInt() and 255) shl 8) or (b[o + 1].toInt() and 255)
        fun s16(o: Int) = u16(o).toShort().toInt()
        fun u32(o: Int) = ((b[o].toInt() and 255) shl 24) or ((b[o + 1].toInt() and 255) shl 16) or ((b[o + 2].toInt() and 255) shl 8) or (b[o + 3].toInt() and 255)
        fun f32(o: Int) = java.lang.Float.intBitsToFloat(u32(o))
        val c = IntArray(17) { u16(h + 2 * it) }
        vitesse = c[0]; nf = c[8]
        val nv = c[1]; val nq = c[2]; val nh = c[3]; val nn = c[4]; val nnn = c[5]; val nch = c[7]; val nkf = c[11]
        require(nf in 1..5000 && nch in 1..64 && nkf in 1..20000)
        var o = h + 0x3c
        vec = FloatArray(3 * nv) { f32(o + 4 * it) }; o += 12 * nv
        quat = FloatArray(4 * nq) { s16(o + 2 * it) / 32767f }; o += 8 * nq
        o += 4 * nh + 6 * nn + 14 * nf; o = (o + 3) and 3.inv()
        chNkf = IntArray(nch) { u16(o + 16 * it) }; chId = IntArray(nch) { u16(o + 16 * it + 2) }
        chFs = IntArray(nch) { u32(o + 16 * it + 8) }; chKs = IntArray(nch) { u32(o + 16 * it + 12) }
        o += 16 * nch; o += 2 * nch * nnn; o = (o + 3) and 3.inv()
        fkf = IntArray(nch * nf) { u32(o + 4 * it) }; o += 4 * nch * nf
        kImg = IntArray(nkf) { u16(o + 14 * it) }; kQ = IntArray(nkf) { u16(o + 14 * it + 4) }
        kE = IntArray(nkf) { u16(o + 14 * it + 8) }; kT = IntArray(nkf) { u16(o + 14 * it + 10) }
        o += 14 * nkf
        octets = o - h
        for (k in 0 until nkf) require(kQ[k] < nq && kE[k] < nv && kT[k] < nv)
        for (k in 0 until nch) require(chKs[k] + chNkf[k] <= nkf && chFs[k] + nf <= fkf.size)
        for (v in fkf) require(v in 0 until nkf)
    }
    /** matrices locales des noeuds a l'image t (seuls les noeuds animes sont ecrits) */
    fun locales(t: Float, loc: FloatArray) {
        val f = (Math.floor(t.toDouble()).toInt() % nf + nf) % nf
        for (k in chId.indices) {
            val n = CANAL_NOEUD[chId[k]] ?: continue
            val i = fkf[chFs[k] + f]
            val j = if (i + 1 < chKs[k] + chNkf[k]) i + 1 else chKs[k]
            val fa = kImg[i].toFloat(); var fb = kImg[j].toFloat(); if (fb <= fa) fb = nf.toFloat()
            val tt = t - Math.floor(t.toDouble() / nf).toFloat() * nf
            val u = if (fb == fa) 0f else ((tt - fa) / (fb - fa)).coerceIn(0f, 1f)
            val qa = kQ[i] * 4; val qb = kQ[j] * 4
            val sg = if (quat[qa] * quat[qb] + quat[qa + 1] * quat[qb + 1] + quat[qa + 2] * quat[qb + 2] + quat[qa + 3] * quat[qb + 3] < 0f) -1f else 1f
            var x = quat[qa] * (1 - u) + sg * quat[qb] * u; var y = quat[qa + 1] * (1 - u) + sg * quat[qb + 1] * u
            var z = quat[qa + 2] * (1 - u) + sg * quat[qb + 2] * u; var w = quat[qa + 3] * (1 - u) + sg * quat[qb + 3] * u
            val ln = Math.sqrt((x * x + y * y + z * z + w * w).toDouble()).toFloat().coerceAtLeast(1e-6f); x /= ln; y /= ln; z /= ln; w /= ln
            val ea = kE[i] * 3; val eb = kE[j] * 3; val ta = kT[i] * 3; val tb = kT[j] * 3
            // rotation du quaternion (R), le noeud prend sa transposee ; colonne j multipliee par l'echelle j
            val R = floatArrayOf(1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                                 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                                 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y))
            val o = n * 15
            for (jj in 0..2) { val e = vec[ea + jj] * (1 - u) + vec[eb + jj] * u
                for (ii in 0..2) loc[o + 4 * jj + ii] = R[3 * jj + ii] * e }      // (Gr[i][j] = R[j][i] * e[j], range en [4j + i])
            for (jj in 0..2) loc[o + 12 + jj] = vec[ta + jj] * (1 - u) + vec[tb + jj] * u
        }
    }
    companion object {
        /** numero de canal des animations de Rayman -> noeud de son squelette (ordre de ses fiches de canaux en memoire) */
        val CANAL_NOEUD: Map<Int, Int> = mapOf(0 to 0, 1 to 1, 3 to 2, 4 to 3, 5 to 5, 6 to 7, 7 to 8, 8 to 9, 9 to 11, 10 to 4, 11 to 6,
            12 to 10, 13 to 12, 14 to 13, 15 to 14, 16 to 15, 17 to 16, 18 to 17, 19 to 18, 20 to 19, 21 to 20, 22 to 21, 25 to 22, 26 to 23, 111 to 24)
    }
}

object LecturePose {
    private var so0 = 0L; private var l0 = 0L; private var mr = 0L; private var fix4 = 0L; private var d3 = 0L; private var verifie = 0L
    private var objets: Triple<IntArray, LongArray, IntArray>? = null; private var tObjets = 0L
    private var textures: Map<Long, String> = emptyMap(); private var tTex = 0L
    // build 284 : animations deja lues (par nom), et celles qu'on n'a pas su lire (on reessaie plus tard)
    private val anims = java.util.concurrent.ConcurrentHashMap<String, AnimA3D>(); private val rates = java.util.concurrent.ConcurrentHashMap<String, Long>()
    private var refNom = 0L; private var nom = ""
    @Volatile private var chargement: Thread? = null
    /** build 276 : les matrices globales du jeu sont lues bloc par bloc, donc a des images differentes : quand Rayman va
     *  vite, les morceaux se decalaient. On lit donc les matrices LOCALES (chaque noeud par rapport a son parent, tableau
     *  contigu pointe par +0x20 des noeuds) et la rotation de Rayman, et on recompose le squelette ici, autour de Rayman
     *  (verifie sur les quatre photos : la recomposition redonne les matrices globales du jeu).
     *  Build 284 : si l'animation en cours est connue (nom lu dans le jeu), on ne lit plus que son image et la place de
     *  Rayman (2 blocs) ; l'appli rejoue l'animation a chaque image de l'ecran. */
    fun lire(mod: ModeleAnime?, dossier: File? = null): PoseRayman? {
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
            d3 = l.ptr(perso) ?: 0L
            so0 = s; fix4 = perso - mod.perso; verifie = now
        }
        if (now - tObjets > 1000 || objets == null) {             // (l'objet de chaque noeud : corps, mains, pouvoirs ; et son parent)
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
        // rotation et position de Rayman (un bloc)
        val rr = FloatArray(12)
        for (j in 0 until 12) rr[j] = l.f32(mr + 4 + 4 * j) ?: return null
        val ou = FloatArray(3)
        for (j in 0..2) ou[j] = l.f32(mr + 0x34 + 4 * j) ?: return null
        if (ou.any { !it.isFinite() || Math.abs(it) > 1e5f } || rr.any { !it.isFinite() || Math.abs(it) > 1e3f }) { so0 = 0L; return null }
        val pos = FloatArray(3)
        // build 284 : l'animation en cours (nom et image : un bloc)
        var anim: AnimA3D? = null; var image = 0f
        if (d3 != 0L) {
            val ref = l.ptr(d3 + 0xBC); val img = l.f32(d3 + 0xD4); val hier = l.ptr(d3 + 0x7C)
            if (ref != null && img != null && img.isFinite() && hier != null) {
                if (ref != refNom) {          // (nom du fichier de l'animation, lu octet par octet : il peut passer d'un bloc a l'autre)
                    val sb = StringBuilder()
                    for (k in 0 until 0x50) { val c = l.u8(ref + k) ?: break; if (c == 0) break; sb.append(c.toChar()) }
                    nom = sb.toString().substringAfterLast('\\').lowercase(); refNom = ref
                }
                anim = anims[nom]
                if (anim == null && nom.isNotEmpty()) charger(nom, hier, dossier)
                if (anim != null) image = img
            }
        }
        val loc = FloatArray(PoseRayman.N * 15)
        if (anim == null) {
            // (pas d'animation connue : on lit les 25 matrices locales, 3 a 4 blocs)
            for (k in 0 until PoseRayman.N) {
                val a = l0 + 0x54L * k
                for (j in 0 until 12) loc[k * 15 + j] = l.f32(a + 4 + 4 * j) ?: return null
                for (j in 0..2) loc[k * 15 + 12 + j] = l.f32(a + 0x34 + 4 * j) ?: return null
            }
            if (loc.any { !it.isFinite() || Math.abs(it) > 1e5f }) { so0 = 0L; return null }
            dernieresLocales = loc
        } else {
            System.arraycopy(dernieresLocales ?: identites(), 0, loc, 0, loc.size)
            anim.locales(image, loc)
        }
        val mat = PoseRayman.composer(loc, pa, rr)
        if (now - tTex > 1000) {                                   // (la texture de chaque morceau affiche : change avec les pouvoirs)
            val r = HashMap<Long, String>()
            for ((g, _) in mod.morceaux(PoseRayman(mat, ty, da, fix4, pos, emptyMap()))) for (e in mod.geos[g]?.el ?: emptyList()) {
                val mt = l.ptr(fix4 + e.eo) ?: continue
                val ti = l.u32(mt + 0x68) ?: continue
                mod.tex[ti - fix4]?.let { r[e.eo] = it }
            }
            textures = r; tTex = now
        }
        return PoseRayman(mat, ty, da, fix4, pos, textures, ou = ou, anim = anim, image = image, parents = pa, rot = rr,
            base = if (anim != null) (dernieresLocales ?: identites()) else null)
    }
    @Volatile private var dernieresLocales: FloatArray? = null
    private fun identites() = FloatArray(PoseRayman.N * 15) { val j = it % 15; if (j == 0 || j == 5 || j == 10) 1f else 0f }

    /** lire une animation dans la memoire (une fois) : la fenetre autour de sa hierarchie, en tache de fond */
    private fun charger(nom: String, hier: Long, dossier: File?) {
        if ((rates[nom] ?: 0L) > System.currentTimeMillis()) return
        if (chargement?.isAlive == true) return
        val f = dossier?.let { File(File(it, "anims-r3"), nom.replace(Regex("[^a-z0-9_.]"), "_") + ".bin") }
        if (f != null && f.exists()) try { anims[nom] = AnimA3D(f.readBytes(), 0); return } catch (e: Throwable) { f.delete() }
        chargement = Thread {
            try {
                var ok = false
                for (avant in intArrayOf(0x3000, 0x8000)) {
                    val debut = (hier - avant) and 0x3FFL.inv(); val fin = hier + avant * 2
                    val w = ByteArray((fin - debut).toInt())
                    var b = debut; var trou = false
                    while (b < fin) { val d = MemoireJeu.bloc(b); if (d == null) { trou = true; break }; System.arraycopy(d, 0, w, (b - debut).toInt(), MemoireJeu.BLK); b += MemoireJeu.BLK; Thread.sleep(2) }
                    if (trou) break
                    val H = (hier - debut).toInt()
                    var h = H - 0x3c
                    while (h >= 0 && !ok) {
                        val nv = ((w[h + 2].toInt() and 255) shl 8) or (w[h + 3].toInt() and 255)
                        val nq = ((w[h + 4].toInt() and 255) shl 8) or (w[h + 5].toInt() and 255)
                        if (h + 0x3c + nv * 12 + nq * 8 == H && nv > 0 && nq > 0) {
                            try {
                                val a = AnimA3D(w, h)
                                if (h + a.octets <= w.size) {
                                    val brut = w.copyOfRange(h, h + a.octets)
                                    val a2 = AnimA3D(brut, 0)
                                    anims[nom] = a2
                                    f?.let { it.parentFile?.mkdirs(); it.writeBytes(brut) }
                                    ok = true
                                }
                            } catch (e: Throwable) { }
                        }
                        h -= 2
                    }
                    if (ok) break
                }
                if (!ok) rates[nom] = System.currentTimeMillis() + 10_000
            } catch (e: Throwable) { rates[nom] = System.currentTimeMillis() + 10_000 }
        }.also { it.isDaemon = true; it.start() }
    }
}
