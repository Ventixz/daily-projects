# Learning: Build a RESTful Microservice in Python

**Source:** "Build a RESTful Microservice in Python" (skybert.net) in the Python / Web Applications section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Scope: a stdlib-only JSON CRUD API for notes (my own implementation; no Flask or other dependencies).

Run: `python3 service.py 8000`, then e.g. `curl -X POST localhost:8000/notes -d '{"title":"hi","tags":["a"]}'`.
Test: `python3 -m unittest -v`.

## Layout

- `service.py` — `NoteStore` (thread-safe in-memory store), `validate_note`, a routing handler built on `http.server`, and `make_server`.
- `test_service.py` — 8 tests that boot a real server on a free port and hit it over HTTP, plus a concurrency test of the store.

## What I learned

- **REST is mostly disciplined status codes:** 201 on create, 204 with no body on delete, 400 for bad input, 404 for a missing resource, 405 for a known path with the wrong method.
- **Validate at the boundary and return the reason.** One `validate_note` function turns any bad payload into a 400 with a message, so handlers never see malformed data.
- **A threaded server needs a locked store.** `ThreadingHTTPServer` handles requests concurrently; without the lock, ids could collide. The test hammers the store from 8 threads and checks 1600 unique ids.
- **Never reuse ids.** A monotonic counter stops a client from reading a deleted note's id and getting someone else's data.
- **Binding to port 0 makes server tests simple:** the OS picks a free port, so tests run in parallel without clashes and exercise the real HTTP stack.
- **Return copies from the store** so callers can't mutate stored state by accident.

## Known limits

- Data lives in memory and is lost on restart; no persistence, auth, pagination or `PATCH`.
- Hand-rolled routing with a regex is fine for two routes but would not scale; that's the point where a framework earns its keep.
