package fr.gcstation.telecommande

// Appli build 456 : Wind Waker, le « fantome » d'un ami. Le Link d'un ami (sa position, son cap, son allure, son bateau)
// apparait dans la vue 3D du telephone comme un fantome translucide, jamais dans la vraie partie (rien n'est envoye a la
// console). Les deux telephones se trouvent par un code a partager, par Internet, via un serveur MQTT public gratuit
// (choix du proprietaire, 7 novembre 2026) : broker.hivemq.com:1883, secours test.mosquitto.org:1883. Sujet
// gcstation/fantome/<code>, ~5 messages par seconde (JSON). Serveur public : quiconque connait le code voit les positions,
// sans garantie (dit a l'ecran). Client MQTT 3.1.1 minimal ecrit ici (CONNECT, SUBSCRIBE, PUBLISH QoS 0, PINGREQ).

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

object Fantome {
    val SERVEURS = listOf("broker.hivemq.com" to 1883, "test.mosquitto.org" to 1883)
    /** ce que l'ami envoie : lieu du jeu (etape, salle), position (jeu), cap (radians, angle du jeu), vitesse, nage ; bateau
     *  (x, y, z, cap) s'il est connu ; heure de reception (System.nanoTime) */
    class Ami(val id: String, val nom: String, val scene: String, val salle: Int, val pos: FloatArray, val cap: Float, val vitesse: Float,
              val nage: Boolean, val bateau: FloatArray?, val t: Long) {
        val lieu: String get() = CartesWW.lieu(scene, salle)
        fun frais() = System.nanoTime() - t < 5_000_000_000L
    }
    var actif by mutableStateOf(false); private set
    var code by mutableStateOf(""); private set
    var nom by mutableStateOf("")
    var statut by mutableStateOf("")
    var ami by mutableStateOf<Ami?>(null)
    /** « Aller le voir » : la vue 3D suit l'ami (son lieu s'il est ailleurs) */
    var visite by mutableStateOf(false)
    @Volatile var amiVif: Ami? = null                                   // (lu par le fil OpenGL)
    private var charge = false
    private val monId: String by lazy { java.lang.Long.toString(java.util.Random().nextLong() and 0x7FFFFFFFFFFFL, 36) }
    @Volatile private var fil: Thread? = null
    @Volatile private var sock: java.net.Socket? = null
    @Volatile var envoyes = 0L; @Volatile var recus = 0L

    fun charger(ctx: android.content.Context) {
        if (charge) return
        val sp = ctx.getSharedPreferences("gcs", 0)
        code = sp.getString("fantome_code", "") ?: ""; nom = sp.getString("fantome_nom", "") ?: ""
        charge = true
    }
    fun nouveauCode(): String { val a = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; val r = java.security.SecureRandom(); return String(CharArray(6) { a[r.nextInt(a.length)] }) }
    fun propre(c: String) = c.uppercase().filter { it in 'A'..'Z' || it in '0'..'9' }.take(12)
    fun regler(ctx: android.content.Context, c: String, n: String) {
        code = propre(c); nom = n.take(20)
        ctx.getSharedPreferences("gcs", 0).edit().putString("fantome_code", code).putString("fantome_nom", nom).apply()
    }

    fun demarrer() {
        if (code.length < 4) { statut = "Code trop court (4 caractères au moins)."; return }
        if (fil != null) return
        actif = true; ami = null; amiVif = null
        val t = Thread({ boucle() }, "fantome"); fil = t; t.isDaemon = true; t.start()
    }
    fun arreter() {
        actif = false; visite = false
        try { sock?.close() } catch (e: Exception) { }
        fil?.interrupt(); fil = null; ami = null; amiVif = null; statut = ""
    }

