package fr.gcstation.telecommande

// Appli privee build 522 (console v399) : les notifications du telephone sur la tele. Android donne a ce service les
// notifications qui arrivent (autorisation « Acces aux notifications », a donner une fois dans les reglages d'Android) ;
// celles qui comptent (messages, appels, alarmes, reseaux sociaux, mails) partent a la console : bandeau de Pulse pendant
// un jeu, carte en haut du menu de GC Station sinon (Bandeau.notifier). Reglage : onglet Pulse, « Notifications du
// telephone sur la tele » (coupe par defaut). Rien n'est garde ni envoye ailleurs.

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch

class NotifTele : NotificationListenerService() {
    private var dernier = 0L
    private val vus = HashMap<String, Long>()

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (!actif(this)) return
        if (sbn.packageName == packageName) return
        val n = sbn.notification ?: return
        if (n.flags and Notification.FLAG_GROUP_SUMMARY != 0) return
        if (sbn.isOngoing && n.category != Notification.CATEGORY_CALL) return
        val icone = when (n.category) {
            Notification.CATEGORY_MESSAGE -> Bandeau.MESSAGE
            Notification.CATEGORY_CALL, Notification.CATEGORY_MISSED_CALL -> Bandeau.APPEL
            Notification.CATEGORY_ALARM, Notification.CATEGORY_REMINDER, Notification.CATEGORY_EVENT -> Bandeau.ALARME
            Notification.CATEGORY_SOCIAL -> Bandeau.AMI
            Notification.CATEGORY_EMAIL -> Bandeau.MESSAGE
            else -> return                                    // (le reste : mises a jour, musique, telechargements... : non)
        }
        val ex = n.extras ?: return
        val titre = ex.getCharSequence(Notification.EXTRA_TITLE)?.toString()?.trim().orEmpty()
        val texte = (ex.getCharSequence(Notification.EXTRA_BIG_TEXT) ?: ex.getCharSequence(Notification.EXTRA_TEXT))?.toString()?.trim().orEmpty()
        if (titre.isEmpty() && texte.isEmpty()) return
        // (une meme notification mise a jour plusieurs fois : une seule fois en 20 s ; et pas plus d'une toutes les 3 s)
        val cle = sbn.packageName + "|" + titre + "|" + texte
        val now = System.currentTimeMillis()
        if ((vus[cle] ?: 0L) > now - 20_000L || now - dernier < 3_000L) return
        vus[cle] = now; dernier = now
        if (vus.size > 64) vus.entries.removeIf { it.value < now - 60_000L }
        val appli = try { packageManager.getApplicationLabel(packageManager.getApplicationInfo(sbn.packageName, 0)).toString() } catch (e: Exception) { "Téléphone" }
        val ligne = when {
            icone == Bandeau.APPEL -> if (titre.isNotEmpty()) "Appel : $titre" else texte
            titre.isNotEmpty() && texte.isNotEmpty() -> "$titre : $texte"
            else -> titre.ifEmpty { texte }
        }
        Bandeau.ctx = applicationContext
        Api.pool { Bandeau.notifier(appli.take(30), ligne.replace('\n', ' ').take(60), icone, sbn.packageName) }   // (build 524 : avec son icone)
    }

    companion object {
        fun actif(c: android.content.Context) = c.getSharedPreferences("gcs", 0).getBoolean("notif_tele", false)
        fun actif(c: android.content.Context, on: Boolean) = c.getSharedPreferences("gcs", 0).edit().putBoolean("notif_tele", on).apply()
        /** Android a donne l'acces aux notifications a l'appli */
        fun autorise(c: android.content.Context): Boolean =
            androidx.core.app.NotificationManagerCompat.getEnabledListenerPackages(c).contains(c.packageName)
        fun demander(a: android.app.Activity) = try {
            a.startActivity(android.content.Intent(android.provider.Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
        } catch (e: Exception) { }
    }
}

/** onglet Pulse (appli privee) : envoyer un message sur la tele, et les notifications du telephone sur la tele */
@androidx.compose.runtime.Composable
fun NotifTeleBloc(act: StationActivity) {
    val sc = androidx.compose.runtime.rememberCoroutineScope()
    var msg by androidx.compose.runtime.remember { androidx.compose.runtime.mutableStateOf("") }
    var on by androidx.compose.runtime.remember { androidx.compose.runtime.mutableStateOf(NotifTele.actif(act) && NotifTele.autorise(act)) }
    androidx.compose.foundation.layout.Spacer(androidx.compose.ui.Modifier.height(12.dp))
    androidx.compose.foundation.layout.Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
        androidx.compose.material3.OutlinedTextField(msg, { msg = it.take(60) }, androidx.compose.ui.Modifier.weight(1f),
            placeholder = { androidx.compose.material3.Text("Message pour la télé", color = C.Pale) }, singleLine = true,
            shape = androidx.compose.foundation.shape.RoundedCornerShape(18.dp))
        androidx.compose.foundation.layout.Spacer(androidx.compose.ui.Modifier.width(8.dp))
        Bouton("Envoyer", hauteur = 48.dp, plein = true) {
            val t = msg.trim()
            if (t.isNotEmpty()) sc.launch {
                val err = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { Bandeau.notifier("Message", t, Bandeau.MESSAGE) }
                Lang.toast(act, err ?: "Envoyé sur la télé", android.widget.Toast.LENGTH_SHORT).show()
                if (err == null) msg = ""
            }
        }
    }
    androidx.compose.foundation.layout.Row(androidx.compose.ui.Modifier.fillMaxWidth().padding(top = 8.dp).appui {
            if (!on && !NotifTele.autorise(act)) { NotifTele.actif(act, true); NotifTele.demander(act) } else { on = !on; NotifTele.actif(act, on) }
        }, verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
        Doux("Notifications du téléphone sur la télé", androidx.compose.ui.Modifier.weight(1f), 13, C.Texte)
        androidx.compose.material3.Switch(on, { v ->
                if (v && !NotifTele.autorise(act)) { NotifTele.actif(act, true); NotifTele.demander(act) } else { on = v; NotifTele.actif(act, v) }
            }, colors = androidx.compose.material3.SwitchDefaults.colors(checkedTrackColor = C.Rose, checkedThumbColor = androidx.compose.ui.graphics.Color.White))
    }
    // (retour des reglages d'Android : l'acces vient d'etre donne)
    androidx.compose.runtime.LaunchedEffect(Unit) {
        while (true) { kotlinx.coroutines.delay(1500); val a = NotifTele.actif(act) && NotifTele.autorise(act); if (a != on) on = a }
    }
    Doux("Messages, appels, alarmes : en bas de l'image pendant un jeu (Pulse), en haut de l'écran dans GC Station (console v399).", taille = 11, couleur = C.Pale)
}
