# Hermes — gestion des conflits (Réplique)

Deux scripts, une règle : **Mergify est le seul pilote de merge.** Rien ici ne
fusionne.

## `gate.py` — le garde de fraîcheur (dry-run)

Lancé par `hermes-lite.yml` sur `pull_request`, `check_run: completed` et
`workflow_dispatch`.

- Branche en retard, **pas de conflit** → rebase côté serveur
  (`PUT /pulls/N/update-branch`) ; l'événement `synchronize` rejoue le garde :
  auto-réparation.
- **Conflit réel** → check rouge `hermes-lite/verdict` + étiquette
  `hermes/conflit`. Le garde s'arrête là : il ne résout rien.
- Branche fraîche et checks requis au vert → étiquette `hermes/approuvee`. En
  dry-run (`HERMES_LITE_DRY_RUN=1`, réglage posé par le socle) il étiquette et
  ne merge jamais ; tant que la file Mergify pilote les merges, cette variable
  reste posée — **jamais deux pilotes sur le même dépôt**.

## `resolve_conflict.py` (+ `conflict_lib.py`) — le résolveur de conflit

`resolve_conflict.py` porte la politique, `conflict_lib.py` la plomberie
(processus, API GitHub, invite, appel InferHub) ; les deux en stdlib seule.
Déclenché par `hermes-conflict-resolver.yml` quand l'étiquette `hermes/conflit`
est posée, ou à la main avec l'entrée `pr_number`. Il lit `PR_NUMBER` et
`GITHUB_REPOSITORY`, récupère la PR via `gh api`, puis :

1. **Voie déterministe d'abord.** `git fetch` + `git rebase origin/<base>`. Si
   le rebase passe seul, la branche est poussée en `--force-with-lease`,
   `hermes/conflit` est retirée, `hermes/approuvee` posée et la PR commentée.
   Aucune IA, aucun coût.
2. **Voie InferHub seulement sur un conflit réel.** Chaque fichier en conflit
   est découpé en blocs `<<<<<<<` / `=======` / `>>>>>>>` et envoyé à InferHub
   (compatible OpenAI, `POST https://api.inferhub.dev/v1/chat/completions`,
   modèle `combo/cheap`) avec une invite stricte : résoudre le conflit,
   respecter le **contrat gelé de `CONTRACT.md`** (conteneur binaire, classes
   d'événements, codes de verdict, lignes canoniques, API C et API Python — les
   deux jumeaux doivent rester comparables), ne renvoyer **que** le fichier
   complet, sans balise de code.
3. **Vérification avant toute poussée** : aucune balise de conflit résiduelle,
   `gcc -std=c11 -fsyntax-only -Wall -Wextra -Werror -Icore/include
   -Itarget/declic` sur chaque `.c`/`.h` touché, `python3 -m py_compile` sur
   chaque `.py` touché, puis les gardes du dépôt (`size_guard.py` : fichiers
   < 500 lignes et fonctions < 50 lignes ; `purity_guard.py` : cœur C sans I/O
   ni horloge, hôte Python en stdlib seule). Les zones QNX (`target/`,
   `image/`) ne sont pas compilables sur un runner Linux : elles sont ignorées
   **bruyamment**, et `qcc` reste le seul juge (voir `docs/DESIGN.md`). Sans
   `python3`, les gardes sont ignorées bruyamment — le workflow `ci` les
   rejoue.
4. **Échec propre.** Toute vérification en échec → `git rebase --abort`,
   l'étiquette `hermes/conflit` reste, la PR est commentée avec le fichier et la
   raison, sortie 1. Une poussée cassée n'est jamais tentée. Chaque étape est
   journalisée sur stdout : le journal du runner est la seule preuve.

## Secret `INFERHUB_API_KEY`

Le workflow **échoue immédiatement avec un message explicite** quand le secret
est absent (pas de traceback, rien de poussé). À poser une fois par dépôt :

```bash
gh secret set INFERHUB_API_KEY --repo Tradoooo/replique
# ou : Settings → Secrets and variables → Actions → New repository secret
```

La clé n'est lue que depuis l'environnement, jamais écrite dans un fichier, et
elle est caviardée (`sk-***`) de chaque ligne de journal et de chaque message
d'erreur.

## Repli

Ce n'est pas un gardien de blocage : s'il échoue, rebase à la main —
`git fetch origin`, `git rebase origin/main`, résoudre, `git push
--force-with-lease`. Le garde retire alors l'étiquette.
