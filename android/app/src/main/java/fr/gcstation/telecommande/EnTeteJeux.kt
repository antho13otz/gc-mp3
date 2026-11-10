package fr.gcstation.telecommande

// Appli privee build 503 : le haut de l'onglet Jeux refait autour de Pulse. Une grande carte « Pulse » : son logo anime
// a gauche (le meme que l'ecran de chargement du build 502), a droite le dernier lancement (jeu, resultat, quand, ou),
// dessous Journal / Partager / Succes. Puis la recherche, les filtres, les deux vues, et (build 504) un menu ⋮ pour
// « Tout garder pour hors-ligne » et « Envoyer jaquettes et disques a la console ». Build 512 : aussi dans la version publique (Rangee.ACTIF, logo Pulse fixe).

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** le haut de l'onglet Jeux (appli privee) ; version : change a chaque nouvelle lecture du dernier lancement */
@Composable
internal fun EnTeteJeuxPulse(act: StationActivity, dernier: LastLaunch, version: Int, q: String, surQ: (String) -> Unit, filtre: Int, surFiltre: (Int) -> Unit,
                    avecVues: Boolean, avecConsole: Boolean, etagere: () -> Unit, mur: () -> Unit, garder: () -> Unit, envoyer: () -> Unit) {
    val present = version >= 0 && dernier.present      // (la lecture de « version » fait redessiner la carte)
    val enJeu = St.jeu
    Column {
        // ---- la carte Pulse
        Column(Modifier.fillMaxWidth().verre(RoundedCornerShape(26.dp), C.Rose.copy(alpha = 0.40f)).padding(horizontal = 14.dp, vertical = 14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Box(Modifier.size(104.dp), contentAlignment = Alignment.Center) { Prive.LogoPulse(Modifier.size(104.dp)) }
                    Text("Pulse", style = TextStyle(brush = Brush.verticalGradient(listOf(Color(255, 150, 215), Color(130, 150, 255)))),
                        fontSize = 22.sp, fontWeight = FontWeight.Black, letterSpacing = 1.sp)
                }
                Spacer(Modifier.width(14.dp))
                Column(Modifier.weight(1f)) {
                    if (present) {
                        Titre("Dernier lancement")
                        Spacer(Modifier.height(4.dp))
                        Text(dernier.nom, Modifier.appui { dernier.ouvrir() }, color = C.Texte, fontSize = 16.sp, fontWeight = FontWeight.Bold,
                            maxLines = 2, overflow = TextOverflow.Ellipsis, lineHeight = 20.sp)
                        Doux(dernier.jeuId(), taille = 11, lignes = 1)
                        Spacer(Modifier.height(6.dp))
                        Text(dernier.verdictTxt, color = Color(dernier.couleur), fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        if (dernier.detail.isNotEmpty()) Doux(dernier.detail, taille = 12, lignes = 2)
                    } else {
                        Titre(if (enJeu != null) "En jeu" else "Pulse")
                        Spacer(Modifier.height(4.dp))
                        Text(when {
                            enJeu != null -> enJeu.name.ifEmpty { enJeu.id }
                            St.connecte -> "Prêt"
                            else -> "Console pas connectée"
                        }, color = C.Texte, fontSize = 16.sp, fontWeight = FontWeight.Bold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        Doux(if (St.connecte) "Pulse reste en mémoire pendant les jeux : le résultat du dernier lancement s'affichera ici."
                             else "Le dernier lancement s'affichera ici une fois la console connectée.", taille = 12)
                    }
                }
            }
            if (present) {
                Spacer(Modifier.height(12.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Bouton("Journal", Modifier.weight(1f), hauteur = 38.dp) { LastLaunch.showJournal(act) }
                    Bouton("Partager", Modifier.weight(1f), plein = true, icone = Ic.Partager, hauteur = 38.dp) { LastLaunch.share(act, dernier.jeuId()) }
                    Bouton("Succès", Modifier.weight(1f), hauteur = 38.dp) { LastLaunch.showAch(act) }
                }
            }
        }
        // ---- chercher, filtrer
        Spacer(Modifier.height(10.dp))
        Champ(q, surQ, "Chercher un jeu", icone = Ic.Chercher)
        Spacer(Modifier.height(8.dp))
        RangeeDePuces {
            Puce("Tous", filtre == 0) { surFiltre(0) }
            Puce("★ Favoris", filtre == 1) { surFiltre(1) }
            Puce("Déjà joués", filtre == 2) { surFiltre(2) }
        }
        // ---- les vues, et (build 504) un menu ⋮ pour les deux actions qui servent rarement
        if (avecVues || avecConsole) Row(Modifier.fillMaxWidth().padding(top = 10.dp), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            if (avecVues) {
                Bouton("Étagère 3D", Modifier.weight(1f), plein = true, hauteur = 40.dp, onClick = etagere)
                Bouton("Mur de jaquettes", Modifier.weight(1f), hauteur = 40.dp, onClick = mur)
            } else Spacer(Modifier.weight(1f))
            if (avecConsole) Box {
                var menu by remember { mutableStateOf(false) }
                Box(Modifier.size(40.dp).verre(CircleShape).appui { menu = true }, contentAlignment = Alignment.Center) { Icon(Ic.Points, "Autres actions", tint = C.Texte, modifier = Modifier.size(20.dp)) }
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    DropdownMenuItem(text = { Text("Tout garder pour hors-ligne") }, onClick = { menu = false; garder() })
                    DropdownMenuItem(text = { Text("Envoyer jaquettes et disques à la console") }, onClick = { menu = false; envoyer() })
                }
            }
        }
    }
}
