# target/declic — Déclic (démon de figeage de trace au crash)

Déclic est le composant **cible** de Réplique : un démon qui **fige le tampon
de trace circulaire au moment exact d'une terminaison de processus**, puis
archive tout ce qui explique le crash — trace `.rkv1`, `pidin`, `slog2`, core
dump — dans un conteneur vérifiable (`RDA1`, empreintes SHA-256).

Ce dossier contient deux couches :

| Couche | Fichiers | Compilée où ? |
|---|---|---|
| **Logique portable** (C11 pur) | `declic.h`, `declic_portable.c`, `sha256_portable.{c,h}` | **PC et cible** (`make test`, `build/declic-test`) |
| **Colle QNX** | `declic_qnx.c` | **cible seulement** (`#ifdef __QNX__`, `qcc`) |

---

## 1. Construire

### Sur PC (logique portable seulement — aucune licence QNX requise)

Depuis la racine du dépôt :

```sh
make test          # vert sans QNX, sans cible, sans licence (exigence S8)
build/declic-test  # test direct de la logique portable
```

Vérification directe (celle du lot) :

```sh
gcc -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic \
    -Icore/include -Itarget/declic \
    core/tests/declic_test.c target/declic/*_portable.c \
    core/src/event.c core/src/verdict.c core/src/kev.c \
    -o /tmp/declic-test && /tmp/declic-test
```

Contrôle croisé du SHA-256 maison (les empreintes attendues dans le test
viennent de ces vecteurs publics) :

```sh
printf ''   | sha256sum   # e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
printf abc  | sha256sum   # ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
```

### Sur la cible (SDP 8.0 requis)

```sh
source ~/qnx800/qnxsdp-env.sh   # à adapter au chemin d'installation du SDP
make target-qnx                 # racine → $(MAKE) -C target/declic -f Makefile.qnx
# ou, directement :
cd target/declic && make -f Makefile.qnx
```

`qcc -V gcc_ntox86_64 -O2 -g -Wall -Wextra -Werror` est appliqué à tout le
code. `-Wpedantic` est appliqué **à la logique portable seule** (les en-têtes
du SDP ne sont pas nécessairement « pedantic »). La colle QNX est compilée en
`-std=gnu11` : en mode strict ISO (`-std=c11`), la bibliothèque masque les
déclarations POSIX (`popen`, `clock_gettime`, `sigaction`, `posix_spawn`,
`waitpid`) — un piège classique de compilation croisée.

Le binaire produit est `target/declic/declic`. L'image invitée
(`image/buildfile`) l'installe avec `mkifs`.

---

## 2. Lancer sur la cible

Déclic s'exécute comme un démon ordinaire. Il crée son dossier de sortie,
démarre `tracelogger` (le tampon circulaire), puis attend une terminaison.

```sh
mkdir -p /var/log/declic
declic --out /var/log/declic --all --poll 500
```

Options :

| Option | Effet |
|---|---|
| `--out DIR` | dossier de sortie des archives (défaut `/var/log/declic`) |
| `--trace PATH` | fichier `.kev` écrit par `tracelogger` (défaut `<out>/trace.kev`) |
| `--all` | surveiller **tous** les pid (sauf exclusions) |
| `--pid N` | surveiller un pid précis (répétable ; désactive `--all`) |
| `--signal N` | ne capturer que ces signaux (répétable ; liste blanche) |
| `--exclude N` | ne jamais capturer ce pid (répétable ; prioritaire) |
| `--poll MS` | période de scrutation du repli `pidin` (défaut 500 ms) |
| `--no-hook` | désactiver l'écoute du canal des crochets |
| `--no-poll` | désactiver le repli `pidin` |
| `--once` | sortir après le premier événement (utile pour la démo S7) |

