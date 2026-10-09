import json
import threading
import unittest
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from server import Bridge, make_handler


class FakeCms(BaseHTTPRequestHandler):
    def do_GET(self):
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        value = lambda key: query.get(key, [""])[0]
        if value("ac") == "list":
            body = {"class": [{"type_id": 1, "type_name": "电影"}]}
        elif value("ids"):
            body = {"list": [{
                "vod_id": value("ids"), "vod_name": "测试片",
                "vod_play_from": "直链", "vod_play_url":
                "第一集$https://media.example/one.m3u8#第二集$https://media.example/two.html"
            }]}
        else:
            body = {"page": 1, "pagecount": 1,
                    "list": [{"vod_id": "raw-vod-1", "vod_name": "测试片"}]}
        encoded = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def log_message(self, *_):
        pass


class BridgeIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cms = ThreadingHTTPServer(("127.0.0.1", 0), FakeCms)
        cls.cms_thread = threading.Thread(target=cls.cms.serve_forever, daemon=True)
        cls.cms_thread.start()
        config = {"sites": [{"key": "test", "name": "Test", "type": 1,
                             "api": "http://127.0.0.1:{}/api.php?ac=list".format(cls.cms.server_port)}]}
        cls.bridge = ThreadingHTTPServer(("127.0.0.1", 0),
                                         make_handler(Bridge(config), "http://127.0.0.1"))
        cls.bridge_thread = threading.Thread(target=cls.bridge.serve_forever, daemon=True)
        cls.bridge_thread.start()
        cls.base = "http://127.0.0.1:{}".format(cls.bridge.server_port)

    @classmethod
    def tearDownClass(cls):
        cls.bridge.shutdown()
        cls.cms.shutdown()
        cls.bridge.server_close()
        cls.cms.server_close()

    def fetch(self, path):
        with urllib.request.urlopen(self.base + path) as response:
            return json.load(response)

    def test_search_detail_and_playback(self):
        config = self.fetch("/config.json")
        self.assertEqual(config["sites"][0]["type"], 1000)
        self.assertEqual(self.fetch("/api/sites/test/categories")["class"][0]["type_name"], "电影")
        found = self.fetch("/api/sites/test/search?q=" + urllib.parse.quote("测试") + "&page=1")
        vod = found["list"][0]
        self.assertNotEqual(vod["vod_id"], "raw-vod-1")
        detail = self.fetch("/api/sites/test/detail?id=" + vod["vod_id"])["list"][0]
        episodes = detail["vod_play_url"].split("#")
        first_id = episodes[0].split("$", 1)[1]
        play = self.fetch("/api/sites/test/play?flag=" + urllib.parse.quote("直链") + "&id=" + first_id)
        self.assertEqual(play["url"], "https://media.example/one.m3u8")
        self.assertNotIn("raw-vod-1", json.dumps(detail))
        second_id = episodes[1].split("$", 1)[1]
        with self.assertRaises(urllib.error.HTTPError) as error:
            self.fetch("/api/sites/test/play?flag=" + urllib.parse.quote("直链") + "&id=" + second_id)
        self.assertEqual(error.exception.code, 422)


if __name__ == "__main__":
    unittest.main()
