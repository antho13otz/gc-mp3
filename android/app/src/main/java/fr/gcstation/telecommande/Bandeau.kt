package fr.gcstation.telecommande

// Appli privee build 509 (console v397) : le bandeau des succes sur la tele. La console pose, sous Pulse, un petit
// programme qui dessine un bandeau en bas de l'image pendant le jeu (source/natres/bandeau.c). Ici : l'appli y ecrit le
// texte d'un succes RetroAchievements (qu'elle est seule a voir), ou celui du bouton de test, par l'ecriture "W" de Pulse.
// Ou est le bandeau : en-tete de Pulse (0x817EE000) + 0x19C = la liste des cheats ; ses mots 1-2 valent C0000000 00000003,
// les mots 3-4 (lis r3 / ori r3) donnent l'adresse de sa zone de donnees (premier mot "BAN1", ou "BAN2" depuis la v399). Positions : bandeau.h.

object Bandeau {
    /** caracteres apres l'ASCII, dans l'ordre de la police de la console (tools/police_bandeau.py, codes 127 et suivants) */
    const val EXTRA = "àâäçéèêëîïôöùûüœÀÂÇÉÈÊÎÔÙÛŒ«»…"
    private const val PULSE = 0x817EE000L
    private const val MAGIE = 0x42414E31L                  // "BAN1" (v397-v398)
    private const val MAGIE2 = 0x42414E32L                 // "BAN2" (v399 : etiquette et icone du telephone, type 4)
    private const val MAGIE3 = 0x42414E33L                 // "BAN3" (v401 : 4 cases d'icone en image, 16 x 16 YUYV)
    private const val O_COUL = 0x3C; private const val O_IMG = 0x480; private const val IMG_W = 16
    private const val O_RASEQ = 0x20; private const val O_RATYPE = 0x24; private const val O_RATXT = 0x60
    private const val O_RAETIQ = 0x420; private const val O_RAICONE = 0x440
    /** icones (codes de la police du bandeau, et de la carte du menu de la console) */
    const val TROPHEE = 1; const val MESSAGE = 2; const val APPEL = 3; const val ALARME = 4; const val AMI = 5; const val INFO = 6

    private fun g32(b: ByteArray, o: Int) = (b[o].toLong() and 255 shl 24) or (b[o + 1].toLong() and 255 shl 16) or (b[o + 2].toLong() and 255 shl 8) or (b[o + 3].toLong() and 255)

    /**
     * build 524 : texte montrable par la console : sans emojis ni symboles (ils donnaient « ? »), et les lettres accentuees
     * que la police ne connait pas ramenees a la lettre simple (n pour ñ...). Pour le bandeau et pour la carte du menu.
     */
    fun nettoyer(t: String): String {
        val sb = StringBuilder()
        var i = 0
        while (i < t.length) {
            val cp = t.codePointAt(i); i += Character.charCount(cp)
            val ch = if (cp < 0x10000) cp.toChar() else ' '
            when {
                cp in 32..126 || EXTRA.indexOf(ch) >= 0 || ch == '’' || ch == '‘' || ch == '“' || ch == '”' -> sb.appendCodePoint(cp)
                cp >= 0x10000 || cp in 0x2190..0x2BFF || cp in 0xFE00..0xFE0F || cp == 0x200D || cp == 0x20E3 -> { }   // (emojis, symboles, liaisons)
                Character.getType(cp) == Character.OTHER_SYMBOL.toInt() -> { }
                Character.isWhitespace(cp) -> sb.append(' ')
                else -> {
                    val simple = java.text.Normalizer.normalize(String(Character.toChars(cp)), java.text.Normalizer.Form.NFD).filter { it.code in 32..126 }
                    sb.append(simple)                       // (ñ -> n ; rien si ce n'est pas une lettre latine)
                }
            }
        }
        return sb.toString().replace(Regex(" {2,}"), " ").trim()
    }

    /** texte -> codes de la police du bandeau (64 octets, 0 a la fin) */
    fun codes(t0: String): ByteArray {
        val t = nettoyer(t0)
        val o = ByteArray(64)
        var k = 0
        for (ch in t) {
            if (k >= 63) break
            val c = when {
                ch.code in 32..126 -> ch.code
                ch == '’' || ch == '‘' -> '\''.code
                ch == '“' || ch == '”' -> '"'.code
                EXTRA.indexOf(ch) >= 0 -> 127 + EXTRA.indexOf(ch)
                else -> '?'.code
            }
            o[k++] = c.toByte()
        }
        return o
    }

