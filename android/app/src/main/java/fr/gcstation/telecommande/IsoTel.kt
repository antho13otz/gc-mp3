package fr.gcstation.telecommande

// Appli build 207 : extraire main.dol (ou n'importe quel fichier) d'une image de jeu GameCube (.iso / .gcm) rangee sur le
// telephone, sans la console. Format du disque GameCube (en-tete, DOL, table des fichiers FST) : en-tete 0x420 = position
// du main.dol, 0x424 / 0x428 = position et taille de la FST ; une entree FST = 12 octets (drapeau dossier, nom sur 24 bits,
// position ou parent, taille ou fin).

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.nio.ByteBuffer
import java.nio.channels.FileChannel

object IsoTel {
    class Fichier(val chemin: String, val pos: Long, val taille: Long)
    class Disque(val id: String, val titre: String, val fichiers: List<Fichier>)

    private fun lire(c: FileChannel, pos: Long, n: Int): ByteBuffer {
        val b = ByteBuffer.allocate(n)
        var p = pos
        while (b.hasRemaining()) { val k = c.read(b, p); if (k <= 0) break; p += k }
        b.flip(); return b                                                    // (gros-boutiste par defaut, comme la GameCube)
    }

    /** lit l'en-tete et la table des fichiers ; null si ce n'est pas une image GameCube non compressee */
    fun ouvrir(ctx: android.content.Context, u: Uri): Disque? {
        ctx.contentResolver.openFileDescriptor(u, "r")?.use { pfd ->
            java.io.FileInputStream(pfd.fileDescriptor).channel.use { c ->
                val total = c.size()
                val h = lire(c, 0, 0x440)
                if (h.limit() < 0x440 || h.getInt(0x1C) != 0xC2339F3D.toInt()) return null      // (signature GameCube)
                val id = String(ByteArray(6) { h.get(it) }, Charsets.US_ASCII)
                val titre = String(ByteArray(0x3E0) { h.get(0x20 + it) }, Charsets.ISO_8859_1).substringBefore('\u0000').trim()
                val l = ArrayList<Fichier>()
                // main.dol : sa taille = la fin de la section la plus loin (7 sections de code, 11 de donnees)
                val dol = h.getInt(0x420).toLong() and 0xFFFFFFFFL
                if (dol in 1 until total) {
                    val d = lire(c, dol, 0x100)
                    var fin = 0x100L
                    for (k in 0 until 18) {
                        val o = d.getInt(4 * k).toLong() and 0xFFFFFFFFL; val t = d.getInt(0x90 + 4 * k).toLong() and 0xFFFFFFFFL
                        if (o != 0L && t != 0L) fin = maxOf(fin, o + t)
                    }
                    if (dol + fin <= total) l.add(Fichier("/main.dol", dol, fin))
                }
                val fst = h.getInt(0x424).toLong() and 0xFFFFFFFFL
                val fstT = (h.getInt(0x428).toLong() and 0xFFFFFFFFL).toInt()
                if (fst in 1 until total && fstT in 12..(64 shl 20)) {
                    val f = lire(c, fst, fstT)
                    val n = f.getInt(8)
                    if (n in 1..(fstT / 12)) {
                        val noms = 12 * n
                        fun nom(i: Int): String { val o = noms + (f.getInt(12 * i) and 0xFFFFFF); val sb = StringBuilder(); var p = o
                            while (p < f.limit() && f.get(p).toInt() != 0) { sb.append((f.get(p).toInt() and 0xFF).toChar()); p++ }; return sb.toString() }
                        // parcours : la fin de chaque dossier ouvert (pile)
                        val pile = ArrayList<Pair<Int, String>>()
                        var i = 1
                        while (i < n) {
                            while (pile.isNotEmpty() && i >= pile.last().first) pile.removeAt(pile.size - 1)
                            val base = pile.lastOrNull()?.second ?: ""
                            val dossier = (f.getInt(12 * i) ushr 24) != 0
                            if (dossier) pile.add((f.getInt(12 * i + 8)) to "$base/" + nom(i))
                            else l.add(Fichier("$base/" + nom(i), f.getInt(12 * i + 4).toLong() and 0xFFFFFFFFL, f.getInt(12 * i + 8).toLong() and 0xFFFFFFFFL))
                            i++
                        }
                    }
                }
                return Disque(id, titre, l)
            }
        }
        return null
    }

