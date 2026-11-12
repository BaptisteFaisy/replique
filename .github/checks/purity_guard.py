#!/usr/bin/env python3
"""purity_guard.py — garde de pureté du dépôt Réplique (CONTRACT.md §7).

Règles :
  * core/src et core/include : C11 PUR — aucun fichier, aucune horloge, aucun
    socket, aucun en-tête QNX, aucune allocation dynamique. `<stdio.h>` n'est
    toléré que dans core/src/report.c (vsnprintf, sortie canonique).
  * core/tests : les pilotes de différentiel et les tests ont droit à
    stdio/stdlib/fopen/malloc — ce sont des PILOTES, pas le cœur.
  * host/**/*.py : stdlib seule (sys.stdlib_module_names) ou paquet interne.
Sortie : `chemin:ligne: <symbole> — <règle>` ; code de retour 1 si infraction.
"""
from __future__ import annotations

import ast
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORE_DIRS = ("core/src", "core/include")
STDIO_ALLOWED = os.path.join("core", "src", "report.c")

FORBIDDEN_INCLUDES = (
    "<time.h>", "<sys/time.h>", "<sys/socket.h>", "<netdb.h>", "<unistd.h>",
    "<pthread.h>", "<sys/kernel.h>", "<sys/neutrino.h>", "<sys/procnto.h>",
    "<process.h>", "<ham/ham.h>", "<stdlib.h>",
)
FORBIDDEN_CALLS = (
    "fopen", "fclose", "fread", "fwrite", "open", "socket", "time", "clock",
    "clock_gettime", "gettimeofday", "rand", "srand", "random", "malloc",
    "calloc", "realloc", "free", "getenv", "system",
)
CALL_RE = re.compile(r"\b(" + "|".join(FORBIDDEN_CALLS) + r")\s*\(")
INCLUDE_RE = re.compile(r"^\s*#\s*include\s*(<[^>]+>|\"[^\"]+\")")
INTERNAL_PREFIXES = ("replique",)


def core_files() -> list[str]:
    found = []
    for sub in CORE_DIRS:
        base = os.path.join(ROOT, sub)
        for dirpath, _dirnames, filenames in os.walk(base):
            for name in sorted(filenames):
                if name.endswith((".c", ".h")):
                    found.append(os.path.relpath(os.path.join(dirpath, name), ROOT))
    return sorted(found)


def host_files() -> list[str]:
    found = []
    base = os.path.join(ROOT, "host")
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = [d for d in dirnames if d != "__pycache__"]
        for name in sorted(filenames):
            if name.endswith(".py"):
                found.append(os.path.relpath(os.path.join(dirpath, name), ROOT))
    return sorted(found)


def strip_comments(line: str) -> str:
    """Neutralise les commentaires pour ne pas signaler du texte documentaire."""
    index = line.find("//")
    if index >= 0:
        line = line[:index]
    index = line.find("/*")
    if index >= 0:
        line = line[:index]
    return line


def check_core(rel: str) -> list[str]:
    problems = []
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.read().splitlines()
    for number, raw in enumerate(lines, start=1):
        line = strip_comments(raw)
        match = INCLUDE_RE.match(line)
        if match:
            header = match.group(1)
            if header == "<stdio.h>":
                if rel != STDIO_ALLOWED:
                    problems.append(f"{rel}:{number}: <stdio.h> — cœur pur : "
                                    f"stdio réservé à core/src/report.c")
                continue
            if header in FORBIDDEN_INCLUDES:
                problems.append(f"{rel}:{number}: {header} — cœur pur : ni "
                                f"horloge, ni socket, ni QNX, ni allocation")
        for call in CALL_RE.findall(line):
            problems.append(f"{rel}:{number}: {call}() — cœur pur : aucun "
                            f"fichier, horloge, socket ni allocation (CONTRACT §7)")
    return problems


def check_host(rel: str) -> list[str]:
    problems = []
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8", errors="replace") as fh:
        source = fh.read()
    try:
        tree = ast.parse(source)
    except SyntaxError as exc:
        return [f"{rel}:{exc.lineno or 1}: fichier illisible — {exc.msg}"]
    allowed = set(sys.stdlib_module_names)
    for node in ast.walk(tree):
        names = []
        if isinstance(node, ast.Import):
            names = [alias.name.split(".")[0] for alias in node.names]
        elif isinstance(node, ast.ImportFrom):
            if node.level:
                continue                      # import relatif interne
            names = [(node.module or "").split(".")[0]]
        for name in names:
            if not name:
                continue
            if name in allowed or name.startswith(INTERNAL_PREFIXES):
                continue
            problems.append(f"{rel}:{node.lineno}: import {name} — hôte : "
                            f"stdlib seule, aucune dépendance tierce")
    return problems


def main() -> int:
    problems = []
    for rel in core_files():
        problems.extend(check_core(rel))
    for rel in host_files():
        problems.extend(check_host(rel))
    for line in problems:
        print(line)
    if problems:
        print(f"purity_guard : {len(problems)} infraction(s)")
        return 1
    print("purity_guard OK — core/ sans I/O, horloge, socket ni allocation ; "
          "host/ en stdlib seule")
    return 0


if __name__ == "__main__":
    sys.exit(main())