    // ---------------------------------------------------------------- MQTT 3.1.1 (le strict minimum, QoS 0)
    private fun longueur(o: java.io.ByteArrayOutputStream, n0: Int) { var n = n0
        do { var b = n % 128; n /= 128; if (n > 0) b = b or 128; o.write(b) } while (n > 0) }
    private fun chaine(o: java.io.ByteArrayOutputStream, s: String) { val b = s.toByteArray(Charsets.UTF_8); o.write(b.size shr 8); o.write(b.size and 255); o.write(b) }
    private fun paquet(type: Int, corps: ByteArray): ByteArray { val o = java.io.ByteArrayOutputStream(); o.write(type); longueur(o, corps.size); o.write(corps); return o.toByteArray() }
    private fun connect(id: String): ByteArray { val c = java.io.ByteArrayOutputStream()
        chaine(c, "MQTT"); c.write(4); c.write(0x02); c.write(0); c.write(30); chaine(c, id); return paquet(0x10, c.toByteArray()) }
    private fun subscribe(sujet: String): ByteArray { val c = java.io.ByteArrayOutputStream()
        c.write(0); c.write(1); chaine(c, sujet); c.write(0); return paquet(0x82, c.toByteArray()) }
    private fun publish(sujet: String, msg: ByteArray): ByteArray { val c = java.io.ByteArrayOutputStream(); chaine(c, sujet); c.write(msg); return paquet(0x30, c.toByteArray()) }
    private fun lirePaquet(i: java.io.InputStream): Pair<Int, ByteArray>? {
        val t = i.read(); if (t < 0) return null
        var n = 0; var m = 1
        while (true) { val b = i.read(); if (b < 0) return null; n += (b and 127) * m; if (b and 128 == 0) break; m *= 128; if (m > 128 * 128 * 128) return null }
        if (n > 65536) return null
        val d = ByteArray(n); var k = 0
        while (k < n) { val r = i.read(d, k, n - k); if (r < 0) return null; k += r }
        return t to d
    }

    private fun boucle() {
        var essai = 0
        while (actif) {
            val (hote, port) = SERVEURS[essai % SERVEURS.size]
            try {
                statut = "Connexion à $hote…"
                val s = java.net.Socket(); sock = s
                s.connect(java.net.InetSocketAddress(hote, port), 8000); s.soTimeout = 40_000; s.tcpNoDelay = true
                val o = java.io.BufferedOutputStream(s.getOutputStream()); val i = java.io.BufferedInputStream(s.getInputStream())
                o.write(connect("gcs-" + monId)); o.flush()
                val ca = lirePaquet(i)
                if (ca == null || ca.first != 0x20 || ca.second.size < 2 || ca.second[1].toInt() != 0) throw java.io.IOException("refusé")
                val sujet = "gcstation/fantome/$code"
                o.write(subscribe(sujet)); o.flush()
                statut = "Connecté ($hote) · code $code"
                essai = 0
                val lecteur = Thread({
                    try { while (actif) { val p = lirePaquet(i) ?: break; if (p.first and 0xF0 == 0x30) recevoir(p.first, p.second) } } catch (e: Exception) { }
                    try { s.close() } catch (e: Exception) { }
                }, "fantome-lecture")
                lecteur.isDaemon = true; lecteur.start()
                var dernierPing = System.nanoTime()
                while (actif && !s.isClosed) {
                    val m = message()
                    if (m != null) { synchronized(o) { o.write(publish(sujet, m)); o.flush() }; envoyes++ }
                    if (System.nanoTime() - dernierPing > 15_000_000_000L) { synchronized(o) { o.write(byteArrayOf(0xC0.toByte(), 0)); o.flush() }; dernierPing = System.nanoTime() }
                    Thread.sleep(200)                                      // (~5 messages par seconde)
                }
            } catch (e: InterruptedException) { break
            } catch (e: Exception) {
                if (!actif) break
                statut = "Serveur $hote injoignable, nouvel essai…"; essai++
            } finally { try { sock?.close() } catch (e: Exception) { } }
            try { Thread.sleep(3000) } catch (e: InterruptedException) { break }
        }
        fil = null
    }

