package fr.gcstation.telecommande

// L'ecran principal de l'appli (Jetpack Compose). Meme fonctionnement que l'ancien ecran (MainActivity) :
// - trouve la console (reponse en direct, puis balayage du Wi-Fi), garde son adresse ;
// - etat lu toutes les secondes (ou quand la console previent : Live), position qui avance entre deux lectures ;
// - console injoignable : bandeau, nouvel essai tout seul, console cherchee si son adresse a change ;
// - partie en cours, envois de fichiers (barre + notification), "Partager -> GC Station", boutons de volume.
// Les ecrans sont dans Ecran*.kt ; le reseau et le reste viennent du code Java partage (Api, Live, PlaybackService...).

import android.content.ClipData
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.OpenableColumns
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.net.Inet4Address
import java.net.NetworkInterface
import java.util.Collections
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference

/** un envoi de fichiers en cours (ce que la barre affiche) */
data class Envoi(val nom: String, val idx: Int, val nb: Int, val pct: Int, val debit: String, val fini: Boolean = false, val texte: String = "")

/** l'etat de l'appli, lu par les ecrans (Compose se redessine quand il change) */
object St {
    var connecte by mutableStateOf(false)
    var panneau by mutableStateOf(true)                     // (ecran de connexion par-dessus tout)
    var panneauMsg by mutableStateOf("")
    var cherche by mutableStateOf(false)
    var etat by mutableStateOf<JSONObject?>(null)
    var horsLigne by mutableStateOf(false)
    var horsLigneMode by mutableStateOf(false)             // (« Utiliser hors-ligne » : sans la console, ce qui est garde sur le telephone)
    var jeu by mutableStateOf<Live.Game?>(null)
    var jeuMasque by mutableStateOf(false)
    var envoi by mutableStateOf<Envoi?>(null)
    var onglet by mutableIntStateOf(0)                       // 0 Lecture, 1 Musique, 2 Jeux, 3 Tele, 4 Plus
    var sous by mutableStateOf("")                            // (ecran ouvert depuis Plus : son nom, "" = la grille)
    var seconde by mutableLongStateOf(0L)                     // (avance chaque seconde : horloges a l'ecran)
    var choixConsoles by mutableStateOf<List<Array<String>>>(emptyList())
    var jeuAOuvrir by mutableStateOf<JSONArray?>(null)        // (Recherche, raccourci : fiche d'un jeu a ouvrir)
    var listes by mutableIntStateOf(0)                        // (augmente : les listes se rechargent)
    var visible = false
    @JvmField var activite: StationActivity? = null
    fun cmd(c: String, i: Int = -1) = Api.cmd(c, i)
}

class StationActivity : ComponentActivity(), Host {
    private val ui = Handler(Looper.getMainLooper())
    private var fails = 0
    private var lastStateAt = 0L
    private var wasOffline = false
    private var rediscovering = false
    private var pendingShare: Intent? = null
    private var pendingGame: Intent? = null
    private val prefs get() = getSharedPreferences("gcs", MODE_PRIVATE)

