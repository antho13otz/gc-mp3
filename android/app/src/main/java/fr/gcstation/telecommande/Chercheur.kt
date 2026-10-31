package fr.gcstation.telecommande

// Appli : chercheur de codes (GC Station v306). Pendant une partie lancee avec Pulse (reglage de la console « Chercheur
// de codes (appli) »), le telephone lit la memoire du jeu par le Broadband Adapter (UDP, port 51237, voir natres.h) :
//  - une recherche : valeur connue (ex. 3 vies) ou inconnue, puis comparaisons (= valeur, plus grand, plus petit, a
//    change, pareil, + valeur, - valeur) entre deux « photos » de la memoire, jusqu'a quelques adresses ;
//  - le jeu est mis en pause le temps d'une photo (24 Mo, les mots nuls ne passent pas), puis repart tout seul ;
//  - une adresse trouvee : changer sa valeur tout de suite, en faire un cheat (code Action Replay / Gecko 00/02/04) ou un
//    succes (m8/m16/m32(adresse) >= valeur) : gardes sur le telephone et envoyes a GC Station au retour du jeu.
// Succes obtenus pendant le jeu : Pulse les annonce (« GCAC ») et l'appli les demande toutes les 3 s : notification.

import android.content.Context
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.padding

import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.SocketTimeoutException
import java.util.BitSet

/** le reseau vers Pulse (resident du jeu) : etat, pause, lecture et ecriture de la memoire */
object MemoireJeu {
    const val PORT = 51237
    const val BASE = 0x80000000L
    const val BLK = 1024
    const val FIN = 0x017EE000                       // (au-dessus : Pulse lui-meme, pas cherche)
    const val TAILLE = 0x01800000

    class Etat(val pause: Boolean, val jeu: String, val image: Long, val nSucces: Int, val obtenus: Long)

    private var sock: DatagramSocket? = null
    private var seq = 1
    private val buf = ByteArray(2048)
    @Volatile var annonce: ((Int, Long) -> Unit)? = null   // (succes annonces par Pulse : n, obtenus)

    private fun s(): DatagramSocket = sock ?: DatagramSocket().apply { try { receiveBufferSize = 4 shl 20 } catch (e: Exception) { } }.also { sock = it }
    @Synchronized fun fermer() { try { sock?.close() } catch (e: Exception) { }; sock = null }
    private fun hote(): InetAddress? = try { Api.host?.substringBefore(':')?.let { InetAddress.getByName(it) } } catch (e: Exception) { null }

    private fun g32(b: ByteArray, o: Int) = (b[o].toLong() and 255 shl 24) or (b[o + 1].toLong() and 255 shl 16) or (b[o + 2].toLong() and 255 shl 8) or (b[o + 3].toLong() and 255)
    private fun p32(b: ByteArray, o: Int, v: Long) { b[o] = (v shr 24).toByte(); b[o + 1] = (v shr 16).toByte(); b[o + 2] = (v shr 8).toByte(); b[o + 3] = v.toByte() }
    private fun magie(b: ByteArray, m: String) = b[0].toInt() == m[0].code && b[1].toInt() == m[1].code && b[2].toInt() == m[2].code && b[3].toInt() == m[3].code

    private fun envoyer(h: InetAddress, t: Char, sq: Int, a: Long, b: Long, c: Long) {
        val d = ByteArray(20)
        d[0] = 'G'.code.toByte(); d[1] = 'C'.code.toByte(); d[2] = 'M'.code.toByte(); d[3] = t.code.toByte()
        p32(d, 4, sq.toLong()); p32(d, 8, a); p32(d, 12, b); p32(d, 16, c)
        s().send(DatagramPacket(d, 20, h, PORT))
    }
    /** un paquet de la console dans buf (longueur), 0 : annonce de succes traitee, -1 : rien */
    private fun recevoir(ms: Int): Int {
        val so = s()
        so.soTimeout = ms.coerceAtLeast(1)
        val p = DatagramPacket(buf, buf.size)
        try { so.receive(p) } catch (e: SocketTimeoutException) { return -1 }
        if (p.length >= 16 && magie(buf, "GCAC")) { annonce?.invoke(g32(buf, 4).toInt(), g32(buf, 8) or (g32(buf, 12) shl 32)); return 0 }
        return p.length
    }

