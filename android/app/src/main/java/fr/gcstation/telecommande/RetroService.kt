package fr.gcstation.telecommande

// Appli build 544 : pendant une partie RetroAchievements, un service au premier plan (notification « partie en cours ») garde
// l'appli en vie quand elle passe derriere (ecran d'accueil, autre appli, ecran eteint). Sans lui, Android (et plus encore
// les surcouches comme HyperOS) gele ou ralentit l'appli en arriere-plan : des images du direct se perdaient (essai du
// 11 novembre). Arrete a la fin de la partie.

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder

class RetroService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ARRET) { arreterPremierPlan(); stopSelf(); return START_NOT_STICKY }
        val jeu = intent?.getStringExtra("jeu").orEmpty()
        val nm = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= 26 && nm.getNotificationChannel(CANAL) == null)
            nm.createNotificationChannel(NotificationChannel(CANAL, "RetroAchievements : partie en cours", NotificationManager.IMPORTANCE_LOW))
        val b = if (Build.VERSION.SDK_INT >= 26) Notification.Builder(this, CANAL) else @Suppress("DEPRECATION") Notification.Builder(this)
        b.setSmallIcon(R.drawable.ic_stat).setOngoing(true)
            .setContentTitle("RetroAchievements : partie en cours")
            .setContentText(if (jeu.isEmpty()) "Les succès sont vérifiés, même appli en arrière-plan" else "$jeu · succès vérifiés, même appli en arrière-plan")
            .setContentIntent(PendingIntent.getActivity(this, 9500, Host.home(this), PendingIntent.FLAG_IMMUTABLE))
        try {
            if (Build.VERSION.SDK_INT >= 29) startForeground(ID, b.build(), ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
            else startForeground(ID, b.build())
        } catch (e: Exception) { RetroSucces.noter("service au premier plan refusé par Android : " + e.javaClass.simpleName); stopSelf() }
        return START_NOT_STICKY
    }

    private fun arreterPremierPlan() {
        try { if (Build.VERSION.SDK_INT >= 24) stopForeground(STOP_FOREGROUND_REMOVE) else @Suppress("DEPRECATION") stopForeground(true) } catch (e: Exception) { }
    }

    companion object {
        private const val CANAL = "retro"
        private const val ID = 9501
        private const val ARRET = "arret"
        /** debut de partie (ou retour de l'appli au premier plan pendant une partie) */
        fun demarrer(c: Context, jeu: String) {
            try {
                val i = Intent(c, RetroService::class.java).putExtra("jeu", jeu)
                if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i) else c.startService(i)
            } catch (e: Exception) { RetroSucces.noter("service au premier plan pas démarré : " + e.javaClass.simpleName) }   // (appli derriere au lancement : reessaye au retour)
        }
        fun arreter(c: Context) {
            try { c.startService(Intent(c, RetroService::class.java).setAction(ARRET)) } catch (e: Exception) { }
        }
    }
}
