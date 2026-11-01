package fr.gcstation.telecommande

// Appli : couleurs, fond, cartes en verre, boutons et icones (dessinees ici : pas de grosse bibliotheque d'icones).

import android.view.HapticFeedbackConstants
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.addPathNodes
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** une ambiance de l'appli (les couleurs de tous les ecrans) */
class Palette(val rose: Color, val violet: Color, val bleu: Color, val cyan: Color, val fond: Color, val fond2: Color,
              val texte: Color, val doux: Color, val pale: Color, val surface: Color)

object Ambiances {
    val NOMS = listOf("Néon", "Sombre sobre", "GameCube indigo", "Couleurs du jeu")
    val NEON = Palette(Color(0xFFFF2FD0), Color(0xFFB03CFF), Color(0xFF5A5CFF), Color(0xFF21E8FF), Color(0xFF07020F), Color(0xFF120624),
        Color(0xFFF4EEFF), Color(0xFFB7A8D8), Color(0xFF6E6290), Color(0xFF140A26))
    val SOBRE = Palette(Color(0xFF7FA7FF), Color(0xFF9294B0), Color(0xFF6C8CFF), Color(0xFFA6CCFF), Color(0xFF0A0A0C), Color(0xFF17171B),
        Color(0xFFEDEDF0), Color(0xFFA9A9B4), Color(0xFF6B6B77), Color(0xFF161619))
    val INDIGO = Palette(Color(0xFF9A8CFF), Color(0xFF6656D0), Color(0xFF4B3F99), Color(0xFFC3BCFF), Color(0xFF090720), Color(0xFF1A1550),
        Color(0xFFF1EFFF), Color(0xFFB4AEDF), Color(0xFF6B64A2), Color(0xFF151140))
    /** les couleurs d'une jaquette : la plus vive, une deuxieme d'une autre teinte, et un fond sombre de la teinte dominante */
    fun duJeu(b: android.graphics.Bitmap): Palette? {
        val p = android.graphics.Bitmap.createScaledBitmap(b, 32, 44, true)
        val hsv = FloatArray(3)
        var best = -1f; var c1 = 0; var h1 = 0f
        val teintes = FloatArray(12)
        for (y in 0 until p.height) for (x in 0 until p.width) {
            val c = p.getPixel(x, y)
            android.graphics.Color.colorToHSV(c, hsv)
            val w = hsv[1] * hsv[2]
            teintes[((hsv[0] / 30f).toInt()).coerceIn(0, 11)] += w
            if (w > best) { best = w; c1 = c; h1 = hsv[0] }
        }
        if (best < 0.25f) return null                       // (jaquette grise : on garde le neon)
        var best2 = -1f; var c2 = 0
        for (y in 0 until p.height) for (x in 0 until p.width) {
            val c = p.getPixel(x, y)
            android.graphics.Color.colorToHSV(c, hsv)
            var d = kotlin.math.abs(hsv[0] - h1); if (d > 180) d = 360 - d
            val w = hsv[1] * hsv[2]
            if (d > 60 && w > best2) { best2 = w; c2 = c }
        }
        if (best2 < 0.2f) c2 = c1
        val dom = teintes.indices.maxByOrNull { teintes[it] } ?: 0
        fun vif(c: Int): Color { android.graphics.Color.colorToHSV(c, hsv); hsv[1] = hsv[1].coerceAtLeast(0.55f); hsv[2] = hsv[2].coerceAtLeast(0.9f); return Color(android.graphics.Color.HSVToColor(hsv)) }
        fun ton(s: Float, v: Float): Color = Color(android.graphics.Color.HSVToColor(floatArrayOf(dom * 30f + 15f, s, v)))
        val a = vif(c1); val b2 = vif(c2)
        return Palette(a, androidx.compose.ui.graphics.lerp(a, b2, 0.5f), b2, b2, ton(0.55f, 0.05f), ton(0.6f, 0.13f),
            Color(0xFFF6F2FF), ton(0.18f, 0.82f), ton(0.25f, 0.55f), ton(0.55f, 0.10f))
    }
}