    /** I (etat), P (pause : a = 1, reprise : a = 0), W (ecrire c, b octets, en a) ; null : Pulse ne repond pas */
    @Synchronized fun demande(t: Char, a: Long = 0, b: Long = 0, c: Long = 0): Etat? {
        val h = hote() ?: return null
        try {
            repeat(3) {
                val sq = ++seq
                envoyer(h, t, sq, a, b, c)
                val fin = System.currentTimeMillis() + 450
                while (true) {
                    val reste = (fin - System.currentTimeMillis()).toInt()
                    if (reste <= 0) break
                    val n = recevoir(reste)
                    if (n >= 36 && magie(buf, "GCMA") && g32(buf, 4).toInt() == sq) {
                        val jeu = String(buf, 12, 6, Charsets.ISO_8859_1).trim { it <= ' ' }
                        return Etat(g32(buf, 8) != 0L, jeu, g32(buf, 20), g32(buf, 24).toInt(), g32(buf, 28) or (g32(buf, 32) shl 32))
                    }
                }
            }
        } catch (e: Exception) { fermer() }
        return null
    }

    /** lit les blocs (numeros, dans l'ordre) dans mem (decalage = adresse - BASE) ; false : abandon ou console muette */
    @Synchronized fun lire(blocs: IntArray, mem: ByteArray, enPause: Boolean, progres: (Float) -> Unit, annule: () -> Boolean): Boolean {
        val h = hote() ?: return false
        val max = if (enPause) 64 else 2
        val file = ArrayDeque<IntArray>()                     // {premier bloc, nombre, essais}
        var i = 0
        while (i < blocs.size) {
            var j = i + 1
            while (j < blocs.size && blocs[j] == blocs[j - 1] + 1 && j - i < max) j++
            file.addLast(intArrayOf(blocs[i], j - i, 0)); i = j
        }
        var faits = 0
        try {
            while (file.isNotEmpty()) {
                if (annule()) return false
                val (b0, n, essais) = file.removeFirst()
                if (essais > 6) return false
                val sq = ++seq
                envoyer(h, 'R', sq, BASE + b0.toLong() * BLK, n.toLong(), 0)
                val eu = BooleanArray(n)
                var k = 0
                while (k < n) {
                    val r = recevoir(if (k == 0) 400 else 250)
                    if (r < 0) break
                    if (r < 48 || !magie(buf, "GCMD") || g32(buf, 4).toInt() != sq) continue
                    val off = (g32(buf, 8) - BASE).toInt()
                    val x = off / BLK - b0
                    if (off % BLK != 0 || x < 0 || x >= n || eu[x]) continue
                    var o = 48
                    for (w in 0 until BLK / 4) {
                        val d = off + w * 4
                        if (buf[16 + (w shr 3)].toInt() and (0x80 shr (w and 7)) != 0 && o + 4 <= r) {
                            mem[d] = buf[o]; mem[d + 1] = buf[o + 1]; mem[d + 2] = buf[o + 2]; mem[d + 3] = buf[o + 3]; o += 4
                        } else { mem[d] = 0; mem[d + 1] = 0; mem[d + 2] = 0; mem[d + 3] = 0 }
                    }
                    eu[x] = true; k++; faits++
                }
                if (k < n) {                                    // (paquets perdus : redemandes)
                    var x = 0
                    while (x < n) {
                        if (eu[x]) { x++; continue }
                        var y = x
                        while (y < n && !eu[y]) y++
                        file.addFirst(intArrayOf(b0 + x, y - x, essais + 1)); x = y
                    }
                }
                progres(faits.toFloat() / blocs.size)
            }
        } catch (e: Exception) { fermer(); return false }
        return true
    }

