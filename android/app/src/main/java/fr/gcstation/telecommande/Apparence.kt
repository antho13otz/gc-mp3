package fr.gcstation.telecommande

// Appli : Plus > Apparence. L'ambiance (couleurs de tous les ecrans : Neon, Sombre sobre, GameCube indigo, ou les
// couleurs du jeu en cours / de la derniere fiche ouverte, tirees de sa jaquette), la taille du texte, les vibrations
// et le 120 Hz. Garde dans les preferences ; tout change tout de suite.

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun EcranApparence() {
    val act = LocalContext.current as StationActivity
    val pr = remember { act.getSharedPreferences("gcs", 0) }
    var vib by remember { mutableStateOf(C.vibrations) }
    var hz by remember { mutableStateOf(pr.getBoolean("hz_max", true)) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 14.dp).padding(bottom = 20.dp)) {
        Titre("Ambiance")
        Spacer(Modifier.height(8.dp))
        for ((i, nom) in Ambiances.NOMS.withIndex()) {
            val p = when (i) { 1 -> Ambiances.SOBRE; 2 -> Ambiances.INDIGO; 3 -> C.duJeu ?: Ambiances.NEON; else -> Ambiances.NEON }
            val choisi = C.ambiance == i
            Row(Modifier.fillMaxWidth().padding(bottom = 8.dp).verre(RoundedCornerShape(18.dp), if (choisi) p.rose else C.Bord)
                .appui { C.ambiance = i; pr.edit().putInt("ambiance", i).apply() }.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                // (apercu : le fond et les trois couleurs)
                Box(Modifier.size(width = 64.dp, height = 44.dp).background(Brush.verticalGradient(listOf(p.fond2, p.fond)), RoundedCornerShape(10.dp))
                    .border(1.dp, Color(0x33FFFFFF), RoundedCornerShape(10.dp)), contentAlignment = Alignment.Center) {
                    Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        for (c in listOf(p.rose, p.violet, p.cyan)) Box(Modifier.size(12.dp).background(c, CircleShape))
                    }
                }
                Spacer(Modifier.size(12.dp))
                Column(Modifier.weight(1f)) {
                    Text(nom, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.Bold)
                    Doux(when (i) {
                        0 -> "Le néon rose, violet et cyan d'origine"
                        1 -> "Gris profond et bleu doux, discret"
                        2 -> "L'indigo de la GameCube"
                        else -> if (C.duJeu != null) "Celles de la jaquette du jeu en cours (ou de la dernière fiche ouverte)" else "Ouvre la fiche d'un jeu : ses couleurs viennent de sa jaquette"
                    }, taille = 12, lignes = 2)
                }
                if (choisi) Box(Modifier.size(12.dp).background(p.rose, CircleShape))
            }
        }
        Spacer(Modifier.height(10.dp))
        Titre("Taille du texte")
        Spacer(Modifier.height(8.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for ((t, k) in listOf("Petit" to 0.9f, "Normal" to 1f, "Grand" to 1.15f, "Très grand" to 1.3f))
                Puce(t, kotlin.math.abs(C.texte - k) < 0.01f) { C.texte = k; pr.edit().putFloat("taille_texte", k).apply() }
        }
        Spacer(Modifier.height(18.dp))
        Titre("Animations")
        Spacer(Modifier.height(8.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Puce("Vibrations", vib) { vib = !vib; C.vibrations = vib; pr.edit().putBoolean("vibrations", vib).apply() }
            Puce("120 Hz", hz) { hz = !hz; pr.edit().putBoolean("hz_max", hz).apply(); act.frequence() }
        }
        Spacer(Modifier.height(6.dp))
        Doux("120 Hz : l'appli demande au téléphone sa fréquence la plus haute (défilements et animations plus fluides ; un peu plus de batterie).", taille = 11, couleur = C.Pale)
    }
}
