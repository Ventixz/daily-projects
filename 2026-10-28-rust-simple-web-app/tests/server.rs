use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::{Arc, Mutex};
use std::thread;

use rust_simple_web_app::{server, store::Store};

fn start() -> String {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let addr = listener.local_addr().unwrap().to_string();
    thread::spawn(move || server::run(listener, Arc::new(Mutex::new(Store::new()))));
    addr
}

fn send(addr: &str, raw: &str) -> String {
    let mut s = TcpStream::connect(addr).unwrap();
    s.write_all(raw.as_bytes()).unwrap();
    let mut out = String::new();
    s.read_to_string(&mut out).unwrap();
    out
}

#[test]
fn end_to_end_over_tcp() {
    let addr = start();
    let body = "title=write+tests";
    let post = format!(
        "POST /todos HTTP/1.1\r\nAccept: application/json\r\nContent-Length: {}\r\n\r\n{}",
        body.len(),
        body
    );
    let r = send(&addr, &post);
    assert!(r.starts_with("HTTP/1.1 201 Created"), "{r}");
    assert!(r.ends_with(r#"{"id":1,"title":"write tests","done":false}"#), "{r}");

    let r = send(&addr, "GET /todos HTTP/1.1\r\n\r\n");
    assert!(r.contains("application/json") && r.contains("write tests"), "{r}");

    let r = send(&addr, "GET / HTTP/1.1\r\n\r\n");
    assert!(r.contains("<span>write tests</span>"), "{r}");
}

#[test]
fn bad_request_and_concurrency() {
    let addr = start();
    assert!(send(&addr, "garbage\r\n\r\n").starts_with("HTTP/1.1 400"));

    let handles: Vec<_> = (0..20)
        .map(|i| {
            let addr = addr.clone();
            thread::spawn(move || {
                let b = format!("title=t{i}");
                send(&addr, &format!("POST /todos HTTP/1.1\r\nContent-Length: {}\r\n\r\n{}", b.len(), b))
            })
        })
        .collect();
    for h in handles {
        assert!(h.join().unwrap().starts_with("HTTP/1.1 303"));
    }
    let list = send(&addr, "GET /todos HTTP/1.1\r\n\r\n");
    assert_eq!(list.matches(r#""id":"#).count(), 20, "{list}");
}