object C {
    var ambiance by androidx.compose.runtime.mutableIntStateOf(0)         // (0 neon, 1 sobre, 2 indigo, 3 couleurs du jeu)
    var duJeu by androidx.compose.runtime.mutableStateOf<Palette?>(null)  // (couleurs du jeu en cours / du dernier ouvert)
    var texte by androidx.compose.runtime.mutableFloatStateOf(1f)         // (taille du texte : 0,9 / 1 / 1,15 / 1,3)
    var vibrations = true                                                 // (petite vibration des boutons)
    val p: Palette get() = when (ambiance) { 1 -> Ambiances.SOBRE; 2 -> Ambiances.INDIGO; 3 -> duJeu ?: Ambiances.NEON; else -> Ambiances.NEON }
    val Rose: Color get() = p.rose
    val Violet: Color get() = p.violet
    val Bleu: Color get() = p.bleu
    val Cyan: Color get() = p.cyan
    val Vert = Color(0xFF2FE08A)
    val Rouge = Color(0xFFFF4D6D)
    val Orange = Color(0xFFFFA040)
    val Fond: Color get() = p.fond
    val Fond2: Color get() = p.fond2
    val Texte: Color get() = p.texte
    val Doux: Color get() = p.doux
    val Pale: Color get() = p.pale
    val Verre = Color(0x14FFFFFF)
    val Bord = Color(0x26FFFFFF)
    val Neon: Brush get() = Brush.linearGradient(listOf(Rose, Violet, Cyan))
    /** le jeu dont prendre les couleurs (fiche ouverte, ou jeu lance) : ses couleurs sont calculees en fond */
    var idJeu by androidx.compose.runtime.mutableStateOf("")
    fun dernierJeu(c: android.content.Context, id: String) {
        if (id.length != 6 || id == idJeu) return
        idJeu = id
        try { c.getSharedPreferences("gcs", 0).edit().putString("ambiance_jeu", id).apply() } catch (e: Exception) { }
    }
    /** les choix gardes (appele au demarrage) */
    fun lire(c: android.content.Context) {
        val pr = c.getSharedPreferences("gcs", 0)
        ambiance = pr.getInt("ambiance", 0).coerceIn(0, 3); texte = pr.getFloat("taille_texte", 1f).coerceIn(0.8f, 1.4f)
        idJeu = pr.getString("ambiance_jeu", "") ?: ""; vibrations = pr.getBoolean("vibrations", true)
    }
}

private fun couleurs(): ColorScheme = darkColorScheme(
    primary = C.Rose, onPrimary = Color.White, secondary = C.Cyan, onSecondary = Color(0xFF001F24),
    tertiary = C.Violet, background = C.Fond, onBackground = C.Texte, surface = C.p.surface, onSurface = C.Texte,
    surfaceVariant = androidx.compose.ui.graphics.lerp(C.p.surface, Color.White, 0.05f), onSurfaceVariant = C.Doux, outline = C.Bord, error = C.Rouge,
    surfaceContainer = C.p.surface, surfaceContainerHigh = androidx.compose.ui.graphics.lerp(C.p.surface, Color.White, 0.04f),
    surfaceContainerHighest = androidx.compose.ui.graphics.lerp(C.p.surface, Color.White, 0.08f),
    surfaceContainerLow = androidx.compose.ui.graphics.lerp(C.p.surface, Color.Black, 0.2f), surfaceContainerLowest = C.Fond)

@Composable
fun StationTheme(content: @Composable () -> Unit) {
    val t = Typography()
    val d = androidx.compose.ui.platform.LocalDensity.current
    // (taille du texte choisie : en plus de celle d'Android ; les couleurs suivent l'ambiance, en douceur)
    androidx.compose.runtime.CompositionLocalProvider(androidx.compose.ui.platform.LocalDensity provides androidx.compose.ui.unit.Density(d.density, d.fontScale * C.texte)) {
        MaterialTheme(
            colorScheme = couleurs(),
            typography = t.copy(
                titleLarge = t.titleLarge.copy(fontWeight = FontWeight.Bold, letterSpacing = 0.2.sp),
                titleMedium = t.titleMedium.copy(fontWeight = FontWeight.SemiBold)),
            content = content)
    }
}