    private val live = object : Live.Listener {
        override fun changed() { if (St.connecte && St.visible) { ui.removeCallbacks(poll); ui.post(poll) } }
        override fun game(g: Live.Game?) { St.jeuMasque = false; St.jeu = g }
    }

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        enableEdgeToEdge()
        St.activite = this
        hauteFrequence()
        setContent { StationTheme { App() } }
        PlaybackService.channels(this)
        if (android.os.Build.VERSION.SDK_INT >= 33 && checkSelfPermission("android.permission.POST_NOTIFICATIONS") != android.content.pm.PackageManager.PERMISSION_GRANTED)
            requestPermissions(arrayOf("android.permission.POST_NOTIFICATIONS"), 1)
        Api.host = prefs.getString("host", null)
        if (isShare(intent)) pendingShare = intent else raccourci(intent)
        start()
    }

    override fun onDestroy() { super.onDestroy(); if (St.activite === this) St.activite = null }

    @Suppress("DEPRECATION")
    private fun hauteFrequence() {
        try {
            val d = (if (android.os.Build.VERSION.SDK_INT >= 30) display else windowManager.defaultDisplay) ?: return
            val cur = d.mode
            var best = cur
            for (m in d.supportedModes) if (m.physicalWidth == cur.physicalWidth && m.physicalHeight == cur.physicalHeight && m.refreshRate > best.refreshRate) best = m
            val lp = window.attributes
            lp.preferredDisplayModeId = best.modeId
            window.attributes = lp
        } catch (e: Exception) { }
    }

    // ---------- etat de la console ----------
    private val poll: Runnable = object : Runnable {
        override fun run() {
            if (!St.visible || !St.connecte) return
            ui.removeCallbacks(this)
            Api.get("/api/state") { r ->
                if (!St.visible || !St.connecte) return@get
                ui.removeCallbacks(this)
                if (r != null) {
                    fails = 0
                    online()
                    try { val j = JSONObject(r); lastStateAt = SystemClock.uptimeMillis(); St.etat = j } catch (e: Exception) { }
                } else offlineStep(++fails)
                ui.postDelayed(this, if (r == null) 3000L else if (Live.pushing()) 5000L else 1000L)
            }
        }
    }

    /** chaque seconde : horloges ; en direct, la position du morceau avance ici entre deux lectures */
    private val tick: Runnable = object : Runnable {
        override fun run() {
            if (!St.visible) return
            ui.postDelayed(this, 1000)
            St.seconde = SystemClock.uptimeMillis() / 1000
            val g = Live.game
            if (g !== St.jeu) St.jeu = g
            val s = St.etat ?: return
            if (!St.connecte || fails > 0 || !Live.pushing()) return
            val dt = (SystemClock.uptimeMillis() - lastStateAt) / 1000
            if (dt < 1 || s.optInt("playing", 0) == 0) return
            try {
                val c = JSONObject(s.toString())
                val dur = c.optInt("dur", 0)
                val el = c.optInt("el", 0) + 1
                c.put("el", if (dur > 0) minOf(dur, el) else el)
                lastStateAt = SystemClock.uptimeMillis()
                St.etat = c
            } catch (e: Exception) { }
        }
    }

    private fun offlineStep(n: Int) {
        if (Live.game != null) { St.jeu = Live.game; St.horsLigne = false; return }   // (en jeu : la console ne repond pas, normal)
        if (n < 3) return
        wasOffline = true
        St.horsLigne = true
        if (n % 6 == 3 && !rediscovering) {
            rediscovering = true
            val old = Api.host
            Thread {
                val f = Live.discover(1500)
                ui.post {
                    rediscovering = false
                    if (f.isEmpty() || old == null || old != Api.host) return@post
                    var ip = f[0][0]
                    for (x in f) if (x[0] == old) ip = old
                    if (ip != old) {
                        Api.host = ip
                        prefs.edit().putString("host", ip).apply()
                        Live.resubscribe()
                        Toast.makeText(this, "Console retrouvée : $ip", Toast.LENGTH_SHORT).show()
                        ui.removeCallbacks(poll); ui.post(poll)
                    }
                }
            }.start()
        }
    }

    private fun online() {
        val g = Live.consoleBack()
        if (g != null) Toast.makeText(this, "Partie terminée : " + g.name + " · " + Live.dur(g.playedSec()), Toast.LENGTH_LONG).show()
        else if (wasOffline) Toast.makeText(this, "Console de retour", Toast.LENGTH_SHORT).show()
        wasOffline = false
        St.horsLigne = false
        St.jeu = Live.game
    }

    override fun onResume() {
        super.onResume()
        St.visible = true
        Live.add(this, live)
        St.jeu = Live.game
        ui.removeCallbacks(poll); if (St.connecte) ui.post(poll)
        ui.removeCallbacks(tick); ui.postDelayed(tick, 1000)
    }

    override fun onPause() { super.onPause(); St.visible = false; Live.remove(live); ui.removeCallbacks(tick) }

    // ---------- connexion ----------
    fun start() {
        val h = Api.host
        if (h == null) { search(); return }
        panneau("Connexion à la console ($h)…", true)
        Thread {
            val ok = Api.isConsole(h, 1500)
            ui.post { if (ok || Live.game != null) connect(h) else search() }
        }.start()
    }

    fun connect(ip: String) {
        Api.host = ip
        prefs.edit().putString("host", ip).apply()
        St.connecte = true
        St.panneau = false
        St.horsLigneMode = false
        St.choixConsoles = emptyList()
        fails = 0
        Live.resubscribe()
        PlaybackService.start(this)
        BuildCheck.run(this)
        ui.removeCallbacks(poll)
        ui.post(poll)
        pendingShare?.let { pendingShare = null; share(it) }
        pendingGame?.let { pendingGame = null; lancerRaccourci(it) }
        Backup.auto(this)
        AppUpdate.check(this)
        St.listes++
    }

    private fun prefixes(): List<String> {
        val r = ArrayList<String>()
        try {
            for (ni in Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp || ni.isLoopback) continue
                for (a in Collections.list(ni.inetAddresses)) {
                    if (a !is Inet4Address || !a.isSiteLocalAddress) continue
                    val ip = a.hostAddress ?: continue
                    val p = ip.substring(0, ip.lastIndexOf('.') + 1)
                    if (ni.name.startsWith("wlan")) { r.remove(p); r.add(0, p) } else if (!r.contains(p)) r.add(p)
                }
            }
        } catch (e: Exception) { }
        return r
    }

    fun search() {
        if (St.cherche) return
        St.cherche = true
        St.connecte = false
        panneau("Recherche de la console sur le Wi-Fi…", true)
        Thread {
            val quick = Live.discover(1500)
            if (quick.isNotEmpty()) { ui.post { St.cherche = false; trouvees(quick) }; return@Thread }
            val pre = prefixes()
            val found = AtomicReference<String?>(null)
            val done = AtomicInteger()
            for (p in pre) {
                if (found.get() != null) break
                val ex = Executors.newFixedThreadPool(48)
                for (i in 1..254) {
                    val ip = p + i
                    ex.execute {
                        if (found.get() == null && Api.isConsole(ip, 600)) found.compareAndSet(null, ip)
                        val d = done.incrementAndGet()
                        if (d % 32 == 0) ui.post { St.panneauMsg = "Recherche de la console sur le Wi-Fi… " + minOf(100, d * 100 / (254 * maxOf(1, pre.size))) + " %" }
                    }
                }
                ex.shutdown()
                try { ex.awaitTermination(30, TimeUnit.SECONDS) } catch (e: InterruptedException) { }
            }
            val ip = found.get()
            ui.post {
                St.cherche = false
                if (ip != null) connect(ip)
                else panneau(if (pre.isEmpty()) "Pas de Wi-Fi : connecte le téléphone au même réseau que la console."
                             else "Console introuvable. Allume-la et active la télécommande (onglet TÉLÉPHONE), ou tape son adresse.", false)
            }
        }.start()
    }

    private fun trouvees(f: List<Array<String>>) {
        if (f.size == 1) { connect(f[0][0]); return }
        panneau(f.size.toString() + " consoles GC Station sur ce Wi-Fi : laquelle ?", false)
        St.choixConsoles = f
    }

    fun panneau(msg: String, occupe: Boolean) {
        St.panneau = true
        St.panneauMsg = msg
        St.cherche = occupe
    }

    /** adresse tapee a la main */
    fun adresse(t: String) {
        val ip = t.trim().replaceFirst(Regex("^https?://"), "").replace(Regex("/.*$"), "")
        if (ip.isEmpty()) return
        Api.host = ip
        start()
    }

    /** la manette en jeu, meme si l'appli n'a pas vu le lancement */
    fun manetteEnJeu() {
        if (Api.host == null) { Toast.makeText(this, "Tape d'abord l'adresse de la console", Toast.LENGTH_SHORT).show(); return }
        val g = Live.game
        ManetteActivity.open(this, if (g != null && g.padMask != 0) g.padMask else 0xF)
    }

    fun actualiser() { St.listes++; QueueTab.tracks = null }

    // ---------- Host (ecrans Java partages) ----------
    override fun select(k: Int) {
        when (k) {
            0 -> { St.onglet = 0 }
            1, 2 -> { St.onglet = 1 }
            3 -> { St.onglet = 3 }
            MainActivity.T_GAMES -> { St.onglet = 2 }
            MainActivity.T_FILES -> { St.onglet = 4; St.sous = "Fichiers" }
            MainActivity.T_PHOTOS -> { St.onglet = 4; St.sous = "Photos" }
            MainActivity.T_QUEUE -> { St.onglet = 1 }
            MainActivity.T_SETTINGS -> { St.onglet = 4; St.sous = "Réglages" }
            MainActivity.T_CARDS -> { St.onglet = 4; St.sous = "Cartes mémoire" }
            MainActivity.T_SALON -> { St.onglet = 4; St.sous = "Salon" }
            MainActivity.T_TOOLS -> { St.onglet = 4; St.sous = "Outils" }
            MainActivity.T_SEARCH -> { St.onglet = 4; St.sous = "Recherche" }
            MainActivity.T_COPIES -> { St.onglet = 4; St.sous = "Copies" }
            else -> { St.onglet = 4; St.sous = "" }
        }
    }

    override fun switchTo(ip: String) {
        Toast.makeText(this, "Console $ip", Toast.LENGTH_SHORT).show()
        QueueTab.tracks = null
        St.connecte = false
        Api.host = ip
        start()
        St.onglet = 0
    }

    override fun openGame(x: JSONArray) { St.onglet = 2; St.jeuAOuvrir = x }
    override fun fullPad(on: Boolean) { St.onglet = 3 }

    // ---------- raccourcis, partage ----------
    override fun onNewIntent(i: Intent) {
        super.onNewIntent(i)
        setIntent(i)
        if (isShare(i)) { if (St.connecte) share(i) else pendingShare = i } else raccourci(i)
    }

    private fun raccourci(i: Intent?) {
        val act = i?.action ?: return
        if (act.startsWith(MainActivity.ACT_TAB)) {
            try { select(act.substring(MainActivity.ACT_TAB.length).toInt()) } catch (e: Exception) { }
        } else if (MainActivity.ACT_GAME == act) {
            if (St.connecte) lancerRaccourci(i) else pendingGame = i
        }
    }

    private fun lancerRaccourci(i: Intent) {
        St.onglet = 2
        GamesTab.launch(this, i.getStringExtra("id"), 0, 1, i.getStringExtra("nom"))
    }

    private fun isShare(i: Intent?) = i != null && (Intent.ACTION_SEND == i.action || Intent.ACTION_SEND_MULTIPLE == i.action)

    @Suppress("DEPRECATION")
    private fun share(i: Intent) {
        val uris = ArrayList<Uri>()
        if (Intent.ACTION_SEND == i.action) { val u = i.getParcelableExtra<Uri>(Intent.EXTRA_STREAM); if (u != null) uris.add(u) }
        else { val l = i.getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM); if (l != null) uris.addAll(l) }
        val txt = i.getStringExtra(Intent.EXTRA_TEXT)
        if (uris.isEmpty() && txt != null) {
            val m = Regex("https?://\\S+").find(txt)
            if (m != null) {
                Api.get("/api/open?u=" + Api.enc(m.value)) { r -> Toast.makeText(this, if (r != null) "Ouvert sur la télé (Cube Browser)" else "Pas de réponse de la console", Toast.LENGTH_LONG).show() }
                return
            }
        }
        envoyer(uris)
    }

    @Deprecated("Deprecated in Java")
    @Suppress("DEPRECATION")
    override fun onActivityResult(req: Int, res: Int, data: Intent?) {
        super.onActivityResult(req, res, data)
        if (res != RESULT_OK || data == null) return
        val u = data.data
        if ((req == CardsTab.REQ_GCI || req == CardsTab.REQ_RAW) && u != null && CardsTab.pendingCard != null) carte(u, CardsTab.pendingCard, req == CardsTab.REQ_RAW)
    }

    /** uris choisies (Fichiers > Envoyer) */
    fun envoyerChoix(l: List<Uri>) = envoyer(l)
    fun envoyerClip(c: ClipData?) { if (c != null) envoyer((0 until c.itemCount).mapNotNull { c.getItemAt(it).uri }) }

    // ---------- boutons de volume du telephone -> volume de la console ----------
    override fun dispatchKeyEvent(e: android.view.KeyEvent): Boolean {
        val k = e.keyCode
        if (St.connecte && (k == android.view.KeyEvent.KEYCODE_VOLUME_UP || k == android.view.KeyEvent.KEYCODE_VOLUME_DOWN)) {
            if (e.action == android.view.KeyEvent.ACTION_DOWN) {
                val dir = if (k == android.view.KeyEvent.KEYCODE_VOLUME_UP) 1 else -1
                if (!PlaybackService.adjust(dir)) Api.cmd(if (dir > 0) "volup" else "voldown", -1)
                ui.removeCallbacks(poll); ui.postDelayed(poll, 250)
            }
            return true
        }
        return super.dispatchKeyEvent(e)
    }

    // ---------- envois : barre + notification, ecran eteint, 3 essais, par morceaux ----------
    @Volatile private var upD = 0L
    @Volatile private var upT = 0L
    @Volatile private var upMs = 0L
    @Volatile private var upAt = 0L
    @Volatile private var upAsk = false
    @Volatile private var sName = ""
    @Volatile private var sDone = 0L
    @Volatile private var sSize = 0L
    @Volatile private var sT0 = 0L
    @Volatile private var sIdx = 0
    @Volatile private var sCount = 0

    private val sendTick: Runnable = object : Runnable {
        override fun run() {
            if (sCount == 0) return
            var sz = sSize; var d = sDone; var el = System.currentTimeMillis() - sT0
            if (System.currentTimeMillis() - upAt < 3000 && upT > 0) { sz = upT; d = upD; el = upMs }
            val pct = if (sz > 0) minOf(100L, d * 100 / sz).toInt() else 0
            if (!upAsk) {
                upAsk = true
                Api.get("/api/up") { r ->
                    upAsk = false
                    try { val j = JSONObject(r ?: ""); if (j.optLong("t") == sSize) { upD = j.optLong("d"); upT = j.optLong("t"); upMs = j.optLong("ms"); upAt = System.currentTimeMillis() } } catch (e: Exception) { }
                }
            }
            val sp = if (el > 800 && d > 0) String.format(java.util.Locale.FRANCE, "%.1f Mo/s", d / 1048576.0 / (el / 1000.0)) else ""
            St.envoi = Envoi(sName, sIdx, sCount, pct, sp)
            val t = (if (sCount > 1) "Envoi " + (sIdx + 1) + "/" + sCount + " : " else "Envoi : ") + sName
            notifEnvoi(t, "$pct %" + if (sp.isEmpty()) "" else " · $sp", pct, false)
            ui.postDelayed(this, 400)
        }
    }

    private fun notifEnvoi(title: String, text: String, pct: Int, end: Boolean) {
        try {
            PlaybackService.channels(this)
            @Suppress("DEPRECATION")
            val b = if (android.os.Build.VERSION.SDK_INT >= 26) android.app.Notification.Builder(this, PlaybackService.CH_INFO) else android.app.Notification.Builder(this)
            b.setSmallIcon(R.drawable.ic_stat).setContentTitle(title).setContentText(text).setColor(Neon.PINK).setOnlyAlertOnce(true).setShowWhen(false)
                .setContentIntent(android.app.PendingIntent.getActivity(this, PlaybackService.ID_SEND, Host.home(this), android.app.PendingIntent.FLAG_IMMUTABLE))
            if (!end) b.setOngoing(true).setProgress(100, pct, false) else b.setAutoCancel(true)
            getSystemService(android.app.NotificationManager::class.java).notify(PlaybackService.ID_SEND, b.build())
        } catch (e: Exception) { }
    }

    fun envoyer(uris: List<Uri>) {
        if (uris.isEmpty()) { Toast.makeText(this, "Rien à envoyer (seulement des fichiers)", Toast.LENGTH_LONG).show(); return }
        if (sCount > 0) { Toast.makeText(this, "Un envoi est déjà en cours", Toast.LENGTH_SHORT).show(); return }
        sCount = uris.size; sIdx = 0; sDone = 0; sSize = 0; sName = ""; sT0 = System.currentTimeMillis()
        St.envoi = Envoi("", 0, uris.size, 0, "")
        ui.removeCallbacks(sendTick); ui.post(sendTick)
        Thread {
            var wl: android.os.PowerManager.WakeLock? = null
            var wf: android.net.wifi.WifiManager.WifiLock? = null
            try {
                wl = (getSystemService(POWER_SERVICE) as android.os.PowerManager).newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK, "gcs:envoi")
                wl.acquire(3 * 3600 * 1000L)
                @Suppress("DEPRECATION")
                wf = (applicationContext.getSystemService(WIFI_SERVICE) as android.net.wifi.WifiManager).createWifiLock(android.net.wifi.WifiManager.WIFI_MODE_FULL_HIGH_PERF, "gcs:envoi")
                wf.acquire()
            } catch (e: Exception) { }
            var ok = 0
            val where = StringBuilder()
            for (i in uris.indices) {
                sIdx = i
                var r: String? = null
                var t = 0
                while (t < 3 && r == null) {
                    if (t > 0) try { Thread.sleep(if (Api.lastCode == 503) 4000L else 2000L) } catch (e: InterruptedException) { }
                    sDone = 0; sT0 = System.currentTimeMillis()
                    r = upload(uris[i], null, true)
                    t++
                }
                if (r != null) { ok++; if (r.startsWith("SD:")) where.append(" ").append(r) }
            }
            try { if (wl != null && wl.isHeld) wl.release(); if (wf != null && wf.isHeld) wf.release() } catch (e: Exception) { }
            val n = ok
            val w = where.toString()
            ui.post {
                sCount = 0
                ui.removeCallbacks(sendTick)
                val tout = n == uris.size
                St.envoi = Envoi("", 0, uris.size, if (tout) 100 else St.envoi?.pct ?: 0, "", true,
                    if (tout) "Envoyé : $n fichier" + (if (n > 1) "s" else "") + " ✓" else "Envoyés : $n sur " + uris.size + " (les autres ont échoué après 3 essais)")
                ui.postDelayed({ if (sCount == 0) St.envoi = null }, 5000)
                Toast.makeText(this, (if (tout) "Envoyé sur la console ($n)" else "Envoyés : $n sur " + uris.size) + if (w.isEmpty()) "" else " →$w", Toast.LENGTH_LONG).show()
                notifEnvoi(if (tout) "Envoi terminé" else "Envoi incomplet", (if (tout) "$n fichier(s) sur la console" else "$n sur " + uris.size + " envoyé(s)") + if (w.isEmpty()) "" else " :$w", 100, true)
                St.listes++
            }
        }.start()
    }

    /** une sauvegarde (.gci) ajoutee a une carte, ou la carte remplacee (.raw) */
    private fun carte(u: Uri, card: String, raw: Boolean) {
        Toast.makeText(this, "Envoi vers la carte $card…", Toast.LENGTH_SHORT).show()
        Thread {
            val name = upload(u, "gci", false)
            val r = if (name == null) null else Api.get((if (raw) "/api/cardput" else "/api/gciimp") + "?c=" + Api.enc(card) + "&n=" + Api.enc(name), 8000)
            var m = "Envoi impossible"
            try { if (r != null) m = JSONObject(r).optString("m", "Fait") } catch (e: Exception) { }
            ui.post { Toast.makeText(this, m, Toast.LENGTH_LONG).show(); St.listes++ }
        }.start()
    }

    private fun upload(u: Uri, forceDir: String?, prog: Boolean): String? {
        var tmp: File? = null
        try {
            var name = "fichier"
            val mime = contentResolver.getType(u)
            var size = -1L
            contentResolver.query(u, null, null, null, null)?.use { c ->
                if (c.moveToFirst()) {
                    val ni = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    val si = c.getColumnIndex(OpenableColumns.SIZE)
                    if (ni >= 0 && c.getString(ni) != null) name = c.getString(ni)
                    if (si >= 0 && !c.isNull(si)) size = c.getLong(si)
                }
            }
            if (size <= 0) {
                val f = File.createTempFile("envoi", null, cacheDir)
                tmp = f
                contentResolver.openInputStream(u)?.use { inp -> FileOutputStream(f).use { o -> inp.copyTo(o, 32768) } }
                size = f.length()
            }
            val low = name.lowercase()
            val dir = forceDir ?: when {
                mime != null && mime.startsWith("audio/") -> "mp3"
                mime != null && mime.startsWith("image/") -> "photos"
                mime != null && mime.startsWith("video/") -> "videos"
                low.endsWith(".gci") -> "gci"
                else -> "downloads"
            }
            val inp = (if (tmp != null) FileInputStream(tmp) else contentResolver.openInputStream(u)) ?: return null
            inp.use {
                if (prog) { sName = name; sSize = size }
                val sp: Api.Sent? = if (prog) Api.Sent { d -> sDone = d } else null
                val r = if (Api.canChunk()) Api.putChunks(dir, name, it, size, sp) else Api.put(dir, name, it, size, sp)
                return if (forceDir != null && r != null) name else r
            }
        } catch (e: Exception) {
            return null
        } finally {
            tmp?.delete()
        }
    }
}
