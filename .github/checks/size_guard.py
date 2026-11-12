#!/usr/bin/env python3
"""size_guard.py — garde de taille du dépôt Réplique (CONTRACT.md §7).

Règles (identiques sur tout le portefeuille) :
  * tout fichier de code < 500 LIGNES PHYSIQUES ;
  * toute fonction < 50 LIGNES DE CODE (lignes ni vides ni commentaire seul),
    accolades d'ouverture et de fermeture comprises ; les prototypes et les
    déclarations anticipées ne sont pas des fonctions.

Périmètre suivi : core/, host/, target/, tests/, .github/ (*.py, *.sh, *.yml),
Makefile. Ignorés : LICENSE, *.md, fixtures/**, scenarios/**, build/**.
Sortie : une ligne par infraction, puis un résumé ; code de retour 1 si infraction.
"""
from __future__ import annotations

import ast
import os
import re
import sys

MAX_FILE_LINES = 500
MAX_FUNCTION_LINES = 50
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SKIP_DIRS = {".git", "build", "fixtures", "scenarios", "__pycache__", "node_modules"}
CODE_EXTS = {".c", ".h", ".py", ".sh", ".yml", ".yaml"}
C_FUNC_START = re.compile(r"^[A-Za-z_][A-Za-z0-9_ \t\*]*\(")


def tracked_files() -> list[str]:
    """Fichiers de code suivis, triés (ordre déterministe pour la sortie)."""
    found = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in sorted(filenames):
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, ROOT)
            if name == "Makefile" or os.path.splitext(name)[1] in CODE_EXTS:
                found.append(rel)
    return sorted(found)


def read_lines(rel: str) -> list[str]:
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8", errors="replace") as fh:
        return fh.read().splitlines()


def strip_c_comments(lines: list[str]) -> list[str]:
    """Retire les commentaires C (// et /* */) pour ne compter que le code."""
    out, in_block = [], False
    for line in lines:
        text, i, kept = "", 0, []
        while i < len(line):
            two = line[i:i + 2]
            if in_block:
                if two == "*/":
                    in_block, i = False, i + 2
                else:
                    i += 1
                continue
            if two == "//":
                break
            if two == "/*":
                in_block, i = True, i + 2
                continue
            kept.append(line[i])
            i += 1
        text = "".join(kept).strip()
        out.append(text)
    return out


def code_lines_c(lines: list[str]) -> list[int]:
    """Indices (0-based) des lignes de code d'un fichier C."""
    stripped = strip_c_comments(lines)
    return [i for i, text in enumerate(stripped) if text]


def code_lines_py(lines: list[str]) -> list[int]:
    """Indices des lignes de code d'un fichier Python (hors # et vides)."""
    return [i for i, raw in enumerate(lines)
            if raw.strip() and not raw.strip().startswith("#")]


SKIP_STARTS = ("#", "}", "typedef", "struct", "enum", "union", "static const")


def c_functions(lines: list[str]) -> list[tuple[str, int, int]]:
    """(nom, ligne d'ouverture, ligne de fermeture) de chaque fonction C DÉFINIE.

    Une déclaration (prototypes d'en-tête, déclarations anticipées) se termine
    par « ; » avant toute accolade : elle est ignorée. Le corps est borné par
    comptage d'accolades depuis la ligne qui ouvre le bloc.
    """
    functions = []
    i, n = 0, len(lines)
    while i < n:
        line = lines[i]
        if (not line or line[0].isspace() or line.startswith(SKIP_STARTS)
                or not C_FUNC_START.match(line)):
            i += 1
            continue
        end = i
        while end < n and end - i < 8 and "{" not in lines[end] \
                and ";" not in lines[end]:
            end += 1
        if end >= n or end - i >= 8 or "{" not in lines[end]:
            i = end + 1
            continue
        name = line.split("(")[0].split()[-1].lstrip("*")
        depth, j = 0, end
        while j < n:
            depth += lines[j].count("{") - lines[j].count("}")
            if depth <= 0:
                break
            j += 1
        functions.append((name, end, min(j, n - 1)))
        i = j + 1
    return functions


def check_c(rel: str, lines: list[str]) -> list[str]:
    problems = []
    code = set(code_lines_c(lines))
    for name, start, end in c_functions(lines):
        count = len([i for i in range(start, end + 1) if i in code])
        if count > MAX_FUNCTION_LINES:
            problems.append(f"{rel}:{start + 1}: {count} lignes (max "
                            f"{MAX_FUNCTION_LINES}) — fonction {name}()")
    return problems


def check_python(rel: str, lines: list[str]) -> list[str]:
    problems = []
    code = set(code_lines_py(lines))
    try:
        tree = ast.parse("\n".join(lines))
    except SyntaxError as exc:
        return [f"{rel}:{exc.lineno or 1}: fichier illisible — {exc.msg}"]
    for node in ast.walk(tree):
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        end = getattr(node, "end_lineno", node.lineno)
        count = len([i for i in range(node.lineno, end) if i in code])
        if count > MAX_FUNCTION_LINES:
            problems.append(f"{rel}:{node.lineno}: {count} lignes (max "
                            f"{MAX_FUNCTION_LINES}) — fonction {node.name}()")
    return problems


def main() -> int:
    problems = []
    checked = 0
    for rel in tracked_files():
        lines = read_lines(rel)
        checked += 1
        if len(lines) >= MAX_FILE_LINES:
            problems.append(f"{rel}:1: {len(lines)} lignes (max "
                            f"{MAX_FILE_LINES - 1}) — fichier")
        if rel.endswith(".py"):
            problems.extend(check_python(rel, lines))
        elif rel.endswith((".c", ".h")):
            problems.extend(check_c(rel, lines))
    for line in problems:
        print(line)
    if problems:
        print(f"size_guard : {len(problems)} infraction(s) sur {checked} fichier(s)")
        return 1
    print(f"size_guard OK — {checked} fichier(s), < {MAX_FILE_LINES} lignes, "
          f"fonctions < {MAX_FUNCTION_LINES} lignes de code")
    return 0


if __name__ == "__main__":
    sys.exit(main())
