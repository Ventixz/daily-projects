"""A small RESTful microservice (stdlib only): a JSON CRUD API for notes.

Routes:
  GET    /health          liveness probe
  GET    /notes           list notes (optional ?tag=x filter)
  POST   /notes           create  {"title": str, "body": str?, "tags": [str]?}
  GET    /notes/<id>      fetch one
  PUT    /notes/<id>      replace (same schema as POST)
  DELETE /notes/<id>      remove
"""
import json
import re
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse


class ValidationError(Exception):
    pass


class NoteStore:
    """Thread-safe in-memory store; the lock matters because the server is threaded."""

    def __init__(self):
        self._notes = {}
        self._next_id = 1
        self._lock = threading.Lock()

    def list(self, tag=None):
        with self._lock:
            notes = [dict(n) for n in self._notes.values()]
        return [n for n in notes if tag is None or tag in n["tags"]]

    def get(self, note_id):
        with self._lock:
            note = self._notes.get(note_id)
            return dict(note) if note else None

    def create(self, fields):
        with self._lock:
            note = {"id": self._next_id, **fields}
            self._notes[note["id"]] = note
            self._next_id += 1
            return dict(note)

    def replace(self, note_id, fields):
        with self._lock:
            if note_id not in self._notes:
                return None
            self._notes[note_id] = {"id": note_id, **fields}
            return dict(self._notes[note_id])

    def delete(self, note_id):
        with self._lock:
            return self._notes.pop(note_id, None) is not None


def validate_note(data):
    """Return a clean field dict or raise ValidationError."""
    if not isinstance(data, dict):
        raise ValidationError("body must be a JSON object")
    title = data.get("title")
    if not isinstance(title, str) or not title.strip():
        raise ValidationError("title is required and must be a non-empty string")
    body = data.get("body", "")
    if not isinstance(body, str):
        raise ValidationError("body must be a string")
    tags = data.get("tags", [])
    if not isinstance(tags, list) or not all(isinstance(t, str) for t in tags):
        raise ValidationError("tags must be a list of strings")
    return {"title": title.strip(), "body": body, "tags": tags}


NOTE_PATH = re.compile(r"^/notes/(\d+)$")


def make_handler(store):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):  # keep test output quiet
            pass

        def _send(self, status, payload=None):
            body = b"" if payload is None else json.dumps(payload).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _read_json(self):
            try:
                length = int(self.headers.get("Content-Length", 0))
                return json.loads(self.rfile.read(length) or b"null")
            except (ValueError, json.JSONDecodeError):
                raise ValidationError("request body is not valid JSON")

        def _route(self, method):
            url = urlparse(self.path)
            match = NOTE_PATH.match(url.path)
            try:
                if url.path == "/health" and method == "GET":
                    return self._send(200, {"status": "ok"})
                if url.path == "/notes" and method == "GET":
                    tag = parse_qs(url.query).get("tag", [None])[0]
                    return self._send(200, store.list(tag))
                if url.path == "/notes" and method == "POST":
                    return self._send(201, store.create(validate_note(self._read_json())))
                if match:
                    note_id = int(match.group(1))
                    if method == "GET":
                        note = store.get(note_id)
                    elif method == "PUT":
                        note = store.replace(note_id, validate_note(self._read_json()))
                    elif method == "DELETE":
                        return self._send(204) if store.delete(note_id) else self._send(404, {"error": "not found"})
                    else:
                        return self._send(405, {"error": "method not allowed"})
                    return self._send(200, note) if note else self._send(404, {"error": "not found"})
                if url.path in ("/health", "/notes"):
                    return self._send(405, {"error": "method not allowed"})
                return self._send(404, {"error": "not found"})
            except ValidationError as e:
                return self._send(400, {"error": str(e)})

        def do_GET(self): self._route("GET")
        def do_POST(self): self._route("POST")
        def do_PUT(self): self._route("PUT")
        def do_DELETE(self): self._route("DELETE")

    return Handler


def make_server(port=0, store=None):
    """port=0 picks a free port (used by the tests)."""
    return ThreadingHTTPServer(("127.0.0.1", port), make_handler(store or NoteStore()))


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    server = make_server(port)
    print(f"Listening on http://127.0.0.1:{server.server_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.server_close()