/** fond : degrade sombre et deux halos qui bougent doucement */
@Composable
fun FondAurore(modifier: Modifier = Modifier, actif: Boolean = true) {
    val tr = rememberInfiniteTransition(label = "aurore")
    val p by tr.animateFloat(0f, 1f, infiniteRepeatable(tween(14000), RepeatMode.Reverse), label = "p")
    val k = if (actif) p else 0.5f
    // (rubans de lumiere qui ondulent, comme le fond de la console : memes hauteurs, ondulations et vitesses que
    //  au_ribbon de ui_aurore.c, ramenes a la taille de l'ecran du telephone)
    val temps by androidx.compose.runtime.produceState(0f, actif) {
        if (!actif) return@produceState
        val t0 = System.nanoTime() - (value * 1e9f).toLong()
        while (true) androidx.compose.runtime.withFrameNanos { value = (System.nanoTime() - t0) / 1e9f }
    }
    Canvas(modifier.fillMaxSize()) {
        drawRect(Brush.verticalGradient(listOf(C.Fond2, C.Fond)))
        drawRect(Brush.radialGradient(listOf(C.Violet.copy(alpha = 0.28f), Color.Transparent),
            center = Offset(size.width * (0.15f + 0.5f * k), size.height * 0.12f), radius = size.width * 0.9f))
        drawRect(Brush.radialGradient(listOf(C.Rose.copy(alpha = 0.16f), Color.Transparent),
            center = Offset(size.width * (0.95f - 0.5f * k), size.height * 0.75f), radius = size.width * 0.8f))
        ruban(118f, 16f, 70f, temps, 0.35f, 0.0f, 0.30f)
        ruban(430f, 22f, 110f, temps, 0.25f, 2.1f, 0.26f)
        ruban(460f, 12f, 60f, temps, 0.42f, 4.0f, 0.18f)
    }
}

/** un ruban de lumiere (repere de la console : 640 x 480), fondu vers le haut et un peu vers le bas */
private fun androidx.compose.ui.graphics.drawscope.DrawScope.ruban(base: Float, amp: Float, ep: Float, t: Float, vit: Float, ph: Float, alpha: Float) {
    val N = 24
    val sx = size.width / 640f; val sy = size.height / 480f
    val xs = FloatArray(N + 1); val ys = FloatArray(N + 1)
    for (i in 0..N) {
        val x = -10f + i * 660f / N
        xs[i] = x * sx
        ys[i] = (base + amp * kotlin.math.sin(x * 0.012f + t * vit + ph) + 0.4f * amp * kotlin.math.sin(x * 0.031f - t * vit * 1.7f)) * sy
    }
    val haut = ep * sy; val bas = ep * 0.35f * sy
    for (i in 0 until N) {
        val f = i / N.toFloat()
        val c = (if (f < 0.5f) androidx.compose.ui.graphics.lerp(C.Rose, C.Violet, f * 2f) else androidx.compose.ui.graphics.lerp(C.Violet, C.Cyan, (f - 0.5f) * 2f)).copy(alpha = alpha)
        val y = (ys[i] + ys[i + 1]) / 2
        val p1 = androidx.compose.ui.graphics.Path().apply {
            moveTo(xs[i], ys[i] - haut); lineTo(xs[i + 1], ys[i + 1] - haut); lineTo(xs[i + 1], ys[i + 1]); lineTo(xs[i], ys[i]); close()
        }
        drawPath(p1, Brush.verticalGradient(listOf(Color.Transparent, c), startY = y - haut, endY = y))
        val p2 = androidx.compose.ui.graphics.Path().apply {
            moveTo(xs[i], ys[i]); lineTo(xs[i + 1], ys[i + 1]); lineTo(xs[i + 1], ys[i + 1] + bas); lineTo(xs[i], ys[i] + bas); close()
        }
        drawPath(p2, Brush.verticalGradient(listOf(c, Color.Transparent), startY = y, endY = y + bas))
    }
}

fun Modifier.verre(shape: Shape = RoundedCornerShape(22.dp), bord: Color = C.Bord, fond: Color = C.Verre): Modifier =
    this.clip(shape).background(fond, shape).border(1.dp, bord, shape)

/** petite vibration d'un bouton */
@Composable
fun vibre(): () -> Unit {
    val v = LocalView.current
    return { if (C.vibrations) v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY) }
}

/** zone touchable qui retrecit un peu a l'appui */
@Composable
fun Modifier.appui(onClick: () -> Unit): Modifier {
    val src = remember { MutableInteractionSource() }
    val pressed by src.collectIsPressedAsState()
    val s by animateFloatAsState(if (pressed) 0.94f else 1f, tween(120), label = "appui")
    val buzz = vibre()
    return this.scale(s).clickable(interactionSource = src, indication = null) { buzz(); onClick() }
}

@Composable
fun Carte(modifier: Modifier = Modifier, padding: PaddingValues = PaddingValues(16.dp), content: @Composable () -> Unit) {
    Box(modifier.verre().padding(padding)) { content() }
}

