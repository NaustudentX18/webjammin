#!/usr/bin/env python3
"""WeBjammin — serve a chart directory as a WeBjammin catalogue.

    python3 scripts/catalogue_server.py --charts ~/webjammin/songs --port 8000

Then point the device at it:

    "catalogue": { "index_url": "http://<host>:8000/catalogue.json" }

Every chart in the directory becomes one catalogue entry; the .chart/.mid/.wj.json
file is served as the chart download and any audio file next to it (ogg/mp3/wav)
is served as the audio download.

Only charts you host are listed — nothing is scraped from anywhere.
"""

import argparse
import json
import os
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CHART_EXTS = (".chart", ".mid", ".midi", ".wj.json")
AUDIO_EXTS = (".ogg", ".mp3", ".wav", ".opus")


def humanise(name: str) -> str:
    stem = re.sub(r"\.(wj\.json|chart|mid|midi)$", "", name, flags=re.IGNORECASE)
    stem = stem.replace("_", " ").replace("-", " ").strip()
    return stem.title()


def build_index(charts_dir: str) -> dict:
    songs = []
    for root, _dirs, files in os.walk(charts_dir):
        for name in sorted(files):
            lower = name.lower()
            if not lower.endswith(CHART_EXTS):
                continue
            chart_path = os.path.join(root, name)
            rel = os.path.relpath(chart_path, charts_dir)
            entry = {
                "id": re.sub(r"[^A-Za-z0-9_.-]+", "_", rel),
                "title": humanise(name),
                "artist": "",
                "charter": "",
                "difficulty": "Normal",
                "lane_count": 5,
                "chart_url": f"/files/{rel}",  # made absolute per request
                "license": "see the host",
            }
            # An audio file with the same stem next to the chart.
            stem = os.path.splitext(chart_path)[0]
            for ext in AUDIO_EXTS:
                cand = stem + ext
                if os.path.exists(cand):
                    entry["audio_url"] = "/files/" + os.path.relpath(cand, charts_dir)
                    break
            songs.append(entry)
    return {"format": "webjammin-catalogue", "songs": songs}


class Handler(BaseHTTPRequestHandler):
    charts_dir = "."
    index = {}

    def log_message(self, fmt, *args):  # quieter log
        sys.stderr.write("catalogue: " + (fmt % args) + "\n")

    def _send(self, code: int, body: bytes, ctype: str):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _absolute(self, rel: str) -> str:
        host = self.headers.get("Host", "localhost")
        scheme = "http"
        return f"{scheme}://{host}{rel}"

    def do_GET(self):  # noqa: N802
        if self.path in ("/catalogue.json", "/index.json", "/"):
            index = json.loads(json.dumps(self.index))
            for song in index.get("songs", []):
                for key in ("chart_url", "audio_url"):
                    if song.get(key):
                        song[key] = self._absolute(song[key])
            body = json.dumps(index, indent=2).encode()
            self._send(200, body, "application/json")
            return
        if self.path.startswith("/files/"):
            rel = self.path[len("/files/"):]
            full = os.path.normpath(os.path.join(self.charts_dir, rel))
            if not full.startswith(os.path.abspath(self.charts_dir)) or not os.path.isfile(full):
                self._send(404, b"not found", "text/plain")
                return
            with open(full, "rb") as fh:
                body = fh.read()
            ctype = "application/json" if full.endswith(".json") else "application/octet-stream"
            self._send(200, body, ctype)
            return
        self._send(404, b"not found", "text/plain")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--charts", default="songs", help="directory with the charts")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--host", default="0.0.0.0")
    args = ap.parse_args()

    Handler.charts_dir = os.path.abspath(args.charts)
    Handler.index = build_index(Handler.charts_dir)
    songs = Handler.index["songs"]
    print(f"catalogue: {len(songs)} song(s) from {Handler.charts_dir}")
    for s in songs:
        print(f"  - {s['title']}  ->  {s['chart_url']}")

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"serving on http://{args.host}:{args.port}/catalogue.json")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
