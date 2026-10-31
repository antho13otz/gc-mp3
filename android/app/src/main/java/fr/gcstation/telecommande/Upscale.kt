package fr.gcstation.telecommande

// Appli : agrandissement de l'image de la tele (onglet Tele) par la puce graphique du telephone (Android 13 et plus).
// L'image recue (320x240 ou 640x480, en JPEG) est agrandie a la taille de l'ecran par interpolation bicubique
// (Catmull-Rom, 16 points) au lieu du simple lissage, puis rendue plus nette sans halo : renforcement local borne
// par les pixels voisins (pas de surbrillance autour des contours), et lissage leger des blocs du JPEG dans les
// aplats. Calcul en AGSL (le langage des shaders d'Android), a chaque image affichee. Android 12 et moins : lissage
// normal de l'image.

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
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.ShaderBrush
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.layout.ContentScale

object Upscale {
    val possible: Boolean get() = Build.VERSION.SDK_INT >= 33

    const val AGSL = """
        uniform shader image;
        uniform float2 src;
        uniform float2 dst;
        uniform float force;
        float4 tx(float2 c) { return float4(image.eval(clamp(c, float2(0.5), src - float2(0.5)))); }
        float4 cr(float t) {
            float t2 = t * t, t3 = t2 * t;
            return float4(-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2);
        }
        half4 main(float2 p) {
            float2 q = p / dst * src - float2(0.5);
            float2 f = fract(q);
            float2 b = floor(q) + float2(0.5);
            float4 wx = cr(f.x);
            float4 wy = cr(f.y);
            float4 c = float4(0.0);
            float4 mn = float4(1.0);
            float4 mx = float4(0.0);
            for (int j = 0; j < 4; j++) {
                float4 ligne = float4(0.0);
                for (int i = 0; i < 4; i++) {
                    float4 t = tx(b + float2(float(i - 1), float(j - 1)));
                    ligne += t * wx[i];
                    if (i >= 1 && i <= 2 && j >= 1 && j <= 2) { mn = min(mn, t); mx = max(mx, t); }
                }
                c += ligne * wy[j];
            }
            // (lissage leger des blocs du JPEG la ou l'image est unie)
            float4 lin = mix(mix(tx(b), tx(b + float2(1.0, 0.0)), f.x), mix(tx(b + float2(0.0, 1.0)), tx(b + float2(1.0, 1.0)), f.x), f.y);
            float ecart = dot((mx - mn).rgb, float3(0.333));
            c = mix(lin, c, smoothstep(0.02, 0.08, ecart));
            // (plus net, sans halo : renforcement borne par les 4 pixels voisins)
            float4 s = c + (c - lin) * force;
            s = clamp(s, mn - float4(0.02), mx + float4(0.02));
            return half4(clamp(s, 0.0, 1.0));
        }
    """
}

/** l'image de la tele : agrandie par le telephone (upscaling) si demande et possible, sinon lissage normal */
@Composable
fun ImageTele(img: ImageBitmap, upscaling: Boolean, modifier: Modifier = Modifier) {
    if (upscaling && Upscale.possible) {
        val ok = remember { try { RuntimeShader(Upscale.AGSL); true } catch (e: Exception) { false } }   // (shader refuse : lissage normal)
        if (ok) { ImageAgrandie(img.asAndroidBitmap(), modifier); return }
    }
    Image(img, null, modifier, contentScale = ContentScale.Fit, filterQuality = FilterQuality.High)
}

@RequiresApi(33)
@Composable
private fun ImageAgrandie(bmp: Bitmap, modifier: Modifier) {
    val rs = remember { RuntimeShader(Upscale.AGSL) }
    Box(modifier.fillMaxSize().drawBehind {                  // (a chaque nouvelle image : nouveau dessin)
        val bs = BitmapShader(bmp, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP)
        bs.filterMode = BitmapShader.FILTER_MODE_NEAREST
        rs.setInputShader("image", bs)
        rs.setFloatUniform("src", bmp.width.toFloat(), bmp.height.toFloat())
        rs.setFloatUniform("dst", size.width, size.height)
        rs.setFloatUniform("force", 0.55f)
        drawRect(ShaderBrush(rs))
    })
}
