#!/bin/sh
# build-image.sh — chaîne GÉNÉRIQUE de construction d'une image invitée QNX 8.0.
#
#   qcc (compilation)  →  mkifs (IFS)  →  mkxfs (partition de boot)  →  diskimage (disque)
#
# Un seul script pour tous les projets QNX du portfolio : chaque projet fournit
# SON buildfile (le contenu de l'IFS) et SES sources ; le script en tire l'IFS,
# la partition de boot et le disque amorçable — exactement comme la démo Réplique.
#
# Usage :
#   build-image.sh --projet demo --buildfile exemple/buildfile \
#                  --sources "hello:exemple/hello.c"
#   PROJETS_SOURCES="hello:exemple/hello.c;bug:exemple/bug.c" \
#       build-image.sh --projet demo --buildfile exemple/buildfile
#
#   --projet <nom>         nom (binaires/<nom>-*.img). Obligatoire.
#   --buildfile <fichier>  buildfile du projet (mkifs). Obligatoire.
#   --sources "<groupes>"  sources à compiler : groupes `nom: f.c [g.c …]`
#                          séparés par `;`. Défaut : $PROJETS_SOURCES.
#   --sortie <dir>         répertoire de travail. Défaut : ~/<projet>-img
#   --sdp <dir>            racine du SDP 8.0. Défaut : $QNX_SDP ou ~/qnx800
#   --cible <triple>       cible qcc. Défaut : gcc_ntox86_64
#   --cflags "<flag>"      flags supplémentaires passés à qcc
#   --sans-disque          ne construire que l'IFS (pas partition ni disque)
#
# Pièges déjà payés (détail et messages exacts dans README.md) :
#   - mkifs écrit le FICHIER IMAGE en argument positionnel ; -o est seulement le
#     répertoire des artefacts :  mkifs -v -o . image.resolved ifs.bin
#   - le buildfile résolu remplace littéralement $QNX_TARGET avant mkifs ;
#   - mkxfs -t qnx6fsimg pour la partition de boot, diskimage pour le disque ;
#   - LE DISQUE NE BOOT PAS : l'IFS se lance par -kernel (cf. guest-qemu.sh).
set -eu

