"""commands.py — implémentation des commandes gelées (CONTRACT.md §4.7).

Codes de retour, identiques aux pilotes C :
    0  sortie conforme produite (y compris un verdict PARSE_ERROR pour analyze)
    2  fichier illisible (erreur d'E/S)
    3  trace corrompue, pour dump/replay (analyze imprime le verdict et rend 0)

Les imports sont paresseux : chaque commande ne charge que ses modules.
"""
from __future__ import annotations

import sys
from typing import Sequence, TextIO

EXIT_OK, EXIT_IO, EXIT_PARSE = 0, 2, 3


def _read(path: str):
    from replique import kev

    try:
        with open(path, "rb") as fh:
            blob = fh.read()
    except OSError as exc:
        print(f"replique: {path}: {exc.strerror}", file=sys.stderr)
        return None, None, None, EXIT_IO
    try:
        header, events = kev.parse(blob)
    except Exception as exc:  # ParseError (cause, offset)
        cause = getattr(exc, "cause", 0)
        offset = getattr(exc, "offset", 0)
        from replique import event as ev_names

        detail = ev_names.cause_name(cause)
        print(f"replique: PARSE_ERROR {detail} offset={offset}", file=sys.stderr)
        return None, None, blob, EXIT_PARSE
    return header, events, blob, EXIT_OK


def _config(whatif: Sequence[tuple[int, int]]):
    from replique import model as m, sched

    cfg = sched.default_config()
    cfg.overrides = tuple(whatif)
    for tid, prio in whatif:
        if not 0 <= prio < m.PRIO_LEVELS:
            raise SystemExit(f"priorité hors bornes 0..255 pour tid {tid}")
    return cfg


def dump(trace: str, out: TextIO, as_json: bool = False) -> int:
    from replique import report

    header, events, blob, code = _read(trace)
    if code:
        return code
    if as_json:
        out.write(report.document(header, blob, events, [], None))
    else:
        out.write(report.events_text(events))
    return EXIT_OK


def analyze(trace: str, out: TextIO, as_json: bool = False,
            whatif: Sequence[tuple[int, int]] = ()) -> int:
    from replique import report, verdict

    header, events, blob, code = _read(trace)
    cfg = _config(whatif)
    if code == EXIT_PARSE:
        verdicts, res = verdict.pipeline_blob(blob, cfg=cfg)
        report.write_text(out, report.verdicts_text(verdicts))
        return EXIT_OK
    if code:
        return code
    verdicts, res = verdict.pipeline_events(events, cfg=cfg, header=header,
                                            blob=blob)
    if as_json:
        out.write(report.document(header, blob, events, verdicts, res))
    else:
        out.write(report.verdicts_text(verdicts))
    return EXIT_OK


def replay(trace: str, out: TextIO, as_json: bool = False,
           only_hash: bool = False, step: bool = False,
           whatif: Sequence[tuple[int, int]] = ()) -> int:
    from replique import report
    from replique import replay as engine

    header, events, blob, code = _read(trace)
    if code:
        return code
    res = engine.run(events, _config(whatif))
    if step:
        engine.step_through(events, _config(whatif), out)
        return EXIT_OK
    if only_hash:
        out.write(f"hash={res.hash}\n")
    elif as_json:
        out.write(report.to_json({"schema": "replique/1",
                                  "replay": report.replay_json(res)}) + "\n")
    else:
        out.write(report.decisions_text(res.decisions))
    return EXIT_OK


def gen(scenario: str, out_dir: str, all_scenarios: bool = False) -> int:
    from replique import kevgen

    names = kevgen.NAMES if all_scenarios else (scenario,)
    if not all_scenarios and scenario not in kevgen.NAMES:
        print(f"replique: scénario inconnu {scenario!r} "
              f"(attendus : {', '.join(kevgen.NAMES)})", file=sys.stderr)
        return EXIT_IO
    for name in names:
        path = kevgen.write(name, out_dir)
        print(path)
    return EXIT_OK


def view(trace: str, out: TextIO) -> int:
    from replique import deadlock, view as viewer

    header, events, blob, code = _read(trace)
    if code:
        return code
    out.write(viewer.render_svg(events, deadlock.detect(events)))
    return EXIT_OK