@Composable
fun Titre(t: String, modifier: Modifier = Modifier, couleur: Color = C.Cyan) {
    Text((if (Lang.anglais) Lang.t(t) else t).uppercase(), modifier, color = couleur, fontSize = 12.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.6.sp)   // (traduit avant les majuscules)
}

@Composable
fun Doux(t: String, modifier: Modifier = Modifier, taille: Int = 13, couleur: Color = C.Doux, lignes: Int = Int.MAX_VALUE, centre: Boolean = false) {
    Text(t, modifier, color = couleur, fontSize = taille.sp, maxLines = lignes, overflow = TextOverflow.Ellipsis, textAlign = if (centre) TextAlign.Center else null)
}

/** bouton : plein (degrade neon) ou en verre */
@Composable
fun Bouton(t: String, modifier: Modifier = Modifier, plein: Boolean = false, icone: ImageVector? = null, couleur: Color = C.Texte,
           hauteur: Dp = 46.dp, actif: Boolean = true, onClick: () -> Unit) {
    val sh = RoundedCornerShape(16.dp)
    val m = if (plein) modifier.clip(sh).background(C.Neon, sh) else modifier.verre(sh, if (actif) C.Rose.copy(alpha = 0.45f) else C.Bord)
    Row(m.then(if (actif) Modifier.appui(onClick) else Modifier).height(hauteur).padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.Center) {
        if (icone != null) { Icon(icone, null, tint = if (plein) Color.White else couleur, modifier = Modifier.size(18.dp)); if (t.isNotEmpty()) Box(Modifier.size(8.dp)) }
        if (t.isNotEmpty()) Text(t, color = if (!actif) C.Pale else if (plein) Color.White else couleur, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

/** bouton rond (commandes de lecture) */
@Composable
fun Rond(icone: ImageVector, taille: Dp, plein: Boolean = false, onClick: () -> Unit) {
    val m = if (plein) Modifier.size(taille).clip(CircleShape).background(C.Neon, CircleShape)
            else Modifier.size(taille).verre(CircleShape, C.Rose.copy(alpha = 0.5f))
    Box(m.appui(onClick), contentAlignment = Alignment.Center) {
        Icon(icone, null, tint = Color.White, modifier = Modifier.size(taille * 0.42f))
    }
}

@Composable
fun Puce(t: String, choisie: Boolean, onClick: () -> Unit) {
    val sh = RoundedCornerShape(50)
    val m = if (choisie) Modifier.clip(sh).background(C.Neon, sh) else Modifier.verre(sh)
    Box(m.appui(onClick).padding(horizontal = 16.dp, vertical = 9.dp)) {
        Text(t, color = if (choisie) Color.White else C.Doux, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
fun RangeeDePuces(content: @Composable RowScope.() -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), content = content)
}

@Composable
fun Champ(valeur: String, onChange: (String) -> Unit, indice: String, modifier: Modifier = Modifier, icone: ImageVector? = null,
          clavier: KeyboardType = KeyboardType.Text, uneLigne: Boolean = true, mono: Boolean = false) {
    OutlinedTextField(valeur, onChange, modifier.fillMaxWidth(), singleLine = uneLigne,
        placeholder = { Text(indice, color = C.Pale, fontSize = 14.sp) },
        leadingIcon = if (icone != null) ({ Icon(icone, null, tint = C.Doux) }) else null,
        textStyle = TextStyle(color = C.Texte, fontSize = 15.sp, fontFamily = if (mono) androidx.compose.ui.text.font.FontFamily.Monospace else null),
        keyboardOptions = KeyboardOptions(keyboardType = clavier),
        shape = RoundedCornerShape(18.dp),
        colors = OutlinedTextFieldDefaults.colors(
            focusedBorderColor = C.Cyan, unfocusedBorderColor = C.Bord, cursorColor = C.Cyan,
            focusedContainerColor = C.Verre, unfocusedContainerColor = C.Verre,
            focusedTextColor = C.Texte, unfocusedTextColor = C.Texte))
}

/** ligne de liste : pastille, titre, sous-titre, a droite : ce qu'on veut */
@Composable
fun Ligne(titre: String, sous: String = "", modifier: Modifier = Modifier, gauche: @Composable (() -> Unit)? = null,
          droite: @Composable (() -> Unit)? = null) {
    Row(modifier.fillMaxWidth().verre(RoundedCornerShape(18.dp)).padding(horizontal = 14.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically) {
        if (gauche != null) { gauche(); Box(Modifier.size(12.dp)) }
        Column(Modifier.weight(1f)) {
            Text(titre, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (sous.isNotEmpty()) Text(sous, color = C.Doux, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        if (droite != null) droite()
    }
}

/** pastille ronde en degrade avec un symbole */
@Composable
fun Pastille(icone: ImageVector, couleur: Color = C.Rose, taille: Dp = 38.dp) {
    Box(Modifier.size(taille).clip(CircleShape).background(Brush.linearGradient(listOf(couleur.copy(alpha = 0.55f), couleur.copy(alpha = 0.15f)))),
        contentAlignment = Alignment.Center) { Icon(icone, null, tint = Color.White, modifier = Modifier.size(taille * 0.5f)) }
}

/** icones (chemins SVG des icones Material, licence Apache 2.0) */
object Ic {
    private fun v(nom: String, d: String) = ImageVector.Builder(nom, 24.dp, 24.dp, 24f, 24f)
        .addPath(addPathNodes(d), fill = SolidColor(Color.White)).build()
    val Lecture = v("lecture", "M8,5v14l11,-7z")
    val Pause = v("pause", "M6,19h4L10,5L6,5v14zM14,5v14h4L18,5h-4z")
    val Suivant = v("suivant", "M6,18l8.5,-6L6,6v12zM16,6v12h2L18,6h-2z")
    val Precedent = v("precedent", "M6,6h2v12L6,18zM9.5,12l8.5,6L18,6z")
    val Stop = v("stop", "M6,6h12v12H6z")
    val Musique = v("musique", "M12,3v10.55c-0.59,-0.34 -1.27,-0.55 -2,-0.55 -2.21,0 -4,1.79 -4,4s1.79,4 4,4 4,-1.79 4,-4V7h4V3h-6z")
    val Radio = v("radio", "M3.24,6.15C2.51,6.43 2,7.17 2,8v12c0,1.1 0.89,2 2,2h16c1.11,0 2,-0.9 2,-2V8c0,-1.11 -0.89,-2 -2,-2H8.3l8.26,-3.34L15.88,1 3.24,6.15zM7,20c-1.66,0 -3,-1.34 -3,-3s1.34,-3 3,-3 3,1.34 3,3 -1.34,3 -3,3zM20,12h-2v-2h-2v2H4V8h16v4z")
    val Jeux = v("jeux", "M21,6H3c-1.1,0 -2,0.9 -2,2v8c0,1.1 0.9,2 2,2h18c1.1,0 2,-0.9 2,-2V8c0,-1.1 -0.9,-2 -2,-2zM11,13H8v3H6v-3H3v-2h3V8h2v3h3v2zM15.5,15c-0.83,0 -1.5,-0.67 -1.5,-1.5s0.67,-1.5 1.5,-1.5 1.5,0.67 1.5,1.5 -0.67,1.5 -1.5,1.5zM19.5,12c-0.83,0 -1.5,-0.67 -1.5,-1.5S18.67,9 19.5,9s1.5,0.67 1.5,1.5 -0.67,1.5 -1.5,1.5z")
    val Tele = v("tele", "M21,3H3c-1.1,0 -2,0.9 -2,2v12c0,1.1 0.9,2 2,2h5v2h8v-2h5c1.1,0 1.99,-0.9 1.99,-2L23,5c0,-1.1 -0.9,-2 -2,-2zM21,17H3V5h18v12z")
    val Plus = v("plus", "M4,8h4L8,4L4,4v4zM10,20h4v-4h-4v4zM4,20h4v-4L4,16v4zM4,14h4v-4L4,10v4zM10,14h4v-4h-4v4zM16,4v4h4L20,4h-4zM10,8h4L14,4h-4v4zM16,14h4v-4h-4v4zM16,20h4v-4h-4v4z")
    val Dossier = v("dossier", "M10,4H4c-1.1,0 -1.99,0.9 -1.99,2L2,18c0,1.1 0.9,2 2,2h16c1.1,0 2,-0.9 2,-2V8c0,-1.1 -0.9,-2 -2,-2h-8l-2,-2z")
    val Fichier = v("fichier", "M14,2H6c-1.1,0 -1.99,0.9 -1.99,2L4,20c0,1.1 0.89,2 1.99,2H18c1.1,0 2,-0.9 2,-2V8l-6,-6zM13,9V3.5L18.5,9H13z")
    val Photo = v("photo", "M21,19V5c0,-1.1 -0.9,-2 -2,-2H5c-1.1,0 -2,0.9 -2,2v14c0,1.1 0.9,2 2,2h14c1.1,0 2,-0.9 2,-2zM8.5,13.5l2.5,3.01L14.5,12l4.5,6H5l3.5,-4.5z")
    val File = v("file", "M3,13h2v-2H3v2zM3,17h2v-2H3v2zM3,9h2V7H3v2zM7,13h14v-2H7v2zM7,17h14v-2H7v2zM7,7v2h14V7H7z")
    val Reglages = v("reglages", "M19.14,12.94c0.04,-0.3 0.06,-0.61 0.06,-0.94 0,-0.32 -0.02,-0.64 -0.07,-0.94l2.03,-1.58c0.18,-0.14 0.23,-0.41 0.12,-0.61l-1.92,-3.32c-0.12,-0.22 -0.37,-0.29 -0.59,-0.22l-2.39,0.96c-0.5,-0.38 -1.03,-0.7 -1.62,-0.94L14.4,2.81c-0.04,-0.24 -0.24,-0.41 -0.48,-0.41h-3.84c-0.24,0 -0.43,0.17 -0.47,0.41L9.25,5.35C8.66,5.59 8.12,5.92 7.63,6.29L5.24,5.33c-0.22,-0.08 -0.47,0 -0.59,0.22L2.74,8.87C2.62,9.08 2.66,9.34 2.86,9.48l2.03,1.58C4.84,11.36 4.8,11.69 4.8,12s0.02,0.64 0.07,0.94l-2.03,1.58c-0.18,0.14 -0.23,0.41 -0.12,0.61l1.92,3.32c0.12,0.22 0.37,0.29 0.59,0.22l2.39,-0.96c0.5,0.38 1.03,0.7 1.62,0.94l0.36,2.54c0.05,0.24 0.24,0.41 0.48,0.41h3.84c0.24,0 0.44,-0.17 0.47,-0.41l0.36,-2.54c0.59,-0.24 1.13,-0.56 1.62,-0.94l2.39,0.96c0.22,0.08 0.47,0 0.59,-0.22l1.92,-3.32c0.12,-0.22 0.07,-0.47 -0.12,-0.61L19.14,12.94zM12,15.6c-1.98,0 -3.6,-1.62 -3.6,-3.6s1.62,-3.6 3.6,-3.6 3.6,1.62 3.6,3.6S13.98,15.6 12,15.6z")
    val Carte = v("carte", "M18,2h-8L4.02,8 4,20c0,1.1 0.9,2 2,2h12c1.1,0 2,-0.9 2,-2V4c0,-1.1 -0.9,-2 -2,-2zM12,8h-2V4h2v4zM15,8h-2V4h2v4zM18,8h-2V4h2v4z")
    val Salon = v("salon", "M10,20v-6h4v6h5v-8h3L12,3 2,12h3v8z")
    val Outils = v("outils", "M22.7,19l-9.1,-9.1c0.9,-2.3 0.4,-5 -1.5,-6.9 -2,-2 -5,-2.4 -7.4,-1.3L9,6 6,9 1.6,4.7C0.4,7.1 0.9,10.1 2.9,12.1c1.9,1.9 4.6,2.4 6.9,1.5l9.1,9.1c0.4,0.4 1,0.4 1.4,0l2.3,-2.3c0.5,-0.4 0.5,-1.1 0.1,-1.4z")
    val Chercher = v("chercher", "M15.5,14h-0.79l-0.28,-0.27C15.41,12.59 16,11.11 16,9.5 16,5.91 13.09,3 9.5,3S3,5.91 3,9.5 5.91,16 9.5,16c1.61,0 3.09,-0.59 4.23,-1.57l0.27,0.28v0.79l5,4.99L20.49,19l-4.99,-5zM9.5,14C7.01,14 5,11.99 5,9.5S7.01,5 9.5,5 14,7.01 14,9.5 11.99,14 9.5,14z")
    val Copies = v("copies", "M16,1H4c-1.1,0 -2,0.9 -2,2v14h2V3h12V1zM19,5H8c-1.1,0 -2,0.9 -2,2v14c0,1.1 0.9,2 2,2h11c1.1,0 2,-0.9 2,-2V7c0,-1.1 -0.9,-2 -2,-2zM19,21H8V7h11v14z")
    val Cartes = v("cartes", "M12,2C9,7 4,9 4,14c0,2 2,4 4,4 1,0 2,0 3,-1 0,1 -1,4 -3,5h8c-2,-1 -3,-4 -3,-5 1,1 2,1 3,1 2,0 4,-2 4,-4 0,-5 -5,-7 -8,-12z")
    val Manette = v("manette", "M15,7.5V2H9v5.5l3,3 3,-3zM7.5,9H2v6h5.5l3,-3 -3,-3zM9,16.5V22h6v-5.5l-3,-3 -3,3zM16.5,9l-3,3 3,3H22V9h-5.5z")
    val Globe = v("globe", "M12,2C6.48,2 2,6.48 2,12s4.48,10 10,10 10,-4.48 10,-10S17.52,2 12,2zM11,19.93c-3.95,-0.49 -7,-3.85 -7,-7.93 0,-0.62 0.08,-1.21 0.21,-1.79L9,15v1c0,1.1 0.9,2 2,2v1.93zM17.9,17.39c-0.26,-0.81 -1,-1.39 -1.9,-1.39h-1v-3c0,-0.55 -0.45,-1 -1,-1L8,12v-2h2c0.55,0 1,-0.45 1,-1L11,7h2c1.1,0 2,-0.9 2,-2v-0.41c2.93,1.19 5,4.06 5,7.41 0,2.08 -0.8,3.97 -2.1,5.39z")
    val Maj = v("maj", "M19,9h-4V3H9v6H5l7,7 7,-7zM5,18v2h14v-2H5z")
    val Retour = v("retour", "M20,11H7.83l5.59,-5.59L12,4l-8,8 8,8 1.41,-1.41L7.83,13H20v-2z")
    val Fermer = v("fermer", "M19,6.41L17.59,5 12,10.59 6.41,5 5,6.41 10.59,12 5,17.59 6.41,19 12,13.41 17.59,19 19,17.59 13.41,12z")
    val Ajouter = v("ajouter", "M19,13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z")
    val Envoyer = v("envoyer", "M9,16h6v-6h4l-7,-7 -7,7h4zM5,18h14v2H5z")
    val Actualiser = v("actualiser", "M17.65,6.35C16.2,4.9 14.21,4 12,4c-4.42,0 -7.99,3.58 -7.99,8s3.57,8 7.99,8c3.73,0 6.84,-2.55 7.73,-6h-2.08c-0.82,2.33 -3.04,4 -5.65,4 -3.31,0 -6,-2.69 -6,-6s2.69,-6 6,-6c1.66,0 3.14,0.69 4.22,1.78L13,11h7V4l-2.35,2.35z")
    val Etoile = v("etoile", "M12,17.27L18.18,21l-1.64,-7.03L22,9.24l-7.19,-0.61L12,2 9.19,8.63 2,9.24l5.46,4.73L5.82,21z")
    val EtoileVide = v("etoilevide", "M22,9.24l-7.19,-0.62L12,2 9.19,8.63 2,9.24l5.46,4.73L5.82,21 12,17.27 18.18,21l-1.63,-7.03L22,9.24zM12,15.4l-3.76,2.27 1,-4.28 -3.32,-2.88 4.38,-0.38L12,6.1l1.71,4.04 4.38,0.38 -3.32,2.88 1,4.28L12,15.4z")
    val Volume = v("volume", "M3,9v6h4l5,5V4L7,9H3zM16.5,12c0,-1.77 -1.02,-3.29 -2.5,-4.03v8.05c1.48,-0.73 2.5,-2.25 2.5,-4.02zM14,3.23v2.06c2.89,0.86 5,3.54 5,6.71s-2.11,5.85 -5,6.71v2.06c4.01,-0.91 7,-4.49 7,-8.77s-2.99,-7.86 -7,-8.77z")
    val Clavier = v("clavier", "M20,5H4c-1.1,0 -1.99,0.9 -1.99,2L2,17c0,1.1 0.9,2 2,2h16c1.1,0 2,-0.9 2,-2V7c0,-1.1 -0.9,-2 -2,-2zM11,8h2v2h-2V8zM11,11h2v2h-2v-2zM8,8h2v2H8V8zM8,11h2v2H8v-2zM7,13H5v-2h2v2zM7,10H5V8h2v2zM16,17H8v-2h8v2zM16,13h-2v-2h2v2zM16,10h-2V8h2v2zM19,13h-2v-2h2v2zM19,10h-2V8h2v2z")
    val Micro = v("micro", "M12,14c1.66,0 2.99,-1.34 2.99,-3L15,5c0,-1.66 -1.34,-3 -3,-3S9,3.34 9,5v6c0,1.66 1.34,3 3,3zM17.3,11c0,3 -2.54,5.1 -5.3,5.1S6.7,14 6.7,11H5c0,3.41 2.72,6.23 6,6.72V21h2v-3.28c3.28,-0.48 6,-3.3 6,-6.72h-1.7z")
    val PleinEcran = v("pleinecran", "M7,14H5v5h5v-2H7v-3zM5,10h2V7h3V5H5v5zM17,17h-3v2h5v-5h-2v3zM14,5v2h3v3h2V5h-5z")
    val Doigt = v("doigt", "M9,11.24V7.5C9,6.12 10.12,5 11.5,5S14,6.12 14,7.5v3.74c1.21,-0.81 2,-2.18 2,-3.74C16,5.01 13.99,3 11.5,3S7,5.01 7,7.5c0,1.56 0.79,2.93 2,3.74zM18.84,15.87l-4.54,-2.26c-0.17,-0.07 -0.35,-0.11 -0.54,-0.11H13v-6c0,-0.83 -0.67,-1.5 -1.5,-1.5S10,6.67 10,7.5v10.74l-3.43,-0.72c-0.08,-0.01 -0.15,-0.03 -0.24,-0.03 -0.31,0 -0.59,0.13 -0.79,0.33l-0.79,0.8 4.94,4.94c0.27,0.27 0.65,0.44 1.06,0.44h6.79c0.75,0 1.33,-0.55 1.44,-1.28l0.75,-5.27c0.01,-0.07 0.02,-0.14 0.02,-0.2 0,-0.62 -0.38,-1.16 -0.91,-1.38z")
    val Partager = v("partager", "M18,16.08c-0.76,0 -1.44,0.3 -1.96,0.77L8.91,12.7c0.05,-0.23 0.09,-0.46 0.09,-0.7s-0.04,-0.47 -0.09,-0.7l7.05,-4.11c0.54,0.5 1.25,0.81 2.04,0.81 1.66,0 3,-1.34 3,-3s-1.34,-3 -3,-3 -3,1.34 -3,3c0,0.24 0.04,0.47 0.09,0.7L8.04,9.81C7.5,9.31 6.79,9 6,9c-1.66,0 -3,1.34 -3,3s1.34,3 3,3c0.79,0 1.5,-0.31 2.04,-0.81l7.12,4.16c-0.05,0.21 -0.08,0.43 -0.08,0.65 0,1.61 1.31,2.92 2.92,2.92 1.61,0 2.92,-1.31 2.92,-2.92s-1.31,-2.92 -2.92,-2.92z")
    val Wifi = v("wifi", "M1,9l2,2c4.97,-4.97 13.03,-4.97 18,0l2,-2C16.93,2.93 7.08,2.93 1,9zM9,17l3,3 3,-3c-1.65,-1.66 -4.34,-1.66 -6,0zM5,13l2,2c2.76,-2.76 7.24,-2.76 10,0l2,-2C15.14,9.14 8.87,9.14 5,13z")
    val Haut = v("haut", "M7.41,15.41L12,10.83l4.59,4.58L18,14l-6,-6 -6,6z")
    val Bas = v("bas", "M7.41,8.59L12,13.17l4.59,-4.58L18,10l-6,6 -6,-6 1.41,-1.41z")
    val Gauche = v("gauche", "M15.41,16.59L10.83,12l4.58,-4.59L14,6l-6,6 6,6 1.41,-1.41z")
    val Droite = v("droite", "M8.59,16.59L13.17,12 8.59,7.41 10,6l6,6 -6,6 -1.41,-1.41z")
    val Sablier = v("sablier", "M6,2v6h0.01L6,8.01 10,12l-4,4 0.01,0.01H6V22h12v-5.99h-0.01L18,16l-4,-4 4,-3.99 -0.01,-0.01H18V2H6z")
    val Trophee = v("trophee", "M19,5h-2V3H7v2H5c-1.1,0 -2,0.9 -2,2v1c0,2.55 1.92,4.63 4.39,4.94 0.63,1.5 1.98,2.63 3.61,2.96V19H7v2h10v-2h-4v-3.1c1.63,-0.33 2.98,-1.46 3.61,-2.96C19.08,12.63 21,10.55 21,8V7c0,-1.1 -0.9,-2 -2,-2zM5,8V7h2v3.82C5.84,10.4 5,9.3 5,8zM19,8c0,1.3 -0.84,2.4 -2,2.82V7h2v1z")
}
