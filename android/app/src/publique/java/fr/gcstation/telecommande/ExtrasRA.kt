package fr.gcstation.telecommande

// Build 566 : historique des succes, redemarrage et mise a jour de la console : ExtrasCommun (src/main).
// Build 554 : appli publique : rien (historique des succes, rapports de partie, touches en direct, redemarrage a distance :
// appli privee seulement, src/prive/.../ExtrasRA.kt).

import android.content.Context
import androidx.compose.runtime.Composable

object ExtrasRA {
    const val ACTIF = false
    fun succes(c: Context?, jeu: String, titre: String, desc: String, points: Int, badge: String, hardcore: Boolean) =
        ExtrasCommun.succes(c, jeu, titre, desc, points, badge, hardcore)   // (build 566)
    fun rapport(c: Context?, texte: String) { }
    fun fxLancement(fx: String): String = fx
    @Composable fun Historique() = ExtrasCommun.Historique()                 // (build 566)
    @Composable fun Rapports() { }
    @Composable fun Touches() { }
    @Composable fun Redemarrer(act: android.app.Activity) = ExtrasCommun.Redemarrer(act)   // (build 566)
    @Composable fun MajConsole(c: Context) = ExtrasCommun.MajConsole(c)   // (build 566)
}
