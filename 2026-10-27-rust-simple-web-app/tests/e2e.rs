use rust_simple_web_app::{handle, Store};
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::{Arc, Mutex};
use std::thread;

fn spawn_server() -> String {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let addr = listener.local_addr().unwrap().to_string();
    let store = Arc::new(Mutex::new(Store::default()));
    thread::spawn(move || {
        for s in listener.incoming().flatten() {
            let store = Arc::clone(&store);
            thread::spawn(move || {
                let mut s = s;
                handle(&mut s, &store);
            });
        }
    });
    addr
}

fn send(addr: &str, raw: &str) -> String {
    let mut s = TcpStream::connect(addr).unwrap();
    s.write_all(raw.as_bytes()).unwrap();
    let mut out = String::new();
    s.read_to_string(&mut out).unwrap();
    out
}

fn post(addr: &str, path: &str, body: &str) -> String {
    send(addr, &format!("POST {} HTTP/1.1\r\nHost: t\r\nContent-Length: {}\r\n\r\n{}", path, body.len(), body))
}

#[test]
fn full_round_trip_over_tcp() {
    let addr = spawn_server();
    let r = post(&addr, "/add", "text=write+tests");
    assert!(r.starts_with("HTTP/1.1 303"), "{r}");
    let page = send(&addr, "GET / HTTP/1.1\r\nHost: t\r\n\r\n");
    assert!(page.starts_with("HTTP/1.1 200 OK"));
    assert!(page.contains("write tests"));
    post(&addr, "/toggle/1", "");
    let json = send(&addr, "GET /api/todos HTTP/1.1\r\n\r\n");
    assert!(json.ends_with(r#"[{"id":1,"text":"write tests","done":true}]"#), "{json}");
    assert!(send(&addr, "NONSENSE\r\n\r\n").starts_with("HTTP/1.1 400"));
}

#[test]
fn concurrent_clients_share_state() {
    let addr = spawn_server();
    let hs: Vec<_> = (0..8)
        .map(|i| {
            let a = addr.clone();
            thread::spawn(move || post(&a, "/add", &format!("text=item{i}")))
        })
        .collect();
    for h in hs {
        assert!(h.join().unwrap().starts_with("HTTP/1.1 303"));
    }
    let json = send(&addr, "GET /api/todos HTTP/1.1\r\n\r\n");
    assert_eq!(json.matches("\"id\"").count(), 8);
}