# ------------------------------------------------------------------- utilitaires
usage() {
    sed -n '2,31p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}
die() { echo "build-image: $*" >&2; exit 1; }

# ------------------------------------------------------------------- arguments
PROJET=""
BUILDFILE=""
SOURCES="${PROJETS_SOURCES:-}"
CIBLES="gcc_ntox86_64"
CFLAGS=""
AVEC_DISQUE=1
RACINE="$(pwd)"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --projet)    PROJET="$2"; shift 2 ;;
            --buildfile) BUILDFILE="$2"; shift 2 ;;
            --sources)   SOURCES="$2"; shift 2 ;;
            --sortie)    SORTIE="$2"; shift 2 ;;
            --sdp)       SDP="$2"; shift 2 ;;
            --cible)     CIBLES="$2"; shift 2 ;;
            --cflags)    CFLAGS="$2"; shift 2 ;;
            --sans-disque) AVEC_DISQUE=0; shift ;;
            --aide|-h)   usage 0 ;;
            *)           die "argument inconnu : $1" ;;
        esac
    done
    [ -n "$PROJET" ]    || die "--projet est obligatoire"
    [ -n "$BUILDFILE" ] || die "--buildfile est obligatoire"
    SDP="${SDP:-${QNX_SDP:-$HOME/qnx800}}"
    QNX_HOST="${QNX_HOST:-$SDP/host/linux/x86_64}"
    QNX_TARGET="${QNX_TARGET:-$SDP/target/qnx}"
    SORTIE="${SORTIE:-$HOME/$PROJET-img}"
    case "$BUILDFILE" in /*) : ;; *) BUILDFILE="$RACINE/$BUILDFILE" ;; esac
    case "$SORTIE" in /*) : ;; *) SORTIE="$RACINE/$SORTIE" ;; esac
    export QNX_HOST QNX_TARGET
    PATH="$QNX_HOST/usr/bin:$PATH"; export PATH
}

# ------------------------------------------------------------------- compilation
# Chaque groupe `nom: f.c [g.c …]` devient un binaire `nom`. Un groupe sans
# `nom:` prend le nom du projet (un seul groupe anonyme autorisé).
compile() {
    echo "== 1/4 compilation des binaires du projet (qcc, $CIBLES) =="
    if [ -z "$SOURCES" ]; then
        echo "   (aucune source : on saute la compilation, les binaires doivent"
        echo "    déjà être dans $SORTIE, référencés `./nom` par le buildfile)"
        return 0
    fi
    # printf '%s\n' : sans le saut de ligne final, read dropperait le dernier
    # groupe (il renvoie faux en fin de flux).
    printf '%s\n' "$SOURCES" | tr ';' '\n' | while IFS= read -r groupe; do
        [ -n "$groupe" ] || continue
        nom="$PROJET"; fichiers="$groupe"
        case "$groupe" in
            *:*) nom="${groupe%%:*}"; fichiers="${groupe#*:}" ;;
        esac
        [ -n "$nom" ] || die "groupe sans nom dans --sources : $groupe"
        # chemins absolus (on a fait `cd $SORTIE`) : les .c sont relatifs à
        # l'endroit où le script a été invoqué ($RACINE).
        abs=""
        for f in $fichiers; do
            case "$f" in /*) abs="$abs $f" ;; *) abs="$abs $RACINE/$f" ;; esac
        done
        # shellcheck disable=SC2086
        qcc -V "$CIBLES" -Wall -Wextra -Werror $CFLAGS -o "$SORTIE/$nom" $abs \
            || die "échec de compilation de $nom"
        echo "   qcc → $SORTIE/$nom"
    done
}

# ------------------------------------------------------------------- IFS
construire_ifs() {
    echo "== 2/4 IFS (mkifs) =="
    # mkifs résout les `[search=…]` ; on remplace $QNX_TARGET par son chemin
    # absolu car mkifs ne développe pas les variables d'environnement.
    sed "s|\$QNX_TARGET|$QNX_TARGET|g" "$BUILDFILE" > "$SORTIE/image.resolved"
    mkifs -v -o "$SORTIE" "$SORTIE/image.resolved" "$SORTIE/ifs.bin"
    ls -l "$SORTIE/ifs.bin"
}

# ------------------------------------------------------------------- partition
construire_partition_boot() {
    echo "== 3/4 partition de boot (mkxfs, qnx6fsimg) =="
    cat > "$SORTIE/boot_part.build" <<EOF
[search=.:$QNX_TARGET/x86_64:$QNX_TARGET/x86_64/boot/sys:]
[+dupignore]
.boot/qnx_sdp.ifs=ifs.bin
multiboot.boot = boot/sys/multiboot.boot
EOF
    mkxfs -t qnx6fsimg -o "$SORTIE" "$SORTIE/boot_part.build" "$SORTIE/boot.part"
    ls -l "$SORTIE/boot.part"
}

# ------------------------------------------------------------------- disque
construire_disque() {
    echo "== 4/4 disque amorçable (diskimage) =="
    BOOT_SECTORS=$(( ($(stat -c %s "$SORTIE/boot.part") + 511) / 512 ))
    cat > "$SORTIE/layout.txt" <<EOF
[sector_size=512 cylinders=$((BOOT_SECTORS + 1)) sectors_per_track=1 heads=1 start_at_cylinder=1]
[partition=1 boot=true type=177 num_sectors=${BOOT_SECTORS}] "boot.part"
EOF
    diskimage -p -s 163840 -c "$SORTIE/layout.txt" -o "$SORTIE/$PROJET-disk.img" \
        -b "$QNX_TARGET/x86_64/boot/sys/ipl-diskpc1-nomsg"
    ls -l "$SORTIE/$PROJET-disk.img"
}

# ------------------------------------------------------------------- principal
main() {
    parse_args "$@"
    mkdir -p "$SORTIE"
    [ -f "$BUILDFILE" ] || die "buildfile introuvable : $BUILDFILE"
    for o in qcc mkifs mkxfs diskimage; do
        command -v "$o" >/dev/null 2>&1 || die "$o introuvable (QNX_HOST=$QNX_HOST ?)"
    done
    # Les buildfiles référencent les binaires par `./nom` (chemin de recherche
    # qui commence par `.`) : on construit DANS $SORTIE et on y travaille.
    cd "$SORTIE"
    compile
    construire_ifs
    if [ "$AVEC_DISQUE" = 1 ]; then
        construire_partition_boot
        construire_disque
    fi
    echo "OK :"
    echo "   IFS   → $SORTIE/ifs.bin"
    if [ "$AVEC_DISQUE" = 1 ]; then
        echo "   disque → $SORTIE/$PROJET-disk.img"
    fi
    echo "   (démarrage : guest-qemu.sh --ifs $SORTIE/ifs.bin …)"
}

main "$@"