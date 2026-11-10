package fr.gcstation.telecommande

// Build 486, VERSION PUBLIQUE : la carte marine du jeu (archive lue sur le disque, puis decodee) n'est pas dans cette
// version (rien qui extrait ou convertit des fichiers de jeux) : meme objet, vide ; la carte reste en grille simple.

import android.content.Context
import androidx.compose.ui.graphics.ImageBitmap

object CarteMarineWW {
    const val CHEMIN = ""
    val tuiles: Map<String, ImageBitmap> = emptyMap()
    val etat: String = ""
    val noms: List<String> = emptyList()
    fun telecharger(c: Context) { }
    fun charger(c: Context) { }
    fun entiere(i: Int): Boolean = false
    fun tuile(i: Int, v: Int): ImageBitmap? = null
}
