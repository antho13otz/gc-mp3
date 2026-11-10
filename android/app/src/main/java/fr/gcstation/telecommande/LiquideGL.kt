package fr.gcstation.telecommande

// Build 514 (appli privee, style « Verre liquide ») : un vrai verre, dans l'esprit d'iOS 26 et de MagicOS.
// Le fond de l'appli (FondAurore, Theme.kt) est calcule, pas une image : chaque surface de verre le recalcule chez elle,
// vu a travers une lentille. Au bord, le fond est tire vers l'exterieur (le verre se courbe), les trois couleurs n'y sont
// pas tirees de la meme facon (petite irisation), le centre est un peu grossi. Le reflet du bord suit l'inclinaison du
// telephone (capteur de gravite). Une onde peut traverser le verre (barre d'onglets : a chaque toucher).
// Android 13 et plus (AGSL) ; sinon, ou sur l'ecran du lecteur (fond = pochette), le verre de la 510 (verreLiquide).

import androidx.compose.foundation.shape.CornerBasedShape
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.asComposeRenderEffect
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInRoot

object Liquide {
    /** le temps et la position des taches de couleur du fond (ecrits par FondAurore) */
    var temps by mutableFloatStateOf(0f)
    var k by mutableFloatStateOf(0.5f)
    var ecranW = 1080f
    var ecranH = 2400f
    /** inclinaison du telephone (-1 a 1), lissee */
    var inclX by mutableFloatStateOf(0f)
    var inclY by mutableFloatStateOf(0f)
    private val ok: Boolean by lazy {
        if (android.os.Build.VERSION.SDK_INT >= 33) try { android.graphics.RuntimeShader(AGSL_VERRE); true } catch (e: Exception) { false } else false
    }
    /** le vrai verre est possible ici (style choisi, Android 13+) ; build 521 : aussi sur l'ecran du lecteur (pochette) */
    val actif: Boolean get() = C.liquide && ok
    /** build 521 : la pochette floue du lecteur (FondPochette) : toute petite (16 px de large : agrandie avec lissage, elle
     *  donne le meme flou tres doux), sa force (0 : pas de pochette) et sa derive lente (0 a 1) */
    var pochette: android.graphics.Bitmap? = null
        set(v) { field = v; pochShader = null }
    var pochA by mutableFloatStateOf(0f)
    var pochK by mutableFloatStateOf(0.5f)
    private var pochShader: android.graphics.BitmapShader? = null
    private val vide by lazy { android.graphics.Bitmap.createBitmap(1, 1, android.graphics.Bitmap.Config.ARGB_8888) }
    fun pochShader(): android.graphics.BitmapShader {
        pochShader?.let { return it }
        val s = android.graphics.BitmapShader(pochette ?: vide, android.graphics.Shader.TileMode.CLAMP, android.graphics.Shader.TileMode.CLAMP)
        if (android.os.Build.VERSION.SDK_INT >= 33) s.filterMode = android.graphics.BitmapShader.FILTER_MODE_LINEAR
        pochShader = s
        return s
    }
    fun nouveau(): android.graphics.RuntimeShader? =
        if (ok && android.os.Build.VERSION.SDK_INT >= 33) try { android.graphics.RuntimeShader(AGSL_VERRE) } catch (e: Exception) { null } else null

    /** capteur de gravite : le reflet suit l'inclinaison (appele par FondAurore tant que le style est choisi) */
    fun capteur(c: android.content.Context): (() -> Unit) {
        val sm = c.getSystemService(android.content.Context.SENSOR_SERVICE) as? android.hardware.SensorManager ?: return {}
        val s = sm.getDefaultSensor(android.hardware.Sensor.TYPE_GRAVITY) ?: sm.getDefaultSensor(android.hardware.Sensor.TYPE_ACCELEROMETER) ?: return {}
        val l = object : android.hardware.SensorEventListener {
            override fun onSensorChanged(e: android.hardware.SensorEvent) {
                val x = (-e.values[0] / 9.81f).coerceIn(-1f, 1f)
                val y = (e.values[1] / 9.81f - 0.75f).coerceIn(-1f, 1f)   // (tenu droit devant soi : vers 0)
                inclX += (x - inclX) * 0.12f; inclY += (y - inclY) * 0.12f
            }
            override fun onAccuracyChanged(s: android.hardware.Sensor?, a: Int) { }
        }
        sm.registerListener(l, s, android.hardware.SensorManager.SENSOR_DELAY_UI)
        return { sm.unregisterListener(l) }
    }

