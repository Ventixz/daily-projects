use crate::http::{ParseError, Request, Response};
use crate::store::{PutOutcome, Store};
use std::io::BufReader;
use std::net::{TcpListener, TcpStream};
use std::sync::Arc;
use std::thread;

/// Binds `addr` and serves forever. A thin wrapper around `serve` so tests
/// can bind an OS-assigned port themselves (`serve` takes an already-bound
/// `TcpListener`) instead of guessing at a free one.
pub fn run(addr: &str, store: Arc<Store>) -> std::io::Result<()> {
    let listener = TcpListener::bind(addr)?;
    println!("listening on {}", listener.local_addr()?);
    serve(listener, store)
}

/// One thread per accepted connection, each with its own clone of the
/// `Arc<Store>`. There's no connection pool cap here on purpose -- see
/// LEARNING.md's scope-cuts section for why that's fine for this project
/// but wouldn't be for a public-facing service.
pub fn serve(listener: TcpListener, store: Arc<Store>) -> std::io::Result<()> {
    for stream in listener.incoming() {
        let stream = stream?;
        let store = Arc::clone(&store);
        thread::spawn(move || handle_connection(stream, store));
    }
    Ok(())
}

fn handle_connection(stream: TcpStream, store: Arc<Store>) {
    let peer = stream.peer_addr().ok();
    let cloned = match stream.try_clone() {
        Ok(cloned) => cloned,
        Err(e) => {
            eprintln!("failed to clone stream for {peer:?}: {e}");
            return;
        }
    };
    let mut reader = BufReader::new(cloned);
    let mut writer = stream;

    let request = match Request::parse(&mut reader) {
        Ok(Some(request)) => request,
        Ok(None) => return,
        Err(ParseError::Malformed(reason)) => {
            let _ = Response::new(400, "Bad Request")
                .with_body(reason.as_bytes().to_vec())
                .write(&mut writer);
            return;
        }
        Err(ParseError::Io(e)) => {
            eprintln!("read error from {peer:?}: {e}");
            return;
        }
    };

    let response = route(&request, &store);
    if let Err(e) = response.write(&mut writer) {
        eprintln!("write error to {peer:?}: {e}");
    }
}

/// Maps a request to a store operation. Split out from `handle_connection`
/// so routing can be unit tested against a `Store` directly, with no socket
/// in the loop.
fn route(request: &Request, store: &Store) -> Response {
    let segments: Vec<&str> = request
        .path
        .trim_start_matches('/')
        .split('/')
        .filter(|s| !s.is_empty())
        .collect();

    match (request.method.as_str(), segments.as_slice()) {
        ("GET", ["items"]) => list_items(store),
        ("POST", ["items"]) => create_item(request, store),
        ("GET", ["items", id]) => with_id(id, |id| get_item(id, store)),
        ("PUT", ["items", id]) => with_id(id, |id| put_item(id, request, store)),
        ("DELETE", ["items", id]) => with_id(id, |id| delete_item(id, store)),
        _ => Response::new(404, "Not Found"),
    }
}

/// Parses the `{id}` path segment before dispatching, so a garbled id (not
/// missing, not wrong-typed but present and non-numeric) answers 400, not
/// 404. Folding the two into "not found" would tell a client "there's
/// nothing at this URL" when the real problem is "this URL is malformed" --
/// different problems, different fixes on the client's end.
fn with_id(raw: &str, handler: impl FnOnce(u64) -> Response) -> Response {
    match raw.parse::<u64>() {
        Ok(id) => handler(id),
        Err(_) => Response::new(400, "Bad Request")
            .with_body(b"id must be a non-negative integer".to_vec()),
    }
}

fn list_items(store: &Store) -> Response {
    let body = json_array(&store.list());
    Response::new(200, "OK")
        .with_header("content-type", "application/json")
        .with_body(body)
}

fn create_item(request: &Request, store: &Store) -> Response {
    let id = store.create(request.body.clone());
    Response::new(201, "Created")
        .with_header("location", format!("/items/{id}"))
        .with_header("content-type", "application/json")
        .with_body(format!("{{\"id\":{id}}}").into_bytes())
}

