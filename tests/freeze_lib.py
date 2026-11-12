"""freeze_lib.py — écrit scenarios/<nom>.json depuis une exécution réelle.

Entrées (variables d'environnement, posées par tests/freeze_scenarios.sh) :
  TRACE     chemin de la fixture .rkv1
  VERDICTS  sorties canoniques « vd|… » (identiques en C et en Python)
  HASH      ligne « hash=<u64> » du rejeu
  OUT       fichier JSON à écrire
Le JSON porte les clés dans l'ordre gelé par scenarios/SPEC.md §6.
"""
from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "host"))

from replique import kev                      # noqa: E402
from replique import model as m                # noqa: E402

CODES_INFO = {"OK", "REPLAY_DETERMINISTIC", "CORRELATION_OK", "WHATIF"}


def parse_line(line: str) -> dict:
    champs = dict(part.split("=", 1) for part in line.strip().split("|")[1:])
    return champs


def verdictsAttendus(lignes: list[str]) -> list[dict]:
    """Verdicts porteurs de diagnostic, dans l'ordre canonique du tri."""
    out = []
    for line in lignes:
        champs = parse_line(line)
        if champs["code"] in CODES_INFO:
            continue
        item = {"code": champs["code"], "sev": champs["sev"],
                "ts": int(champs["ts"]), "tid": int(champs["tid"])}
        if champs["chain"]:
            item["chain"] = [int(t) for t in champs["chain"].split(",")]
        if champs["detail"]:
            item["detail"] = champs["detail"]
        if champs["a"] != "0":
            item["a"] = int(champs["a"])
        out.append(item)
    return out


def main() -> int:
    trace = os.environ["TRACE"]
    with open(trace, "rb") as handle:
        blob = handle.read()
    with open(os.environ["VERDICTS"], encoding="utf-8") as handle:
        lignes = [line for line in handle.read().splitlines() if line]
    hash_line = os.environ.get("HASH", "hash=0")
    try:
        header, events = kev.parse(blob)
        n_events, flags, unit = len(events), header.flags, header.time_unit
    except m.ParseError as exc:
        header, n_events, flags, unit = None, 0, 0, m.TIME_UNIT_NS
        from replique import event as ev
        print(f"# {trace} : trace corrompue ({ev.cause_name(exc.cause)})",
              file=sys.stderr)
    attendus = verdictsAttendus(lignes)
    doc = {
        "schema": "replique-scenario/1",
        "name": os.path.splitext(os.path.basename(trace))[0],
        "fixture": trace.replace(os.path.sep, "/"),
        "n_events": n_events,
        "flags": flags,
        "time_unit": unit,
        "expected_verdicts": attendus,
        "forbidden_verdicts": [v["code"] for v in attendus
                               if v["code"] == "DEADLOCK_FOUND"] and [] or [],
        "replay": {"deterministic": True,
                   "hash": int(hash_line.split("=", 1)[1]),
                   "whatif": {"tid": 0, "prio": 0, "hash_changes": False}},
    }
    with open(os.environ["OUT"], "w", encoding="utf-8") as handle:
        handle.write(json.dumps(doc, separators=(",", ":"), ensure_ascii=False))
        handle.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
