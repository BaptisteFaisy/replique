# image/ — image invitée QNX 8.0 pour la démo Déclic

L'image welcoming de la démo : un invité QNX 8.0 (x86_64) qui contient le
démon **Déclic**, les trois générateurs de bogues et les outils d'analyse
(`tracelogger`, `pidin`, `slog2info`, `dumper`), et un shell sur la console
série.

## Fichiers

| Fichier | Rôle |
|---|---|
| `buildfile` | le contenu de l'IFS (mkifs) : noyau, shell, outils, Déclic, bogues |
| `boot_part.build` | la partition de boot (mkxfs, système de fichiers QNX6) |
| `disk.layout` | le partitionnement du disque (diskimage) |
| `build-image.sh` | la chaîne complète : qcc → mkifs → mkxfs → diskimage |

## Construire

```sh
export QNX_HOST=$SDP/host/linux/x86_64
export QNX_TARGET=$SDP/target/qnx
sh image/build-image.sh ~/replique-img
```

Sur le serveur `r630`, le SDP 8.0 est dans `~/qnx800` : le script y travaille
tel quel (vérifié le 12/11/2026).

## Démarrer (sous QEMU)

```sh
qemu-system-x86_64 -cpu qemu64,+ssse3,+sse4.1,+sse4.2,+popcnt \
  -L $QPREFIX/usr/share/qemu -m 2048 -smp 4 \
  -drive file=~/replique-img/replique-disk.img,format=raw,if=ide,index=0 \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2227-:22 -device e1000,netdev=n0 \
  -display none -vga std -serial file:serial.log
```

Le disque doit être en **IDE** : c'est `devb-eide` qui est dans l'image (le
virtio n'est pas piloté par le noyau de l'invité).

## Scénario de démonstration (S7)

Dans l'invité (shell sur `/dev/ser1`) :

```sh
tracelogger /tmp/trace.kev &      # ou : tracelogger -T /tmp
bug-deadlock &                     # ou bug-prio-inversion / bug-race
kill %1                             # le processus s'interrompt -> Déclic se déclenche
```

Déclic observe la terminaison (`/proc/<pid>`), fige la trace (SIGINT à
`tracelogger`), collecte `pidin` + `slog2info` + le core dump, et écrit une
archive `RDA1` horodatée avec son empreinte SHA-256. L'archive se ramène sur
l'hôte et s'analyse avec le cœur portable (voir `docs/PREUVE.md`).

## État vérifié au 12/11/2026 (serveur r630)

| Étape | État |
|---|---|
| `qcc -V gcc_ntox86_64` compile Déclic et les 3 bogues en `-Werror` | **fait** |
| `mkifs` produit l'IFS (3 Mo) | **fait** |
| `mkxfs -t qnx6fsimg` produit la partition de boot | **fait** |
| `diskimage` produit le disque amorçable (268 Mo) | **fait** |
| QEMU démarre le disque : SeaBIOS → « Booting from Hard Disk… » → « QNX v1.2d Boot Loader: qnx_sdp.ifs » | **fait** |
| Le chargeur rend la main à l'IFS | **à finir** — voir ci-dessous |

### Le point qui reste

Le chargeur QNX affiche `QNX v1.2d Boot Loader: qnx_sdp.ifs` puis s'arrête.
**Bisect fait** : l'image *officielle* du SDP (`~/qnx800/images/qemu/qemu/output`)
placée dans *le même* disque s'arrête exactement au même endroit — le blocage est
donc dans la ligne de commande QEMU ou dans le partitionnement (le quickstart
officiel a trois partitions : boot + system + data ; la nôtre n'en a qu'une),
pas dans l'IFS du projet.

Pistes, dans l'ordre :

1. reprendre la ligne de commande du quickstart (`build/qemu_options` du SDP :
   `-vga none -device virtio-vga-gl -display sdl,gl=on`) — l'invité 8.0 attend
   une carte vidéo **virtio-vga-gl**, pas une console VGA standard ;
2. reproduire les trois partitions du quickstart (`boot.part` + `system.part` +
   `data.part`, types 177/178/179) avec `diskimage` ;
3. à défaut : démarrer l'image quickstart officielle (qui est complète) et y
   injecter `declic` et les bogues par SSH — le reste de la démo est identique.

## Faits établis sur le SDP 8.0 (12/11/2026)

Ces constats sont **vérifiés** sur `~/qnx800` du serveur `r630`, et ils valent
pour la conception de Déclic :

- **le HAM n'existe plus** dans le SDP 8.0 (pas d'en-tête `<ham/ham.h>`,
  pas de binaire `ham`) : le déclencheur de terminaison est donc la
  **scrutation de `/proc/<pid>`**, avec `pidin dead` pour récupérer le signal ;
- **pas de service de noms** (pas de `<name.h>`) : aucun canal nommé, donc pas
  de crochet « message de terminaison » ;
- `tracelogger`, `traceprinter`, `dumper`, `pidin`, `slog2info` sont bien livrés
  dans `target/qnx/x86_64` ;
- `procnto-smp-instr` et `startup-x86` sont dans `target/qnx/x86_64/boot/sys` ;
- les modules `io-blk.so` et `fs-qnx6.so` sont dans `target/qnx/x86_64/lib/dll` :
  **sans eux, procnto ne monte rien** et l'IFS s'arrête au démarrage.