    /** copie le fichier du disque vers l'endroit choisi, par morceaux de 1 Mo */
    fun extraire(ctx: android.content.Context, iso: Uri, f: Fichier, vers: Uri): Boolean = try {
        ctx.contentResolver.openFileDescriptor(iso, "r")!!.use { pfd ->
            java.io.FileInputStream(pfd.fileDescriptor).channel.use { c ->
                ctx.contentResolver.openOutputStream(vers, "w")!!.use { out ->
                    var reste = f.taille; var p = f.pos
                    val tampon = ByteBuffer.allocate(1 shl 20)
                    while (reste > 0) {
                        tampon.clear(); tampon.limit(minOf(reste, tampon.capacity().toLong()).toInt())
                        val k = c.read(tampon, p); if (k <= 0) break
                        out.write(tampon.array(), 0, k); p += k; reste -= k
                    }
                    reste == 0L
                }
            }
        }
    } catch (e: Exception) { false }
}

private fun taille(o: Long): String = when { o >= 1 shl 20 -> String.format(java.util.Locale.FRANCE, "%.1f Mo", o / 1048576.0); o >= 1024 -> (o / 1024).toString() + " Ko"; else -> "$o o" }

@Composable
fun EcranIsoTel() {
    val ctx = LocalContext.current
    val portee = rememberCoroutineScope()
    var iso by remember { mutableStateOf<Uri?>(null) }
    var disque by remember { mutableStateOf<IsoTel.Disque?>(null) }
    var message by remember { mutableStateOf("") }
    var filtre by remember { mutableStateOf("") }
    var choisi by remember { mutableStateOf<IsoTel.Fichier?>(null) }
    val enregistrer = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { u ->
        val f = choisi; val i = iso
        if (u != null && f != null && i != null) {
            message = "Extraction de " + f.chemin + "…"
            portee.launch {
                val ok = withContext(Dispatchers.IO) { IsoTel.extraire(ctx, i, f, u) }
                message = if (ok) "Enregistré : " + f.chemin.substringAfterLast('/') + " (" + taille(f.taille) + ")" else "Impossible d'écrire le fichier à cet endroit"
            }
        }
    }
    val ouvrir = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { u ->
        if (u != null) {
            iso = u; disque = null; message = "Lecture du jeu…"
            portee.launch {
                val d = withContext(Dispatchers.IO) { try { IsoTel.ouvrir(ctx, u) } catch (e: Exception) { null } }
                disque = d
                message = if (d == null) "Ce n'est pas une image GameCube .iso / .gcm (les .rvz, .ciso et .nkit compressés ne sont pas lus)" else ""
            }
        }
    }
    Column(Modifier.fillMaxSize().padding(horizontal = 14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Doux("Choisis l'image d'un jeu GameCube (.iso ou .gcm) rangée sur le téléphone, puis le fichier à en sortir : main.dol est en premier.", taille = 12)
        Bouton(if (disque == null) "Choisir le jeu (.iso)" else "Choisir un autre jeu", Modifier.fillMaxWidth(), plein = disque == null) { ouvrir.launch(arrayOf("*/*")) }
        if (message.isNotEmpty()) Doux(message, taille = 12, couleur = C.Texte)
        val d = disque
        if (d != null) {
            Text(d.titre.ifEmpty { d.id } + "  ·  " + d.id, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.Bold)
            OutlinedTextField(filtre, { filtre = it }, Modifier.fillMaxWidth(), singleLine = true, label = { Text("Filtrer (" + d.fichiers.size + " fichiers)") })
            val l = if (filtre.isBlank()) d.fichiers else d.fichiers.filter { it.chemin.contains(filtre.trim(), ignoreCase = true) }
            LazyColumn(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                items(l, key = { it.chemin + it.pos }) { f ->
                    Carte(Modifier.fillMaxWidth(), padding = androidx.compose.foundation.layout.PaddingValues(12.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) {
                                Text(f.chemin.substringAfterLast('/'), color = C.Texte, fontSize = 14.sp, fontWeight = if (f.chemin == "/main.dol") FontWeight.Bold else FontWeight.Normal, maxLines = 1)
                                Doux(f.chemin.substringBeforeLast('/').ifEmpty { "/" } + " · " + taille(f.taille), taille = 11, lignes = 1)
                            }
                            Spacer(Modifier.width(8.dp))
                            Bouton("Extraire") { choisi = f; enregistrer.launch(d.id + "-" + f.chemin.substringAfterLast('/')) }
                        }
                    }
                }
            }
        }
    }
}
