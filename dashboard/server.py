#!/usr/bin/env python3
"""
BME680 HIL — live web dashboard server.

Owns the debug-probe UART, parses the firmware's [METRIC]/[SUMMARY] lines,
and serves a small self-hosted dashboard (HTML + JSON API) on localhost.

Because only one process can hold the serial port at a time, do NOT run this
while test/harness.py is capturing — stop one before starting the other.

Start/stop:  see dashboard/dashboardctl.sh  (start | stop | status | restart)
Manual run:  uv run --with pyserial python dashboard/server.py

Env overrides:
    BME680_PORT       serial port (default: auto-detect /dev/cu.usbmodem*)
    BME680_BAUD       baud rate   (default: 115200)
    BME680_HTTP_HOST  bind host   (default: 127.0.0.1)
    BME680_HTTP_PORT  http port   (default: 8080)
"""

import glob
import json
import os
import re
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BAUD = int(os.environ.get("BME680_BAUD", "115200"))
HTTP_HOST = os.environ.get("BME680_HTTP_HOST", "127.0.0.1")
HTTP_PORT = int(os.environ.get("BME680_HTTP_PORT", "8080"))
HISTORY_LEN = 300  # ~25 min at one metric / 5 s

HERE = os.path.dirname(os.path.abspath(__file__))
INDEX_HTML = os.path.join(HERE, "index.html")

# --- shared state (guarded by _lock) -------------------------------------
_lock = threading.Lock()
_state = {
    "connected": False,
    "port": None,
    "latest": None,     # dict of last [METRIC]
    "summary": None,    # dict of last [SUMMARY]
    "history": deque(maxlen=HISTORY_LEN),
    "last_line_ts": 0.0,
}

_KV_RE = re.compile(r"(\w+)=(-?\d+(?:\.\d+)?)")


def _detect_port():
    env = os.environ.get("BME680_PORT")
    if env:
        return env
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def _parse_kv(line):
    """Parse 'key=value key2=value2' into a dict of floats/ints."""
    out = {}
    for k, v in _KV_RE.findall(line):
        out[k] = float(v) if "." in v else int(v)
    return out


def _serial_worker():
    """Background thread: read the UART forever, reconnecting as needed."""
    import serial  # imported here so the module loads even w/o pyserial for --help

    while True:
        port = _detect_port()
        if not port:
            with _lock:
                _state["connected"] = False
                _state["port"] = None
            time.sleep(2)
            continue
        try:
            with serial.Serial(port, BAUD, timeout=2) as ser:
                with _lock:
                    _state["connected"] = True
                    _state["port"] = port
                while True:
                    raw = ser.readline()
                    if not raw:
                        continue
                    line = raw.decode("utf-8", errors="replace").strip()
                    if not line:
                        continue
                    now = time.time()
                    if line.startswith("[METRIC]"):
                        d = _parse_kv(line)
                        if d.get("read_ok") == 1:
                            with _lock:
                                _state["latest"] = d
                                _state["last_line_ts"] = now
                                _state["history"].append({
                                    "t": now,
                                    "temp": d.get("temp"),
                                    "hum": d.get("hum"),
                                    "press": d.get("press"),
                                    "gas": d.get("gas"),
                                    "iaq": d.get("iaq"),
                                })
                    elif line.startswith("[SUMMARY]"):
                        d = _parse_kv(line)
                        with _lock:
                            _state["summary"] = d
                            _state["last_line_ts"] = now
        except Exception as e:
            with _lock:
                _state["connected"] = False
            # brief backoff before retrying (probe unplugged, port busy, etc.)
            time.sleep(2)


def _snapshot():
    with _lock:
        stale = (time.time() - _state["last_line_ts"]) > 15 if _state["last_line_ts"] else True
        return {
            "connected": _state["connected"] and not stale,
            "port": _state["port"],
            "latest": _state["latest"],
            "summary": _state["summary"],
            "history": list(_state["history"]),
            "server_time": time.time(),
        }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass  # silence per-request logging

    def _send(self, code, body, ctype):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/api/data"):
            body = json.dumps(_snapshot()).encode("utf-8")
            self._send(200, body, "application/json")
            return
        if self.path in ("/", "/index.html"):
            try:
                with open(INDEX_HTML, "rb") as f:
                    body = f.read()
                self._send(200, body, "text/html; charset=utf-8")
            except FileNotFoundError:
                self._send(500, b"index.html not found", "text/plain")
            return
        self._send(404, b"not found", "text/plain")


def main():
    t = threading.Thread(target=_serial_worker, daemon=True)
    t.start()
    srv = ThreadingHTTPServer((HTTP_HOST, HTTP_PORT), Handler)
    print(f"BME680 dashboard on http://{HTTP_HOST}:{HTTP_PORT}  (serial: {_detect_port() or 'auto'})")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()


if __name__ == "__main__":
    main()
