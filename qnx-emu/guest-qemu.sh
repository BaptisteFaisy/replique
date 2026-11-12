#!/bin/sh
# guest-qemu.sh — lancement GÉNÉRIQUE de l'invité QNX 8.0 sous QEMU.
#
# Démarre l'IFS du projet (image invitée) avec la ligne de commande qui marche.
#
# LA LIGNE QUI MARCHE (vérifiée sur le r630, démo Réplique, 12/11/2026) :
#
#   qemu-system-x86_64 --cpu max -smp 4 -m 2048 -L $QPREFIX/usr/share/qemu \
#     -kernel ifs.bin \
#     -object rng-random,filename=/dev/urandom,id=rng0 \
#     -device virtio-rng-pci,rng=rng0 \
#     -display none -vga std \
#     -serial file:serial.log \
#     -monitor unix:mon.sock,server,nowait
#
# Pièges déjà payés (détail dans README.md) :
#   - `-cpu qemu64` ne démarre pas : il faut `--cpu max` ;
#   - il MANQUE le périphérique RNG virtio (virtio-rng) sans quoi le noyau
#     s'arrête : `-object rng-random … -device virtio-rng-pci,rng=rng0` ;
#   - l'IFS se lance par `-kernel`, PAS par le disque : en disque, le chargeur
#     s'arrête sur « QNX v1.2d Boot Loader » (le disque est construit pour la
#     trace, mais on ne boote pas dessus).
#
# Usage :
#   guest-qemu.sh --ifs <ifs.bin> [--serie <log>] [--moniteur <sock>] \
#                 [--memoire <Mo>] [--cpu <modèle>] [--smp <n>] \
#                 [--qemu <binaire>] [--prefixe <dir>] [--detacher] [--net <port>]
#
# Tout peut aussi passer par l'environnement (QNX_EMS_*), cf. systemd/ et web.py.
set -eu

usage() { sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
die() { echo "guest-qemu: $*" >&2; exit 1; }

IFS_BIN="${QNX_EMS_IFS:-}"
SERIE="${QNX_EMS_SERIE:-serial.log}"
MON="${QNX_EMS_MON:-mon.sock}"
MEM="${QNX_EMS_MEM:-2048}"
CPU="${QNX_EMS_CPU:-max}"
SMP="${QNX_EMS_SMP:-4}"
QEMU="${QNX_EMS_QEMU:-}"
QPREFIX="${QNX_EMS_PREFIXE:-}"
NET=""
DETACHER=0

parse_args() {
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --ifs)      IFS_BIN="$2"; shift 2 ;;
            --serie)    SERIE="$2"; shift 2 ;;
            --moniteur) MON="$2"; shift 2 ;;
            --memoire)  MEM="$2"; shift 2 ;;
            --cpu)      CPU="$2"; shift 2 ;;
            --smp)      SMP="$2"; shift 2 ;;
            --qemu)     QEMU="$2"; shift 2 ;;
            --prefixe)  QPREFIX="$2"; shift 2 ;;
            --net)      NET="-netdev user,id=n0,hostfwd=tcp:127.0.0.1:$2-:22 -device virtio-net-pci,netdev=n0"; shift 2 ;;
            --detacher) DETACHER=1; shift ;;
            --aide|-h)  usage 0 ;;
            *)          die "argument inconnu : $1" ;;
        esac
    done
    [ -n "$IFS_BIN" ] || die "--ifs est obligatoire (chemin de l'image IFS)"
    [ -f "$IFS_BIN" ] || die "IFS introuvable : $IFS_BIN"
    if [ -z "$QEMU" ]; then
        QEMU="$(command -v qemu-system-x86_64 2>/dev/null || true)"
        [ -n "$QEMU" ] || die "qemu-system-x86_64 introuvable (--qemu <chemin> ?)"
    fi
}

# les libs du qemu localisé peuvent exiger un LD_LIBRARY_PATH (qprefix)

main() {
    parse_args "$@"
    export LD_LIBRARY_PATH="$QPREFIX/usr/lib/x86_64-linux-gnu:$QPREFIX/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
    mkdir -p "$(dirname "$SERIE")" "$(dirname "$MON")" 2>/dev/null || true
    # La commande QEMU est construite ICI (dans main, pas dans une fonction :
    # `set --` d'une fonction ne modifie pas $@ du script en sh POSIX).
    set -- "$QEMU" \
        --cpu "$CPU" -smp "$SMP" -m "$MEM" \
        -L "$QPREFIX/usr/share/qemu" \
        -kernel "$IFS_BIN" \
        -object rng-random,filename=/dev/urandom,id=rng0 \
        -device virtio-rng-pci,rng=rng0 \
        -display none -vga std \
        -serial "file:$SERIE" \
        -monitor "unix:$MON,server,nowait"
    # $NET est volontairement non quoté : déjà une chaîne espacée complète
    # shellcheck disable=SC2086
    if [ -n "$NET" ]; then
        set -- "$@" $NET
    fi
    echo "guest-qemu: $*"
    if [ "$DETACHER" = 1 ]; then
        setsid nohup "$@" >/dev/null 2>&1 &
        echo "détaché (pid $!)"
    else
        exec "$@"
    fi
}

main "$@"