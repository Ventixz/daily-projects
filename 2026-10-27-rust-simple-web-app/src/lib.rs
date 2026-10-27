//! A tiny std-only web app: HTTP/1.1 parsing, routing, form handling, HTML + JSON output.

use std::collections::HashMap;
use std::io::{BufRead, BufReader, Read, Write};
use std::net::TcpStream;

const MAX_BODY: usize = 64 * 1024;

#[derive(Debug, PartialEq)]
pub struct Request {
    pub method: String,
    pub path: String,
    pub headers: HashMap<String, String>,
    pub body: Vec<u8>,
}

#[derive(Debug, PartialEq)]
pub struct Response {
    pub status: u16,
    pub content_type: &'static str,
    pub location: Option<String>,
    pub body: String,
}

impl Response {
    fn html(status: u16, body: String) -> Self {
        Response { status, content_type: "text/html; charset=utf-8", location: None, body }
    }
    fn redirect(to: &str) -> Self {
        Response { status: 303, content_type: "text/plain", location: Some(to.to_string()), body: String::new() }
    }
    pub fn to_bytes(&self) -> Vec<u8> {
        let reason = match self.status {
            200 => "OK",
            303 => "See Other",
            400 => "Bad Request",
            404 => "Not Found",
            405 => "Method Not Allowed",
            _ => "Error",
        };
        let mut head = format!(
            "HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n",
            self.status, reason, self.content_type, self.body.len()
        );
        if let Some(l) = &self.location {
            head.push_str(&format!("Location: {}\r\n", l));
        }
        head.push_str("\r\n");
        let mut out = head.into_bytes();
        out.extend_from_slice(self.body.as_bytes());
        out
    }
}

/// Read one request from a stream. Returns None on a malformed or oversized request.
pub fn read_request<R: Read>(stream: R) -> Option<Request> {
    let mut reader = BufReader::new(stream);
    let mut line = String::new();
    reader.read_line(&mut line).ok()?;
    let mut parts = line.split_whitespace();
    let method = parts.next()?.to_string();
    let path = parts.next()?.to_string();
    parts.next().filter(|v| v.starts_with("HTTP/"))?;

    let mut headers = HashMap::new();
    loop {
        let mut h = String::new();
        if reader.read_line(&mut h).ok()? == 0 {
            return None;
        }
        let h = h.trim_end();
        if h.is_empty() {
            break;
        }
        let (k, v) = h.split_once(':')?;
        headers.insert(k.trim().to_ascii_lowercase(), v.trim().to_string());
    }
    let len: usize = headers.get("content-length").map_or(Some(0), |v| v.parse().ok())?;
    if len > MAX_BODY {
        return None;
    }
    let mut body = vec![0; len];
    reader.read_exact(&mut body).ok()?;
    Some(Request { method, path, headers, body })
}

