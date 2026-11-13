#!/bin/sh
# image/build-image.sh — construit le disque invité QNX 8.0 de la démo Réplique.
#
# Prérequis : le SDP 8.0 installé, QNX_HOST/QNX_TARGET pointant dessus, et
# les sources du projet (ce dépôt). Le script :
#   1. compile Déclic et les trois générateurs de bogues avec qcc (QNX 8.0) ;
#   2. construit l'IFS (mkifs) à partir de image/buildfile ;
#   3. construit la partition de boot (mkxfs, système de fichiers QNX6) ;
#   4. assemble le disque amorçable (diskimage, chargeur ipl-diskpc1).
#
# Recette de diskimage tirée de $QNX_HOST/common/mkqnximage/rundiskimage
# (image de type "bios"), seule façon de faire booter un IFS sous QEMU.
#
# Usage :
#   export QNX_HOST=$SDP/host/linux/x86_64
#   export QNX_TARGET=$SDP/target/qnx
#   sh image/build-image.sh [répertoire_de_travail]
set -eu

SDP="${QNX_SDP:-$HOME/qnx800}"
: "${QNX_HOST:=$SDP/host/linux/x86_64}"
: "${QNX_TARGET:=$SDP/target/qnx}"
export QNX_HOST QNX_TARGET
PATH="$QNX_HOST/usr/bin:$PATH"
export PATH

SRC="${REPLIQUE_SRC:-$(cd "$(dirname "$0")/.." && pwd)}"
WORK="${1:-$HOME/replique-img}"
mkdir -p "$WORK"
cd "$WORK"

echo "== 1/4 compilation des binaires du projet (qcc, QNX 8.0) =="
( cd "$SRC"
  qcc -V gcc_ntox86_64 -Wall -Wextra -Werror -o "$WORK/declic" \
      target/declic/declic_qnx.c target/declic/declic_watch.c \
      target/declic/declic_exec.c target/declic/declic_portable.c \
      target/declic/sha256_portable.c target/declic/kev_rkv1.c -Itarget/declic
  for bug in deadlock prio_inversion race; do
      qcc -V gcc_ntox86_64 -Wall -Wextra -Werror \
          -o "$WORK/bug-$bug" "image/bugs/$bug.c"
  done )
cp "$WORK/bug-prio_inversion" "$WORK/bug-prio-inversion"
cp "$SRC/image/demo.sh" .

echo "== 2/4 IFS (mkifs) =="
sed "s|\$QNX_TARGET|$QNX_TARGET|g" "$SRC/image/buildfile" > image.resolved
mkifs -v -o . image.resolved ifs.bin
ls -l ifs.bin

echo "== 3/4 partition de boot (mkxfs, système de fichiers QNX6) =="
cp "$SRC/image/boot_part.build" .
sed "s|\$QNX_TARGET|$QNX_TARGET|g" boot_part.build > boot_part.resolved
mkxfs -t qnx6fsimg -o . boot_part.resolved boot.part
ls -l boot.part

echo "== 4/4 disque amorçable (diskimage) =="
BOOT_SECTORS=$(( ($(stat -c %s boot.part) + 511) / 512 ))
cat > layout.txt <<LAYOUT
[sector_size=512 cylinders=$((BOOT_SECTORS + 1)) sectors_per_track=1 heads=1 start_at_cylinder=1]
[partition=1 boot=true type=177 num_sectors=${BOOT_SECTORS}] "boot.part"
LAYOUT
diskimage -p -s 163840 -c layout.txt -o replique-disk.img \
    -b "$QNX_TARGET/x86_64/boot/sys/ipl-diskpc1-nomsg"
ls -l replique-disk.img
echo "OK : disque prêt dans $WORK/replique-disk.img"
