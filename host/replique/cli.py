"""cli.py — surface de commande gelée de l'hôte Réplique (CONTRACT.md §4).

Sortie canonique uniquement sur stdout (texte §4, ou JSON §4.5 avec --json) ;
les diagnostics vont sur stderr. Aucune horloge dans les sorties canoniques :
le différentiel C↔Python compare ces octets.

Commandes :
    dump    <fichier.rkv1>            lignes "ev|" (§4.1)
    analyze <fichier.rkv1>            lignes "vd|" (§4.2), tri §4.4
    replay  <fichier.rkv1>            lignes "dec|" (§4.3) ; --hash → "hash=<u64>"
    gen     <scénario>|--all          écrit les fixtures (.rkv1)
    view    <fichier.rkv1>            SVG du graphe d'attente (C7)
Options transverses : --json, --whatif TID=PRIO (répétable), --out CHEMIN.
"""
from __future__ import annotations

import argparse
import sys


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="replique", description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="cmd", required=True)

    for name, helpt in (("dump", "événements canoniques"),
                        ("analyze", "verdicts canoniques"),
                        ("replay", "décisions d'ordonnancement")):
        s = sub.add_parser(name, help=helpt)
        s.add_argument("trace")
        s.add_argument("--json", action="store_true")
        s.add_argument("--whatif", action="append", default=[], metavar="TID=PRIO")
        if name == "replay":
            s.add_argument("--hash", action="store_true", dest="only_hash")
            s.add_argument("--step", action="store_true", help="rejeu pas-à-pas (X6)")

    g = sub.add_parser("gen", help="engendrer les fixtures")
    g.add_argument("scenario", nargs="?", default="")
    g.add_argument("--all", action="store_true")
    g.add_argument("--out", default="fixtures")

    v = sub.add_parser("view", help="SVG du graphe d'attente")
    v.add_argument("trace")
    v.add_argument("--out", default="-")
    return p


def _parse_whatif(items: list[str]) -> list[tuple[int, int]]:
    out = []
    for raw in items:
        tid, _, prio = raw.partition("=")
        if not _:
            raise SystemExit(f"--whatif attendu TID=PRIO, reçu {raw!r}")
        out.append((int(tid), int(prio)))
    return out


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    # `--out` n'est un fichier que pour `view` ; pour `gen` c'est un répertoire.
    if args.cmd == "gen" or getattr(args, "out", "-") in (None, "-"):
        return _dispatch(args, sys.stdout)
    with open(args.out, "w", encoding="utf-8") as out:
        return _dispatch(args, out)


def _dispatch(args: argparse.Namespace, out) -> int:
    from replique import commands

    if args.cmd == "dump":
        return commands.dump(args.trace, out, as_json=args.json)
    if args.cmd == "analyze":
        return commands.analyze(args.trace, out, as_json=args.json,
                                whatif=_parse_whatif(args.whatif))
    if args.cmd == "replay":
        return commands.replay(args.trace, out, as_json=args.json,
                               only_hash=getattr(args, "only_hash", False),
                               step=args.step, whatif=_parse_whatif(args.whatif))
    if args.cmd == "gen":
        return commands.gen(args.scenario, args.out, all_scenarios=args.all)
    if args.cmd == "view":
        return commands.view(args.trace, out)
    raise SystemExit(f"commande inconnue : {args.cmd}")


if __name__ == "__main__":
    raise SystemExit(main())
