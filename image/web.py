#!/usr/bin/env python3
"""web.py — vitrine HTTP de l'émulateur QNX 8.0 (Réplique).

Sert, depuis le serveur r630 qui héberge l'invité QEMU :
    GET  /               page HTML (écran + console + boutons de touches)
    GET  /screen.png     capture d'écran VGA fraîche (via le moniteur QEMU)
    GET  /console        le journal de la console série (texte brut)
    GET  /key?k=RET      envoie une touche à l'invité (sendkey du moniteur)

Stdlib seule (http.server + sockets AF_UNIX + zlib). Aucune dépendance.
"""
import http.server
import os
import socket
import struct
import sys
import time
import zlib

DIR = os.path.expanduser("~/replique-demo")
MON = os.path.join(DIR, "mon-kernel.sock")
SERIAL = os.path.join(DIR, "serial-kernel.log")
VGA_PPM = os.path.join(DIR, "web-vga.ppm")
QEMU_BIN = os.path.join(os.path.expanduser("~/qnx71-vm/qprefix/usr/bin/qemu-system-x86_64"))
QEMU_LIB = os.path.expanduser("~/qnx71-vm/qprefix")

KEYS = {
    "ret": b"ret", "spc": b"spc", "esc": b"esc", "up": b"up", "down": b"down",
    "l": b"l", "c": b"c", "cr": b"ctrl-c", "tab": b"tab", "backspace": b"backspace",
}


def qemu_monitor(cmd):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(3)
    try:
        s.connect(MON)
    except OSError:
        return None
    try:
        s.recv(4096)
    except OSError:
        pass
    s.sendall(cmd + b"\n")
    time.sleep(0.3)
    out = b""
    try:
        while True:
            part = s.recv(4096)
            if not part:
                break
            out += part
    except OSError:
        pass
    s.close()
    return out


def screendump():
    qemu_monitor(b"screendump " + VGA_PPM.encode())
    time.sleep(0.5)


def ppm2png(ppm_path):
    with open(ppm_path, "rb") as fh:
        data = fh.read()
    parts, idx = [], 0
    while len(parts) < 4:
        while data[idx:idx+1].isspace():
            idx += 1
        if data[idx:idx+1] == b"#":
            while data[idx:idx+1] not in (b"\n", b""):
                idx += 1
            continue
        start = idx
        while not data[idx:idx+1].isspace():
            idx += 1
        parts.append(data[start:idx])
    idx += 1
    w, h = int(parts[1]), int(parts[2])
    raw = data[idx:idx + w*h*3]
    rows = b"".join(b"\x00" + raw[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 6))
            + chunk(b"IEND", b""))


PAGE = """<!doctype html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Démo QNX 8.0 — Réplique</title>
<style>
body{margin:0;background:#0e1016;color:#e7e9ee;font:15px/1.5 system-ui,sans-serif}
header{padding:1rem;border-bottom:1px solid #262b38;background:#151923}
header b{color:#7fd1c1}
main{display:grid;grid-template-columns:minmax(0,1.2fr) minmax(0,1fr);gap:1rem;padding:1rem;max-width:1200px;margin:0 auto}
@media(max-width:820px){main{grid-template-columns:1fr}}
.panel{background:#151923;border:1px solid #262b38;border-radius:8px;overflow:hidden}
.panel h2{margin:0;padding:.6rem .8rem;font-size:.95rem;background:#1b2030;border-bottom:1px solid #262b38}
#screen{width:100%;height:340px;object-fit:contain;background:#000}
#console{height:440px;overflow:auto;font:12px/1.45 ui-monospace,monospace;white-space:pre-wrap;padding:.6rem;color:#c7e7c0}
.btns{display:flex;flex-wrap:wrap;gap:.4rem;padding:.6rem .8rem}
button{background:#232a3c;color:#e7e9ee;border:1px solid #38415a;border-radius:6px;padding:.35rem .6rem;cursor:pointer}
button:hover{background:#2d3852}
.meta{color:#8a93a6;padding:.2rem .8rem .6rem;font-size:.8rem}
#age{color:#8a93a6}
</style></head><body>
<header><b>Réplique</b> — émulateur QNX 8.0 en direct <span id="age"></span></header>
<main>
<div class="panel"><h2>Écran de l'invité (capture VGA)</h2>
<img id="screen" src="/screen.png" alt="écran QNX"><div class="meta">rafraîchi toutes les 2 s — la démo Déclic tourne au boot</div></div>
<div class="panel"><h2>Console série</h2>
<div class="btns">
<button data-k="ret">Entrée</button><button data-k="spc">Espace</button>
<button data-k="up">↑</button><button data-k="down">↓</button>
<button data-k="c">c</button><button data-k="l">l</button>
<button data-k="cr">Ctrl-C</button><button data-k="tab">Tab</button>
</div><pre id="console">connexion…</pre></div>
</main>
<script>
function tick(){var i=document.getElementById('screen');i.src='/screen.png?t='+Date.now();
 fetch('/console').then(function(r){return r.text()}).then(function(t){document.getElementById('console').textContent=t;var c=document.getElementById('console');c.scrollTop=c.scrollHeight;});
 fetch('/age').then(function(r){return r.text()}).then(function(t){document.getElementById('age').textContent=t;});}
document.querySelectorAll('button').forEach(function(b){b.onclick=function(){fetch('/key?k='+b.dataset.k);};});
setInterval(tick,2000);tick();
</script></body></html>"""


def age():
    if not os.path.exists(SERIAL):
        return "— invité absent"
    try:
        sec = time.time() - os.path.getmtime(SERIAL)
        return "(console fraîche, %d s)" % int(sec)
    except OSError:
        return ""


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, ctype, body):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        # La page de demo est hebergee ailleurs (GitHub Pages) : le navigateur
        # doit pouvoir lire la console depuis l autre origine.
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "*")
        if ctype == "image/png":
            self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        from urllib.parse import urlparse, parse_qs
        path = urlparse(self.path).path
        q = parse_qs(urlparse(self.path).query)
        if path == "/screen.png":
            screendump()
            try:
                self._send(200, "image/png", ppm2png(VGA_PPM))
            except OSError:
                self._send(503, "text/plain", b"ecran indisponible")
        elif path == "/console":
            body = open(SERIAL, "rb").read() if os.path.exists(SERIAL) else b""
            self._send(200, "text/plain; charset=utf-8", body)
        elif path == "/age":
            self._send(200, "text/plain", age().encode())
        elif path == "/key":
            key = q.get("k", [""])[0].strip()
            name = KEYS.get(key, key.lower().encode())
            qemu_monitor(b"sendkey " + (name if isinstance(name, bytes) else name.encode()))
            self._send(200, "text/plain", b"ok")
        elif path == "/" or path == "/index.html":
            self._send(200, "text/html; charset=utf-8", PAGE.encode())
        else:
            self._send(404, "text/plain", b"introuvable")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    http.server.ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()
