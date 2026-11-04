package fr.gcstation.telecommande

// Appli (build 361) : exporter les packs 3D installes (Wind Waker, Mario Kart, Rayman 3) tels qu'ils sont sur le telephone, en .zip
// dans Telechargements/GC Remote (MediaStore, Android 10 ou plus), en parties de 350 Mo au plus (limite d'envoi de
// 400 Mio) : la taille est verifiee avant chaque fichier. Ecrit en continu (ZipOutputStream vers le flux MediaStore,
// fichier par fichier, sans tout charger en memoire), dans un fil a part, avec l'avancement ; puis le partage.
// Les zips se reinstallent tels quels avec « Installer / mettre a jour des packs » (fichiers a plat, comme les packs).
// Bouton dans la fiche de chaque jeu (EcranJeux.kt : Wind Waker, Mario Kart, Rayman 3).

import android.app.Activity
import android.content.ContentValues
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import java.io.File
import java.io.FilterOutputStream
import java.io.OutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.zip.Deflater
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

object ExportPacks {
    /** {nom affiche, dossier dans filesDir, debut du nom des zips} */
    val JEUX = listOf(Triple("Wind Waker", "cartes-ww", "windwaker"), Triple("Mario Kart: Double Dash!!", "cartes-mk", "mariokart"), Triple("Rayman 3", "cartes-r3", "rayman3"))
    const val PARTIE = 350L * 1000 * 1000                     // (350 Mo : sous la limite d'envoi de 400 Mio)

    /** avancement : fichiers faits / total, octets lus / total, partie en cours ; fini : message de fin */
    class Etat(val jeu: String, val fait: Int, val total: Int, val octets: Long, val octetsTotal: Long, val partie: Int, val fini: String? = null)
    @Volatile var annule = false

    /** les fichiers du pack (dans les sous-dossiers aussi), tries par nom */
    fun fichiers(d: File): List<File> = d.walkTopDown().filter { it.isFile }.sortedBy { it.relativeTo(d).path }.toList()

    private class Compteur(o: OutputStream) : FilterOutputStream(o) {
        var n = 0L
        override fun write(b: Int) { out.write(b); n++ }
        override fun write(b: ByteArray, off: Int, len: Int) { out.write(b, off, len); n += len }
    }

    /** un zip dans Telechargements/GC Remote : {uri, flux} */
    private fun nouveau(a: Activity, nom: String): Pair<Uri, OutputStream>? {
        val cv = ContentValues()
        cv.put(MediaStore.Downloads.DISPLAY_NAME, nom)
        cv.put(MediaStore.Downloads.MIME_TYPE, "application/zip")
        cv.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/GC Remote")
        cv.put(MediaStore.Downloads.IS_PENDING, 1)                 // (invisible tant qu'il n'est pas fini)
        val u = a.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv) ?: return null
        val o = a.contentResolver.openOutputStream(u) ?: return null
        return u to o
    }
    private fun publier(a: Activity, u: Uri) {
        val cv = ContentValues(); cv.put(MediaStore.Downloads.IS_PENDING, 0)
        try { a.contentResolver.update(u, cv, null, null) } catch (e: Exception) { }
    }

    /** exporte le pack d'un jeu ; maj : l'avancement (fil a part) ; rend les zips ecrits */
    fun exporter(a: Activity, jeu: Triple<String, String, String>, maj: (Etat) -> Unit): List<Uri> {
        val d = File(a.filesDir, jeu.second)
        val l = fichiers(d)
        val total = l.sumOf { it.length() }
        val date = SimpleDateFormat("yyyyMMdd-HHmm", Locale.FRANCE).format(Date())
        val faits = ArrayList<Uri>()
        var partie = 0
        var zip: ZipOutputStream? = null
        var cpt: Compteur? = null
        var uri: Uri? = null
        var octets = 0L
        val buf = ByteArray(256 * 1024)
        fun fermer() {
            zip?.finish(); zip?.close(); zip = null
            uri?.let { publier(a, it); faits.add(it) }; uri = null
        }
        try {
            for ((i, f) in l.withIndex()) {
                if (annule) break
                val taille = f.length()
                // (avant chaque fichier : la partie depasserait-elle 350 Mo ? au pire, un fichier deja compresse
                //  grossit un peu dans le zip : marge de 1 % + l'entete)
                val c = cpt
                if (zip == null || (c != null && c.n > 0 && c.n + taille + taille / 100 + 1024 > PARTIE)) {
                    fermer()
                    partie++
                    val (u, o) = nouveau(a, jeu.third + "-installe-" + date + "-partie" + partie + ".zip") ?: throw java.io.IOException("Téléchargements inaccessibles")
                    uri = u
                    val cc = Compteur(o.buffered(1024 * 1024)); cpt = cc
                    zip = ZipOutputStream(cc).apply { setLevel(Deflater.BEST_SPEED) }
                }
                val z = zip!!
                val e = ZipEntry(f.relativeTo(d).path.replace(File.separatorChar, '/'))
                e.time = f.lastModified()
                z.putNextEntry(e)
                f.inputStream().use { ins -> while (true) { val n = ins.read(buf); if (n <= 0) break; z.write(buf, 0, n); octets += n } }
                z.closeEntry()
                if (i % 20 == 0 || i == l.size - 1) maj(Etat(jeu.first, i + 1, l.size, octets, total, partie))
            }
            fermer()
        } catch (e: Exception) {
            try { zip?.close() } catch (x: Exception) { }
            uri?.let { try { a.contentResolver.delete(it, null, null) } catch (x: Exception) { } }
            maj(Etat(jeu.first, 0, l.size, octets, total, partie, "Arrêté : " + (e.message ?: "erreur d'écriture") + (if (faits.isEmpty()) "" else " (" + faits.size + " partie(s) déjà enregistrée(s))")))
            return faits
        }
        maj(Etat(jeu.first, l.size, l.size, octets, total, partie,
            if (annule) "Arrêté : " + faits.size + " partie(s) enregistrée(s), incomplètes"
            else "Fini : " + l.size + " fichiers, " + mo(total) + " → " + faits.size + " zip dans Téléchargements/GC Remote"))
        return faits
    }

    fun partager(a: Activity, l: List<Uri>, nom: String) {
        if (l.isEmpty()) return
        val i = if (l.size == 1) Intent(Intent.ACTION_SEND).setType("application/zip").putExtra(Intent.EXTRA_STREAM, l[0])
                else Intent(Intent.ACTION_SEND_MULTIPLE).setType("application/zip").putParcelableArrayListExtra(Intent.EXTRA_STREAM, ArrayList(l))
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        try { a.startActivity(Intent.createChooser(i, "Envoyer le pack $nom")) } catch (e: Exception) { }
    }
    fun mo(o: Long) = if (o >= 1000L * 1000 * 1000) String.format(Locale.FRANCE, "%.2f Go", o / 1e9) else String.format(Locale.FRANCE, "%.0f Mo", o / 1e6)
}