    // ---------- succes en direct : demandes toutes les 3 s pendant une partie (et annonces de Pulse) ----------
    @Volatile private var veille: Thread? = null
    fun veiller(c: Context) {
        val g = Live.game ?: return
        if (!g.mem || veille?.isAlive == true) return
        val ctx = c.applicationContext
        val jeu = g.id
        veille = Thread {
            var vus = -1L
            fun voir(n: Int, bits: Long) {
                if (n <= 0) return
                if (vus < 0) { vus = bits; return }
                val neufs = bits and vus.inv()
                vus = vus or bits
                for (i in 0 until minOf(n, 64)) if (neufs shr i and 1L == 1L) {
                    val t = Attente.titreSucces(ctx, jeu, i)
                    PlaybackService.notify(ctx, 9306 + i % 8, "🏆 " + Lang.t("Succès obtenu") + (if (t.isEmpty()) "" else " : $t"), Lang.t("En direct, pendant la partie (chercheur de codes)"))
                }
            }
            annonce = { n, b -> voir(n, b) }
            while (Live.game?.id == jeu) {
                demande('I')?.let { voir(it.nSucces, it.obtenus) }
                try { Thread.sleep(3000) } catch (e: InterruptedException) { break }
            }
            annonce = null
        }.apply { isDaemon = true; start() }
    }
}

/** cheats et succes crees pendant le jeu : gardes ici, envoyes a GC Station quand elle revient */
object Attente {
    private fun pr(c: Context) = c.getSharedPreferences("gcs", 0)
    fun liste(c: Context): JSONArray = try { JSONArray(pr(c).getString("attente_codes", "[]")) } catch (e: Exception) { JSONArray() }
    private fun garder(c: Context, a: JSONArray) { pr(c).edit().putString("attente_codes", a.toString()).apply() }
    fun ajouter(c: Context, sorte: String, jeu: String, nom: String, valeur: String) {
        val a = liste(c)
        a.put(JSONObject().put("k", sorte).put("id", jeu).put("nom", nom).put("v", valeur))
        garder(c, a)
    }
    fun retirer(c: Context, i: Int) { val a = liste(c); a.remove(i); garder(c, a) }

    /** (fil a part) console revenue : tout envoyer ; rend le nombre envoye */
    fun envoyer(c: Context): Int {
        val a = liste(c)
        if (a.length() == 0) return 0
        val reste = JSONArray()
        var n = 0
        for (i in 0 until a.length()) {
            val o = a.optJSONObject(i) ?: continue
            val id = o.optString("id"); val nom = o.optString("nom"); val v = o.optString("v")
            val r = if (o.optString("k") == "cheat") Api.get("/api/cheatedit?id=$id&op=a&name=" + Api.enc(nom) + "&codes=" + v, 10000)
                    else Api.get("/api/succesadd?id=$id&codes=" + Api.enc(v), 10000)
            if (r == null) reste.put(o)                          // (pas de reponse : la prochaine fois)
            else if (o.optString("k") == "cheat" || r.contains("\"ok\":1")) n++
        }
        garder(c, reste)
        return n
    }

    /** titres des succes (liste de GC Station gardee quand elle repond) : le i-eme du jeu */
    fun garderSucces(c: Context, json: String) { pr(c).edit().putString("succes_cache", json).apply() }
    fun titreSucces(c: Context, jeu: String, i: Int): String = try {
        val a = JSONArray(pr(c).getString("succes_cache", "[]"))
        var k = 0; var t = ""
        for (x in 0 until a.length()) { val e = a.getJSONArray(x); if (e.getString(0) == jeu) { if (k == i) { t = e.getString(1); break }; k++ } }
        t
    } catch (e: Exception) { "" }
}

/** la recherche en cours (garde quand on change d'ecran) */
private object Recherche {
    var taille by mutableStateOf(1)                          // 1, 2 ou 4 octets
    var mem: ByteArray? = null                               // derniere photo de la memoire
    var cand: BitSet? = null                                 // adresses encore possibles (decalage / taille) ; null : toutes
    var nb by mutableLongStateOf(-1L)                        // -1 : pas de recherche
    var tache by mutableStateOf("")                          // travail en cours ("" : rien)
    var progres by mutableFloatStateOf(0f)
    var annule = false
    var etat by mutableStateOf<MemoireJeu.Etat?>(null)
    var erreur by mutableStateOf("")
    var etapes by mutableStateOf(0)
    val res = mutableStateListOf<LongArray>()                // {adresse, valeur de la photo, valeur actuelle}
    val direct by lazy { ByteArray(MemoireJeu.TAILLE) }       // (valeurs en direct : lues ici, une seule fois alloue)

