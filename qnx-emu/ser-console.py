#!/usr/bin/env python3
"""ser-console.py — ligne de commande vers l'invité QNX, par la console série.

Avec `guest-qemu.sh --serie-socket <sock>` (ou QNX_EMS_SERIE_SOCK), l'invité
expose sa console série 1 sur un socket UNIX (QEMU `-chardev socket`). Ce
script s'y connecte, envoie une commande, et affiche la sortie jusqu'à un
marqueur de fin. Il permet de PILOTER l'invité depuis l'hôte — `pidin`, un
scénario, figer une trace — sans reconstruire l'image ni toucher l'écran VGA.

Usage :
    ser-console.py 'pidin'                 # exécute et affiche
    ser-console.py --file cmd.sh           # exécute un fichier de commandes
    ser-console.py --raw 'texte'           # envoie brut, sans attendre
    ser-console.py --timeout 30 'cmd'      # délai d'attente (secondes)
    ser-console.py --socket /chemin.sock 'cmd'

Socket par défaut : $QNX_EMS_SERIE_SOCK, sinon ./ser.sock.
Stdlib seule.
"""
import argparse
import os
import socket
import sys
import time

DEFAULT_SOCK = os.environ.get("QNX_EMS_SERIE_SOCK", "ser.sock")
MARK = "__ZC_END__"


def connect(path, timeout):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(path)
    return s


def drain(sock, seconds):
    """Lit ce qui arrive pendant `seconds` (le tampon peut être ancien)."""
    end = time.time() + seconds
    out = b""
    while time.time() < end:
        try:
            part = sock.recv(65536)
        except socket.timeout:
            break
        except OSError:
            break
        if not part:
            break
        out += part
    return out


def run(sock, command, timeout):
    """Envoie `command` et rend (sortie, code de retour)."""
    sock.sendall((command + "; echo " + MARK + "$?\n").encode())
    buf = b""
    deadline = time.time() + timeout
    code = None
    while time.time() < deadline:
        try:
            part = sock.recv(65536)
        except socket.timeout:
            break
        except OSError:
            break
        if not part:
            break
        buf += part
        # Le marqueur est émis deux fois : l'echo de la commande, puis sa sortie.
        if buf.count(MARK.encode()) >= 2:
            tail = buf.split(MARK.encode())[-1]
            digits = b""
            for ch in tail:
                if 48 <= ch <= 57:
                    digits += bytes([ch])
                else:
                    break
            if digits:
                code = int(digits)
                break
    return buf, code


def clean(buf, command):
    """Retire l'echo de la commande et le marqueur, pour un affichage propre."""
    text = buf.decode("utf-8", "replace")
    lines = []
    for line in text.replace("\r\n", "\n").split("\n"):
        if MARK in line:
            line = line.split(MARK)[0]
            if line.strip() == "":
                continue
        lines.append(line)
    # La première ligne reprise est l'echo de la commande envoyée.
    while lines and lines[0].strip() in ("", command.strip()):
        lines.pop(0)
    return "\n".join(lines).rstrip()


def run_file(sock, path, timeout):
    with open(path) as fh:
        commands = [ln for ln in fh.read().split("\n")
                    if ln.strip() and not ln.strip().startswith("#")]
    for cmd in commands:
        buf, _ = run(sock, cmd, timeout)
        print(clean(buf, cmd))
        print("---")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("command", nargs="?", help="commande à exécuter dans l'invité")
    ap.add_argument("--file", help="fichier de commandes (une par ligne)")
    ap.add_argument("--raw", action="store_true", help="envoie sans attendre la fin")
    ap.add_argument("--socket", default=DEFAULT_SOCK, help="chemin du socket série")
    ap.add_argument("--timeout", type=float, default=20.0, help="délai d'attente (s)")
    ap.add_argument("--settle", type=float, default=0.5, help="pause après envoi (s)")
    args = ap.parse_args()

    if not os.path.exists(args.socket):
        sys.stderr.write("socket absent : %s\n" % args.socket)
        return 2
    try:
        sock = connect(args.socket, args.timeout)
    except OSError as exc:
        sys.stderr.write("connexion impossible : %s\n" % exc)
        return 2

    try:
        drain(sock, 0.3)
        if args.raw:
            sock.sendall((args.command or "").encode())
            time.sleep(args.settle)
            sys.stdout.write(drain(sock, args.settle).decode("utf-8", "replace"))
            return 0
        if args.file:
            return run_file(sock, args.file, args.timeout)
        if not args.command:
            sys.stderr.write("rien à faire : donne une commande\n")
            return 2
        buf, code = run(sock, args.command, args.timeout)
        print(clean(buf, args.command))
        return code if code is not None else 0
    finally:
        sock.close()


if __name__ == "__main__":
    sys.exit(main())