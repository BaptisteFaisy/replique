# ADOPTER.md — adopter le système d'émulation pour un nouveau projet QNX

Objectif : donner à votre projet QNX (Tamis, Glane, Éclat, Veille…) **la même
vitrine** que Réplique — un invité QNX 8.0 qui démarre réellement, avec écran
VGA en direct, console série et injection de touches, plus une page sur le site
du dossier QNX.

Prérequis : le SDP 8.0 installé (`~/qnx800`), le QEMU hôte
(`~/qnx71-vm/qprefix`), et ce paquet `qnx-emu`. Toutes les commandes ci-dessous
se passent sur le serveur `r630`.

## (a) Écrire son `buildfile` (l'IFS)

Copiez le modèle et adaptez la section « fichiers » (vos binaires) :

```sh
mkdir -p image && cp qnx-emu/exemple/buildfile image/buildfile
```

Règles (déjà payées, cf. `README.md`) :

- les binaires du projet arrivent par `./nom` (le chemin de recherche commence
  par le répertoire de construction) ;
- `x = bin/x` place `x` **à la racine de l'IFS** (`/proc/boot`) — appelable
  directement ;
- les applets toybox se lient à la racine : `[type=link] /proc/boot/echo=/proc/boot/toybox` ;
- gardez les quatre lignes vitales du démarrage :
  `procmgr_symlink /proc/boot/ldqnx-64.so.2 /usr/lib/ldqnx-64.so.2` (sinon tout
  binaire échoue en `(83)`), `devc-con`, `devc-ser8250 -e`, `waitfor /dev/ser1`.

## (b) Compiler ses binaires avec `qcc`

`build-image.sh` le fait pour vous (étape suivante) ; en manuel :

```sh
export QNX_HOST=$HOME/qnx800/host/linux/x86_64 QNX_TARGET=$HOME/qnx800/target/qnx
qcc -V gcc_ntox86_64 -Wall -Wextra -Werror -o monprog src/monprog.c
```

## (c) Construire l'image

```sh
qnx-emu/build-image.sh --projet monprojet --buildfile image/buildfile \
    --sources "monprog:src/monprog.c" --sortie ~/monprojet-img
```

Produit `~/monprojet-img/ifs.bin` (et la partition de boot + le disque, pour la
trace). Le script résout `$QNX_TARGET` et applique `mkifs` → `mkxfs` →
`diskimage` avec la bonne syntaxe (cf. pièges 10 du `README.md`).

## (d) Lancer l'invité et le service

En manuel (pour vérifier avant de systemd-iser) :

```sh
qnx-emu/guest-qemu.sh --ifs ~/monprojet-img/ifs.bin \
    --serie ~/monprojet-demo/serial.log --moniteur ~/monprojet-demo/mon.sock \
    --qemu $HOME/qnx71-vm/qprefix/usr/bin/qemu-system-x86_64 \
    --prefixe $HOME/qnx71-vm/qprefix --detacher
tail -f ~/monprojet-demo/serial.log   # doit montrer votre scénario
```

Puis en service systemd utilisateur (copier les modèles, renommer `<projet>`) :

```sh
cp qnx-emu/systemd/replique-ems-qemu.service ~/.config/systemd/user/monprojet-qemu.service
cp qnx-emu/systemd/replique-ems-web.service  ~/.config/systemd/user/monprojet-web.service
# remplacer <projet>, fixer le port (un par projet), puis :
systemctl --user daemon-reload
systemctl --user enable --now monprojet-qemu.service monprojet-web.service
```

**Attention** : dans `Environment=` systemd ne développe **pas** `%h` (vérifié) —
les chemins y sont en absolu ; `%h` ne marche que dans `ExecStart`.

## (e) Exposer la vitrine (funnel)

Un port HTTP par projet ; ouvrir le tunnel HTTPS :

```sh
tailscale funnel --bg --https=<port> http://127.0.0.1:<port>
tailscale funnel status   # vérifier « Funnel on » pour https://…:<port>
```

## (f) Ajouter sa page au site du dossier QNX

1. Copier le gabarit : `cp src/replique-demo.html src/<projet>-demo.html` ;
2. pointer l'`API` (variable JS) sur votre URL funnel :
   `var API="https://<machine>.ts.net:<port>";` ;
3. ajouter une entrée dans `STATIQUES` de `tools/build_site.py` :
   `("src/<projet>-demo.html", "<projet>"),` ;
4. (optionnel) ajouter le slug à `NAV` pour l'affichage du menu ;
5. regénérer : `python3 tools/build_site.py` — la CI vérifie que `docs/` colle à
   la génération, donc regénérez aussi `docs/` (même sortie, cible `docs/`).

## (g) Vérifier que « ça marche »

Cochez, dans l'ordre :

1. **l'invité démarre** : `~/monprojet-demo/serial.log` contient le banner
   `QNX 8.0 - invite …` puis votre scénario, sans `Unable to start` et sans
   `(83)` ;
2. **la console montre le scénario** : `curl http://127.0.0.1:<port>/console`
   renvoie ce journal, `/age` donne `(console fraîche, N s)` ;
3. **l'écran VGA répond aux touches** : `/screen.png` renvoie une image (la
   console VGA avec le prompt `ksh`), `curl "…/key?k=ret"` renvoie `ok` et la
   prise d'écran suivante montre l'effet ;
4. **la page du site est en ligne** : la page `docs/<projet>.html` affiche
   l'écran et la console par-dessus le funnel (CORS ouvert dans `web.py`).

Si l'invité ne démarre pas : commenter `[+optional]` du buildfile pour voir les
fichiers manquants, et relire les pièges du `README.md` (les `(83)` = lien
`ldqnx-64.so.2` absent ; un `Unable to start "xxx" (2)` = binaire absent de
l'IFS).