    fun v(m: ByteArray, o: Int, t: Int): Long = when (t) {
        1 -> m[o].toLong() and 255
        2 -> (m[o].toLong() and 255 shl 8) or (m[o + 1].toLong() and 255)
        else -> (m[o].toLong() and 255 shl 24) or (m[o + 1].toLong() and 255 shl 16) or (m[o + 2].toLong() and 255 shl 8) or (m[o + 3].toLong() and 255)
    }
    fun masque(t: Int) = if (t == 4) 0xFFFFFFFFL else (1L shl (8 * t)) - 1
}

private val MODES = listOf("=" to "Égal à", ">" to "Plus grand", "<" to "Plus petit", "!" to "A changé", "~" to "Pas changé", "+" to "Augmenté de", "-" to "Diminué de")

@Composable
fun EcranChercheur() {
    val act = LocalContext.current as StationActivity
    val scope = rememberCoroutineScope()
    var valeur by remember { mutableStateOf("") }
    var mode by remember { mutableStateOf("=") }
    var choisi by remember { mutableStateOf<LongArray?>(null) }
    var attente by remember { mutableStateOf(Attente.liste(act).length()) }
    val R = Recherche
    val g = St.jeu

    fun nombre(s: String): Long? = s.trim().let { if (it.startsWith("0x", true)) it.substring(2).toLongOrNull(16) else it.toLongOrNull() }

    // photo de la memoire (jeu en pause), puis filtre
    fun photo(nouvelle: Boolean, cible: Long?) {
        if (R.tache.isNotEmpty()) return
        R.annule = false; R.erreur = ""; R.tache = "Pause du jeu…"; R.progres = 0f
        scope.launch {
            val t = R.taille
            val ok = withContext(Dispatchers.IO) {
                val e = MemoireJeu.demande('P', 1)
                if (e == null) return@withContext "Pulse ne répond pas. Sur la console : START > Réglages > Chercheur de codes (appli) = Oui, puis relance le jeu (Broadband Adapter branché)."
                R.etat = e
                val ancienne = R.mem
                val neuf = if (nouvelle || ancienne == null) ByteArray(MemoireJeu.TAILLE) else ancienne.copyOf()
                val c = R.cand
                val blocs = if (nouvelle || c == null) IntArray(MemoireJeu.FIN / MemoireJeu.BLK) { it } else {
                    val l = ArrayList<Int>(); var last = -1
                    var i = c.nextSetBit(0)
                    while (i >= 0) { val b = i * t / MemoireJeu.BLK; if (b != last) { l.add(b); last = b }; i = c.nextSetBit(i + 1) }
                    l.toIntArray()
                }
                R.tache = "Photo de la mémoire (jeu en pause)…"
                val lu = MemoireJeu.lire(blocs, neuf, true, { R.progres = it }, { R.annule })
                MemoireJeu.demande('P', 0)                  // (reprise ; sinon toute seule apres 20 s)
                if (!lu) return@withContext if (R.annule) "" else "La console a cessé de répondre pendant la photo"
                R.tache = "Comparaison…"
                val n = MemoireJeu.FIN / t
                val m = Recherche.masque(t)
                val nc = BitSet(n)
                if (nouvelle) {
                    if (cible != null) for (i in 0 until n) { if (Recherche.v(neuf, i * t, t) == cible and m) nc.set(i) }
                    R.cand = if (cible != null) nc else null
                    R.nb = if (cible != null) nc.cardinality().toLong() else n.toLong()
                } else {
                    val old = ancienne!!
                    val x = (cible ?: 0L) and m
                    var i = if (c == null) 0 else c.nextSetBit(0)
                    while (i in 0 until n) {
                        val a = Recherche.v(old, i * t, t); val b = Recherche.v(neuf, i * t, t)
                        val garde = when (mode) {
                            "=" -> b == x; ">" -> b > a; "<" -> b < a; "!" -> b != a; "~" -> b == a
                            "+" -> b == (a + x) and m; else -> b == (a - x) and m
                        }
                        if (garde) nc.set(i)
                        i = if (c == null) i + 1 else c.nextSetBit(i + 1)
                    }
                    R.cand = nc; R.nb = nc.cardinality().toLong()
                }
                R.mem = neuf
                R.etapes = if (nouvelle) 1 else R.etapes + 1
                // (liste : 300 au plus)
                val l = ArrayList<LongArray>()
                val cc = R.cand
                if (cc != null) {
                    var i = cc.nextSetBit(0)
                    while (i >= 0 && l.size < 300) { val v = Recherche.v(neuf, i * t, t); l.add(longArrayOf(MemoireJeu.BASE + i.toLong() * t, v, v)); i = cc.nextSetBit(i + 1) }
                }
                withContext(Dispatchers.Main) { R.res.clear(); R.res.addAll(l) }
                ""
            }
            R.erreur = ok; R.tache = ""
        }
    }

    // valeurs en direct quand il reste peu d'adresses (jeu pas en pause : 2 blocs a la fois)
    LaunchedEffect(R.nb, R.etapes) {
        while (R.nb in 1L..40L && R.tache.isEmpty()) {
            delay(1200)
            if (R.tache.isNotEmpty()) continue
            val t = R.taille
            val lst = R.res.toList()
            val tmp = Recherche.direct
            val blocs = lst.map { ((it[0] - MemoireJeu.BASE) / MemoireJeu.BLK).toInt() }.distinct().sorted().toIntArray()
            val ok = withContext(Dispatchers.IO) { MemoireJeu.lire(blocs, tmp, false, { }, { false }) }
            if (!ok) continue
            for ((k, r) in lst.withIndex()) {
                val v = Recherche.v(tmp, (r[0] - MemoireJeu.BASE).toInt(), t)
                if (k < R.res.size && R.res[k][0] == r[0] && R.res[k][2] != v) R.res[k] = longArrayOf(r[0], r[1], v)
            }
        }
    }
    LaunchedEffect(Unit) { R.etat = withContext(Dispatchers.IO) { MemoireJeu.demande('I') } }

    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        item {
            val e = R.etat
            Doux(when {
                g == null -> "Lance un jeu avec Pulse : l'appli lit sa mémoire pendant la partie."
                !g.mem && e == null -> "Ce jeu a été lancé sans le chercheur de codes. Sur la console : START > Réglages > Chercheur de codes (appli) = Oui, puis relance le jeu."
                e == null -> "Pulse ne répond pas (Broadband Adapter branché ? même Wi-Fi ?)"
                else -> "En jeu : " + (g.name.ifEmpty { e.jeu }) + " (" + e.jeu + ")" + if (e.nSucces > 0) " · succès : " + java.lang.Long.bitCount(e.obtenus) + "/" + e.nSucces else ""
            }, taille = 12, couleur = if (e == null) C.Orange else C.Doux)
        }
        item {
            Titre("Taille de la valeur")
            Spacer(Modifier.height(6.dp))
            RangeeDePuces {
                for ((k, n) in listOf(1 to "Octet (0-255)", 2 to "16 bits", 4 to "32 bits"))
                    Puce(n, R.taille == k) { if (R.tache.isEmpty() && R.taille != k) { R.taille = k; R.nb = -1; R.cand = null; R.res.clear() } }
            }
        }
        if (R.tache.isNotEmpty()) item {
            Column(Modifier.fillMaxWidth().verre().padding(14.dp)) {
                Text(R.tache, color = C.Texte, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.height(8.dp))
                LinearProgressIndicator(progress = { R.progres }, Modifier.fillMaxWidth(), color = C.Rose, trackColor = C.Bord)
                Spacer(Modifier.height(8.dp))
                Doux("Le jeu est figé pendant la photo, puis repart tout seul.", taille = 11, couleur = C.Pale)
                Spacer(Modifier.height(8.dp))
                Bouton("Annuler", Modifier.fillMaxWidth()) { R.annule = true }
            }
        }
        if (R.erreur.isNotEmpty()) item { Doux(R.erreur, taille = 12, couleur = C.Rouge) }
        item {
            Column(Modifier.fillMaxWidth().verre().padding(14.dp)) {
                if (R.nb < 0) {
                    Titre("Nouvelle recherche")
                    Spacer(Modifier.height(8.dp))
                    Champ(valeur, { valeur = it }, "Valeur à l'écran (ex. 3 vies)", clavier = KeyboardType.Number)
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Bouton("Valeur connue", Modifier.weight(1f), plein = true, actif = R.tache.isEmpty() && nombre(valeur) != null) { photo(true, nombre(valeur)) }
                        Bouton("Valeur inconnue", Modifier.weight(1f), actif = R.tache.isEmpty()) { photo(true, null) }
                    }
                    Spacer(Modifier.height(8.dp))
                    Doux("Exemple : tu as 3 vies. Cherche 3, perds une vie dans le jeu, puis compare « Égal à 2 ». Recommence jusqu'à quelques adresses. Barre d'énergie sans chiffre : valeur inconnue, puis « Plus petit » après un coup.", taille = 11, couleur = C.Pale)
                } else {
                    Text(String.format(java.util.Locale.FRANCE, "%,d", R.nb) + if (R.nb > 1) Lang.t(" adresses possibles") else Lang.t(" adresse possible"),
                        color = C.Texte, fontSize = 18.sp, fontWeight = FontWeight.Bold)
                    Doux("Étape " + R.etapes + " · joue un peu, puis compare avec la photo d'avant", taille = 11)
                    Spacer(Modifier.height(8.dp))
                    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        for ((k, n) in MODES) Puce(n, mode == k) { mode = k }
                    }
                    if (mode in listOf("=", "+", "-")) { Spacer(Modifier.height(8.dp)); Champ(valeur, { valeur = it }, "Valeur", clavier = KeyboardType.Number) }
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Bouton("Comparer", Modifier.weight(1f), plein = true, actif = R.tache.isEmpty() && R.nb > 0 && (mode !in listOf("=", "+", "-") || nombre(valeur) != null)) {
                            photo(false, if (mode in listOf("=", "+", "-")) nombre(valeur) else null)
                        }
                        Bouton("Recommencer", Modifier.weight(1f), actif = R.tache.isEmpty()) { R.nb = -1; R.cand = null; R.mem = null; R.res.clear(); R.etapes = 0 }
                    }
                }
            }
        }
        if (R.nb in 1L..300L) {
            item { Titre(if (R.nb <= 40) "Adresses (valeurs en direct)" else "Adresses") }
            items(R.res.toList()) { r ->
                val ch = r[1] != r[2]
                Row(Modifier.fillMaxWidth().verre().appui { choisi = r }.padding(horizontal = 14.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(String.format("%08X", r[0]), color = C.Cyan, fontSize = 14.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.weight(1f))
                    Text(if (ch) "${r[1]} → ${r[2]}" else "${r[2]}", color = if (ch) C.Orange else C.Texte, fontSize = 14.sp, fontFamily = FontFamily.Monospace)
                }
            }
        } else if (R.nb > 300) item { Doux("Trop d'adresses pour les afficher : compare encore.", taille = 12) }
        if (attente > 0) item {
            Spacer(Modifier.height(6.dp))
            Titre("En attente du retour à GC Station")
            Spacer(Modifier.height(6.dp))
            val a = Attente.liste(act)
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                for (i in 0 until a.length()) {
                    val o = a.getJSONObject(i)
                    Ligne((if (o.optString("k") == "cheat") "Cheat · " else "Succès · ") + o.optString("nom"), o.optString("id") + " · " + o.optString("v"),
                        droite = { Text("✕", color = C.Pale, fontSize = 18.sp, modifier = Modifier.appui { Attente.retirer(act, i); attente = Attente.liste(act).length() }.padding(6.dp)) })
                }
            }
            Doux("Envoyés tout seuls quand la console revient (A + Z + START).", taille = 11, couleur = C.Pale)
        }
        item { Spacer(Modifier.height(20.dp)) }
    }

    choisi?.let { r -> DialogueAdresse(act, r, g?.id?.ifEmpty { null } ?: R.etat?.jeu ?: "", R.taille, { choisi = null }) { attente = Attente.liste(act).length() } }
}

