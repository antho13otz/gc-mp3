package fr.gcstation.telecommande

// Appli : journal de bug de la vue 3D (bouton « 🐞 Bug » du plein écran). En continu, les dernieres secondes de ce que
// l'appli a recu (direct de Pulse, lectures de la memoire) et de ce qu'elle a dessine ; au clic, ce journal, une capture
// de la vue et l'etat de la camera sont ranges dans un .zip (Telechargements/GC Remote), puis proposes au partage.
// Sert a corriger les bugs des modeles 3D (roues qui flottent, personnage etire, objet qui saute...) d'apres ce qui
// s'est vraiment passe, sans avoir a les reproduire.

import android.content.Context
import android.graphics.Bitmap

object Journal3D {
    private const val MAX = 6000                           // (lignes gardees : ~8 s de direct, dessin et lectures)
    private val lignes = ArrayDeque<String>(MAX + 16)
    private val t0 = System.nanoTime()
    @Volatile var actif = true
    /** une ligne (horodatee en millisecondes depuis l'ouverture de l'appli) */
    fun note(s: String) {
        if (!actif) return
        val l = "%8d %s".format(java.util.Locale.ROOT, (System.nanoTime() - t0) / 1_000_000L, s)
        synchronized(lignes) { lignes.addLast(l); while (lignes.size > MAX) lignes.removeFirst() }
    }
    fun tout(): List<String> = synchronized(lignes) { lignes.toList() }
    /** matrice 3 x 4 en texte court (rotation a 3 decimales, position a 1) */
    fun m(a: FloatArray?, o: Int = 0): String {
        if (a == null || a.size < o + 12) return "-"
        val b = StringBuilder()
        for (i in 0..11) { if (i > 0) b.append(if (i % 4 == 0) " | " else " "); b.append(if (i % 4 == 3) "%.1f".format(java.util.Locale.ROOT, a[o + i]) else "%.3f".format(java.util.Locale.ROOT, a[o + i])) }
        return b.toString()
    }
    fun v(vararg x: Float): String = x.joinToString(" ") { "%.2f".format(java.util.Locale.ROOT, it) }

    /** le .zip : journal.txt (en-tete + lignes), vue.png ; rend son adresse (ou null) */
    fun enregistrer(c: Context, capture: Bitmap?, entete: String): android.net.Uri? = try {
        val nom = "bug3d-" + java.text.SimpleDateFormat("yyyyMMdd-HHmmss", java.util.Locale.FRANCE).format(java.util.Date()) + ".zip"
        val octets = java.io.ByteArrayOutputStream()
        java.util.zip.ZipOutputStream(octets).use { z ->
            z.putNextEntry(java.util.zip.ZipEntry("journal.txt"))
            val w = z.bufferedWriter(Charsets.UTF_8)
            w.write(entete); w.write("\n---------- dernieres lignes (ms, quoi) ----------\n")
            for (l in tout()) { w.write(l); w.write("\n") }
            w.flush(); z.closeEntry()
            if (capture != null) { z.putNextEntry(java.util.zip.ZipEntry("vue.png")); capture.compress(Bitmap.CompressFormat.PNG, 100, z); z.closeEntry() }
        }
        val data = octets.toByteArray()
        if (android.os.Build.VERSION.SDK_INT >= 29) {
            val cv = android.content.ContentValues()
            cv.put(android.provider.MediaStore.Downloads.DISPLAY_NAME, nom)
            cv.put(android.provider.MediaStore.Downloads.MIME_TYPE, "application/zip")
            cv.put(android.provider.MediaStore.Downloads.RELATIVE_PATH, android.os.Environment.DIRECTORY_DOWNLOADS + "/GC Remote")
            val u = c.contentResolver.insert(android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv)
            if (u != null) c.contentResolver.openOutputStream(u)?.use { it.write(data) }
            u
        } else {
            val f = java.io.File(c.getExternalFilesDir(android.os.Environment.DIRECTORY_DOWNLOADS), nom)
            f.writeBytes(data)
            android.net.Uri.fromFile(f)
        }
    } catch (e: Exception) { null }
}
