# N1 bridge: first runnable slice

This service lets the existing Switch TVBox screens browse MacCMS type 1 sites through N1. It uses Python's standard library only. It does **not** execute CatVod JAR, JS, Python crawlers, scrape web player pages, or bypass site login. Only configured sites are advertised to the Switch.

## Local or N1 setup

1. Confirm Python 3.8+ is available on N1 with `python3 --version`.
2. Copy this directory to N1 and copy `sites.example.json` to `sites.json`, or use the checked `sites.n1.json` as a starting point.
3. Replace the example site with an available MacCMS JSON API you are allowed to use. Keep `key` unique and restricted to ASCII letters, digits, `_` and `-`.
4. Run `python3 server.py --config sites.json --listen 0.0.0.0 --port 8090 --public-base http://192.168.50.161:8090`.
5. Verify `http://192.168.50.161:8090/health` and `http://192.168.50.161:8090/config.json` from another device on the LAN.
6. In switch-tvbox, set **Settings → 数据源配置** to `http://192.168.50.161:8090/config.json`, then reload the home screen.

If N1 only has Python 2 but Docker is available, build and run the included
Dockerfile on N1 instead. Mount `sites.json` as a read-only file; it is not
copied into the image:

```sh
docker build -t switch-tvbox-bridge .
docker run -d --name switch-tvbox-bridge --restart unless-stopped \
  -p 192.168.50.161:8090:8090 \
  -v "$(pwd)/sites.json:/app/sites.json:ro" switch-tvbox-bridge
```

The Docker build needs access to the Python 3.11 Alpine base image. Verify
`docker logs switch-tvbox-bridge` and `http://192.168.50.161:8090/health`
before entering the URL on Switch.

## Current N1 deployment (2026-10-09)

The service is running at `http://192.168.50.161:8090`. Its config URL is
`http://192.168.50.161:8090/config.json`. Docker stores its files under
`/mnt/usbaa/switch-tvbox/bridge` and runs `switch-tvbox-bridge:local` with
the `sites.json` file mounted read-only. The two enabled MacCMS sites are
recorded in `sites.n1.json`. From the LAN, `/health` reports two sites.

Both enabled sites returned categories, search results, detail and episodes.
One episode per site returned an `.m3u8` URL; media bytes and playback on a
Switch have not been tested. One candidate was removed because its detail
request failed. The tested FanTaiYing backup feed contained 48 type-3 crawler
sites and no MacCMS sites, so this bridge does not yet support that feed.

The Switch uses its current search, detail, line and episode views. Clicking an episode calls N1 `/play`, which returns a direct media URL and request headers. Video data goes directly from the media host to Switch. A source that returns a web page or a crawler-only episode ID gets a clear error until a real adapter is implemented.

The bridge generates short-lived opaque IDs for videos and episodes. They are held in memory for six hours; after a service restart, reload the search/detail page. The service is intended for a trusted LAN. Do not expose port 8090 to the Internet, and do not put cookies or tokens in `sites.json` until the service has explicit credential storage and access control.

## Local check

Run `python3 -m unittest -v test_server` in this directory. The test starts a local fake CMS and exercises the bridge through HTTP, including a successful episode and rejection of a web player page.

## Next adapters

MacCMS is the first source family. Add JS/JAR/Python adapters on N1 only after verifying each runtime's required host APIs. The adapter must produce the same categories, page, detail and playback shape; do not advertise a site in `/config.json` before all required operations work. The existing T3 investigation in `../../docs/site-rules/gaps.md` records unresolved sites.
