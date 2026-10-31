package fr.gcstation.telecommande

// Appli : le clavier du telephone, partout. Des qu'un champ de texte s'ouvre sur la tele (recherche, adresse web, nom,
// clavier a l'ecran de GC Station : "kb" dans /api/state), un panneau monte en bas de l'appli, quel que soit l'onglet, et
// le clavier du telephone s'ouvre tout seul. Le texte part en direct sur la tele (/api/type), OK valide, le micro dicte.
// Le panneau disparait quand la tele ferme son clavier ; × le cache jusqu'au prochain champ ; « Ouvrir tout seul »
// (garde dans les preferences) : sans, le panneau monte mais le clavier du telephone attend qu'on touche le champ.

import android.content.Intent
import android.speech.RecognizerIntent
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay

@Composable
fun ClavierPartout() {
    val ctx = LocalContext.current
    val prefs = remember { ctx.getSharedPreferences("gcs", 0) }
    var auto by remember { mutableStateOf(prefs.getBoolean("clavier_auto", true)) }
    val s = St.etat
    val kb = St.connecte && (s?.optInt("kb", 0) ?: 0) != 0
    var cache by remember { mutableStateOf(false) }          // (× : cache jusqu'au prochain champ)
    var ouvertures by remember { mutableIntStateOf(0) }      // (chaque nouveau champ : le clavier se rouvre)
    var txt by remember { mutableStateOf("") }
    var tape by remember { mutableStateOf(false) }
    LaunchedEffect(kb) { if (kb) { cache = false; ouvertures++ } else tape = false }
    LaunchedEffect(kb, s?.optString("kbt", "")) { if (kb && !tape) txt = s?.optString("kbt", "") ?: "" }
    LaunchedEffect(txt, tape) { if (tape) { delay(250); lire("/api/type?go=0&t=" + Api.enc(txt), 3000) } }
    val dictee = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { r ->
        val l = r.data?.getStringArrayListExtra(RecognizerIntent.EXTRA_RESULTS)
        if (!l.isNullOrEmpty()) { txt = if (txt.isEmpty()) l[0] else txt + (if (txt.endsWith(" ")) "" else " ") + l[0]; tape = true }
    }
    val valider = { tape = false; val t = txt; Api.pool { Api.get("/api/type?go=1&t=" + Api.enc(t), 3000) } }
    Box(Modifier.fillMaxSize().imePadding(), contentAlignment = Alignment.BottomCenter) {
        AnimatedVisibility(kb && !cache, enter = slideInVertically { it }, exit = slideOutVertically { it }) {
            val focus = remember { FocusRequester() }
            val clavier = LocalSoftwareKeyboardController.current
            LaunchedEffect(ouvertures, auto) { if (auto) { delay(150); try { focus.requestFocus(); clavier?.show() } catch (e: Exception) { } } }
            Column(Modifier.fillMaxWidth().navigationBarsPadding().padding(10.dp)
                .verre(RoundedCornerShape(22.dp), C.Cyan.copy(alpha = 0.7f), Color(0xF01A0E30)).padding(14.dp)) {
                val h = s?.optString("kbh", "") ?: ""
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Ic.Clavier, null, tint = C.Cyan, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text(h.ifEmpty { "Champ de texte ouvert sur la télé" }, Modifier.weight(1f), color = C.Cyan, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Puce("Ouvrir tout seul", auto) { auto = !auto; prefs.edit().putBoolean("clavier_auto", auto).apply() }
                    Spacer(Modifier.width(6.dp))
                    Box(Modifier.size(34.dp).verre(CircleShape).appui { cache = true; clavier?.hide() }, contentAlignment = Alignment.Center) {
                        Icon(Ic.Fermer, null, tint = C.Texte, modifier = Modifier.size(16.dp))
                    }
                }
                Spacer(Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(txt, { txt = it; tape = true }, Modifier.weight(1f).focusRequester(focus), singleLine = true,
                        placeholder = { Text("Écris ici : le texte part sur la télé", color = C.Pale) },
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done), keyboardActions = KeyboardActions(onDone = { valider() }),
                        colors = OutlinedTextFieldDefaults.colors(focusedTextColor = C.Texte, unfocusedTextColor = C.Texte, focusedBorderColor = C.Cyan, unfocusedBorderColor = C.Bord, cursorColor = C.Cyan),
                        shape = RoundedCornerShape(14.dp))
                    Spacer(Modifier.width(8.dp))
                    Box(Modifier.size(46.dp).verre(CircleShape).appui {
                        val it = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH)
                            .putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
                            .putExtra(RecognizerIntent.EXTRA_LANGUAGE, java.util.Locale.getDefault().toLanguageTag())
                            .putExtra(RecognizerIntent.EXTRA_PROMPT, "Parle : le texte s'écrit sur la télé")
                        try { dictee.launch(it) } catch (e: Exception) { Toast.makeText(ctx, "Pas de reconnaissance vocale sur ce téléphone", Toast.LENGTH_LONG).show() }
                    }, contentAlignment = Alignment.Center) { Icon(Ic.Micro, null, tint = C.Texte) }
                    Spacer(Modifier.width(8.dp))
                    Bouton("OK", plein = true) { valider() }
                }
            }
        }
    }
}
