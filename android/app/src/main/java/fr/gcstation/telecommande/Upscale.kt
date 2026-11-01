package fr.gcstation.telecommande

// Appli : l'image de la tele agrandie par la puce graphique du telephone (Android 13 et plus), en AGSL.
// 1. Agrandissement qui suit les contours : 16 pixels autour du point, poids d'un noyau de Lanczos (approche
//    polynomiale), etire le long des contours et resserre en travers (selon la direction locale de la luminance) :
//    bords nets sans escaliers, aplats doux ; pas de halo (borne par les 4 pixels voisins) ; blocs du JPEG adoucis
//    dans les aplats ; puis netteté (renforcement local, borne lui aussi).
// 2. Images intermediaires (120 Hz) : entre l'image precedente A et la derniere B, a l'instant t (0 a 1), chaque
//    point est pris dans A recule de t x mouvement et dans B avance de (1 - t) x mouvement, puis melange
//    (mouvement estime par blocs : Mouvement.kt). L'image affichee a donc une image de retard (~50 ms).
// Android 12 et moins : lissage normal de l'image.

import android.graphics.Bitmap
import android.graphics.BitmapShader
import android.graphics.RuntimeShader
import android.graphics.Shader
import android.os.Build
import androidx.annotation.RequiresApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ShaderBrush
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale

object Upscale {
    val possible: Boolean get() = Build.VERSION.SDK_INT >= 33

    const val AGSL = """
        uniform shader imgA;
        uniform shader imgB;
        uniform shader champ;
        uniform float2 src;
        uniform float2 dst;
        uniform float2 tch;
        uniform float t;
        uniform float interp;
        uniform float force;
        float4 tx(float2 c, float2 mv) {
            float4 b = float4(imgB.eval(clamp(c + (1.0 - t) * mv, float2(0.5), src - float2(0.5))));
            if (interp < 0.5) return b;
            float4 a = float4(imgA.eval(clamp(c - t * mv, float2(0.5), src - float2(0.5))));
            return mix(a, b, t);
        }
        float lz(float x2) {                                   // (Lanczos 2, approche polynomiale ; x2 = distance au carre)
            if (x2 >= 4.0) return 0.0;
            float a = 0.4 * x2 - 1.0, b = 0.25 * x2 - 1.0;
            return (1.5625 * a * a - 0.5625) * b * b;
        }
        float lum(float4 c) { return dot(c.rgb, float3(0.299, 0.587, 0.114)); }
        half4 main(float2 p) {
            float2 q = p / dst * src;
            float2 mv = float2(0.0);
            if (interp > 0.5) { float4 m = float4(champ.eval(q / src * tch)); mv = (m.rg * 255.0 - float2(128.0)) / 4.0; }
            float2 i0 = floor(q - float2(0.5));
            float2 f = q - float2(0.5) - i0;
            float2 b = i0 + float2(0.5);
            float4 T[16];
            float L[16];
            for (int k = 0; k < 16; k++) {
                T[k] = tx(b + float2(float(k - (k / 4) * 4 - 1), float(k / 4 - 1)), mv);
                L[k] = lum(T[k]);
            }
            // direction locale (gradient sur les 4 pixels du centre et leurs voisins)
            float gx = (L[6] + L[10] - L[5] - L[9]) * 0.75 + (L[7] + L[11] - L[4] - L[8]) * 0.25;
            float gy = (L[9] + L[10] - L[5] - L[6]) * 0.75 + (L[13] + L[14] - L[1] - L[2]) * 0.25;
            float gl = sqrt(gx * gx + gy * gy);
            float2 dir = gl > 1e-4 ? float2(gx, gy) / gl : float2(1.0, 0.0);
            float e = clamp(gl * 6.0, 0.0, 1.0);
            float sa = 1.0 + e * 0.9, sl = 1.0 / (1.0 + e * 0.9);
            float4 acc = float4(0.0);
            float ws = 0.0;
            for (int k = 0; k < 16; k++) {
                float2 o = float2(float(k - (k / 4) * 4 - 1), float(k / 4 - 1)) - f;
                float ac = dot(o, dir) * sa, al = dot(o, float2(-dir.y, dir.x)) * sl;
                float w = lz(ac * ac + al * al);
                acc += T[k] * w; ws += w;
            }
            float4 c = acc / max(ws, 1e-4);
            float4 mn = min(min(T[5], T[6]), min(T[9], T[10]));
            float4 mx = max(max(T[5], T[6]), max(T[9], T[10]));
            c = clamp(c, mn, mx);
            // blocs du JPEG adoucis dans les aplats ; puis netteté bornee
            float4 lin = mix(mix(T[5], T[6], f.x), mix(T[9], T[10], f.x), f.y);
            float ecart = lum(mx) - lum(mn);
            c = mix(lin, c, smoothstep(0.02, 0.07, ecart));
            float4 s = clamp(c + (c - lin) * force, mn - float4(0.015), mx + float4(0.015));
            return half4(clamp(s, 0.0, 1.0));
        }
    """
}

