package fr.gcstation.telecommande

// Build 556 : appli publique : pas de cartouche dans la GBA du telephone (rien qui lit ou rend des fichiers de jeux).
// Build 559 : les fonctions natives de la cartouche ne sont meme pas compilees ici (gba_jni.c, GCS_PRIVE) : jamais appelees.

import androidx.compose.runtime.Composable

object Cartouche {
    fun preparer() { }
    fun garder() { }
    @Composable fun Choix() { }
}
