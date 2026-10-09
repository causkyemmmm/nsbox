"""Small LAN bridge for the Switch TVBox UI (Python 3.8+, stdlib only).

Each configured MacCMS site is exposed through the CatVod-shaped JSON endpoints
consumed by BridgeProvider. Other crawler kinds must provide their own adapter.
"""

import argparse
import json
import re
import secrets
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


MEDIA_EXTENSIONS = {".m3u8", ".mp4", ".mkv", ".avi", ".mov", ".flv", ".ts", ".m4v", ".webm"}
SITE_KEY = re.compile(r"^[A-Za-z0-9_-]+$")
MAX_UPSTREAM_BYTES = 4 * 1024 * 1024


class BridgeError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


class TokenStore:
    """Unpredictable, short-lived IDs keep source URLs and credentials out of UI URLs."""

    def __init__(self):
        self._values = {}
        self._lock = threading.Lock()

    def put(self, value):
        token = secrets.token_urlsafe(18)
        with self._lock:
            now = time.monotonic()
            if len(self._values) > 20000:
                self._values = {k: v for k, v in self._values.items() if v[0] > now}
            self._values[token] = (now + 6 * 3600, value)
        return token

    def get(self, token):
        with self._lock:
            item = self._values.get(token)
            if item is None or item[0] <= time.monotonic():
                raise BridgeError(404, "Item expired; reload the page")
            return item[1]


class MacCmsSite:
    def __init__(self, config, tokens):
        self.key = config["key"]
        self.name = config["name"]
        self.api = config["api"]
        self.headers = {"User-Agent": config.get("user_agent", "Mozilla/5.0")}
        if config.get("referer"):
            self.headers["Referer"] = config["referer"]
        self.tokens = tokens

    def _request(self, **query):
        parsed = urllib.parse.urlsplit(self.api)
        existing = [(key, value) for key, value in
                    urllib.parse.parse_qsl(parsed.query, keep_blank_values=True)
                    if key not in query]
        url = urllib.parse.urlunsplit(parsed._replace(query=urllib.parse.urlencode(existing + list(query.items()))))
        request = urllib.request.Request(url, headers=self.headers)
        try:
            with urllib.request.urlopen(request, timeout=15) as response:
                if response.status != 200:
                    raise BridgeError(502, "Upstream HTTP error")
                raw = response.read(MAX_UPSTREAM_BYTES + 1)
                if len(raw) > MAX_UPSTREAM_BYTES:
                    raise BridgeError(502, "Upstream response too large")
                charset = response.headers.get_content_charset() or "utf-8"
                try:
                    return json.loads(raw.decode(charset))
                except (LookupError, UnicodeDecodeError, json.JSONDecodeError):
                    raise BridgeError(502, "Upstream returned invalid JSON")
        except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError):
            raise BridgeError(502, "Upstream request failed")

    def categories(self):
        data = self._request(ac="list")
        categories = data.get("class") if isinstance(data, dict) else None
        if not isinstance(categories, list):
            raise BridgeError(502, "Upstream categories missing")
        return {"class": categories}

    def _page(self, data):
        items = data.get("list") if isinstance(data, dict) else None
        if not isinstance(items, list):
            raise BridgeError(502, "Upstream list missing")
        result = dict(data)
        result["list"] = []
        for item in items:
            if not isinstance(item, dict) or "vod_id" not in item:
                continue
            entry = dict(item)
            entry["vod_id"] = self.tokens.put((self.key, "vod", str(item["vod_id"])))
            result["list"].append(entry)
        return result

    def list(self, tid, page):
        return self._page(self._request(ac="detail", t=tid, pg=page))

    def search(self, keyword, page):
        return self._page(self._request(ac="detail", wd=keyword, pg=page))

    def detail(self, original_id):
        data = self._request(ac="detail", ids=original_id)
        items = data.get("list") if isinstance(data, dict) else None
        if not isinstance(items, list) or not items or not isinstance(items[0], dict):
            raise BridgeError(502, "Upstream detail missing")
        entry = dict(items[0])
        names = str(entry.get("vod_play_from", "")).split("$$$")
        lines = str(entry.get("vod_play_url", "")).split("$$$")
        if len(names) != len(lines) or not entry.get("vod_play_url"):
            raise BridgeError(502, "Upstream playback lines invalid")
        converted = []
        for flag, line in zip(names, lines):
            episodes = []
            for item in line.split("#"):
                if not item:
                    continue
                label, sep, raw_id = item.partition("$")
                if not sep:
                    raw_id = label
                if not label or not raw_id:
                    raise BridgeError(502, "Upstream episode invalid")
                token = self.tokens.put((self.key, "episode", flag, raw_id))
                episodes.append(label + "$" + token)
            converted.append("#".join(episodes))
        entry["vod_id"] = self.tokens.put((self.key, "vod", original_id))
        entry["vod_play_url"] = "$$$".join(converted)
        return {"list": [entry]}

    def play(self, flag, original_id):
        url = original_id
        parsed = urllib.parse.urlsplit(url)
        extension = Path(parsed.path).suffix.lower()
        if parsed.scheme not in ("http", "https") or extension not in MEDIA_EXTENSIONS:
            raise BridgeError(422, "Site returned a page or unsupported episode ID")
        return {"url": url, "header": self.headers}