/** ce que l'onglet Tele affiche : l'image precedente (a), la derniere (b), leur mouvement, l'instant t (0 a 1) */
class Vue(val a: Bitmap?, val b: Bitmap, val champ: Bitmap?, val t: Float)

/** l'image de la tele : agrandie par le telephone (upscaling) si demande et possible, sinon lissage normal */
@Composable
fun ImageTele(v: Vue, upscaling: Boolean, modifier: Modifier = Modifier) {
    if (upscaling && Upscale.possible) {
        val ok = remember { try { RuntimeShader(Upscale.AGSL); true } catch (e: Exception) { false } }   // (shader refuse : lissage normal)
        if (ok) { ImageAgrandie(v, modifier); return }
    }
    Image(v.b.asImageBitmap(), null, modifier, contentScale = ContentScale.FillBounds, filterQuality = FilterQuality.High)
}

@RequiresApi(33)
@Composable
private fun ImageAgrandie(v: Vue, modifier: Modifier) {
    val rs = remember { RuntimeShader(Upscale.AGSL) }
    Box(modifier.fillMaxSize().drawBehind {                  // (a chaque image et a chaque instant t : nouveau dessin)
        fun bs(b: Bitmap, lisse: Boolean) = BitmapShader(b, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP).also {
            it.filterMode = if (lisse) BitmapShader.FILTER_MODE_LINEAR else BitmapShader.FILTER_MODE_NEAREST
        }
        val interp = v.a != null && v.champ != null && v.a.width == v.b.width && v.a.height == v.b.height
        val sb = bs(v.b, false)
        rs.setInputShader("imgB", sb)
        rs.setInputShader("imgA", if (interp) bs(v.a!!, false) else sb)
        rs.setInputShader("champ", if (interp) bs(v.champ!!, true) else sb)
        rs.setFloatUniform("src", v.b.width.toFloat(), v.b.height.toFloat())
        // (la place a deja la forme de la tele (4:3 ou 16:9, reglage de la console) : l'image la remplit, comme sur la
        //  tele ; l'image de la console n'a pas forcement ces proportions, ex. 640 x 574 en PAL)
        val dw = size.width; val dh = size.height
        rs.setFloatUniform("dst", dw, dh)
        rs.setFloatUniform("tch", if (interp) v.champ!!.width.toFloat() else 1f, if (interp) v.champ!!.height.toFloat() else 1f)
        rs.setFloatUniform("t", if (interp) v.t else 1f)
        rs.setFloatUniform("interp", if (interp) 1f else 0f)
        rs.setFloatUniform("force", 0.6f)
        translate((size.width - dw) / 2, (size.height - dh) / 2) { drawRect(ShaderBrush(rs), size = androidx.compose.ui.geometry.Size(dw, dh)) }
    })
}
