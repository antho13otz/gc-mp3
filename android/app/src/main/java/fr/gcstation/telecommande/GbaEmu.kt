package fr.gcstation.telecommande

// Appli (build 375) : l'emulateur GBA de l'appli (GBA du telephone). Le programme que le jeu a envoye a la « GBA »
// (TingleTuner.kt : recu par Pulse, dechiffre ici) tourne dans mGBA (licence MPL 2.0, src/main/cpp : pont gba_jni.c).
// Relais avec Pulse (natres.h, paquets "GCGB") :
//  - ce que le jeu ecrit a la GBA (commande 15) arrive ici dans l'ordre : donne a la GBA emulee mot par mot, des qu'elle a
//    lu le precedent ; tout lu -> « JOY_RECV lu » (option 2) pour que Pulse efface le drapeau 0x02 vu par le jeu ;
//  - ce que la GBA ecrit dans JOY_TRANS (drapeau 0x08) part a Pulse, numerote (option 1, numero en bits 16-23) ; quand
//    Pulse dit que le jeu l'a lu (octet 5 de son etat, console v333), la GBA emulee l'apprend (commande 14) ;
//  - les drapeaux 0x30 de JOYSTAT (ecrits par le programme de la GBA) suivent a chaque paquet (option 8).

import android.graphics.Bitmap
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.util.concurrent.ConcurrentLinkedQueue

/** le pont natif (gba_jni.c) ; un seul fil s'en sert (celui de l'emulateur) */
object GbaNatif {
    val pret: Boolean = try { System.loadLibrary("gbapont"); true } catch (e: Throwable) { false }
    @JvmStatic external fun demarrer(prog: ByteArray): Boolean
    @JvmStatic external fun arreter()
    @JvmStatic external fun image(touches: Int, pixels: IntArray, son: ShortArray, mots: IntArray, nmots: Int, res: IntArray): Int
    @JvmStatic external fun joy(cmd: Int, data: ByteArray): Int
    @JvmStatic external fun regs(out: IntArray)
}