pub fn percent_decode(s: &str) -> String {
    let b = s.as_bytes();
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        match b[i] {
            b'+' => out.push(b' '),
            b'%' if i + 2 < b.len() => {
                let hex = std::str::from_utf8(&b[i + 1..i + 3]).ok().and_then(|h| u8::from_str_radix(h, 16).ok());
                match hex {
                    Some(v) => {
                        out.push(v);
                        i += 2;
                    }
                    None => out.push(b'%'),
                }
            }
            c => out.push(c),
        }
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

pub fn parse_form(body: &[u8]) -> HashMap<String, String> {
    String::from_utf8_lossy(body)
        .split('&')
        .filter_map(|kv| kv.split_once('='))
        .map(|(k, v)| (percent_decode(k), percent_decode(v)))
        .collect()
}

pub fn escape_html(s: &str) -> String {
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;").replace('\'', "&#39;")
}

fn escape_json(s: &str) -> String {
    let mut o = String::new();
    for c in s.chars() {
        match c {
            '"' => o.push_str("\\\""),
            '\\' => o.push_str("\\\\"),
            '\n' => o.push_str("\\n"),
            '\r' => o.push_str("\\r"),
            '\t' => o.push_str("\\t"),
            c if (c as u32) < 0x20 => o.push_str(&format!("\\u{:04x}", c as u32)),
            c => o.push(c),
        }
    }
    o
}

#[derive(Debug, Clone, PartialEq)]
pub struct Todo {
    pub id: u32,
    pub text: String,
    pub done: bool,
}

#[derive(Default)]
pub struct Store {
    pub todos: Vec<Todo>,
    next_id: u32,
}

impl Store {
    pub fn add(&mut self, text: &str) -> Option<u32> {
        let text = text.trim();
        if text.is_empty() || text.len() > 200 {
            return None;
        }
        self.next_id += 1;
        self.todos.push(Todo { id: self.next_id, text: text.to_string(), done: false });
        Some(self.next_id)
    }
    pub fn toggle(&mut self, id: u32) -> bool {
        self.todos.iter_mut().find(|t| t.id == id).map(|t| t.done = !t.done).is_some()
    }
    pub fn delete(&mut self, id: u32) -> bool {
        let before = self.todos.len();
        self.todos.retain(|t| t.id != id);
        self.todos.len() != before
    }
}

fn page(store: &Store) -> String {
    let mut items = String::new();
    for t in &store.todos {
        let style = if t.done { " style=\"text-decoration:line-through\"" } else { "" };
        items.push_str(&format!(
            "<li><form method=\"post\" action=\"/toggle/{id}\" style=\"display:inline\"><button>{mark}</button></form> \
             <span{style}>{text}</span> \
             <form method=\"post\" action=\"/delete/{id}\" style=\"display:inline\"><button>x</button></form></li>\n",
            id = t.id,
            mark = if t.done { "[x]" } else { "[ ]" },
            style = style,
            text = escape_html(&t.text),
        ));
    }
    format!(
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Todos</title></head><body>\
         <h1>Todos</h1><form method=\"post\" action=\"/add\"><input name=\"text\" autofocus> <button>Add</button></form>\
         <ul>\n{}</ul></body></html>",
        items
    )
}

pub fn route(store: &mut Store, req: &Request) -> Response {
    let path = req.path.split('?').next().unwrap_or("");
    let segs: Vec<&str> = path.trim_matches('/').split('/').collect();
    match (req.method.as_str(), segs.as_slice()) {
        ("GET", [""]) => Response::html(200, page(store)),
        ("GET", ["api", "todos"]) => {
            let items: Vec<String> = store
                .todos
                .iter()
                .map(|t| format!("{{\"id\":{},\"text\":\"{}\",\"done\":{}}}", t.id, escape_json(&t.text), t.done))
                .collect();
            Response { status: 200, content_type: "application/json", location: None, body: format!("[{}]", items.join(",")) }
        }
        ("POST", ["add"]) => match parse_form(&req.body).get("text").and_then(|t| store.add(t)) {
            Some(_) => Response::redirect("/"),
            None => Response::html(400, "<h1>400</h1><p>Todo text must be 1-200 characters.</p>".into()),
        },
        ("POST", [action @ ("toggle" | "delete"), id]) => {
            let ok = match id.parse::<u32>() {
                Ok(id) if *action == "toggle" => store.toggle(id),
                Ok(id) => store.delete(id),
                Err(_) => false,
            };
            if ok { Response::redirect("/") } else { Response::html(404, "<h1>404</h1><p>No such todo.</p>".into()) }
        }
        (_, [""]) | (_, ["api", "todos"]) | (_, ["add"]) | (_, ["toggle", _]) | (_, ["delete", _]) => {
            Response::html(405, "<h1>405</h1>".into())
        }
        _ => Response::html(404, "<h1>404</h1><p>Not found.</p>".into()),
    }
}

pub fn handle(stream: &mut TcpStream, store: &std::sync::Mutex<Store>) {
    let resp = match read_request(&*stream) {
        Some(req) => route(&mut store.lock().unwrap(), &req),
        None => Response::html(400, "<h1>400</h1>".into()),
    };
    let _ = stream.write_all(&resp.to_bytes());
}

#[cfg(test)]
mod tests {
    use super::*;

    fn req(method: &str, path: &str, body: &str) -> Request {
        Request { method: method.into(), path: path.into(), headers: HashMap::new(), body: body.as_bytes().to_vec() }
    }

    #[test]
    fn parses_request_with_body() {
        let raw = "POST /add HTTP/1.1\r\nHost: x\r\nContent-Length: 6\r\n\r\ntext=a";
        let r = read_request(raw.as_bytes()).unwrap();
        assert_eq!((r.method.as_str(), r.path.as_str()), ("POST", "/add"));
        assert_eq!(r.body, b"text=a");
        assert_eq!(r.headers["host"], "x");
    }

    #[test]
    fn rejects_malformed_and_oversized() {
        assert!(read_request("garbage\r\n\r\n".as_bytes()).is_none());
        assert!(read_request("GET / HTTP/1.1\r\nContent-Length: 999999\r\n\r\n".as_bytes()).is_none());
        assert!(read_request("POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\nshort".as_bytes()).is_none());
    }

    #[test]
    fn percent_decoding() {
        assert_eq!(percent_decode("a+b%21%26"), "a b!&");
        assert_eq!(percent_decode("100%"), "100%");
        assert_eq!(percent_decode("%zz%4"), "%zz%4");
        assert_eq!(percent_decode("%E2%9C%93"), "✓");
    }

    #[test]
    fn form_parsing() {
        let f = parse_form(b"text=buy+milk&x=%3D");
        assert_eq!(f["text"], "buy milk");
        assert_eq!(f["x"], "=");
    }

    #[test]
    fn add_toggle_delete_flow() {
        let mut s = Store::default();
        assert_eq!(route(&mut s, &req("POST", "/add", "text=one")).status, 303);
        assert_eq!(route(&mut s, &req("POST", "/add", "text=two")).status, 303);
        assert_eq!(s.todos.len(), 2);
        assert_eq!(route(&mut s, &req("POST", "/toggle/1", "")).status, 303);
        assert!(s.todos[0].done);
        assert_eq!(route(&mut s, &req("POST", "/delete/1", "")).status, 303);
        assert_eq!(s.todos.len(), 1);
        assert_eq!(s.todos[0].id, 2);
        assert_eq!(s.add("three"), Some(3), "ids are never reused");
    }

    #[test]
    fn validation_and_errors() {
        let mut s = Store::default();
        assert_eq!(route(&mut s, &req("POST", "/add", "text=+++")).status, 400);
        assert_eq!(route(&mut s, &req("POST", "/add", "")).status, 400);
        assert_eq!(route(&mut s, &req("POST", "/toggle/9", "")).status, 404);
        assert_eq!(route(&mut s, &req("POST", "/toggle/abc", "")).status, 404);
        assert_eq!(route(&mut s, &req("GET", "/add", "")).status, 405);
        assert_eq!(route(&mut s, &req("GET", "/nope", "")).status, 404);
    }

    #[test]
    fn html_is_escaped() {
        let mut s = Store::default();
        route(&mut s, &req("POST", "/add", "text=%3Cscript%3Ealert(1)%3C%2Fscript%3E"));
        let body = route(&mut s, &req("GET", "/", "")).body;
        assert!(!body.contains("<script>"));
        assert!(body.contains("&lt;script&gt;"));
    }

    #[test]
    fn json_api() {
        let mut s = Store::default();
        assert_eq!(route(&mut s, &req("GET", "/api/todos?x=1", "")).body, "[]");
        route(&mut s, &req("POST", "/add", "text=say+%22hi%22"));
        let r = route(&mut s, &req("GET", "/api/todos", ""));
        assert_eq!(r.content_type, "application/json");
        assert_eq!(r.body, r#"[{"id":1,"text":"say \"hi\"","done":false}]"#);
    }

    #[test]
    fn response_serialisation() {
        let b = Response::redirect("/").to_bytes();
        let t = String::from_utf8(b).unwrap();
        assert!(t.starts_with("HTTP/1.1 303 See Other\r\n"));
        assert!(t.contains("Location: /\r\n"));
        assert!(t.contains("Content-Length: 0\r\n"));
    }
}
