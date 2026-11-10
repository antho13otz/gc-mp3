package fr.gcstation.telecommande

// Appli build 457, VERSION PUBLIQUE : la meme porte que src/prive/.../Prive.kt.
// Build 486 (demande du proprietaire, 8 novembre 2026) : la vue 3D complete (packs Wind Waker, Mario Kart, Rayman 3 que la
// personne installe elle-meme) et le compagnon de jeu sont aussi dans la version publique. Restent seulement dans la
// version privee : l'extraction d'un .iso (IsoTel.kt) et l'export / partage des packs installes (ExportPacks.kt), ainsi
// que la manette, les bongos et le clavier en jeu et le Salon. Aucun pack n'est fourni avec l'appli.

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

object Prive {
    const val ACTIF = false
    fun manetteOuvrir(a: android.app.Activity, masque: Int) { }
    fun manetteReglages(a: android.app.Activity) { }
    fun bongosOuvrir(a: android.app.Activity, masque: Int) { }
    fun clavierOuvrir(a: android.app.Activity, masque: Int) { }
    fun ctxCompagnon(): android.content.Context? = Compagnon.ctx
    fun jeuEnCours(): String = Compagnon.jeuReel.ifEmpty { Compagnon.jeu }
    fun epingler(c: android.content.Context, jeu: String, adr: Long, taille: Int, nom: String, max: Long) =
        Epingles.ajouter(c, jeu, Epingles.E(adr, taille, nom, max))
    fun extraireVersUri(a: android.app.Activity, id: String, chemin: String, u: android.net.Uri) { }   // (pas d'extraction)
    fun extraireLancer(a: android.app.Activity, id: String, chemin: String) { }
    fun carteMarine(c: android.content.Context) { }                     // (pas de lecture du disque du jeu)
    fun discord(a: android.app.Activity) { }                            // (build 498 : presence Discord, appli privee seulement)
    @Composable fun accueilConsoles(): Boolean = false                  // (build 501 : choix GameCube / Switch, appli privee seulement)
    fun changerConsole() { }
    @Composable fun lancementPulse() { }                                // (build 502 : appli privee seulement)
    @Composable fun LogoPulse(m: Modifier) = LogoPulseAnime(m)
    @Composable fun NotifTeleReglages(act: StationActivity) { }        // (build 522 : appli privee seulement, pour l'instant)        // (build 513 : le vrai logo anime, comme l'appli privee)
    fun posDirectWW(): FloatArray? = JeuWW.posDirect()
    fun curseurTT(): FloatArray? = JeuWW.curseurTT()
    fun diagObjetsWW(): String = JeuWW.diagObjets()

    /** fiche d'un jeu : les lieux en 3D (packs installes par la personne), sans export des packs */
    @Composable fun SectionsJeu(act: StationActivity, id: String) {
        if (id.startsWith("GRH")) { Spacer(Modifier.height(22.dp)); Titre("Rayman 3 : progression et cartes"); Spacer(Modifier.height(8.dp)); SectionRayman3(act) }
        if (id.startsWith("GZL")) { Spacer(Modifier.height(22.dp)); Titre("Wind Waker : les lieux en 3D"); Spacer(Modifier.height(8.dp)); SectionWW(act) }
        if (id.startsWith("GM4")) { Spacer(Modifier.height(22.dp)); Titre("Mario Kart : circuits en 3D"); Spacer(Modifier.height(8.dp)); SectionMKDD(act) }
    }
    @Composable fun Ecran(sous: String): Boolean = when (sous) {
        "Compagnon de jeu" -> { EcranCompagnon(); true }
        "Rayman 3 : progression" -> { EcranRayman3(); true }
        else -> false
    }
    val ECRANS = setOf("Compagnon de jeu", "Rayman 3 : progression")

    fun packsInstalles(c: android.content.Context): List<PackInstalle> = emptyList()   // (pas d'export des packs)
    fun mo(o: Long) = String.format(java.util.Locale.FRANCE, "%.0f Mo", o / 1e6)
    fun exporterPacks(a: android.app.Activity, dossiers: List<String>): Pair<List<android.net.Uri>, List<String>> = emptyList<android.net.Uri>() to emptyList()
    fun partagerPacks(a: android.app.Activity, l: List<android.net.Uri>) { }
    fun installerPack(c: android.content.Context, nom: String, u: android.net.Uri): Int? {
        val k = nom.lowercase()
        return when {
            k.startsWith("windwaker") -> CartesWW.installer(c, u)
            k.startsWith("mariokart") -> CartesMK.installer(c, u)
            k.startsWith("rayman3") -> CartesR3.installer(c, u)
            else -> null
        }
    }
}