class Bridge:
    def __init__(self, config):
        self.tokens = TokenStore()
        self.sites = {}
        for entry in config.get("sites", []):
            if not isinstance(entry, dict) or entry.get("type") != 1:
                raise ValueError("Only configured MacCMS type 1 sites are supported")
            key = entry.get("key", "")
            api = entry.get("api", "")
            if not SITE_KEY.fullmatch(key) or key in self.sites:
                raise ValueError("Site keys must be unique ASCII letters, digits, _ or -")
            if urllib.parse.urlsplit(api).scheme not in ("http", "https"):
                raise ValueError("Site API must be HTTP(S)")
            self.sites[key] = MacCmsSite(entry, self.tokens)

    def site(self, key):
        if key not in self.sites:
            raise BridgeError(404, "Unknown site")
        return self.sites[key]

    def config(self, base_url):
        return {"sites": [
            {"key": site.key, "name": site.name, "type": 1000,
             "api": base_url + "/api/sites/" + site.key,
             "searchable": 1, "quickSearch": 1, "filterable": 0}
            for site in self.sites.values()
        ]}


def make_handler(bridge, public_base):
    class Handler(BaseHTTPRequestHandler):
        def _respond(self, status, payload):
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            try:
                if len(self.path) > 4096:
                    raise BridgeError(414, "Request too long")
                parsed = urllib.parse.urlsplit(self.path)
                query = urllib.parse.parse_qs(parsed.query)
                get = lambda name, default="": query.get(name, [default])[0]
                if parsed.path == "/health":
                    payload = {"status": "ok", "sites": len(bridge.sites)}
                elif parsed.path == "/config.json":
                    payload = bridge.config(public_base)
                else:
                    match = re.fullmatch(r"/api/sites/([A-Za-z0-9_-]+)/(categories|list|search|detail|play)", parsed.path)
                    if match is None:
                        raise BridgeError(404, "Unknown endpoint")
                    site = bridge.site(match.group(1))
                    action = match.group(2)
                    if action == "categories":
                        payload = site.categories()
                    elif action in ("list", "search"):
                        try:
                            page = int(get("page", "1"))
                        except ValueError:
                            raise BridgeError(400, "Invalid page")
                        if not 1 <= page <= 10000:
                            raise BridgeError(400, "Invalid page")
                        if action == "list":
                            payload = site.list(get("tid"), page)
                        else:
                            keyword = get("q").strip()
                            if not keyword:
                                raise BridgeError(400, "Empty search")
                            payload = site.search(keyword, page)
                    elif action == "detail":
                        item = bridge.tokens.get(get("id"))
                        if len(item) != 3:
                            raise BridgeError(400, "Wrong item")
                        key, kind, original_id = item
                        if key != site.key or kind != "vod":
                            raise BridgeError(400, "Wrong item")
                        payload = site.detail(original_id)
                    else:
                        item = bridge.tokens.get(get("id"))
                        if len(item) != 4:
                            raise BridgeError(400, "Wrong episode")
                        key, kind, flag, original_id = item
                        if key != site.key or kind != "episode" or flag != get("flag"):
                            raise BridgeError(400, "Wrong episode")
                        payload = site.play(flag, original_id)
                self._respond(200, payload)
            except BridgeError as error:
                self._respond(error.status, {"error": str(error)})
            except Exception:
                self._respond(500, {"error": "Internal bridge error"})

    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, help="JSON file listing approved MacCMS sites")
    parser.add_argument("--listen", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8090)
    parser.add_argument("--public-base", default="http://192.168.50.161:8090")
    args = parser.parse_args()
    bridge = Bridge(json.loads(Path(args.config).read_text(encoding="utf-8")))
    server = ThreadingHTTPServer((args.listen, args.port), make_handler(bridge, args.public_base.rstrip("/")))
    print("N1 bridge listening on {}:{} ({} sites)".format(args.listen, args.port, len(bridge.sites)), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
