package fr.gcstation.telecommande

// Build 526 : appli publique : RetroAchievements en mode normal (softcore) SEULEMENT. Pas de mode hardcore : ni reglage,
// ni refus, ni texte (tout cela est dans l'appli privee, src/prive/.../ModeHardcore.kt). Rien ici ne met une partie en hardcore.

import android.content.Context
import androidx.compose.runtime.Composable

object ModeHardcore {
    const val NOM = ""
    const val TEXTE_PARTIE = ""
    const val TEXTE_POINTS = ""
    const val TEXTE_PILOTAGE = ""
    const val TEXTE_BOMBE = ""
    fun charger(c: Context) { }
    fun refuse(c: Context?, quoi: String): Boolean = false
    fun raison(g: Live.Game): String = ""
    fun veut(raison: String) = false
    fun relancer() { }
    fun imagesManquees(n: Long) { }
    const val PAUSE_MAX = 60
    fun pauseTropLongue(n: Long) { }
    @Composable fun Reglage() { }
    @Composable fun OutilsCoupes() { }
}