    /** adresse de la zone de donnees du bandeau ; 0 : pas de bandeau dans ce jeu ; null : Pulse ne repond pas */
    private fun trouver(): Long? {
        val h = MemoireJeu.bloc(PULSE) ?: return null
        val ch = g32(h, 0x19C)
        if (ch < 0x80000000L || ch >= 0x81800000L) return 0L
        val l = MemoireJeu.bloc(ch and 3L.inv()) ?: return null
        if (g32(l, 4) != 0xC0000000L || g32(l, 8) != 3L) return 0L
        val d = (g32(l, 12) and 0xFFFF shl 16) or (g32(l, 16) and 0xFFFF)
        val b = MemoireJeu.bloc(d) ?: return null
        val m = g32(b, 0)
        version = if (m == MAGIE3) 3 else if (m == MAGIE2) 2 else 1
        if (d != zoneIcones) { zoneIcones = d; cases.clear() }        // (autre partie : les cases sont vides)
        return if (m == MAGIE || m == MAGIE2 || m == MAGIE3) d else 0L
    }
    private var version = 1
    /** v401 : cases d'icone deja remplies dans le bandeau de cette partie : application -> case (0 a 3) */
    private var zoneIcones = 0L
    /** pour lire les icones des applications (donne par NotifTele et par l'ecran Pulse) */
    var ctx: android.content.Context? = null
    private val cases = LinkedHashMap<String, Int>()
    /** build 523 : le bandeau n'a qu'une place pour le telephone : attendre (10 s au plus) qu'il ait pris la notification
     *  precedente (RAVU = RASEQ) avant d'ecrire la suivante ; sinon, deux notifications rapprochees : seule la 2e passait */
    private const val O_RAVU = 0x28
    private fun attendrePrise(d: Long) {
        val fin = System.currentTimeMillis() + 10_000L
        while (System.currentTimeMillis() < fin) {
            val b = MemoireJeu.bloc(d) ?: return
            if (g32(b, O_RAVU) == g32(b, O_RASEQ)) return
            Thread.sleep(250)
        }
    }

    /** montre un texte sur la tele : type 2 = succes RetroAchievements, 3 = test ; rend null si c'est fait, sinon pourquoi */
    @Synchronized fun envoyer(type: Int, texte: String): String? {
        if (MemoireJeu.demande('I', garde = false) == null)
            return "Pulse ne répond pas : il faut une partie en cours, lancée avec « Chercheur de codes (appli) » allumé sur la console"
        val d = trouver() ?: return "Pulse ne répond pas"
        if (d == 0L) return "Pas de bandeau dans cette partie : réglage « Succès sur la télé (Pulse) » coupé sur la console, ou console avant la v397"
        attendrePrise(d)
        val c = codes(texte)
        for (k in 0 until 16) {
            val w = g32(c, k * 4)
            if (MemoireJeu.demande('W', d + O_RATXT + k * 4, 4, w, garde = false) == null) return "Pulse ne répond plus"
        }
        if (MemoireJeu.demande('W', d + O_RATYPE, 4, type.toLong(), garde = false) == null) return "Pulse ne répond plus"
        val b = MemoireJeu.bloc(d) ?: return "Pulse ne répond plus"
        if (MemoireJeu.demande('W', d + O_RASEQ, 4, (g32(b, O_RASEQ) + 1) and 0xFFFFFFFFL, garde = false) == null) return "Pulse ne répond plus"
        return null
    }

    /**
     * build 522 (console v399) : une notification sur la tele. Pendant un jeu lance avec Pulse : dans le bandeau (type 4 :
     * l'etiquette et l'icone, console v399 ; avant : type 3, l'etiquette mise devant le texte). Sinon : la carte qui descend
     * en haut du menu de GC Station (/api/notif, console v399). Rend null si c'est fait, sinon pourquoi.
     */
    fun notifier(etiquette: String, texte: String, icone: Int = MESSAGE, appli: String? = null): String? {
        if (St.jeu != null) {
            val e = envoyerNotif(etiquette, texte, icone, appli)
            if (e == null) return null
        }
        val px = appli?.let { iconeRgba4444(it) }?.let { "&px=$it" } ?: ""
        val r = Api.get("/api/notif?e=" + Api.enc(nettoyer(etiquette)) + "&m=" + Api.enc(nettoyer(texte)) + "&i=" + icone + px, 3000)
            ?: return "Console injoignable"
        return if (r.contains("\"ok\":1")) null else if (r.contains("\"ok\":0")) "La console affiche déjà une notification, réessaie" else "Console trop ancienne (v399 ou plus)"
    }
    @Synchronized private fun envoyerNotif(etiquette: String, texte: String, icone0: Int, appli: String?): String? {
        var icone = icone0
        if (MemoireJeu.demande('I', garde = false) == null) return "Pulse ne répond pas"
        val d = trouver() ?: return "Pulse ne répond pas"
        if (d == 0L) return "Pas de bandeau dans cette partie"
        if (version < 2) return envoyer(3, "$etiquette : $texte")
        attendrePrise(d)
        if (version >= 3 && appli != null) caseIcone(d, appli)?.let { icone = 16 + it }   // (v401 : l'image de l'application)
        val c = codes(etiquette)
        for (k in 0 until 8) if (MemoireJeu.demande('W', d + O_RAETIQ + k * 4, 4, g32(c, k * 4), garde = false) == null) return "Pulse ne répond plus"
        if (MemoireJeu.demande('W', d + O_RAICONE, 4, icone.toLong(), garde = false) == null) return "Pulse ne répond plus"
        return envoyer(4, texte)
    }