    private fun recevoir(entete: Int, d: ByteArray) {
        if (d.size < 2) return
        val lt = ((d[0].toInt() and 255) shl 8) or (d[1].toInt() and 255)
        var k = 2 + lt; if ((entete shr 1) and 3 != 0) k += 2               // (identifiant de paquet si QoS > 0)
        if (k > d.size) return
        try {
            val j = org.json.JSONObject(String(d, k, d.size - k, Charsets.UTF_8))
            val id = j.optString("id"); if (id.isEmpty() || id == monId) return
            val p = j.getJSONArray("p")
            val b = j.optJSONArray("b")?.let { a -> if (a.length() >= 4) FloatArray(4) { a.getDouble(it).toFloat() } else null }
            val a = Ami(id, j.optString("nom", "Ami").take(20).ifBlank { "Ami" }, j.optString("sc").take(8).filter { it.code in 33..126 }, j.optInt("sa", -1),
                floatArrayOf(p.getDouble(0).toFloat(), p.getDouble(1).toFloat(), p.getDouble(2).toFloat()),
                j.optDouble("c", 0.0).toFloat(), j.optDouble("v", 0.0).toFloat(), j.optInt("n", 0) == 1, b, System.nanoTime())
            if (!a.pos.all { it.isFinite() && Math.abs(it) < 1e6f }) return
            recus++; amiVif = a; ami = a
        } catch (e: Exception) { }
    }

    /** mon Link, lu dans le direct (ou la derniere lecture du compagnon) ; null hors de Wind Waker */
    private fun message(): ByteArray? {
        val e = Compagnon.etat as? JeuWW.Etat ?: return null
        if (!e.partie || !e.aPos || e.scene.isEmpty()) return null
        val pd = JeuWW.posDirect()
        val pos = pd ?: floatArrayOf(e.x, e.y, e.z)
        val j = org.json.JSONObject()
        j.put("id", monId); j.put("nom", nom.ifBlank { e.nom.ifBlank { "Ami" } })
        j.put("sc", e.scene); j.put("sa", e.salle)
        j.put("p", org.json.JSONArray().put(r1(pos[0])).put(r1(pos[1])).put(r1(pos[2])))
        j.put("c", Math.round(e.cap * 1000.0) / 1000.0); j.put("v", r1(e.vitesse))
        j.put("n", if (e.scene == "sea" && pos[1] < -30f) 1 else 0)
        JeuWW.bateauFantome()?.let { b -> j.put("b", org.json.JSONArray().put(r1(b[0])).put(r1(b[1])).put(r1(b[2])).put(Math.round(b[3] * 1000.0) / 1000.0)) }
        return j.toString().toByteArray(Charsets.UTF_8)
    }
    private fun r1(f: Float) = Math.round(f * 10.0) / 10.0

    /** pour la vue 3D : le fantome s'il est dans ce lieu : x, y, z (repere de l'appli), cap (jeu), vitesse, nage */
    fun vuePour(niveau: String): Pair<FloatArray, FloatArray?>? {
        val a = amiVif ?: return null
        if (!actif || !a.frais() || a.lieu != niveau.lowercase()) return null
        val p = CartesWW.pos(a.pos[0], a.pos[1], a.pos[2])
        return floatArrayOf(p[0], p[1], p[2], a.cap, a.vitesse, if (a.nage) 1f else 0f) to a.bateau
    }
}

