# image — image invitée QNX 8.0 pour la démo Réplique

L'image invitée contient le minimum nécessaire à la démonstration S7 :
**tuer un processus sur l'invité QNX → Déclic produit automatiquement une
archive complète, lisible sur PC**.

Contenu de l'image (voir `buildfile`) :

| Composant | Rôle |
|---|---|
| `startup-bios`, `devc`, `procnto` | démarrage et noyau x86_64 |
| `tracelogger` | le tampon de trace circulaire (la source de la trace) |
| `dumper` | les core dumps (vidage d'incident) |
| `sl-pinfo.so` / slog2 | le journal système |
| `declic` | le démon de figeage (`target/declic/`) |
| `deadlock`, `prio_inversion`, `race` | les générateurs de bogues (`image/bugs/`) |

---

## 1. Construire l'image

Pré-requis : SDP 8.0 installé et `qcc`/`mkifs` dans le `PATH`.

```sh
source ~/qnx800/qnxsdp-env.sh          # à adapter au chemin d'installation

# 1. Déclic (cible)
cd target/declic && make -f Makefile.qnx && cd ../..

# 2. les générateurs de bogues
cd image/bugs && make -f Makefile.qnx && cd ../..

# 3. l'image
cd image && mkifs -r $QNX_TARGET/x86_64le buildfile replique.ifs && cd ..
```

> **À VÉRIFIER SUR LE SDP 8.0** : l'option `-r` de `mkifs` (répertoire des
> binaires cibles), le nom exact de l'image produite (`.ifs`) et la syntaxe
> des sections `[image]` / `[+script]` du buildfile. Le point de départ
> recommandé est le buildfile d'exemple fourni par le SDP pour QEMU : on y
> ajoute `declic` et les trois générateurs, plutôt que d'inventer une image.

## 2. Lancer l'image sous QEMU

```sh
qemu-system-x86_64 -m 512 -nographic \
    -kernel image/replique.ifs \
    -append "console=ttyS0"
```

Variante « disque » (si l'image est transformée en `.qcow2` ou `.img`) :

```sh
qemu-system-x86_64 -m 512 -nographic -hda image/replique.img
```

> **À VÉRIFIER SUR LE SDP 8.0** : la méthode de démarrage exacte sous QEMU
> pour une image QNX 8.0 (`-kernel` avec un `.ifs`, ou une image disque), et
> les arguments du noyau. Le SDP fournit généralement un script de lancement
> QEMU dans ses exemples.

Une fois l'invité démarré, vérifier que Déclic est bien lancé (sinon le
lancer à la main) :

```sh
pidin | grep declic           # le démon tourne-t-il ?
mkdir -p /var/log/declic
declic --out /var/log/declic --all --poll 500 &
```

---

## 3. Démo S7 : tuer un processus et récupérer l'archive

### a) Avec un générateur de bogues (scénarios de référence)

```sh
deadlock            # interblocage (SPEC.md §2) — ne se termine jamais
# ou
prio_inversion      # inversion de priorité (SPEC.md §3)
# ou
race                # course avec terminaison (SPEC.md §4)
```

Dans un autre terminal de l'invité :

```sh
kill -SEGV <pid>             # terminer brutalement le processus
ls -l /var/log/declic/       # l'archive apparaît : declic-<pid>-<ticks>
```

### b) Avec un processus quelconque

```sh
sleep 300 &
kill -SEGV $!
ls -l /var/log/declic/
```

### c) Récupérer l'archive sur le PC hôte

Depuis l'hôte (QEMU) :

```sh
# via le partage de fichiers de QEMU (à configurer), ou :
scp -P <port-ssh-invité> root@localhost:/var/log/declic/declic-* .
```

Puis, sur le PC, lire l'archive avec le cœur portable ou le jumeau Python
(voir `README.md` du projet) :

```sh
python3 -m replique.cli dump archive-declic.rda1
```

L'archive contient **5 sections** (voir `target/declic/README.md` §3) :
`trace` (`.rkv1`), `pidin`, `slog2`, `core`, `manifest`. Chacune porte son
empreinte SHA-256 dans la table ; l'empreinte de l'archive entière est
imprimée par le démon à l'écriture.

---

## 4. État honnête (essais E2 / E3 non tranchés)

Le SDP 8.0 n'est pas installé sur la machine de développement : **rien de
cette image n'a été construit ni démarré**. Les points non vérifiés sont
listés dans `target/declic/README.md` §4 (figeage de `tracelogger` — E2,
observabilité de la terminaison — E3, syntaxe `mkifs`, démarrage QEMU). Le
repli par scrutation `pidin` du démon est le seul déclencheur qui n'utilise
aucune API QNX exotique : c'est celui qui a le plus de chances de
fonctionner du premier coup.
