# CircleCI pour GC Station — à faire par Anthony (enregistré le 8 novembre 2026, 17 h 44)

Déjà fait par Claude (commit fb90eda) : `.circleci/config.yml` (compile seulement la branche `compiler`),
publication sur GitLab avec la clé `GITLAB_TOKEN`.

## 1. Créer le compte CircleCI
- https://circleci.com/signup/ → « Sign up with GitLab », autoriser CircleCI.
- Ajouter le projet gc-station-group/gc-station ; configuration déjà dans le dépôt (`.circleci/config.yml`), branche `compiler`.

## 2. Créer une clé d'accès GitLab
- GitLab → projet gc-station → Settings → Access tokens → Add new token.
- Nom : circleci ; rôle : Developer ; cocher seulement « api » ; expiration : dans un an.
- Copier la clé (affichée une seule fois).

## 3. La donner à CircleCI
- CircleCI → projet → Project Settings → Environment Variables → Add.
- Nom : GITLAB_TOKEN ; valeur : la clé.

Ne jamais envoyer la clé à Claude (ni texte ni capture). Ensuite : dire à Claude de lancer une compilation d'essai.
