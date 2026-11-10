# Revenir sur GitHub (quand le compte sera rétabli)

Depuis le 6 novembre 2026 (suspension du compte GitHub antho13otz), GC Station est compilé et publié sur
**GitLab** : gitlab.com/GC-Station-group/GC-Station. Les workflows `.github/workflows/` sont gardés **à jour**
à chaque envoi, pour pouvoir revenir sur GitHub sans rien réécrire.

## Ce qui a changé dans les workflows GitHub (build 907)

- **Compilation à la main seulement** (Actions > le workflow > *Run workflow*) : plus de compilation à chaque
  envoi, plus aucune tâche planifiée (clé de l'appli, catalogue, fiches). C'est l'activité automatique trop
  intense qui avait fait suspendre le compte.
- **Numéros de build** : lus dans `ci/numeros.env` (le même fichier que GitLab), plus `github.run_number`.
  Le numéro change dans le même commit que la version publiée.
- **Clé de l'appli** : le secret `ANDROID_DEBUG_KEYSTORE_B64` (la même clé que la variable du projet GitLab,
  copie de secours `debug.keystore` chez le propriétaire), plus le cache de GitHub.

## Le jour du retour

1. Pousser le dépôt sur GitHub (un seul envoi, historique compris, sans forcer).
2. GitHub > le dépôt > *Settings* > *Secrets and variables* > *Actions* > *New repository secret* :
   `ANDROID_DEBUG_KEYSTORE_B64` = le contenu de `cle-signature-appli-base64.txt` (la même clé que GitLab :
   l'appli s'installe par-dessus, que l'APK vienne de GitHub ou de GitLab).
3. *Settings* > *Actions* > *General* > *Workflow permissions* : « Read and write permissions »
   (publication des releases).
4. Lancer à la main : *Compiler GC MP3 Player* (console, publie la release `dernier`), puis si besoin
   *Catalogue d'applis* et *Fiches des jeux* (releases `catalogue` et `fiches`).
5. Sur la console : remettre la recherche des mises à jour sur GitHub. Dans `SD:/gc-mp3/gc-mp3.txt` (ou la page
   des réglages du téléphone), mettre `maj_depot = github:antho13otz/gc-mp3`.
   (Sans le préfixe `github:`, l'ancien réglage `antho13otz/gc-mp3` est lu comme GitLab depuis le build 907.)
6. Décider où publier ensuite : GitHub **ou** GitLab, pas les deux à chaque version (une seule compilation
   par version).

## Le réglage `maj_depot` (console, build 907 et plus)

| Valeur | Mises à jour, catalogue et fiches lus sur |
|---|---|
| `gitlab:gc-station-group/gc-station` (défaut) | GitLab : paquets du projet (`console`, `catalogue`, `fiches`, version `dernier`) |
| `github:antho13otz/gc-mp3` | GitHub : releases `dernier`, `catalogue`, `fiches` |
| `<propriétaire>/<dépôt>` (sans préfixe) | GitHub (sauf `antho13otz/gc-mp3`, lu comme GitLab) |
