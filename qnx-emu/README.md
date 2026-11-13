# qnx-emu — système réutilisable d'émulation QNX 8.0

Un **invité QNX 8.0 réel** (SDP 8.0) sous QEMU, exposé sur le web : écran VGA
en direct, console série, injection de touches. Chaque projet QNX du portfolio
(Réplique, Tamis, Glane, Éclat, Veille) construit **son** image invitée (ses
binaires, ses outils) et obtient **la même** vitrine, plus une page sur le site
du dossier QNX.

Le paquet est **autonome** : il ne dépend ni de `~/replique-src`, ni d'aucun
dépôt de projet. Démonstration complète vérifiée le 12/11/2026 avec
`exemple/` (compilation → IFS → boot → vitrine).

## Schéma

```
projet (Tamis, Glane, Éclat, Veille…)
   │  sources .c + image/buildfile
   ▼
build-image.sh  ──  qcc → mkifs → mkxfs → diskimage
   │                        (mkxfs + diskimage : uniquement pour la trace,
   ▼                         car l'IFS se boote par -kernel, pas le disque)
 ifs.bin
   │
   ▼
guest-qemu.sh  ──  QEMU --cpu max, RNG virtio, -kernel ifs.bin,
   │               -vga std, -serial file:, -monitor unix:
   ▼
 invité QNX 8.0  (console VGA + console série)
   │
   ├─► /screen.png  (capture VGA via le moniteur QEMU)
   └─► /console, /key?k=…  (journal série + sendkey)
   ▼
web.py  ──  vitrine HTTP (port QNX_EMS_PORT)
   ▼
tailscale funnel --bg --https=<port> http://127.0.0.1:<port>
   ▼
src/<projet>-demo.html  ──►  tools/build_site.py  ──►  docs/
```

## Fichiers du paquet

