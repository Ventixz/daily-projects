use rust_microservice::server;
use rust_microservice::store::Store;
use std::collections::HashSet;
use std::io::{Read, Write};
use std::net::{Shutdown, SocketAddr, TcpListener, TcpStream};
use std::sync::Arc;
use std::thread;
use std::time::Duration;

fn spawn_server() -> SocketAddr {
    let listener = TcpListener::bind("127.0.0.1:0").expect("bind ephemeral port");
    let addr = listener.local_addr().expect("local addr");
    let store = Arc::new(Store::new());
    thread::spawn(move || {
        let _ = server::serve(listener, store);
    });
    addr
}

/// Sends a raw HTTP request over a fresh connection and returns the whole
/// response as a string. The server closes every connection after one
/// response (see LEARNING.md), so reading to EOF is exactly "read the whole
/// response" -- no framing logic needed on the test side.
fn request(addr: SocketAddr, raw: &str) -> String {
    let mut stream = TcpStream::connect(addr).expect("connect");
    stream
        .set_read_timeout(Some(Duration::from_secs(2)))
        .expect("set read timeout");
    stream.write_all(raw.as_bytes()).expect("write request");
    stream
        .shutdown(Shutdown::Write)
        .expect("shutdown write half");
    let mut response = String::new();
    stream.read_to_string(&mut response).expect("read response");
    response
}

fn location_header(response: &str) -> &str {
    response
        .lines()
        .find(|line| line.to_ascii_lowercase().starts_with("location:"))
        .expect("response should carry a location header")
}

#[test]
fn full_crud_lifecycle_over_real_sockets() {
    let addr = spawn_server();

    let created = request(
        addr,
        "POST /items HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello",
    );
    assert!(created.starts_with("HTTP/1.1 201 Created"), "{created}");
    assert_eq!(location_header(&created).trim(), "location: /items/1");

    let fetched = request(addr, "GET /items/1 HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    assert!(fetched.starts_with("HTTP/1.1 200 OK"), "{fetched}");
    assert!(fetched.ends_with("hello"), "{fetched}");

    let updated = request(
        addr,
        "PUT /items/1 HTTP/1.1\r\nContent-Length: 5\r\n\r\nworld",
    );
    assert!(updated.starts_with("HTTP/1.1 200 OK"), "{updated}");

    let refetched = request(addr, "GET /items/1 HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    assert!(refetched.ends_with("world"), "{refetched}");

    let deleted = request(
        addr,
        "DELETE /items/1 HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
    );
    assert!(deleted.starts_with("HTTP/1.1 204 No Content"), "{deleted}");

    let missing = request(addr, "GET /items/1 HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
    assert!(missing.starts_with("HTTP/1.1 404 Not Found"), "{missing}");
}

#[test]
fn concurrent_posts_never_collide_on_an_id() {
    let addr = spawn_server();

    let handles: Vec<_> = (0..32)
        .map(|i| {
            thread::spawn(move || {
                let body = format!("item-{i}");
                let raw = format!(
                    "POST /items HTTP/1.1\r\nContent-Length: {}\r\n\r\n{}",
                    body.len(),
                    body
                );
                request(addr, &raw)
            })
        })
        .collect();

    let mut locations = HashSet::new();
    for handle in handles {
        let response = handle.join().expect("client thread panicked");
        assert!(response.starts_with("HTTP/1.1 201 Created"), "{response}");
        locations.insert(location_header(&response).to_string());
    }
    assert_eq!(
        locations.len(),
        32,
        "expected 32 distinct ids, got collisions"
    );
}

#[test]
fn put_reserves_its_id_against_future_posts() {
    let addr = spawn_server();

    request(
        addr,
        "PUT /items/500 HTTP/1.1\r\nContent-Length: 4\r\n\r\nseed",
    );
    let created = request(addr, "POST /items HTTP/1.1\r\nContent-Length: 3\r\n\r\nnew");

    assert_ne!(location_header(&created).trim(), "location: /items/500");
}

#[test]
fn malformed_id_in_path_is_400_not_404() {
    let addr = spawn_server();
    let response = request(
        addr,
        "GET /items/not-a-number HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
    );
    assert!(
        response.starts_with("HTTP/1.1 400 Bad Request"),
        "{response}"
    );
}

#[test]
fn a_request_line_with_no_bytes_at_all_just_closes_quietly() {
    let addr = spawn_server();
    let mut stream = TcpStream::connect(addr).expect("connect");
    stream
        .shutdown(Shutdown::Write)
        .expect("shutdown write half");
    let mut response = Vec::new();
    stream.read_to_end(&mut response).expect("read response");
    assert!(
        response.is_empty(),
        "expected no response for a client that sent nothing"
    );
}