object GbaEmu {
    const val W = 240
    const val H = 160
    // build 382 : simples variables (plus d'etat Compose) : cet objet peut naitre dans le fil reseau pendant que l'ecran se
    // compose (plantage « Reading a state that was created after the snapshot was taken ») ; l'ecran les relit a chaque image
    @Volatile var marche = false
        private set
    @Volatile var images = 0                              // (une de plus a chaque image de la GBA)
        private set
    @Volatile var message = ""
        private set
    val ecran: Bitmap = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888)

    @Volatile var touches = 0
    fun appui(bit: Int, bas: Boolean) { touches = if (bas) touches or (1 shl bit) else touches and (1 shl bit).inv() }

    // relais (lu par le fil reseau de GbaTel)
    val entrees = ConcurrentLinkedQueue<Long>()           // (mots ecrits par le jeu, a donner a la GBA)
    @Volatile var transVal = 0L                           // (JOY_TRANS de la GBA, en attente d'etre lu par le jeu)
    @Volatile var transId = 0                             // (son numero, 1-255 ; 0 : rien en attente)
    @Volatile var luPret = false                          // (la GBA a lu tout ce que le jeu a ecrit : option 2 a envoyer)
    @Volatile var drapeaux = 0                            // (JOYSTAT & 0x30 de la GBA)
    @Volatile var tidLuPulse = 0                          // (dernier numero lu par le jeu, dit par Pulse)
    @Volatile var nRecus = 0L; @Volatile var nEnvoyes = 0L
    // build 381 : ce que le debut du programme ecrit dans JOY_TRANS juste apres son lancement (la preuve demandee par le
    // demarrage du jeu, puis le code du jeu) : Pulse l'a deja donne au jeu a sa place (console v346) ; ces mots-la sont
    // lus tout de suite ici, une fois chacun, sans passer par Pulse
    @Volatile var avaler: Set<Long> = emptySet()
    // build 383 : code du jeu (mot 0xAC) : le debut du programme l'ecrit, attend que le jeu le lise, puis attend que le jeu le
    // lui renvoie ; les mots du jeu sont retenus jusqu'a ce qu'il l'ait ecrit (sinon il attend pour toujours)
    @Volatile var codeJeu = -1L
    // build 387 (console v347) : remises a zero du jeu pendant le relais, dites par Pulse : donnees a la GBA emulee (son
    // programme y repond par son identifiant) ; resetsVus : celles qu'elle a deja traitees (dit a Pulse, qui ignore ce qui
    // est plus ancien)
    @Volatile var resetsPulse = 0
    @Volatile var resetsVus = 0
    @Volatile var regsVus = ""                             // (build 383 : registres du cable vus par la GBA, pour le rapport)
    @Volatile var retenus = false
    @Volatile var nAvales = 0

    @Volatile private var fil: Thread? = null
    @Volatile private var stop = false

    fun demarrer(programme: ByteArray) {
        arreter()
        if (!GbaNatif.pret) { message = "Émulateur GBA absent de cette version de l'appli"; return }
        entrees.clear(); resetsVus = 0; transId = 0; transVal = 0; luPret = false; drapeaux = 0; tidLuPulse = 0; nRecus = 0; nEnvoyes = 0; nAvales = 0
        stop = false
        fil = Thread({ boucle(programme) }, "gba-emu").apply { isDaemon = true; start() }
    }
    fun arreter() {
        stop = true
        fil?.let { try { it.join(500) } catch (e: Exception) { } }
        fil = null
        marche = false
    }

    private fun boucle(programme: ByteArray) {
        if (!GbaNatif.demarrer(programme)) { message = "Le programme ne démarre pas dans l'émulateur"; return }
        marche = true; message = "La GBA tourne"
        val px = IntArray(W * H)
        val son = ShortArray(4096)
        val res = IntArray(2)
        val r = IntArray(4)
        val mots = IntArray(512)
        val d = ByteArray(8)
        var son0: AudioTrack? = null
        try {
            son0 = AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(AudioFormat.Builder().setSampleRate(32768).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                .setBufferSizeInBytes(32768).build().apply { play() }
        } catch (e: Exception) { son0 = null }
        var attenteLu = false                              // (un JOY_TRANS de la GBA attend que le jeu le lise)
        var numero = 0                                     // (dernier numero donne a un JOY_TRANS, 1-255)
        var donnesSansLu = false                           // (des mots du jeu donnes, pas encore tous lus par la GBA)
        val aAvaler = avaler.toMutableSet()
        var retenir = codeJeu >= 0 && codeJeu in aAvaler          // (build 383 : mots du jeu retenus jusqu'au code ecrit)
        var nImages = 0
        var resetsFaits = 0
        var apresReset = false                             // (build 388 : le 1er JOY_TRANS apres une remise a zero : l'identifiant)
        var idGba = -1L                                    // (l'identifiant de la GBA, vu a la 1re remise a zero)
        var codeLu = false                                 // (build 384 : le code ecrit par la GBA a ete lu ici)
        var joycnt = 0
        var prochain = System.nanoTime()
        try {
            while (!stop) {
                // la GBA apprend que le jeu a lu son JOY_TRANS
                if (attenteLu && tidLuPulse == numero) { GbaNatif.joy(0x14, d); attenteLu = false; transId = 0; nEnvoyes++ }
                val rp = resetsPulse
                if (rp != resetsFaits) {                   // (build 387 : remise a zero par le jeu : a la GBA ; son JOY_TRANS d'avant est perime)
                    GbaNatif.joy(0xFF, d); resetsFaits = rp; attenteLu = false; transId = 0; apresReset = true
                }
                // une image, en donnant les mots du jeu des que la GBA est prete
                var n = 0
                if (retenir && ++nImages > 180) retenir = false    // (3 s sans le code : on donne quand meme)
                // build 384 : apres le code, la GBA attend JOYCNT = 4 exactement (« lu par le jeu ») et l'efface ; le mot du jeu
                // seulement ensuite (sinon JOYCNT = 6 : elle attendait pour toujours)
                if (retenir && codeLu && joycnt and 4 == 0) retenir = false
                if (!retenir) for (w in entrees) { if (n >= mots.size) break; mots[n++] = w.toInt() }
                GbaNatif.image(touches, px, son, mots, n, res)
                repeat(res[1]) { entrees.poll(); nRecus++ }
                if (res[1] > 0) donnesSansLu = true
                GbaNatif.regs(r)
                val js = r[0]
                joycnt = r[3]
                regsVus = "JOYSTAT %02X, JOYCNT %02X, JOY_TRANS %08X, JOY_RECV %08X".format(r[0], r[3], r[1], r[2]); retenus = retenir
                if (donnesSansLu && entrees.isEmpty() && js and 0x02 == 0) { donnesSansLu = false; luPret = true }
                val tv = r[1].toLong() and 0xFFFFFFFFL
                // build 388 : l'identifiant donne apres une remise a zero : des la 2e fois, Pulse l'a deja donne au jeu (console
                // v347) et le jeu l'a lu aussitot : lu ici tout de suite (sinon la GBA refuse la suite : « La connexion a echoue »)
                // build 389 : un JOY_TRANS de la GBA ne compte qu'avec ses drapeaux 0x20 / 0x30 (son programme ecrit 0 en se
                // remettant en place, sans drapeau : ni donne au jeu ni lu ici, sinon il y voit une erreur et recommence) ;
                // l'identifiant : celui ecrit avec 0x20 (par sa reponse a la remise a zero)
                val drap = js and 0x30
                val idDejaDonne = !attenteLu && js and 0x08 != 0 && apresReset && drap == 0x20 && idGba >= 0 && tv == idGba
                if (!attenteLu && js and 0x08 != 0 && apresReset && drap == 0x20) { apresReset = false; if (idGba < 0) idGba = tv }
                if (idDejaDonne) {
                    GbaNatif.joy(0x14, d); nAvales++
                } else if (!attenteLu && js and 0x08 != 0 && aAvaler.remove(tv)) {   // (build 381 : deja donne au jeu par Pulse)
                    GbaNatif.joy(0x14, d); nAvales++
                    if (tv == codeJeu) { codeLu = true; GbaNatif.regs(r); joycnt = r[3] }   // (build 386 : JOYCNT relu apres la lecture : 4)
                } else if (!attenteLu && js and 0x08 != 0 && drap != 0) {      // (la GBA a ecrit JOY_TRANS : pour le jeu)
                    transVal = tv
                    numero = numero % 255 + 1; transId = numero
                    attenteLu = true
                }
                drapeaux = js and 0x30
                resetsVus = resetsFaits
                synchronized(ecran) { ecran.setPixels(px, 0, W, 0, 0, W, H) }
                images++
                if (res[0] > 0) try { son0?.write(son, 0, res[0] * 2, AudioTrack.WRITE_NON_BLOCKING) } catch (e: Exception) { }
                prochain += 16_742_706L                    // (59,73 images par seconde, comme la GBA)
                val att = prochain - System.nanoTime()
                if (att > 0) Thread.sleep(att / 1_000_000, (att % 1_000_000).toInt()) else if (att < -100_000_000L) prochain = System.nanoTime()
            }
        } catch (e: Exception) {
            message = "Émulateur arrêté : ${e.message}"
        } finally {
            try { son0?.stop(); son0?.release() } catch (e: Exception) { }
            GbaNatif.arreter()
            marche = false
        }
    }

    /** option et JOY_TRANS a mettre dans notre prochain paquet "GCGB" (fil reseau) */
    fun options(): Pair<Long, Long> {
        var o = 0L
        val id = transId
        if (id > 0) o = o or 1L or (id.toLong() shl 16)
        if (luPret) { o = o or 2L; luPret = false }
        if (marche) o = o or 8L or (drapeaux.toLong() shl 8) or 0x10L or ((resetsVus.toLong() and 0xFF) shl 24)
        return Pair(o, transVal)
    }
}

