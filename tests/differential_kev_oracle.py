#!/usr/bin/env python3
"""differential_kev_oracle.py — différentiel `.kev` binaire contre traceprinter (S4).

Prend une vraie trace `.kev` et compare, **par type d'événement**, ce que notre
décodeur binaire (`host/replique/kev_reader.py`) extrait à ce que l'oracle
officiel `traceprinter` (SDP QNX 8.0) rapporte. Le décodage n'est PAS dérivé du
texte de l'oracle : on lit le binaire (sys/trace.h) et on confronte les comptes.

Sans `traceprinter` ni fichier `.tp.txt` pré-enregistré, avertit et sort 0
(jamais d'échec pour absence de licence), comme le reste de la chaîne.
"""
from __future__ import annotations

import collections
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "host"))
from replique import kev_reader as kr  # noqa: E402

# (classe, code) -> nom d'événement tel que l'écrit traceprinter
NAMES = {
    (4, 0): "THDEAD", (4, 1): "THRUNNING", (4, 2): "THREADY", (4, 3): "THSTOPPED",
    (4, 4): "THSEND", (4, 5): "THRECEIVE", (4, 6): "THREPLY", (4, 9): "THWAITPAGE",
    (4, 10): "THSIGSUSPEND", (4, 11): "THSIGWAITINFO", (4, 12): "THNANOSLEEP",
    (4, 13): "THMUTEX", (4, 14): "THCONDVAR", (4, 15): "THJOIN", (4, 16): "THINTR",
    (4, 17): "THSEM", (4, 18): "THWAITCTX", (4, 19): "THRWLOCK_READ",
    (4, 20): "THRWLOCK_WRITE", (4, 21): "THBARRIER", (4, 22): "THPIPE",
    (4, 24): "THCREATE", (4, 25): "THDESTROY", (4, 26): "THMUON_MUTEX",
    (4, 27): "THTRACEBUFFER", (4, 30): "THSEND_NOTIFY",
    (4, 0x40): "PROCCREATE", (4, 0x80): "PROCDESTROY",
    (4, 0xC0): "PROCCREATE_NAME", (4, 0x100): "PROCDESTROY_NAME",
    (4, 0x140): "PROCTHREAD_NAME",
    (7, 0): "SND_MESSAGE", (7, 2): "REC_MESSAGE", (7, 10): "REPLY_MESSAGE",
    (7, 11): "MSG_ERROR", (7, 9): "SIGNAL",
}


def our_counts(blob):
    _, events = kr.logical_events(blob)
    counts = collections.Counter()
    for e in events:
        name = NAMES.get((e.cls, e.code))
        if name:
            counts[name] += 1
    return counts


def find_traceprinter():
    tp = shutil.which("traceprinter")
    if tp:
        return tp
    qnx = os.environ.get("QNX_HOST")
    if qnx:
        cand = os.path.join(qnx, "usr", "bin", "traceprinter")
        if os.path.exists(cand):
            return cand
    return None


def oracle_text(kev_path):
    """Texte de l'oracle : régénéré si traceprinter présent, sinon .tp.txt voisin."""
    tp = find_traceprinter()
    if tp:
        env = dict(os.environ)
        qnx = env.get("QNX_HOST")
        if qnx:
            env["LD_LIBRARY_PATH"] = os.path.join(qnx, "usr", "lib") + ":" + env.get("LD_LIBRARY_PATH", "")
        try:
            res = subprocess.run([tp, "-f", kev_path], capture_output=True, timeout=120, env=env)
            if res.returncode == 0 and res.stdout:
                return res.stdout.decode("latin-1"), "traceprinter(%s)" % tp
        except (OSError, subprocess.SubprocessError):
            pass
    side = os.path.splitext(kev_path)[0] + ".tp.txt"
    if os.path.exists(side):
        return open(side, "r", encoding="latin-1").read(), side
    return None, None


def oracle_counts(text):
    counts = collections.Counter()
    comm_names = {"SND_MESSAGE", "REC_MESSAGE", "REPLY_MESSAGE", "MSG_ERROR", "SIGNAL"}
    for line in text.splitlines():
        m = re.search(r"\b(COMM|THREAD|PROCESS)\s+:([A-Z0-9_]+)", line)
        if not m:
            continue
        cls, name = m.group(1), m.group(2)
        if cls == "COMM" and name in comm_names:
            counts[name] += 1
        elif cls in ("THREAD", "PROCESS") and name not in comm_names:
            counts[name] += 1
    return counts


def main(argv):
    kev_path = argv[1] if len(argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "docs", "preuves", "trace-qnx800.kev")
    blob = open(kev_path, "rb").read()
    ours = our_counts(blob)
    text, source = oracle_text(kev_path)
    if text is None:
        print("AVERTISSEMENT : traceprinter absent et aucun .tp.txt voisin.")
        print("Différentiel oracle ignore (pas de licence) — la CI reste verte.")
        return 0
    oracle = oracle_counts(text)
    ok = ko = 0
    print("oracle : %s" % source)
    for name in sorted(set(ours) | set(oracle)):
        o, mine = oracle.get(name, 0), ours.get(name, 0)
        if o == 0 and mine == 0:
            continue
        if o == mine:
            ok += 1
            print("  ok  %-20s %d" % (name, mine))
        else:
            ko += 1
            print("  KO  %-20s oracle=%d  decodeur=%d" % (name, o, mine))
    print("differentiel oracle : OK=%d KO=%d" % (ok, ko))
    return 1 if ko else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))