| Fichier | Rôle |
|---|---|
| `README.md` | ce document (schéma + pièges) |
| `ADOPTER.md` | guide d'adoption pas-à-pas pour un autre projet |
| `build-image.sh` | chaîne générique : qcc → mkifs → mkxfs → diskimage |
| `guest-qemu.sh` | lancement QEMU générique (la ligne qui marche est en commentaire ; `--serie-socket` pour une console pilotable) |
| `ser-console.py` | pilote l'invité par la console série (socket) : `pidin`, un scénario, figer une trace |
| `web.py` | vitrine HTTP, **généralisée** (tout passe par l'environnement) |
| `systemd/replique-ems-qemu.service` | modèle d'unité systemd utilisateur (invité) |
| `systemd/replique-ems-web.service` | modèle d'unité systemd utilisateur (vitrine) |
| `exemple/buildfile` | squelette d'IFS QNX 8.0 commenté, prêt à copier |
| `exemple/hello.c` | binaire témoin minimal (valide la chaîne de bout en bout) |

## Démarrage rapide (testé)

```sh
export QNX_HOST=$HOME/qnx800/host/linux/x86_64   # SDP 8.0 (r630)
export QNX_TARGET=$HOME/qnx800/target/qnx
# 1. construire l'image d'exemple
./build-image.sh --projet demo --buildfile exemple/buildfile \
    --sources "hello:exemple/hello.c" --sortie ~/demo-img
# 2. lancer l'invité
./guest-qemu.sh --ifs ~/demo-img/ifs.bin --serie ~/demo-img/serial.log \
    --moniteur ~/demo-img/mon.sock --qemu $HOME/qnx71-vm/qprefix/usr/bin/qemu-system-x86_64 \
    --prefixe $HOME/qnx71-vm/qprefix --detacher
# 3. la vitrine
QNX_EMS_SERIE=~/demo-img/serial.log QNX_EMS_MON=~/demo-img/mon.sock \
QNX_EMS_PPM=~/demo-img/web-vga.ppm QNX_EMS_PORT=8091 QNX_EMS_TITRE="Démo QNX 8.0" \
    python3 web.py
```

## Piloter l'invité — le canal série (socket)

Par défaut `-serial file:<log>` n'écrit que la sortie de l'invité. Avec
`--serie-socket <sock>` (ou `QNX_EMS_SERIE_SOCK`), QEMU expose la console
série sur un **socket UNIX** : le journal reste écrit dans `--serie`
(`logfile=`), et `ser-console.py` peut en plus **écrire** dans l'invité —
lancer `pidin`, un scénario, figer une trace — **sans reconstruire l'image** ni
toucher l'écran VGA.

```sh
./guest-qemu.sh --ifs ~/demo-img/ifs.bin --serie ~/demo-img/serial.log \
    --serie-socket ~/demo-img/ser.sock --qemu … --prefixe … --detacher
./ser-console.py --socket ~/demo-img/ser.sock 'pidin'          # lit
./ser-console.py --socket ~/demo-img/ser.sock --file cmd.sh    # joue un script
```

C'est ce canal qui a permis la trace **« wide »** du projet Réplique
(`image/exp-wide.sh`) : `tracelogger -w` lancé **après** le boot, dans un
invité séparé, sans régresser l'affichage VGA de la démo publique.

## Les pièges déjà frappés (messages exacts, vérifiés)

1. **`-cpu qemu64` ne démarre pas.** Le guest reste muet (journal série vide,
   QEMU tourne indéfiniment). Il faut **`--cpu max`** (la ligne qui marche est
   en tête de `guest-qemu.sh`). *Vérifié : `-cpu qemu64` → sérial vide ;
   `--cpu max` → boot.*
2. **L'IFS se lance par `-kernel`, pas par le disque.** Le disque construit par
   `build-image.sh` sert de trace, pas d'amorce : en boot disque, le chargeur
   s'arrête sur **« QNX v1.2d Boot Loader »** (piste close : même l'image SDP
   officielle s'y arrête). Le RNG virtio (`-object rng-random … -device
   virtio-rng-pci`) est dans la ligne qui marche : gardez-le.
3. **Sans le lien de l'interpréteur ELF, tout binaire échoue.** L'interpréteur
   des binaires 64 bits est `/usr/lib/ldqnx-64.so.2`. Sans
   `procmgr_symlink /proc/boot/ldqnx-64.so.2 /usr/lib/ldqnx-64.so.2`, **chaque
   binaire échoue** :
   `Unable to start "devc-con" (83)` → `(83)` = **ELIBACC**
   (« can't access shared library »). *Vérifié (c'est le premier piège dans
   lequel l'exemple est tombé).*
4. **Une IFS sans `io-blk.so`/`fs-qnx6.so` ne monte rien** (aucun système de
   fichiers) ; **sans `libz.so.2` (dans `usr/lib`, PAS `lib/`) l'IFS ne
   démarre pas.** Les deux sont dans `exemple/buildfile`.
5. **QNX 8.0 n'a plus de coreutils : `toybox` est multi-applets.** Il n'a **pas
   d'applet `kill`** : `kill` répond
   `toybox: Unknown command kill (see "toybox --help")` *(vérifié)*. Le terme
   d'un processus, c'est **`slay`** (binaire QNX).
6. **Le shell de démarrage `[+script]` n'a ni pipes ni redirections, et `$!`
   n'y fonctionne pas.** `echo pipe | cat` affiche littéralement `pipe | cat` ;
   `echo x > /tmp/f` affiche `x > /tmp/f` ; `echo $!` affiche `$!` *(vérifié)*.
   Pour un vrai scénario : écrire un script et le lancer par `ksh -c "<script>"`.
7. **Les liens d'applets toybox se placent à la racine de l'IFS**, pas dans
   `/bin` (il n'y a pas de `/bin`). `[type=link] /proc/boot/echo=/proc/boot/toybox`
   — la racine de l'IFS est `/proc/boot` et c'est elle que contient le `PATH`.
8. **Pas de HAM dans le SDP 8.0** : le déclencheur de terminaison est la
   scrutation de `/proc/<pid>` (signal récupéré par `pidin dead`), pas un
   gestionnaire de haute disponibilité.
9. **`/var` est un lien mort sans disque.** `ls /var` →
   `ls: /var: No such file or directory` *(vérifié)*. Écrire dans `/dev/shmem`
   (volatile) ou monter un disque RAM (`devb-ram`) pour un vrai système de
   fichiers.
10. **`mkifs` sort le fichier image en argument positionnel ; `-o` est le
    répertoire des artefacts** : `mkifs -v -o . image.resolved ifs.bin`.
    **`mkxfs -t qnx6fsimg`** pour la partition de boot, **`diskimage`** pour le
    disque. *C'est ce que fait `build-image.sh` — chaîne validée.*

## Ce qui reste à faire pour un projet donné

Tout ce qui dépend du projet, et qui ne peut pas être généré ici :

- écrire **son** `buildfile` (ses binaires, son scénario de démo) depuis
  `exemple/buildfile` ;
- compiler **ses** sources (`--sources "nom:src/….c; autre:src/….c"`) ;
- choisir ses chemins (série, moniteur, PPM) et son **port HTTP** — un port
  DISTINCT par projet (sinon collision de système de fichiers et de funnel) ;
- ouvrir son tunnel `tailscale funnel --bg --https=<port> http://127.0.0.1:<port>`
  avec ce port ;
- ajouter sa page `src/<projet>-demo.html` + une entrée dans la liste
  `STATIQUES` de `tools/build_site.py` (cf. `ADOPTER.md`), puis regénérer `docs/`.