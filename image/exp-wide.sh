#!/bin/sh
# exp-wide.sh — essai E2 élargi : capture « wide » de la trace pendant
# l'interblocage, pilotée par la console série (aucune reconstruction d'image).
#
# Pourquoi le mode « wide » : la trace par défaut (« fast events ») ne contient
# PAS les événements de synchronisation du processus fautif ; `-w` les capture
# (création des threads, priorités, état bloqué, terminaison). Le lancer après
# le boot évite la régression d'affichage VGA observée quand `-w` est dans le
# scénario de démarrage.
#
# Pré-requis : invité `--serie-socket` (cf. qnx-emu/guest-qemu.sh) et
#   qnx-emu/ser-console.py --socket <sock> 'sh /proc/boot/exp-wide.sh'
set -u
PATH=/proc/boot:/system/bin
LD_LIBRARY_PATH=/proc/boot:/system/lib:/system/lib/dll
export PATH LD_LIBRARY_PATH HOME=/proc/boot

TRACE=/dev/shmem/wide.kev
OUT=/dev/shmem/declic-exp

echo "=== EXP : tracelogger wide ==="
tracelogger -w -F2 -c -r -f "$TRACE" -S 16M -k 16 -b 256 &
TPID=$!
sleep 3
echo "=== EXP : bug-deadlock ==="
bug-deadlock &
BPID=$!
sleep 4
echo "=== EXP : bug pid=$BPID trace pid=$TPID ==="
ls -l /dev/shmem/
declic --pid "$BPID" --trace-pid "$TPID" --trace "$TRACE" --out "$OUT" --signal 15 --status 0 &
DPID=$!
sleep 2
echo "=== EXP : slay du bogue ==="
kill -TERM "$BPID"
wait $DPID
echo "=== EXP : fin ==="
ls -l /dev/shmem/ /dev/shmem/declic-exp/ 2>&1