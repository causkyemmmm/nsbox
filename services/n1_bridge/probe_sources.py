"""Check plain TVBox type-1 feeds before enabling them in the N1 bridge."""

import argparse
import json
import urllib.parse
import urllib.request
from pathlib import PurePosixPath

from server import MEDIA_EXTENSIONS


def request(api, **query):
    parsed = urllib.parse.urlsplit(api)
    original = [(key, value) for key, value in
                urllib.parse.parse_qsl(parsed.query, keep_blank_values=True)
                if key not in query]
    url = urllib.parse.urlunsplit(parsed._replace(
        query=urllib.parse.urlencode(original + list(query.items()))))
    req = urllib.request.Request(url, headers={"User-Agent": "okhttp/3.15"})
    with urllib.request.urlopen(req, timeout=8) as response:
        return json.loads(response.read(512 * 1024).decode("utf-8"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("config", help="Downloaded plain TVBox JSON config")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    with open(args.config, encoding="utf-8") as stream:
        source = json.load(stream)
    working = []
    for index, site in enumerate(source.get("sites", []), 1):
        if site.get("type") != 1 or not site.get("api", "").startswith(("http://", "https://")):
            continue
        try:
            categories = request(site["api"], ac="list")
            search = request(site["api"], ac="detail", wd="西游记", pg=1)
            if not isinstance(categories.get("class"), list) or not isinstance(search.get("list"), list):
                raise ValueError("missing class/list")
            if not search["list"]:
                raise ValueError("no search sample")
            detail = request(site["api"], ac="detail", ids=search["list"][0]["vod_id"])
            if not isinstance(detail.get("list"), list) or not detail["list"]:
                raise ValueError("missing detail")
            play_urls = detail["list"][0].get("vod_play_url", "")
            episodes = [part.partition("$")[2] for line in play_urls.split("$$$")
                        for part in line.split("#") if "$" in part]
            if not any(urllib.parse.urlsplit(url).scheme in ("http", "https") and
                       PurePosixPath(urllib.parse.urlsplit(url).path).suffix.lower() in MEDIA_EXTENSIONS
                       for url in episodes):
                raise ValueError("no direct media episode")
        except Exception as error:
            print("skip {}: {}".format(index, type(error).__name__), flush=True)
            continue
        working.append({"key": "source_{}".format(index), "name": site.get("name", "Source {}".format(index)),
                        "type": 1, "api": site["api"], "user_agent": "okhttp/3.15"})
        print("ok {}: {}".format(index, site.get("key", "")), flush=True)

    with open(args.output, "w", encoding="utf-8") as stream:
        json.dump({"sites": working}, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print("enabled {} sites".format(len(working)), flush=True)


if __name__ == "__main__":
    main()