@Composable
private fun DialogueAdresse(act: StationActivity, r: LongArray, jeu: String, t: Int, fermer: () -> Unit, ajoute: () -> Unit) {
    val scope = rememberCoroutineScope()
    var quoi by remember { mutableStateOf(0) }                 // 0 menu, 1 ecrire, 2 cheat, 3 succes
    var v by remember { mutableStateOf(r[2].toString()) }
    var nom by remember { mutableStateOf("") }
    var desc by remember { mutableStateOf("") }
    var cmp by remember { mutableStateOf(">=") }
    val adr = String.format("%08X", r[0])
    val m = Recherche.masque(t)
    fun nombre(s: String): Long? = s.trim().let { if (it.startsWith("0x", true)) it.substring(2).toLongOrNull(16) else it.toLongOrNull() }
    AlertDialog(onDismissRequest = fermer, containerColor = C.Fond2,
        title = { Text(when (quoi) { 1 -> "Changer la valeur"; 2 -> "Créer un cheat"; 3 -> "Créer un succès"; else -> "Adresse $adr" }, color = C.Texte) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                when (quoi) {
                    0 -> {
                        Doux("Valeur : " + r[2] + " (" + (8 * t) + " bits)", taille = 13)
                        Bouton("Changer la valeur maintenant", Modifier.fillMaxWidth()) { quoi = 1 }
                        Bouton("Créer un cheat (valeur bloquée)", Modifier.fillMaxWidth()) { quoi = 2 }
                        Bouton("Créer un succès", Modifier.fillMaxWidth()) { quoi = 3 }
                    }
                    1 -> {
                        Champ(v, { v = it }, "Nouvelle valeur", clavier = KeyboardType.Number)
                        Doux("Écrite tout de suite dans le jeu (une fois : le jeu peut la changer ensuite).", taille = 11, couleur = C.Pale)
                    }
                    2 -> {
                        Champ(nom, { nom = it }, "Nom du cheat (ex. Vies infinies)")
                        Champ(v, { v = it }, "Valeur bloquée", clavier = KeyboardType.Number)
                        Doux("Pulse réécrit cette valeur à chaque image. Le cheat est ajouté au jeu au retour à GC Station.", taille = 11, couleur = C.Pale)
                    }
                    else -> {
                        Champ(nom, { nom = it }, "Titre (ex. Collectionneur)")
                        Champ(desc, { desc = it }, "Description (ex. Avoir 99 vies)")
                        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) { for (c in listOf(">=", "==", "<=", ">", "<")) Puce(c, cmp == c) { cmp = c } }
                        Champ(v, { v = it }, "Valeur", clavier = KeyboardType.Number)
                        Doux("Obtenu quand la valeur reste vraie une demi-seconde. Ajouté au jeu au retour à GC Station (pour la prochaine partie).", taille = 11, couleur = C.Pale)
                    }
                }
            }
        },
        confirmButton = {
            if (quoi != 0) TextButton({
                val x = nombre(v)
                if (x == null) { Lang.toast(act, "Valeur ?", Toast.LENGTH_SHORT).show(); return@TextButton }
                when (quoi) {
                    1 -> scope.launch {
                        val e = withContext(Dispatchers.IO) { MemoireJeu.demande('W', r[0], t.toLong(), x and m) }
                        Lang.toast(act, if (e != null) "Valeur écrite dans le jeu" else "Pulse ne répond pas", Toast.LENGTH_SHORT).show()
                        fermer()
                    }
                    2 -> {
                        if (nom.isBlank()) { Lang.toast(act, "Donne un nom au cheat", Toast.LENGTH_SHORT).show(); return@TextButton }
                        val type = when (t) { 1 -> 0x00L; 2 -> 0x02L; else -> 0x04L }
                        val code = String.format("%08X%08X", (type shl 24) or (r[0] and 0x01FFFFFF), x and m)
                        Attente.ajouter(act, "cheat", jeu, nom.trim(), code)
                        Lang.toast(act, "Gardé : envoyé à GC Station au retour du jeu", Toast.LENGTH_LONG).show()
                        ajoute(); fermer()
                    }
                    else -> {
                        if (nom.isBlank()) { Lang.toast(act, "Donne un titre au succès", Toast.LENGTH_SHORT).show(); return@TextButton }
                        val ligne = nom.trim().replace('|', '/') + " | " + desc.trim().replace('|', '/').ifEmpty { nom.trim() } +
                                " | m" + (8 * t) + "(0x" + adr + ") " + cmp + " " + (x and m)
                        Attente.ajouter(act, "succes", jeu, nom.trim(), ligne)
                        Lang.toast(act, "Gardé : envoyé à GC Station au retour du jeu", Toast.LENGTH_LONG).show()
                        ajoute(); fermer()
                    }
                }
            }) { Text(if (quoi == 1) "Écrire" else "Garder", color = C.Rose) }
        },
        dismissButton = { TextButton(fermer) { Text("Annuler", color = C.Doux) } })
}
