//! Minimal HTTP/1.1 request parsing and response serialisation (std only).

use std::collections::HashMap;
use std::io::{self, BufRead};

const MAX_BODY: usize = 64 * 1024;

#[derive(Debug, PartialEq)]
pub struct Request {
    pub method: String,
    pub path: String,
    pub query: HashMap<String, String>,
    pub headers: HashMap<String, String>,
    pub body: Vec<u8>,
}

#[derive(Debug)]
pub enum ParseError {
    Malformed(&'static str),
    TooLarge,
    Io(io::Error),
}

impl From<io::Error> for ParseError {
    fn from(e: io::Error) -> Self {
        ParseError::Io(e)
    }
}

pub fn parse_request<R: BufRead>(reader: &mut R) -> Result<Request, ParseError> {
    let mut line = String::new();
    if reader.read_line(&mut line)? == 0 {
        return Err(ParseError::Malformed("empty request"));
    }
    let mut parts = line.trim_end().split(' ');
    let method = parts.next().filter(|m| !m.is_empty()).ok_or(ParseError::Malformed("no method"))?;
    let target = parts.next().ok_or(ParseError::Malformed("no target"))?;
    match parts.next() {
        Some(v) if v.starts_with("HTTP/1.") => {}
        _ => return Err(ParseError::Malformed("bad version")),
    }

    let (raw_path, raw_query) = match target.split_once('?') {
        Some((p, q)) => (p, q),
        None => (target, ""),
    };

    let mut headers = HashMap::new();
    loop {
        let mut h = String::new();
        if reader.read_line(&mut h)? == 0 {
            return Err(ParseError::Malformed("eof in headers"));
        }
        let h = h.trim_end();
        if h.is_empty() {
            break;
        }
        let (k, v) = h.split_once(':').ok_or(ParseError::Malformed("bad header"))?;
        headers.insert(k.trim().to_ascii_lowercase(), v.trim().to_string());
    }

    let len = match headers.get("content-length") {
        Some(v) => v.parse::<usize>().map_err(|_| ParseError::Malformed("bad content-length"))?,
        None => 0,
    };
    if len > MAX_BODY {
        return Err(ParseError::TooLarge);
    }
    let mut body = vec![0; len];
    reader.read_exact(&mut body)?;

    Ok(Request {
        method: method.to_string(),
        path: percent_decode(raw_path),
        query: parse_form(raw_query),
        headers,
        body,
    })
}

/// Decode `%XX` escapes and `+` (as space). Invalid escapes are kept literally.
pub fn percent_decode(s: &str) -> String {
    let b = s.as_bytes();
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        match b[i] {
            b'+' => out.push(b' '),
            b'%' if i + 2 < b.len() && hex(b[i + 1]).is_some() && hex(b[i + 2]).is_some() => {
                out.push(hex(b[i + 1]).unwrap() * 16 + hex(b[i + 2]).unwrap());
                i += 2;
            }
            c => out.push(c),
        }
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

fn hex(c: u8) -> Option<u8> {
    (c as char).to_digit(16).map(|d| d as u8)
}

/// Parse `a=1&b=two%20words` into a map (later keys win).
pub fn parse_form(s: &str) -> HashMap<String, String> {
    s.split('&')
        .filter(|p| !p.is_empty())
        .map(|p| match p.split_once('=') {
            Some((k, v)) => (percent_decode(k), percent_decode(v)),
            None => (percent_decode(p), String::new()),
        })
        .collect()
}

#[derive(Debug, PartialEq)]
pub struct Response {
    pub status: u16,
    pub content_type: &'static str,
    pub body: String,
    pub extra_headers: Vec<(String, String)>,
}

impl Response {
    pub fn new(status: u16, content_type: &'static str, body: impl Into<String>) -> Self {
        Response { status, content_type, body: body.into(), extra_headers: Vec::new() }
    }
    pub fn html(status: u16, body: impl Into<String>) -> Self {
        Self::new(status, "text/html; charset=utf-8", body)
    }
    pub fn json(status: u16, body: impl Into<String>) -> Self {
        Self::new(status, "application/json", body)
    }
    pub fn redirect(to: &str) -> Self {
        let mut r = Self::new(303, "text/plain", "");
        r.extra_headers.push(("Location".into(), to.into()));
        r
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut s = format!(
            "HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n",
            self.status,
            reason(self.status),
            self.content_type,
            self.body.len()
        );
        for (k, v) in &self.extra_headers {
            s.push_str(&format!("{}: {}\r\n", k, v));
        }
        s.push_str("\r\n");
        s.push_str(&self.body);
        s.into_bytes()
    }
}

fn reason(status: u16) -> &'static str {
    match status {
        200 => "OK",
        201 => "Created",
        303 => "See Other",
        400 => "Bad Request",
        404 => "Not Found",
        405 => "Method Not Allowed",
        413 => "Payload Too Large",
        _ => "Internal Server Error",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    fn parse(s: &str) -> Result<Request, ParseError> {
        parse_request(&mut Cursor::new(s.as_bytes().to_vec()))
    }

    #[test]
    fn parses_get_with_query() {
        let r = parse("GET /a%20b?x=1&y=hi+there HTTP/1.1\r\nHost: h\r\n\r\n").unwrap();
        assert_eq!(r.method, "GET");
        assert_eq!(r.path, "/a b");
        assert_eq!(r.query["y"], "hi there");
        assert_eq!(r.headers["host"], "h");
    }

    #[test]
    fn reads_body_by_content_length() {
        let r = parse("POST /t HTTP/1.1\r\nContent-Length: 7\r\n\r\ntitle=x").unwrap();
        assert_eq!(r.body, b"title=x");
    }

    #[test]
    fn rejects_garbage_and_oversize() {
        assert!(matches!(parse("NOPE\r\n\r\n"), Err(ParseError::Malformed(_))));
        assert!(matches!(parse("GET / HTTP/2\r\n\r\n"), Err(ParseError::Malformed(_))));
        assert!(matches!(
            parse("POST / HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n"),
            Err(ParseError::TooLarge)
        ));
    }

    #[test]
    fn percent_decode_edge_cases() {
        assert_eq!(percent_decode("%41%zz%4"), "A%zz%4");
        assert_eq!(percent_decode("caf%C3%A9"), "café");
    }

    #[test]
    fn response_serialises_with_length() {
        let s = String::from_utf8(Response::redirect("/").to_bytes()).unwrap();
        assert!(s.starts_with("HTTP/1.1 303 See Other\r\n"));
        assert!(s.contains("Location: /\r\n"));
        assert!(s.contains("Content-Length: 0\r\n"));
    }
}