/** le panneau du fantome (sous la vue 3D de Wind Waker) */
@Composable
fun PanneauFantome() {
    val ctx = androidx.compose.ui.platform.LocalContext.current
    Fantome.charger(ctx)
    var ouvert by remember { mutableStateOf(Fantome.actif) }
    var code by remember { mutableStateOf(Fantome.code) }
    var nom by remember { mutableStateOf(Fantome.nom) }
    var tic by remember { mutableStateOf(0) }
    androidx.compose.runtime.LaunchedEffect(Unit) { while (true) { kotlinx.coroutines.delay(1000); tic++ } }
    Column(Modifier.fillMaxWidth().padding(top = 6.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
            Text("👻 Fantôme d'un ami", color = Color.White, fontSize = 13.sp)
            androidx.compose.material3.Switch(Fantome.actif, { on ->
                if (on) { Fantome.regler(ctx, code, nom); Fantome.demarrer(); ouvert = true } else Fantome.arreter()
            }, colors = androidx.compose.material3.SwitchDefaults.colors(checkedTrackColor = C.Rose, checkedThumbColor = Color.White))
        }
        if (!Fantome.actif && !ouvert) {
            androidx.compose.material3.TextButton(onClick = { ouvert = true }) { Text("Régler (code à partager)", color = C.Rose, fontSize = 12.sp) }
            return@Column
        }
        Doux("Le Link de ton ami apparaît ici en fantôme (seulement sur le téléphone, jamais dans ta partie). " +
            "Donnez-vous le même code.", taille = 11)
        Doux("⚠ Serveur public gratuit (broker.hivemq.com, secours test.mosquitto.org) : quiconque connaît le code voit les positions " +
            "de Link. Sans garantie : le service peut être lent ou s'arrêter.", taille = 11, couleur = Color(0xFFFFC94A))
        if (!Fantome.actif) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                androidx.compose.material3.OutlinedTextField(code, { code = Fantome.propre(it) }, Modifier.weight(1f), singleLine = true,
                    label = { Text("Code", fontSize = 11.sp) },
                    colors = androidx.compose.material3.OutlinedTextFieldDefaults.colors(focusedTextColor = C.Texte, unfocusedTextColor = C.Texte, focusedBorderColor = C.Cyan, unfocusedBorderColor = C.Bord, cursorColor = C.Cyan))
                androidx.compose.material3.TextButton(onClick = { code = Fantome.nouveauCode() }) { Text("Nouveau", color = C.Rose, fontSize = 12.sp) }
            }
            androidx.compose.material3.OutlinedTextField(nom, { nom = it.take(20) }, Modifier.fillMaxWidth(), singleLine = true,
                label = { Text("Ton nom (vu par l'ami)", fontSize = 11.sp) },
                colors = androidx.compose.material3.OutlinedTextFieldDefaults.colors(focusedTextColor = C.Texte, unfocusedTextColor = C.Texte, focusedBorderColor = C.Cyan, unfocusedBorderColor = C.Bord, cursorColor = C.Cyan))
            if (code.length >= 4) androidx.compose.material3.TextButton(onClick = {
                val i = android.content.Intent(android.content.Intent.ACTION_SEND).setType("text/plain")
                    .putExtra(android.content.Intent.EXTRA_TEXT, "Wind Waker, fantôme GC Station : mon code est $code")
                ctx.startActivity(android.content.Intent.createChooser(i, "Partager le code"))
            }) { Text("Partager le code", color = C.Rose, fontSize = 12.sp) }
        } else {
            Doux(Fantome.statut + (if (tic >= 0) "" else ""), taille = 11)
            val a = Fantome.ami
            if (a == null || !a.frais()) Doux("En attente de ton ami (même code : ${Fantome.code})…", taille = 11)
            else {
                Doux("${a.nom} : ${CartesWW.nom(a.lieu)}" + (if (a.bateau != null) " · bateau" else ""), taille = 12, couleur = Color(0xFF9FE8FF))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Bouton(if (Fantome.visite) "Revenir à mon Link" else "Aller le voir", hauteur = 36.dp) { Fantome.visite = !Fantome.visite }
                }
                if (Fantome.visite && CartesWW.scene(a.lieu) == null) Doux("Ce lieu n'est pas dans tes packs : la vue ne peut pas y aller.", taille = 11)
            }
        }
    }
}
