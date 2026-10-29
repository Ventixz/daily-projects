use std::io::{BufReader, Cursor, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::{Arc, Mutex};
use std::thread;

use rust_simple_web_app::app::App;
use rust_simple_web_app::http::*;
use rust_simple_web_app::serve_connection;

fn parse(raw: &str) -> Result<Request, ParseError> {
    parse_request(&mut BufReader::new(Cursor::new(raw.as_bytes().to_vec())))
}

fn req(method: &str, path: &str, body: &str) -> Request {
    Request {
        method: method.into(),
        path: path.into(),
        query: Default::default(),
        headers: Default::default(),
        body: body.as_bytes().to_vec(),
    }
}

#[test]
fn percent_decoding() {
    assert_eq!(percent_decode("a+b%20c%41"), "a b cA");
    assert_eq!(percent_decode("100%"), "100%");
    assert_eq!(percent_decode("%zz"), "%zz");
    assert_eq!(percent_decode("%e2%9c%93"), "✓");
}

#[test]
fn parses_get_with_query() {
    let r = parse("GET /search?q=rust+lang&n=2 HTTP/1.1\r\nHost: x\r\n\r\n").unwrap();
    assert_eq!(r.method, "GET");
    assert_eq!(r.path, "/search");
    assert_eq!(r.query["q"], "rust lang");
    assert_eq!(r.headers["host"], "x");
}

#[test]
fn parses_post_body_by_content_length() {
    let r = parse("POST /notes HTTP/1.1\r\nContent-Length: 9\r\n\r\ntext=hi+u").unwrap();
    assert_eq!(r.body, b"text=hi+u");
}

#[test]
fn rejects_malformed_requests() {
    assert_eq!(parse(""), Err(ParseError::Empty));
    assert_eq!(parse("GARBAGE\r\n\r\n"), Err(ParseError::BadRequestLine));
    assert_eq!(parse("GET / HTTP/1.1\r\nnocolon\r\n\r\n"), Err(ParseError::BadHeader));
    assert_eq!(parse("POST / HTTP/1.1\r\nContent-Length: 999999\r\n\r\n"), Err(ParseError::BodyTooLarge));
}

#[test]
fn add_and_delete_notes() {
    let mut app = App::new();
    let r = app.handle(&req("POST", "/notes", "text=buy+milk"));
    assert_eq!((r.status, r.location.as_deref()), (303, Some("/")));
    app.handle(&req("POST", "/notes", "text=call+mum"));
    assert_eq!(app.notes(), ["buy milk", "call mum"]);
    assert_eq!(app.handle(&req("POST", "/notes/delete", "id=0")).status, 303);
    assert_eq!(app.notes(), ["call mum"]);
    assert_eq!(app.handle(&req("POST", "/notes/delete", "id=5")).status, 400);
    assert_eq!(app.handle(&req("POST", "/notes/delete", "id=x")).status, 400);
}

#[test]
fn empty_note_rejected() {
    let mut app = App::new();
    assert_eq!(app.handle(&req("POST", "/notes", "text=+++")).status, 400);
    assert_eq!(app.handle(&req("POST", "/notes", "")).status, 400);
    assert!(app.notes().is_empty());
}

#[test]
fn index_escapes_html() {
    let mut app = App::new();
    app.handle(&req("POST", "/notes", "text=%3Cscript%3Ealert(1)%3C%2Fscript%3E"));
    let body = app.handle(&req("GET", "/", "")).body;
    assert!(!body.contains("<script>"));
    assert!(body.contains("&lt;script&gt;"));
}

#[test]
fn routing_statuses() {
    let mut app = App::new();
    assert_eq!(app.handle(&req("GET", "/nope", "")).status, 404);
    assert_eq!(app.handle(&req("GET", "/notes", "")).status, 405);
    assert_eq!(app.handle(&req("GET", "/health", "")).body, "ok");
}

#[test]
fn response_bytes_have_correct_framing() {
    let s = String::from_utf8(Response::text(404, "not found").to_bytes()).unwrap();
    assert!(s.starts_with("HTTP/1.1 404 Not Found\r\n"));
    assert!(s.contains("Content-Length: 9\r\n"));
    assert!(s.ends_with("\r\n\r\nnot found"));
}

#[test]
fn end_to_end_over_tcp() {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let addr = listener.local_addr().unwrap();
    let app = Arc::new(Mutex::new(App::new()));
    let a = Arc::clone(&app);
    let server = thread::spawn(move || {
        for _ in 0..2 {
            let (s, _) = listener.accept().unwrap();
            serve_connection(s, &a);
        }
    });

    let send = |raw: String| {
        let mut c = TcpStream::connect(addr).unwrap();
        c.write_all(raw.as_bytes()).unwrap();
        let mut out = String::new();
        c.read_to_string(&mut out).unwrap();
        out
    };
    let body = "text=hello+world";
    let post = send(format!(
        "POST /notes HTTP/1.1\r\nHost: t\r\nContent-Length: {}\r\n\r\n{}",
        body.len(),
        body
    ));
    assert!(post.starts_with("HTTP/1.1 303 See Other"));
    assert!(post.contains("Location: /\r\n"));
    let get = send("GET / HTTP/1.1\r\nHost: t\r\n\r\n".to_string());
    assert!(get.starts_with("HTTP/1.1 200 OK"));
    assert!(get.contains("hello world"));
    server.join().unwrap();
}