    /** une onde qui traverse un verre (x, y : dans le verre, en pixels) */
    class Onde { var x = 0f; var y = 0f; var t0 by mutableLongStateOf(0L)
        fun lancer(px: Float, py: Float) { x = px; y = py; t0 = System.nanoTime() } }
}

/**
 * Le vrai verre liquide. teinteA / teinteB : couleur du verre (de gauche a droite ; alpha 0 : verre clair), force : courbure
 * du bord en pixels, onde : vague a faire passer. A la place de verreLiquide quand Liquide.actif.
 */
fun Modifier.verreGL(shape: Shape, teinteA: Color = Color.Transparent, teinteB: Color = teinteA, force: Float = 20f,
                     onde: Liquide.Onde? = null): Modifier = composed {
    val sh = remember { Liquide.nouveau() }
    val pos = remember { FloatArray(2) }
    if (sh == null || android.os.Build.VERSION.SDK_INT < 33) return@composed this.verreLiquide(shape)
    this.onGloballyPositioned { val o = it.positionInRoot(); pos[0] = o.x; pos[1] = o.y }
        .drawBehind {
            val r = (shape as? CornerBasedShape)?.topStart?.toPx(size, this) ?: 0f
            val age = onde?.let { if (it.t0 == 0L) -1f else (System.nanoTime() - it.t0) / 1e9f } ?: -1f
            sh.setFloatUniform("ecran", Liquide.ecranW, Liquide.ecranH)
            sh.setFloatUniform("orig", pos[0], pos[1])
            sh.setFloatUniform("dim", size.width, size.height)
            sh.setFloatUniform("rayon", r)
            sh.setFloatUniform("t", Liquide.temps)
            sh.setFloatUniform("k", Liquide.k)
            sh.setFloatUniform("incl", Liquide.inclX, Liquide.inclY)
            sh.setFloatUniform("force", force * density / 2.75f)
            sh.setFloatUniform("onde", onde?.x ?: 0f, onde?.y ?: 0f, age)
            sh.setFloatUniform("echelle", density / 2.75f)
            sh.setFloatUniform("eclat", C.p.eclat); sh.setFloatUniform("taches", C.p.taches)
            fun col(n: String, c: Color) = sh.setColorUniform(n, android.graphics.Color.valueOf(c.red, c.green, c.blue, c.alpha).toArgb())
            col("fond", C.Fond); col("fond2", C.Fond2); col("violet", C.Violet); col("rose", C.Rose)
            col("c1", C.Rose); col("c2", C.Violet); col("c3", C.Cyan)
            col("teinteA", teinteA); col("teinteB", teinteB)
            val pb = Liquide.pochette
            sh.setInputShader("poch", Liquide.pochShader())
            sh.setFloatUniform("pochT", (pb?.width ?: 1).toFloat(), (pb?.height ?: 1).toFloat())
            sh.setFloatUniform("pochA", if (pb == null) 0f else Liquide.pochA)
            sh.setFloatUniform("pochK", Liquide.pochK)
            drawRect(androidx.compose.ui.graphics.ShaderBrush(sh))
        }
        .clip(shape)
}

