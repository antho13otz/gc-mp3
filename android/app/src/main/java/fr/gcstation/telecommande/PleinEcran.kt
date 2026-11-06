package fr.gcstation.telecommande

// build 407 : reglage commun des fenetres plein ecran (vue 3D, ecran de la GBA). La fenetre de dialogue prend tout l'ecran, a
// sa vraie taille en pixels, barres d'Android cachees (elles reviennent un instant en glissant depuis le bord), et passe sous la
// camera du telephone (encoche). Reglage refait a chaque rotation : avant, en largeur, il restait une bande a droite et en bas
// ou l'on voyait l'appli derriere.

import android.os.Build
import android.view.Gravity
import android.view.ViewGroup
import android.view.Window
import android.view.WindowManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.window.DialogWindowProvider
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.input.pointer.pointerInput

/** a appeler au debut du contenu d'un Dialog plein ecran */
@Composable
fun FenetrePleinEcran() {
    val vue = LocalView.current
    val conf = LocalConfiguration.current
    DisposableEffect(conf.orientation, conf.screenWidthDp, conf.screenHeightDp) {
        (vue.parent as? DialogWindowProvider)?.window?.let { pleinEcran(it) }
        vue.post { (vue.parent as? DialogWindowProvider)?.window?.let { pleinEcran(it) } }   // (une fois la rotation finie)
        onDispose { }
    }
}

@Suppress("DEPRECATION")
private fun tailleEcran(w: Window): Pair<Int, Int> =
    if (Build.VERSION.SDK_INT >= 30) w.windowManager.maximumWindowMetrics.bounds.let { it.width() to it.height() }
    else android.util.DisplayMetrics().also { w.windowManager.defaultDisplay.getRealMetrics(it) }.let { it.widthPixels to it.heightPixels }

fun pleinEcran(w: Window) {
    val (lw, lh) = tailleEcran(w)
    w.addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS or WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN)
    w.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
    w.attributes = w.attributes.also {
        it.width = if (lw > 0) lw else ViewGroup.LayoutParams.MATCH_PARENT
        it.height = if (lh > 0) lh else ViewGroup.LayoutParams.MATCH_PARENT
        it.x = 0; it.y = 0; it.gravity = Gravity.TOP or Gravity.START
        if (Build.VERSION.SDK_INT >= 28) it.layoutInDisplayCutoutMode = if (Build.VERSION.SDK_INT >= 30) WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                                                       else WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        if (Build.VERSION.SDK_INT >= 30) { it.fitInsetsTypes = 0; it.fitInsetsSides = 0; it.isFitInsetsIgnoringVisibility = true }
    }
    w.setLayout(if (lw > 0) lw else ViewGroup.LayoutParams.MATCH_PARENT, if (lh > 0) lh else ViewGroup.LayoutParams.MATCH_PARENT)
    w.decorView.setPadding(0, 0, 0, 0)
    WindowCompat.setDecorFitsSystemWindows(w, false)
    val c = WindowCompat.getInsetsController(w, w.decorView)
    c.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    c.hide(WindowInsetsCompat.Type.systemBars())
}

// build 411 : le plein ecran pour de bon. Une fenetre de dialogue reste limitee par certains telephones a la zone de l'appli
// (en largeur : une bande a droite et en bas, meme apres le build 407). Le plein ecran est maintenant une couche dessinee par
// l'activite elle-meme, par-dessus toute l'appli (HotePleinEcranAfficher, a la racine), barres d'Android cachees, sous l'encoche.
object HotePleinEcran {
    var contenu by androidx.compose.runtime.mutableStateOf<(@Composable () -> Unit)?>(null)
    var fermer: (() -> Unit)? = null
}

/** a la place d'un Dialog plein ecran : le contenu est affiche par-dessus toute l'appli tant que ce composable est la */
@Composable
fun PleinEcranCouche(fermer: () -> Unit, contenu: @Composable () -> Unit) {
    val c = androidx.compose.runtime.rememberUpdatedState(contenu)
    val f = androidx.compose.runtime.rememberUpdatedState(fermer)
    DisposableEffect(Unit) {
        HotePleinEcran.contenu = { c.value() }; HotePleinEcran.fermer = { f.value() }
        onDispose { HotePleinEcran.contenu = null; HotePleinEcran.fermer = null }
    }
}

/** a la racine de l'activite, apres l'appli : la couche plein ecran (si elle est demandee) */
@Composable
fun HotePleinEcranAfficher() {
    val c = HotePleinEcran.contenu ?: return
    val act = androidx.compose.ui.platform.LocalContext.current as? android.app.Activity
    DisposableEffect(act) {
        val w = act?.window
        if (w != null) {
            if (Build.VERSION.SDK_INT >= 28) w.attributes = w.attributes.also {
                it.layoutInDisplayCutoutMode = if (Build.VERSION.SDK_INT >= 30) WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                                               else WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            }
            val ic = WindowCompat.getInsetsController(w, w.decorView)
            ic.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            ic.hide(WindowInsetsCompat.Type.systemBars())
        }
        onDispose { w?.let { WindowCompat.getInsetsController(it, it.decorView).show(WindowInsetsCompat.Type.systemBars()) } }
    }
    androidx.activity.compose.BackHandler { HotePleinEcran.fermer?.invoke() }
    androidx.compose.foundation.layout.Box(androidx.compose.ui.Modifier.fillMaxSize().background(androidx.compose.ui.graphics.Color.Black)
        .pointerInput(Unit) { awaitPointerEventScope { while (true) awaitPointerEvent() } }) { c() }
}