    /** l'icone d'une application du telephone, en bitmap (null si introuvable) */
    private fun iconeAppli(appli: String, w: Int): android.graphics.Bitmap? = try {
        val c = ctx ?: Compagnon.ctx
        val dr = c!!.packageManager.getApplicationIcon(appli)
        val b = android.graphics.Bitmap.createBitmap(w, w, android.graphics.Bitmap.Config.ARGB_8888)
        val cv = android.graphics.Canvas(b)
        dr.setBounds(0, 0, w, w); dr.draw(cv)
        b
    } catch (e: Exception) { null }

    /** carte du menu : 24 x 24 en RGBA 4444, en hexa (4 chiffres par point) */
    private fun iconeRgba4444(appli: String): String? {
        val b = iconeAppli(appli, 24) ?: return null
        val sb = StringBuilder(24 * 24 * 4)
        for (y in 0 until 24) for (x in 0 until 24) {
            val p = b.getPixel(x, y)
            val v = ((p shr 20 and 15) shl 12) or ((p shr 12 and 15) shl 8) or ((p shr 4 and 15) shl 4) or (p ushr 28 and 15)
            sb.append(String.format(java.util.Locale.ROOT, "%04x", v))
        }
        return sb.toString()
    }

    /** bandeau (BAN3) : la case de l'icone de cette application ; remplie une fois par partie (128 ecritures de 4 octets) */
    private fun caseIcone(d: Long, appli: String): Int? {
        cases[appli]?.let { cases.remove(appli); cases[appli] = it; return it }    // (la plus recente en dernier)
        val b = iconeAppli(appli, IMG_W) ?: return null
        val zone = MemoireJeu.bloc(d) ?: return null
        // (posee sur la couleur du fond du bandeau, lue dans la zone : Y Cb Cr)
        val fy = zone[O_COUL].toInt() and 255; val fu = zone[O_COUL + 1].toInt() and 255; val fv = zone[O_COUL + 2].toInt() and 255
        val k = if (cases.size < 4) cases.size else { val f = cases.entries.first(); cases.remove(f.key); f.value }   // (pleine : la plus ancienne)
        val dst = d + O_IMG + k * IMG_W * IMG_W * 2
        for (y in 0 until IMG_W) for (x in 0 until IMG_W step 2) {
            val yuv = IntArray(6)
            for (i in 0..1) {
                val p = b.getPixel(x + i, y); val a = (p ushr 24) / 255f
                val r = (p shr 16 and 255); val g = (p shr 8 and 255); val bl = (p and 255)
                val py = 16 + (65.738f * r + 129.057f * g + 25.064f * bl) / 256f
                val pu = 128 + (-37.945f * r - 74.494f * g + 112.439f * bl) / 256f
                val pv = 128 + (112.439f * r - 94.154f * g - 18.285f * bl) / 256f
                yuv[i * 3] = (fy + (py - fy) * a).toInt(); yuv[i * 3 + 1] = (fu + (pu - fu) * a).toInt(); yuv[i * 3 + 2] = (fv + (pv - fv) * a).toInt()
            }
            val y0 = yuv[0].coerceIn(16, 235); val y1 = yuv[3].coerceIn(16, 235)
            val cb = ((yuv[1] + yuv[4]) / 2).coerceIn(16, 240); val cr = ((yuv[2] + yuv[5]) / 2).coerceIn(16, 240)
            val w = (y0.toLong() shl 24) or (cb.toLong() shl 16) or (y1.toLong() shl 8) or cr.toLong()
            if (MemoireJeu.demande('W', dst + (y * IMG_W + x) * 2, 4, w, garde = false) == null) return null
        }
        cases[appli] = k
        return k
    }

    /** reglage : les succes RetroAchievements aussi sur la tele (appli privee ; oui par defaut) */
    fun raSurTele(c: android.content.Context) = c.getSharedPreferences("gcs", 0).getBoolean("ra_tele", true)
    fun raSurTele(c: android.content.Context, on: Boolean) = c.getSharedPreferences("gcs", 0).edit().putBoolean("ra_tele", on).apply()
}