/** le verre : la meme aurore que FondAurore (degrade, deux taches, trois rubans), lue a travers une lentille */
private const val AGSL_VERRE = """
uniform float2 ecran;
uniform float2 orig;
uniform float2 dim;
uniform float rayon;
uniform float t;
uniform float k;
uniform float2 incl;
uniform float force;
uniform float3 onde;
uniform float echelle;
uniform float eclat;
uniform float taches;
layout(color) uniform half4 fond;
layout(color) uniform half4 fond2;
layout(color) uniform half4 violet;
layout(color) uniform half4 rose;
layout(color) uniform half4 c1;
layout(color) uniform half4 c2;
layout(color) uniform half4 c3;
layout(color) uniform half4 teinteA;
layout(color) uniform half4 teinteB;
uniform shader poch;
uniform float2 pochT;
uniform float pochA;
uniform float pochK;

float sdr(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
half4 ruban(float x, float y, float base, float amp, float ep, float vit, float ph, float a) {
    float yc = base + amp * sin(x * 0.012 + t * vit + ph) + 0.4 * amp * sin(x * 0.031 - t * vit * 1.7);
    float d = y - yc;
    float kk = d < 0.0 ? clamp(1.0 + d / ep, 0.0, 1.0) : clamp(1.0 - d / (ep * 0.35), 0.0, 1.0);
    float f = clamp((x + 10.0) / 660.0, 0.0, 1.0);
    half4 c = f < 0.5 ? mix(c1, c2, half(f * 2.0)) : mix(c2, c3, half((f - 0.5) * 2.0));
    half al = half(a * eclat * kk * kk * (3.0 - 2.0 * kk));
    return half4(c.rgb * al, al);
}
half3 aurore(float2 sp) {
    sp = clamp(sp, float2(0.0), ecran);
    half3 c = mix(fond2.rgb, fond.rgb, half(sp.y / ecran.y));
    float2 cv = float2(ecran.x * (0.15 + 0.5 * k), ecran.y * 0.12);
    c = mix(c, violet.rgb, half(0.28 * taches * clamp(1.0 - length(sp - cv) / (ecran.x * 0.9), 0.0, 1.0)));
    float2 cr = float2(ecran.x * (0.95 - 0.5 * k), ecran.y * 0.75);
    c = mix(c, rose.rgb, half(0.16 * taches * clamp(1.0 - length(sp - cr) / (ecran.x * 0.8), 0.0, 1.0)));
    float x = sp.x / ecran.x * 640.0;
    float y = sp.y / ecran.y * 480.0;
    half4 o = ruban(x, y, 118.0, 16.0, 70.0, 0.35, 0.0, 0.30);
    half4 b = ruban(x, y, 430.0, 22.0, 110.0, 0.25, 2.1, 0.26);
    o = b + o * (1.0 - b.a);
    half4 r3 = ruban(x, y, 460.0, 12.0, 60.0, 0.42, 4.0, 0.18);
    o = r3 + o * (1.0 - r3.a);
    half3 res = o.rgb + c * (1.0 - o.a);
    if (pochA > 0.001) {                                       // (build 521 : la pochette floue du lecteur, comme FondPochette)
        float2 C = ecran * 0.5;
        float S = 1.25 + 0.12 * pochK;
        float2 T = float2(ecran.x * 0.06 * (pochK - 0.5), ecran.y * 0.04 * (0.5 - pochK));
        float s0 = max(ecran.x / pochT.x, ecran.y / pochT.y);
        float2 ip = ((sp - C - T) / S) / s0 + pochT * 0.5;
        res = mix(res, poch.eval(ip).rgb, half(0.38 * pochA));
        float v = sp.y / ecran.y;                              // (voile : 0,35 en haut, rien au milieu, 0,45 en bas)
        float va = v < 0.5 ? 0.35 * (1.0 - v * 2.0) : 0.45 * (v * 2.0 - 1.0);
        res = mix(res, fond.rgb, half(va * pochA));
    }
    return res;
}
half4 main(float2 p) {
    float2 c = dim * 0.5;
    float r = min(rayon, min(c.x, c.y));
    float d = sdr(p - c, c, r);
    if (d > 1.0) return half4(0.0);
    float2 n = float2(sdr(p - c + float2(1.0, 0.0), c, r) - sdr(p - c - float2(1.0, 0.0), c, r),
                      sdr(p - c + float2(0.0, 1.0), c, r) - sdr(p - c - float2(0.0, 1.0), c, r));
    n = n / max(length(n), 0.0001);
    float bord = min(force * 2.8 + 8.0 * echelle, min(c.x, c.y));
    float z = clamp(1.0 + d / bord, 0.0, 1.0);                // (0 dedans, 1 sur le bord)
    float l = z * z * z * force;
    float2 dv = float2(0.0);
    if (onde.z >= 0.0 && onde.z < 1.1) {                      // (onde : un anneau qui s'eloigne et s'efface)
        float2 v = p - onde.xy;
        float dd = length(v);
        float front = onde.z * 900.0 * echelle;
        float lg = 26.0 * echelle;
        float w = exp(-((dd - front) * (dd - front)) / (lg * lg)) * (1.0 - onde.z / 1.1);
        dv = (dd > 0.001 ? v / dd : float2(0.0)) * w * 10.0 * echelle;
    }
    // (build 520 : houle : le fond vu a travers le verre bouge doucement, plus au bord qu'au centre)
    dv += echelle * (0.9 + 1.6 * z) * float2(sin(p.y * 0.045 / echelle + t * 1.3), sin(p.x * 0.04 / echelle - t * 1.1));
    float2 sp = orig + c + (p - c) * 0.93 + n * l + dv;       // (centre un peu grossi, bord tire vers l'exterieur)
    // (build 520 : irisation seulement la ou le bord tire le fond : ailleurs un seul calcul de l'aurore au lieu de trois)
    half3 col;
    if (l > 0.6) {
        float2 irise = n * l * 0.12;
        col = half3(aurore(sp + irise).r, aurore(sp).g, aurore(sp - irise).b);
    } else col = aurore(sp);
    col = col * 1.06 + 0.02;                                  // (build 515 : moins eclairci)
    half4 te = mix(teinteA, teinteB, half(clamp(p.x / dim.x, 0.0, 1.0)));
    col = mix(col, te.rgb, te.a);
    col += half(0.07 * (1.0 - p.y / dim.y));                  // (voile clair en haut)
    float2 L = normalize(float2(-0.45 + incl.x * 0.9, -0.9 + incl.y * 0.9));
    float spec = pow(max(dot(n, L), 0.0), 2.5);
    float spec2 = 0.45 * pow(max(dot(n, -L), 0.0), 3.0);
    float rim = 1.0 - smoothstep(0.0, 1.6 * echelle, abs(d + 1.1 * echelle));
    col += half((0.16 + 0.84 * spec + spec2) * rim * 0.9);   // (le fil de lumiere du bord)
    col += half(z * z * (0.05 + 0.16 * spec - 0.10 * spec2)); // (le bord epais du verre : clair du cote de la lumiere, sombre en face)
    col += half(dv.x * 0.004 + dv.y * 0.004);                 // (l'onde accroche un peu la lumiere)
    // (build 520 : un reflet large balaie lentement le verre, en biais, toutes les ~16 s)
    float bal = (orig.x + p.x + (orig.y + p.y) * 0.55) / ecran.x - (fract(t * 0.0625) * 3.2 - 1.0);
    col += half(0.055 * exp(-bal * bal * 18.0));
    float a = clamp(0.5 - d, 0.0, 1.0);
    return half4(min(col, half3(1.0)) * half(a), half(a));
}
"""

