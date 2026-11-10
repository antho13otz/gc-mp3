package fr.gcstation.telecommande

// Build 452 : exporter / importer les reglages de l'appli (⚙ en haut a droite). Une desinstallation efface les reglages
// (le build 451 a du etre installe apres avoir desinstalle la 450 : nouvelle cle de signature) : ce fichier les garde a part.
// Fichier JSON dans Telechargements/GC Remote (en dehors de l'appli : garde apres une desinstallation), avec le type de
// chaque valeur. Le jeton de mise a jour n'y est pas (secret : a recoller). L'import remplace les valeurs du fichier et
// garde les autres ; l'appli se relance ensuite.

import android.app.Activity
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import android.widget.Toast
import org.json.JSONArray
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object Reglages {
    const val REQ_IMPORT = 7452
    private val FICHIERS = listOf("gcs", "cartes-r3")
    private val SECRETS = setOf("gh_jeton")

    private fun versJson(c: Context): JSONObject {
        val tout = JSONObject().put("format", "gc-remote-reglages-1").put("appli", BuildConfig.VERSION_NAME)
        for (nom in FICHIERS) {
            val o = JSONObject()
            for ((k, v) in c.getSharedPreferences(nom, Context.MODE_PRIVATE).all) {
                if (k in SECRETS || v == null) continue
                val e = JSONObject()
                when (v) {
                    is String -> e.put("t", "s").put("v", v)
                    is Int -> e.put("t", "i").put("v", v)
                    is Long -> e.put("t", "l").put("v", v)
                    is Float -> e.put("t", "f").put("v", v.toDouble())
                    is Boolean -> e.put("t", "b").put("v", v)
                    is Set<*> -> e.put("t", "ss").put("v", JSONArray(v.map { it.toString() }))
                    else -> continue
                }
                o.put(k, e)
            }
            tout.put(nom, o)
        }
        // (build 456 : les packs 3D installes, pour les rappeler a l'import ; les fichiers eux-memes vont dans des zip a cote)
        val pk = JSONArray()
        for (x in Prive.packsInstalles(c)) pk.put(JSONObject().put("jeu", x.jeu).put("dossier", x.dossier).put("fichiers", x.fichiers).put("octets", x.octets))
        tout.put("packs", pk)
        return tout
    }
    const val REQ_PACKS = 7453

    fun exporter(a: Activity) {
        if (Build.VERSION.SDK_INT < 29) { Lang.toast(a, "Exporter : Android 10 ou plus est nécessaire.", Toast.LENGTH_LONG).show(); return }
        try {
            val nom = "gc-remote-reglages-" + SimpleDateFormat("yyyyMMdd-HHmm", Locale.FRANCE).format(Date()) + ".json"
            val cv = ContentValues()
            cv.put(MediaStore.Downloads.DISPLAY_NAME, nom)
            cv.put(MediaStore.Downloads.MIME_TYPE, "application/json")
            cv.put(MediaStore.Downloads.RELATIVE_PATH, android.os.Environment.DIRECTORY_DOWNLOADS + "/GC Remote")
            val u = a.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv) ?: throw Exception("pas de fichier")
            val js = versJson(a)
            a.contentResolver.openOutputStream(u)!!.use { it.write(js.toString(1).toByteArray(Charsets.UTF_8)) }
            // (build 456 : proposer d'exporter aussi les packs 3D installes, en zip a cote, comme « Exporter le pack installe »)
            val pk = js.optJSONArray("packs")
            if (pk == null || pk.length() == 0) { Lang.toast(a, "Réglages enregistrés : Téléchargements/GC Remote/$nom", Toast.LENGTH_LONG).show(); return }
            val liste = (0 until pk.length()).joinToString("\n") { val o = pk.getJSONObject(it); "• " + o.getString("jeu") + " (" + o.getInt("fichiers") + " fichiers, " + Prive.mo(o.getLong("octets")) + ")" }
            NeonDialog(a).title("Réglages enregistrés").message("Téléchargements/GC Remote/$nom\n\nPacks 3D installés :\n$liste\n\n" +
                "Les exporter aussi (zip à côté, dans le même dossier) ? Après une réinstallation, « Importer des réglages » propose de les remettre.")
                .button("Exporter les packs", true) { exporterPacks(a, (0 until pk.length()).map { pk.getJSONObject(it).getString("dossier") }) }
                .cancel("Non merci").show()
        } catch (e: Exception) {
            Lang.toast(a, "Export impossible : " + (e.message ?: "erreur"), Toast.LENGTH_LONG).show()
        }
    }

    /** choisir le fichier (le resultat revient dans StationActivity.onActivityResult) */
    fun choisir(a: Activity) {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        try { @Suppress("DEPRECATION") a.startActivityForResult(i, REQ_IMPORT) } catch (e: Exception) {
            Lang.toast(a, "Pas de sélecteur de fichiers", Toast.LENGTH_LONG).show()
        }
    }

    fun importer(a: Activity, u: Uri) {
        try {
            val txt = a.contentResolver.openInputStream(u)!!.use { it.readBytes().toString(Charsets.UTF_8) }
            val tout = JSONObject(txt)
            if (tout.optString("format") != "gc-remote-reglages-1") throw Exception("ce n'est pas un fichier de réglages de GC Remote")
            var n = 0
            for (nom in FICHIERS) {
                val o = tout.optJSONObject(nom) ?: continue
                val ed = a.getSharedPreferences(nom, Context.MODE_PRIVATE).edit()
                for (k in o.keys()) {
                    if (k in SECRETS) continue
                    val e = o.getJSONObject(k)
                    when (e.optString("t")) {
                        "s" -> ed.putString(k, e.getString("v"))
                        "i" -> ed.putInt(k, e.getInt("v"))
                        "l" -> ed.putLong(k, e.getLong("v"))
                        "f" -> ed.putFloat(k, e.getDouble("v").toFloat())
                        "b" -> ed.putBoolean(k, e.getBoolean("v"))
                        "ss" -> { val arr = e.getJSONArray("v"); ed.putStringSet(k, (0 until arr.length()).map { arr.getString(it) }.toSet()) }
                        else -> continue
                    }
                    n++
                }
                ed.commit()
            }
            // (build 456 : ce telephone avait des packs 3D : proposer de les remettre depuis les zip exportes)
            val pk = tout.optJSONArray("packs")
            if (pk != null && pk.length() > 0) {
                val noms = (0 until pk.length()).joinToString(", ") { pk.getJSONObject(it).getString("jeu") }
                NeonDialog(a).title("Réglages importés").message("$n réglages remis.\n\nLe téléphone d'origine avait aussi des packs 3D : $noms. " +
                    "Remettre les packs maintenant ? Choisis les zip exportés (Téléchargements/GC Remote). L'appli redémarrera ensuite.")
                    .button("Choisir les zip", true) { choisirPacks(a) }
                    .button("Plus tard", false) { redemarrer(a) }.show()
            } else
            NeonDialog(a).title("Réglages importés").message("$n réglages remis. L'appli va redémarrer pour les prendre en compte.")
                .button("Redémarrer", true) { redemarrer(a) }.show()
        } catch (e: Exception) {
            Lang.toast(a, "Import impossible : " + (e.message ?: "fichier illisible"), Toast.LENGTH_LONG).show()
        }
    }

    /** build 456 : exporte les packs 3D installes l'un apres l'autre (fil a part), puis les partage */
    private fun exporterPacks(a: Activity, dossiers: List<String>) {
        if (Build.VERSION.SDK_INT < 29) return
        Lang.toast(a, "Export des packs 3D en cours… (quelques minutes pour les gros packs)", Toast.LENGTH_LONG).show()
        Thread {
            val (tous, msgs) = Prive.exporterPacks(a, dossiers)
            a.runOnUiThread {
                NeonDialog(a).title("Packs exportés").message(msgs.joinToString("\n"))
                    .button("Partager", true) { Prive.partagerPacks(a, tous) }.cancel("Fermer").show()
            }
        }.start()
    }
    /** build 456 : choisir les zip des packs exportes (plusieurs a la fois) */
    fun choisirPacks(a: Activity) {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/zip").putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
        try { @Suppress("DEPRECATION") a.startActivityForResult(i, REQ_PACKS) } catch (e: Exception) { redemarrer(a) }
    }
    /** build 456 : remet les packs choisis (le jeu d'apres le nom du zip : windwaker-, mariokart-, rayman3-) puis redemarre */
    fun installerPacks(a: Activity, l: List<Uri>) {
        Lang.toast(a, "Installation des packs…", Toast.LENGTH_LONG).show()
        Thread {
            var n = 0; val ign = ArrayList<String>()
            for (u in l) {
                val nom = try { a.contentResolver.query(u, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c -> if (c.moveToFirst()) c.getString(0) else null } } catch (e: Exception) { null } ?: ""
                try { val k = Prive.installerPack(a, nom, u); if (k == null) ign.add(nom) else n += k } catch (e: Exception) { ign.add(nom) }
            }
            a.runOnUiThread {
                NeonDialog(a).title("Packs remis").message("$n fichiers installés." + (if (ign.isEmpty()) "" else "\nIgnorés (nom inconnu) : " + ign.joinToString(", ")) +
                    "\nL'appli va redémarrer.").button("Redémarrer", true) { redemarrer(a) }.show()
            }
        }.start()
    }

    private fun redemarrer(a: Activity) {
        val i = a.packageManager.getLaunchIntentForPackage(a.packageName) ?: return
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
        a.startActivity(i)
        a.finishAffinity()
        Runtime.getRuntime().exit(0)
    }
}