Exemple de démo S7 (tuer un processus et récupérer l'archive) :

```sh
declic --out /var/log/declic --all --once &
sleep 1
kill -SEGV 1234          # ou lancer un générateur de bogues (image/bugs)
wait
ls -l /var/log/declic    # declic-1234-<ticks>
```

Déclic écoute aussi le canal nommé `/dev/declic.trigger` : les crochets
(`target/hook/dumper_hook.c`, `target/hook/ham_hook.c`) y envoient une
notification **à l'instant de la faute**, ce qui est plus précoce que la
scrutation `pidin`. Voir `target/hook/README.md`.

---

## 3. Contenu d'une archive (conteneur `RDA1`)

**Réplique Déclic Archive v1** — petit-boutien, auto-descriptif, vérifiable.

```
+----------------------------------------------------------+
| en-tête (32 octets)                                      |
|   magic "RDA1" | version=1 | n_sections | table_offset   |
|   entry_size=80 | flags=0 | total_size                   |
+----------------------------------------------------------+
| table des sections (n_sections × 80 octets)              |
|   name[32] | offset u64 | length u64 | sha256[32]        |
+----------------------------------------------------------+
| blocs (dans l'ordre de la table)                         |
|   trace.rkv1 | pidin | slog2 | core | manifest           |
+----------------------------------------------------------+
```

Les **cinq sections standard**, dans cet ordre figé :

| Section | Contenu |
|---|---|
| `trace` | la trace noyau convertie en conteneur `RKV1` (CONTRACT.md §1) |
| `pidin` | sortie de `pidin` (instantané des processus/threads) |
| `slog2` | sortie de `slog2info` (journal système autour du crash) |
| `core` | vidage d'incident (`dumper`) du processus disparu |
| `manifest` | texte, ordre de clés **figé** (voir ci-dessous) |

Le **manifeste** (clé par ligne, terminée par `\n`) :

```
schema=rda1/1
archive=declic-1234-1794468000000000000
timestamp=1794468000000000000
pid=1234
status=0
signal=11
```

L'horodatage et le nom d'archive sont **fournis par la colle QNX** : la
logique portable ne consulte **jamais** d'horloge.

L'empreinte SHA-256 de **chaque section** est stockée dans la table ;
`declic_archive_verify_all()` les recalcule et refuse une archive altérée.
L'empreinte de l'archive entière est imprimée par le démon à l'écriture.

Lire une archive depuis le PC (jumeau Python) :

```sh
python3 -m replique.cli dump <(python3 -c "…")   # à venir (lot hôte)
```

---

## 4. État honnête des essais E2 / E3

Le SDP 8.0 n'est **pas installé** sur la machine de développement : la colle
QNX est écrite d'après la documentation publique, **sans vérification sur
cible**. Chaque appel QNX porte un commentaire `À VÉRIFIER SUR LE SDP (E2/E3)`.

| Essai | Question | État | Conséquence dans ce code |
|---|---|---|---|
| **E2** | `tracelogger` peut-il être **arrêté proprement** pour figer le tampon circulaire, et le tampon contient-il bien l'historique **avant** le crash ? | **non tranché** | `freeze_trace()` envoie `SIGTERM` au `tracelogger` lancé par le démon et attend sa fin. Si SIGTERM ne vide pas proprement le tampon, il faudra une commande dédiée (à écrire après essai). |
| **E3** | La **terminaison** d'un processus est-elle observable en temps réel pour déclencher Déclic ? | **non tranché** | Trois déclencheurs sont prévus : crochet `dumper` (le plus précoce), crochet HAM, et repli par **scrutation de `pidin`** (le seul qui n'utilise aucune API QNX exotique et qui est donc certain de fonctionner). |

Points **explicitement non vérifiés** (à confirmer sur le SDP) :

- la ligne de commande exacte de `tracelogger` (nom du binaire, arguments) ;
- l'arrêt « propre » de `tracelogger` (signal ou commande dédiée) ;
- les options de `pidin` (`-F "%a"` pour ne lister que les pid, base de
  numération des pid affichés — le code utilise `strtoul(…, 0)` pour accepter
  décimal **ou** hexadécimal) ;
- l'outil de lecture du journal (`slog2info`) et le répertoire exact
  (`/var/log/slog2`) ;
- l'emplacement des dumps produits par `dumper` et la façon de demander le
  dump d'un pid précis (`/var/dumper/core.<pid>` est une hypothèse) ;
- l'API de canal nommé (`name_attach` / `MsgReceive`) et la sémantique exacte
  du message reçu ;
- la syntaxe `mkifs` des sections `[image]` et `[+script]` (`buildfile`).

Aucun de ces points ne bloque la logique portable : elle est testée sur PC
(`build/declic-test`, vert sans QNX) et le format `RDA1` est figé par ce
README. La colle QNX sera ajustée après les essais E2/E3 sur QEMU.
