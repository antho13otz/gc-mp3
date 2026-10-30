package fr.gcstation.telecommande;

// L'ecran principal de l'appli : MainActivity (appli d'essai) ou StationActivity (nouvelle appli, Compose).
// Les ecrans partages (Salon, Recherche, Plus...) passent par ici plutot que par MainActivity.

import android.content.Context;
import android.content.Intent;

public interface Host {
    void select(int k);                                      // (ecran : numeros de MainActivity, T_FILES...)
    void switchTo(String ip);                                // (Salon : piloter une autre console)
    void openGame(org.json.JSONArray x);                     // (fiche d'un jeu)
    void fullPad(boolean on);                                // (manette plein ecran)

    /** l'ecran principal de l'appli installee (notification, widget, raccourci d'un jeu) */
    static Intent home(Context c) {
        Intent i = c.getPackageManager().getLaunchIntentForPackage(c.getPackageName());
        if (i == null) i = new Intent(c, MainActivity.class);
        i.setPackage(null);
        i.removeCategory(Intent.CATEGORY_LAUNCHER);
        i.setAction(null);
        return i;
    }
}
