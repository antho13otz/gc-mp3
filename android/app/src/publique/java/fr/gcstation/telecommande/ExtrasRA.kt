package fr.gcstation.telecommande

// Build 554 : appli publique : rien (historique des succes, rapports de partie, touches en direct, redemarrage a distance :
// appli privee seulement, src/prive/.../ExtrasRA.kt).

import android.content.Context
import androidx.compose.runtime.Composable

object ExtrasRA {
    const val ACTIF = false
    fun succes(c: Context?, jeu: String, titre: String, desc: String, points: Int, badge: String, hardcore: Boolean) { }
    fun rapport(c: Context?, texte: String) { }
    fun fxLancement(fx: String): String = fx
    @Composable fun Historique() { }
    @Composable fun Rapports() { }
    @Composable fun Touches() { }
    @Composable fun Redemarrer(act: android.app.Activity) { }
}