/** l'ecran de la GBA et ses boutons (toucher : appuyer, lever : relacher) */
@Composable
fun EcranGba() {
    var n by androidx.compose.runtime.remember { mutableIntStateOf(0) }
    androidx.compose.runtime.LaunchedEffect(Unit) {       // (build 382 : redessine a chaque image de l'ecran du telephone)
        while (true) { androidx.compose.runtime.withFrameNanos { }; if (GbaEmu.images != n) n = GbaEmu.images }
    }
    Column(verticalArrangement = Arrangement.spacedBy(10.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Box(Modifier.fillMaxWidth().aspectRatio(1.5f).clip(RoundedCornerShape(8.dp)).background(Color.Black)) {
            val bmp = synchronized(GbaEmu.ecran) { GbaEmu.ecran.copy(Bitmap.Config.ARGB_8888, false) }
            Image(bmp.asImageBitmap(), contentDescription = "Écran de la GBA ($n)", modifier = Modifier.fillMaxWidth().aspectRatio(1.5f),
                  contentScale = ContentScale.Fit, filterQuality = FilterQuality.None)
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            ToucheGba("L", 9, Modifier.width(90.dp)); ToucheGba("R", 8, Modifier.width(90.dp))
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                ToucheGba("▲", 6)
                Row { ToucheGba("◀", 5); Spacer(Modifier.size(52.dp)); ToucheGba("▶", 4) }
                ToucheGba("▼", 7)
            }
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) { ToucheGba("B", 1, Modifier.size(56.dp), rond = true); ToucheGba("A", 0, Modifier.size(56.dp), rond = true) }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) { ToucheGba("SELECT", 2, Modifier.width(76.dp)); ToucheGba("START", 3, Modifier.width(76.dp)) }
            }
        }
    }
}

@Composable
private fun ToucheGba(nom: String, bit: Int, modifier: Modifier = Modifier.size(52.dp), rond: Boolean = false) {
    var bas by androidx.compose.runtime.remember { mutableStateOf(false) }
    Box(modifier.clip(if (rond) CircleShape else RoundedCornerShape(10.dp)).background(if (bas) C.Rose else C.Verre)
            .pointerInput(bit) {
                detectTapGestures(onPress = {
                    bas = true; GbaEmu.appui(bit, true)
                    try { tryAwaitRelease() } finally { bas = false; GbaEmu.appui(bit, false) }
                })
            }, contentAlignment = Alignment.Center) {
        Text(nom, color = C.Texte, fontSize = if (nom.length > 2) 11.sp else 18.sp)
    }
}
