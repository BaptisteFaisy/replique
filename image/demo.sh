#!/bin/sh
# demo.sh — démonstration Déclic (mode global), exécutée par ksh via `on`.
#
# Pourquoi ce fichier plutôt que le [+script] de démarrage : le shell de
# démarrage de l'image ne gère pas $! (il lançait `echo` en tâche de fond et on
# récupérait le mauvais pid). ksh, lui, rend $! fiable — c'est ce qui permet de
# surveiller le VRAI processus (le bogue), pas le `echo` d'annonce.

PATH=/proc/boot:/system/bin
LD_LIBRARY_PATH=/proc/boot:/system/lib:/system/lib/dll
export PATH LD_LIBRARY_PATH HOME=/proc/boot

TRACE=/dev/shmem/trace.kev
OUT=/dev/shmem/declic

# 0) Journal slog2 (lu par Déclic) : slogger2 doit tourner, et le pilote
#    devb-ram (disque RAM, point 2) alimente le journal via slog2f.
slogger2 &
sleep 1
devb-ram ram capacity=16384 &
sleep 2

echo "=== demo Declic : mode global ==="

# 1) Tampon de trace circulaire, figé par SIGINT au moment du crash (essai E2).
tracelogger -d1 -E -r -f "$TRACE" -S 16M -s 0 &
TRACEPID=$!
sleep 1

# 2) Le bogue à observer.
echo "=== bug-deadlock : demarrage puis interblocage ==="
bug-deadlock &
BUGPID=$!
sleep 3

# 3) Déclic surveille $BUGPID. Le signal de fin est fourni explicitement
#    (SIGTERM) : QNX 8.0 n'a plus `pidin dead`.
declic --pid "$BUGPID" --trace-pid "$TRACEPID" --trace "$TRACE" \
       --out "$OUT" --signal 15 --status 0 &
DECLICPID=$!
sleep 2

# 4) Terminaison du bogue : Déclic détecte la mort de $BUGPID, fige la trace
#    et archive trace + pidin + slog2 + manifest.
echo "=== slay bug-deadlock : terminaison du processus ==="
kill -TERM "$BUGPID"
wait "$DECLICPID"
echo "=== fin de la demo ==="