/** fiche du jeu : « Exporter le pack installé » (dossier : cartes-ww, cartes-mk ou cartes-r3) ; le partage ensuite */
@Composable
fun ExportPackJeu(dossier: String) {
    val a = LocalContext.current as Activity
    val j = ExportPacks.JEUX.first { it.second == dossier }
    var etat by remember { mutableStateOf<ExportPacks.Etat?>(null) }
    var zips by remember { mutableStateOf<List<Uri>>(emptyList()) }
    var enCours by remember { mutableStateOf(false) }
    var info by remember { mutableStateOf<Pair<Int, Long>?>(null) }
    androidx.compose.runtime.LaunchedEffect(dossier, enCours) {
        info = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { val l = ExportPacks.fichiers(File(a.filesDir, dossier)); l.size to l.sumOf { it.length() } }
    }
    val (n, o) = info ?: (0 to 0L)
    Column(Modifier.fillMaxWidth().padding(top = 10.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        val e = etat
        if (e == null || e.fini != null) {
            Bouton(if (info == null) "Exporter le pack installé…" else if (n == 0) "Exporter le pack : rien d'installé" else "Exporter le pack installé ($n fichiers, " + ExportPacks.mo(o) + ")",
                Modifier.fillMaxWidth(), icone = Ic.Copies, actif = !enCours && n > 0 && Build.VERSION.SDK_INT >= 29) {
                enCours = true; ExportPacks.annule = false; zips = emptyList()
                etat = ExportPacks.Etat(j.first, 0, n, 0, o, 1)
                Thread {
                    val l = ExportPacks.exporter(a, j) { x -> a.runOnUiThread { etat = x } }
                    a.runOnUiThread { enCours = false; zips = l; ExportPacks.partager(a, l, j.first) }
                }.start()
            }
            if (Build.VERSION.SDK_INT < 29) Doux("Exporter : Android 10 ou plus est nécessaire.", taille = 11, couleur = C.Pale)
        }
        if (e != null) {
            if (e.fini == null) {
                LinearProgressIndicator(progress = { if (e.octetsTotal > 0) e.octets.toFloat() / e.octetsTotal else 0f }, modifier = Modifier.fillMaxWidth().height(6.dp), color = C.Rose)
                Doux("Export : fichier " + e.fait + " / " + e.total + " · " + ExportPacks.mo(e.octets) + " / " + ExportPacks.mo(e.octetsTotal) + " · partie " + e.partie, taille = 12)
                Bouton("Arrêter l'export", Modifier.fillMaxWidth()) { ExportPacks.annule = true }
            } else {
                Doux(e.fini, taille = 12, couleur = C.Texte)
                if (zips.isNotEmpty()) Bouton("Partager à nouveau (" + zips.size + " zip)", Modifier.fillMaxWidth(), plein = true) { ExportPacks.partager(a, zips, e.jeu) }
            }
        }
    }
}
