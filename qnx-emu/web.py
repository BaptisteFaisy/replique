#!/usr/bin/env python3
"""web.py — vitrine HTTP d'un invité QNX 8.0 (QEMU).

Généralisation de la vitrine de la démo Réplique : plus rien n'est codé en dur,
tout passe par l'environnement.

    GET  /               page HTML (écran VGA + console + touches)
    GET  /screen.png     capture fraîche de l'écran VGA (moniteur QEMU)
    GET  /console        journal de la console série (texte brut)
    GET  /age            âge du journal (état de fraîcheur)
    GET  /key?k=RET      injecte une touche dans l'invité (sendkey du moniteur)

Variables d'environnement :

    QNX_EMS_SERIE   journal de la console série        (défaut : serial.log)
    QNX_EMS_MON     socket du moniteur QEMU (AF_UNIX)   (défaut : mon.sock)
    QNX_EMS_PPM     capture VGA produite par screendump (défaut : vga.ppm)
    QNX_EMS_PORT    port d'écoute                       (défaut : 8091)
    QNX_EMS_HOST    adresse d'écoute                    (défaut : 0.0.0.0)
    QNX_EMS_TITRE   titre affiché dans la page          (défaut : Démo QNX 8.0)
    QNX_EMS_NOTE    ligne de sous-titre de la page

Stdlib seule (http.server + sockets AF_UNIX + zlib). Aucune dépendance.
Le CORS est ouvert : la page de démo vit sur une autre origine (le site du
dossier QNX), elle doit donc pouvoir lire la console et l'écran.
"""
import http.server
import os
import socket
import struct
import time
import zlib

SERIE = os.environ.get("QNX_EMS_SERIE", "serial.log")
MON = os.environ.get("QNX_EMS_MON", "mon.sock")
PPM = os.environ.get("QNX_EMS_PPM", "vga.ppm")
PORT = int(os.environ.get("QNX_EMS_PORT", "8091"))
HOST = os.environ.get("QNX_EMS_HOST", "0.0.0.0")
TITRE = os.environ.get("QNX_EMS_TITRE", "Démo QNX 8.0")
NOTE = os.environ.get("QNX_EMS_NOTE", "invité QNX 8.0 (QEMU) — écran VGA en direct")

# sendkey : noms du moniteur QEMU pour les touches de la page.
KEYS = {
    "ret": b"ret", "spc": b"spc", "esc": b"esc", "up": b"up", "down": b"down",
    "l": b"l", "c": b"c", "cr": b"ctrl-c", "tab": b"tab", "backspace": b"backspace",
}


# --------------------------------------------------------------- moniteur QEMU
def qemu_monitor(cmd):
    """Envoie une commande au moniteur QEMU (socket AF_UNIX) et rend sa sortie."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(3)
    try:
        s.connect(MON)
    except OSError:
        s.close()
        return None
    try:
        s.recv(4096)
    except OSError:
        pass
    try:
        s.sendall(cmd + b"\n")
        time.sleep(0.3)
        out = b""
        while True:
            part = s.recv(4096)
            if not part:
                break
            out += part
    except OSError:
        out = b""
    s.close()
    return out


def screendump():
    """Demande au moniteur une capture de l'écran VGA (format PPM)."""
    qemu_monitor(b"screendump " + PPM.encode())
    time.sleep(0.5)


# --------------------------------------------------------------- PPM -> PNG
def _entetes_ppm(data):
    """Lit l'en-tête d'un PPM binaire (P6) : rend (largeur, hauteur, offset)."""
    parts, idx = [], 0
    while len(parts) < 4:
        while data[idx:idx + 1].isspace():
            idx += 1
        if data[idx:idx + 1] == b"#":
            while data[idx:idx + 1] not in (b"\n", b""):
                idx += 1
            continue
        start = idx
        while not data[idx:idx + 1].isspace():
            idx += 1
        parts.append(data[start:idx])
    return int(parts[1]), int(parts[2]), idx + 1


def ppm2png(ppm_path):
    """Convertit le PPM de screendump en PNG (sans dépendance : zlib + struct)."""
    with open(ppm_path, "rb") as fh:
        data = fh.read()
    w, h, offset = _entetes_ppm(data)
    raw = data[offset:offset + w * h * 3]
    rows = b"".join(b"\x00" + raw[y * w * 3:(y + 1) * w * 3] for y in range(h))
    return (b"\x89PNG\r\n\x1a\n"
            + _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + _chunk(b"IDAT", zlib.compress(rows, 6))
            + _chunk(b"IEND", b""))


def _chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


# --------------------------------------------------------------- page
PAGE = """<!doctype html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>__TITRE__</title>
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
<header><b>__TITRE__</b> — __NOTE__ <span id="age"></span></header>
<main>
<div class="panel"><h2>Écran de l'invité (capture VGA)</h2>
<img id="screen" src="/screen.png" alt="écran QNX"><div class="meta">rafraîchi toutes les 2 s</div></div>
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
document.querySelectorAll('button').forEach(function(b){b.onclick=function(){fetch('/key?k='+encodeURIComponent(b.dataset.k));};});
setInterval(tick,2000);tick();
</script></body></html>"""

PAGE_HTML = PAGE.replace("__TITRE__", TITRE).replace("__NOTE__", NOTE).encode()


def age():
    """Âge du journal série : « absent » si l'invité n'écrit rien."""
    if not os.path.exists(SERIE):
        return "— invité absent"
    try:
        return "(console fraîche, %d s)" % int(time.time() - os.path.getmtime(SERIE))
    except OSError:
        return ""


# --------------------------------------------------------------- serveur
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, code, ctype, body):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        # La page de démo est hébergée ailleurs (site du dossier QNX) : le
        # navigateur doit pouvoir lire la console depuis l'autre origine.
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "*")
        if ctype == "image/png":
            self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _ecran(self):
        screendump()
        try:
            self._send(200, "image/png", ppm2png(PPM))
        except OSError:
            self._send(503, "text/plain", b"ecran indisponible")

    def _console(self):
        try:
            body = open(SERIE, "rb").read()
        except OSError:
            body = b""
        self._send(200, "text/plain; charset=utf-8", body)

    def _touche(self, cle):
        nom = KEYS.get(cle, cle.lower().encode())
        qemu_monitor(b"sendkey " + (nom if isinstance(nom, bytes) else nom.encode()))
        self._send(200, "text/plain", b"ok")

    def do_GET(self):
        from urllib.parse import urlparse, parse_qs
        url = urlparse(self.path)
        chemin = url.path
        if chemin == "/screen.png":
            self._ecran()
        elif chemin == "/console":
            self._console()
        elif chemin == "/age":
            self._send(200, "text/plain", age().encode())
        elif chemin == "/key":
            self._touche(parse_qs(url.query).get("k", [""])[0].strip())
        elif chemin in ("/", "/index.html"):
            self._send(200, "text/html; charset=utf-8", PAGE_HTML)
        else:
            self._send(404, "text/plain", b"introuvable")


def main():
    http.server.ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
