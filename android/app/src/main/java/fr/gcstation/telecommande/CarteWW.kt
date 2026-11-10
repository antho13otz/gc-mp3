package fr.gcstation.telecommande

// Appli (v313, GC Station v313) : la vraie carte marine de The Wind Waker, et le plan des interieurs.
//  - Carte marine : le jeu dessine chaque case de sa carte avec une image de son disque, /res/Fmap/Fmap.arc
//    ("R%02X_col_%d.bti" : numero de la case en hexadecimal, puis la variante du fichier de sauvegarde,
//    dSv_player_info_c.mRandomSalvagePoint ; d_menu_fmap2.cpp). Pendant la partie, cette archive est dans l'ARAM (que
//    Pulse ne lit pas) : l'appli la demande donc a GC Station hors partie (/api/discfile, lue dans le jeu de la carte
//    SD), la garde (files/jeux/GZLP01/Fmap.arc), puis la decode ici : Yaz0, archive RARC, textures BTI (formats GX).
//    Rien de Nintendo n'est dans l'appli ni dans le depot : les images viennent du jeu de l'utilisateur.
//  - Plan des interieurs (donjons, maisons, iles) : le chemin de Link, garde par scene (files/plans_ww/<scene>.bin :
//    x, y, z, salle en float), dessine vu du dessus ; l'etage en cours plus net (hauteur proche).

import android.content.Context
import android.graphics.Bitmap
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import java.io.File

// (build 486 : FichiersGC et CarteMarineWW (archive lue sur le disque du jeu, decodee ici) : version privee seulement ;
//  la version publique a un CarteMarineWW vide, src/publique/.../CarteMarineWW.kt)

/** le plan des interieurs de Wind Waker : le chemin de Link par scene, garde sur le telephone */
object PlanWW {
    var version by mutableIntStateOf(0)
    private val plans = HashMap<String, ArrayList<FloatArray>>()           // scene -> (x, y, z, salle)
    private val modifies = HashSet<String>()
    private var dernierEnr = 0L

    private fun dossier(c: Context) = File(c.filesDir, "plans_ww")
    private fun nomFichier(s: String) = s.filter { it.isLetterOrDigit() || it == '_' }.ifEmpty { "x" } + ".bin"

    @Synchronized fun points(c: Context?, scene: String): List<FloatArray> = (plans[scene] ?: c?.let { charger(it, scene) } ?: emptyList()).toList()

    @Synchronized private fun charger(c: Context, scene: String): ArrayList<FloatArray> {
        val l = ArrayList<FloatArray>()
        try {
            val f = File(dossier(c), nomFichier(scene))
            if (f.exists()) {
                val bb = java.nio.ByteBuffer.wrap(f.readBytes())
                while (bb.remaining() >= 16) l.add(floatArrayOf(bb.float, bb.float, bb.float, bb.float))
            }
        } catch (e: Exception) { }
        plans[scene] = l
        return l
    }

    /** Link est la : ajoute le point s'il s'est assez deplace (pas en mer, pas a l'ecran titre) */
    @Synchronized fun ajouter(c: Context?, scene: String, x: Float, y: Float, z: Float, salle: Int) {
        if (scene.isEmpty() || scene == "sea" || scene == "sea_T" || c == null) return
        val l = plans[scene] ?: charger(c, scene)
        val d = l.lastOrNull()
        if (d != null && (d[0] - x) * (d[0] - x) + (d[2] - z) * (d[2] - z) < 60f * 60f && Math.abs(d[1] - y) < 100f) return
        // (deja un point tout pres : pas de doublon, seulement le passage)
        if (l.size > 200 && l.any { Math.abs(it[0] - x) < 45f && Math.abs(it[2] - z) < 45f && Math.abs(it[1] - y) < 100f }) return
        if (l.size >= 6000) l.removeAt(0)
        l.add(floatArrayOf(x, y, z, salle.toFloat()))
        modifies.add(scene)
        version++
        val t = System.currentTimeMillis()
        if (t - dernierEnr > 10000) enregistrer(c)
    }

    @Synchronized fun enregistrer(c: Context?) {
        if (c == null || modifies.isEmpty()) return
        dernierEnr = System.currentTimeMillis()
        val d = dossier(c); d.mkdirs()
        for (s in modifies) {
            val l = plans[s] ?: continue
            try {
                val bb = java.nio.ByteBuffer.allocate(l.size * 16)
                for (p in l) { bb.putFloat(p[0]); bb.putFloat(p[1]); bb.putFloat(p[2]); bb.putFloat(p[3]) }
                File(d, nomFichier(s)).writeBytes(bb.array())
            } catch (e: Exception) { }
        }
        modifies.clear()
    }

    @Synchronized fun effacer(c: Context, scene: String) {
        plans[scene] = ArrayList(); modifies.remove(scene)
        File(dossier(c), nomFichier(scene)).delete()
        version++
    }
}