fn get_item(id: u64, store: &Store) -> Response {
    match store.get(id) {
        Some(value) => Response::new(200, "OK")
            .with_header("content-type", "application/octet-stream")
            .with_body(value),
        None => Response::new(404, "Not Found"),
    }
}

fn put_item(id: u64, request: &Request, store: &Store) -> Response {
    match store.put(id, request.body.clone()) {
        PutOutcome::Created => {
            Response::new(201, "Created").with_header("location", format!("/items/{id}"))
        }
        PutOutcome::Replaced => Response::new(200, "OK"),
    }
}

fn delete_item(id: u64, store: &Store) -> Response {
    if store.delete(id) {
        Response::new(204, "No Content")
    } else {
        Response::new(404, "Not Found")
    }
}

fn json_array(items: &[(u64, Vec<u8>)]) -> Vec<u8> {
    let mut out = Vec::new();
    out.push(b'[');
    for (i, (id, value)) in items.iter().enumerate() {
        if i > 0 {
            out.push(b',');
        }
        out.extend_from_slice(format!("{{\"id\":{id},\"value\":").as_bytes());
        out.extend_from_slice(&json_string(value));
        out.push(b'}');
    }
    out.push(b']');
    out
}

/// A minimal JSON string encoder for arbitrary bytes: escapes the
/// characters JSON requires, and replaces anything outside printable ASCII
/// with `?` rather than emitting invalid JSON for binary values the store
/// never validated in the first place.
fn json_string(raw: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(raw.len() + 2);
    out.push(b'"');
    for &b in raw {
        match b {
            b'"' => out.extend_from_slice(b"\\\""),
            b'\\' => out.extend_from_slice(b"\\\\"),
            b'\n' => out.extend_from_slice(b"\\n"),
            b'\r' => out.extend_from_slice(b"\\r"),
            b'\t' => out.extend_from_slice(b"\\t"),
            0x20..=0x7e => out.push(b),
            _ => out.push(b'?'),
        }
    }
    out.push(b'"');
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn req(method: &str, path: &str, body: &[u8]) -> Request {
        Request {
            method: method.to_string(),
            path: path.to_string(),
            headers: Vec::new(),
            body: body.to_vec(),
        }
    }

    #[test]
    fn unknown_path_is_404() {
        let store = Store::new();
        let response = route(&req("GET", "/nope", b""), &store);
        assert_eq!(response.status, 404);
    }

    #[test]
    fn non_numeric_id_is_400_not_404() {
        let store = Store::new();
        let response = route(&req("GET", "/items/abc", b""), &store);
        assert_eq!(response.status, 400);
    }

    #[test]
    fn full_lifecycle_through_route() {
        let store = Store::new();

        let created = route(&req("POST", "/items", b"hi"), &store);
        assert_eq!(created.status, 201);
        assert!(created
            .headers
            .contains(&("location".to_string(), "/items/1".to_string())));

        let fetched = route(&req("GET", "/items/1", b""), &store);
        assert_eq!(fetched.status, 200);
        assert_eq!(fetched.body, b"hi");

        let replaced = route(&req("PUT", "/items/1", b"bye"), &store);
        assert_eq!(replaced.status, 200);

        let deleted = route(&req("DELETE", "/items/1", b""), &store);
        assert_eq!(deleted.status, 204);

        let gone = route(&req("GET", "/items/1", b""), &store);
        assert_eq!(gone.status, 404);
    }

    #[test]
    fn json_string_escapes_quotes_and_backslashes() {
        let encoded = json_string(br#"say "hi"\now"#);
        assert_eq!(encoded, br#""say \"hi\"\\now""#);
    }

    #[test]
    fn json_string_replaces_non_printable_bytes() {
        let encoded = json_string(&[b'a', 0x00, b'b']);
        assert_eq!(encoded, b"\"a?b\"");
    }
}
