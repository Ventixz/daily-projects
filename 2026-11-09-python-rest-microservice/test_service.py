import json
import threading
import unittest
import urllib.error
import urllib.request

import service


class ServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = service.make_server(0)
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def call(self, method, path, body=None, raw=None):
        data = raw if raw is not None else (None if body is None else json.dumps(body).encode())
        req = urllib.request.Request(self.base + path, data=data, method=method)
        try:
            with urllib.request.urlopen(req) as r:
                text = r.read()
                return r.status, json.loads(text) if text else None
        except urllib.error.HTTPError as e:
            text = e.read()
            return e.code, json.loads(text) if text else None

    def test_health(self):
        self.assertEqual(self.call("GET", "/health"), (200, {"status": "ok"}))

    def test_create_and_get(self):
        status, note = self.call("POST", "/notes", {"title": "  Hi ", "tags": ["a"]})
        self.assertEqual(status, 201)
        self.assertEqual((note["title"], note["body"], note["tags"]), ("Hi", "", ["a"]))
        self.assertEqual(self.call("GET", f"/notes/{note['id']}"), (200, note))

    def test_update_and_delete(self):
        _, note = self.call("POST", "/notes", {"title": "old"})
        status, updated = self.call("PUT", f"/notes/{note['id']}", {"title": "new", "body": "b"})
        self.assertEqual((status, updated["title"], updated["id"]), (200, "new", note["id"]))
        self.assertEqual(self.call("DELETE", f"/notes/{note['id']}")[0], 204)
        self.assertEqual(self.call("GET", f"/notes/{note['id']}")[0], 404)
        self.assertEqual(self.call("DELETE", f"/notes/{note['id']}")[0], 404)

    def test_tag_filter(self):
        self.call("POST", "/notes", {"title": "x", "tags": ["filter-me"]})
        self.call("POST", "/notes", {"title": "y", "tags": ["other"]})
        _, notes = self.call("GET", "/notes?tag=filter-me")
        self.assertTrue(notes and all("filter-me" in n["tags"] for n in notes))

    def test_validation_errors(self):
        for body in ({}, {"title": ""}, {"title": 5}, {"title": "t", "tags": "x"}, {"title": "t", "body": 1}, [1]):
            self.assertEqual(self.call("POST", "/notes", body)[0], 400, body)
        self.assertEqual(self.call("POST", "/notes", raw=b"{not json")[0], 400)
        self.assertEqual(self.call("PUT", "/notes/1", {"nope": 1})[0], 400)

    def test_not_found_and_method_not_allowed(self):
        self.assertEqual(self.call("GET", "/nothing")[0], 404)
        self.assertEqual(self.call("PUT", "/notes/9999", {"title": "t"})[0], 404)
        self.assertEqual(self.call("DELETE", "/notes")[0], 405)
        self.assertEqual(self.call("POST", "/notes/1", {"title": "t"})[0], 405)

    def test_ids_not_reused(self):
        _, a = self.call("POST", "/notes", {"title": "a"})
        self.call("DELETE", f"/notes/{a['id']}")
        _, b = self.call("POST", "/notes", {"title": "b"})
        self.assertGreater(b["id"], a["id"])


class StoreTests(unittest.TestCase):
    def test_concurrent_creates_get_unique_ids(self):
        store = service.NoteStore()
        ids = []
        def work():
            for _ in range(200):
                ids.append(store.create({"title": "t", "body": "", "tags": []})["id"])
        threads = [threading.Thread(target=work) for _ in range(8)]
        [t.start() for t in threads]; [t.join() for t in threads]
        self.assertEqual(len(set(ids)), 1600)


if __name__ == "__main__":
    unittest.main()
