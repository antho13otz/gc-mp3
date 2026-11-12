package fr.gcstation.telecommande

// Build 558 : elements visuels de la page du compagnon (en-tete de la partie, cartes des accessoires du telephone) : une
// pastille d'etat, un mode d'emploi replie, une carte d'accessoire avec son icone. Les longs textes d'aide ne s'affichent
// plus d'office : un appui sur « Mode d'emploi » les montre.

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** une petite pastille d'etat (point de couleur + texte) */
@Composable
fun Pastille(t: String, couleur: Color, modifier: Modifier = Modifier) {
    Row(modifier.clip(RoundedCornerShape(50)).background(couleur.copy(alpha = 0.16f)).padding(horizontal = 10.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(7.dp).clip(CircleShape).background(couleur))
        Spacer(Modifier.width(6.dp))
        Text(t, color = couleur, fontSize = 11.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
    }
}

/** un texte d'aide replie : « ⓘ Mode d'emploi » l'ouvre */
@Composable
fun AideRepliable(texte: String, titre: String = "Mode d'emploi", petit: String = "") {
    var ouvert by remember { mutableStateOf(false) }
    Column {
        Text((if (ouvert) "▾ " else "ⓘ ") + titre, color = C.Cyan, fontSize = 12.sp, fontWeight = FontWeight.SemiBold,
            modifier = Modifier.appui { ouvert = !ouvert }.padding(vertical = 2.dp))
        if (ouvert) {
            Doux(texte, Modifier.padding(top = 4.dp), taille = 12)
            if (petit.isNotEmpty()) Doux(petit, Modifier.padding(top = 6.dp), taille = 10, couleur = C.Pale)
        }
    }
}

/** l'icone d'un accessoire ou d'une partie : un emoji dans un rond de couleur */
@Composable
fun IconeRonde(emoji: String, couleur: Color, taille: Int = 44) {
    Box(Modifier.size(taille.dp).clip(RoundedCornerShape((taille / 3).dp)).background(couleur.copy(alpha = 0.18f))
        .border(1.dp, couleur.copy(alpha = 0.45f), RoundedCornerShape((taille / 3).dp)), contentAlignment = Alignment.Center) {
        Text(emoji, fontSize = (taille * 0.48f).sp)
    }
}

/** la carte d'un accessoire du telephone : icone, titre, sous-titre, pastille d'etat, puis son contenu */
@Composable
fun CarteAccessoire(emoji: String, titre: String, sousTitre: String, couleur: Color, etat: String, etatCouleur: Color,
                    content: @Composable () -> Unit) {
    Carte(Modifier.fillMaxWidth(), androidx.compose.foundation.layout.PaddingValues(14.dp)) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconeRonde(emoji, couleur)
                Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                    Text(titre, color = C.Texte, fontSize = 16.sp, fontWeight = FontWeight.Bold, maxLines = 1)
                    Doux(sousTitre, taille = 11, lignes = 1)
                }
                Spacer(Modifier.width(8.dp))
                Pastille(etat, etatCouleur)
            }
            content()
        }
    }
}

/** titre d'une partie de la page */
@Composable
fun TitreSection(t: String) {
    Text(t.uppercase(), color = C.Pale, fontSize = 11.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.2.sp,
        modifier = Modifier.padding(start = 4.dp, top = 6.dp))
}

/** build 558 : un bloc replie (details techniques, journaux) */
@Composable
fun Repliable(titre: String, content: @Composable () -> Unit) {
    var ouvert by remember { mutableStateOf(false) }
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text((if (ouvert) "▾ " else "▸ ") + titre, color = C.Cyan, fontSize = 12.sp, fontWeight = FontWeight.SemiBold,
            modifier = Modifier.appui { ouvert = !ouvert }.padding(vertical = 2.dp))
        if (ouvert) content()
    }
}