/** build 520 : passage d'un onglet a l'autre (style liquide) : l'ecran qui arrive ondule comme une surface d'eau qui se calme */
private const val AGSL_ONDULE = """
uniform shader contenu;
uniform float q;
uniform float t;
uniform float echelle;
half4 main(float2 p) {
    float a = q * q;
    float dx = a * 22.0 * echelle * sin(p.y * 0.010 / echelle + t * 7.0);
    float dy = a * 7.0 * echelle * sin(p.x * 0.018 / echelle - t * 5.0);
    return contenu.eval(p + float2(dx, dy));
}
"""
object Ondule {
    private val sh: android.graphics.RuntimeShader? by lazy {
        if (android.os.Build.VERSION.SDK_INT >= 33) try { android.graphics.RuntimeShader(AGSL_ONDULE) } catch (e: Exception) { null } else null
    }
    /** l'effet pour q (1 : plein, 0 : rien) ; flou (px) en plus pour l'ecran qui part */
    fun effet(q: Float, t: Float, echelle: Float, flou: Float): androidx.compose.ui.graphics.RenderEffect? {
        if (android.os.Build.VERSION.SDK_INT < 33) return null
        val s = sh ?: return null
        if (q < 0.01f && flou < 0.5f) return null
        s.setFloatUniform("q", q); s.setFloatUniform("t", t); s.setFloatUniform("echelle", echelle)
        val o = android.graphics.RenderEffect.createRuntimeShaderEffect(s, "contenu")
        val e = if (flou >= 0.5f) android.graphics.RenderEffect.createChainEffect(o,
                    android.graphics.RenderEffect.createBlurEffect(flou, flou, android.graphics.Shader.TileMode.CLAMP)) else o
        return e.asComposeRenderEffect()
    }
}
