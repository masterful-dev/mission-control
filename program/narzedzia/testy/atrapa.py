#!/usr/bin/env python3
"""Atrapa serwera Centrum: zapisuje kazde zlecenie do pliku, stan podaje ze wzorca. Modele nigdy nie startuja naprawde."""
import http.server, json, os, sys, time
D = os.path.dirname(os.path.abspath(__file__))
S = json.load(open(os.path.join(D, "stan-wzor.json")))
L = open(os.path.join(D, "atrapa.log"), "w", buffering=1)
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def odp(self, kod, o):
        b = json.dumps(o).encode(); self.send_response(kod); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def akcja(self):
        n = int(self.headers.get("Content-Length") or 0); b = self.rfile.read(n).decode() if n else ""
        L.write(f"{self.command} {self.path} {b}\n")
        if self.path.endswith("/log?n=200"): return self.odp(200, {"lines": ["linia 1", "linia 2"]})
        if self.path.startswith("/api/import-desktop") and self.command == "GET": return self.odp(200, [{"id": "x1", "name": "Ikona testowa", "script": "/tmp/x", "args": []}])
        self.odp(200, {"ok": True})
    def do_GET(self):
        if self.path == "/api/state": S["t"] = time.time(); return self.odp(200, S)
        self.akcja()
    do_POST = do_PUT = do_DELETE = akcja
http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
