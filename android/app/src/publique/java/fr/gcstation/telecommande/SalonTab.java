package fr.gcstation.telecommande;

import android.app.Activity;

// build 457 : version publique : pas de Salon (fonction gardee par le proprietaire) ; cet ecran vide n'est jamais propose
class SalonTab extends Tab {
    SalonTab(Activity a) { super(a); }
    @Override android.view.View build() { return new android.view.View(a); }
}
