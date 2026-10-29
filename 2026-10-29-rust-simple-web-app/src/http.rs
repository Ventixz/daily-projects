//! Minimal HTTP/1.1 request parsing and response building (std only).

use std::collections::HashMap;
use std::io::{self, BufRead};

#[derive(Debug, PartialEq)]
pub struct Request {
    pub method: String,
    pub path: String,
    pub query: HashMap<String, String>,
    pub headers: HashMap<String, String>,
    pub body: Vec<u8>,
}

#[derive(Debug, PartialEq)]
pub enum ParseError {
    Empty,
    BadRequestLine,
    BadHeader,
    BodyTooLarge,
    Io(String),
}

const MAX_BODY: usize = 64 * 1024;

fn hex(c: u8) -> Option<u8> {
    (c as char).to_digit(16).map(|d| d as u8)
}

pub fn percent_decode(s: &str) -> String {
    let b = s.as_bytes();
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        match b[i] {
            b'+' => out.push(b' '),
            b'%' if i + 2 < b.len() => match (hex(b[i + 1]), hex(b[i + 2])) {
                (Some(h), Some(l)) => {
                    out.push(h * 16 + l);
                    i += 2;
                }
                _ => out.push(b'%'),
            },
            c => out.push(c),
        }
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

pub fn parse_form(s: &str) -> HashMap<String, String> {
    s.split('&')
        .filter(|p| !p.is_empty())
        .map(|p| match p.split_once('=') {
            Some((k, v)) => (percent_decode(k), percent_decode(v)),
            None => (percent_decode(p), String::new()),
        })
        .collect()
}

pub fn parse_request<R: BufRead>(r: &mut R) -> Result<Request, ParseError> {
    let mut line = String::new();
    let n = r.read_line(&mut line).map_err(|e| ParseError::Io(e.to_string()))?;
    if n == 0 {
        return Err(ParseError::Empty);
    }
    let mut parts = line.trim_end().split(' ');
    let (method, target) = match (parts.next(), parts.next(), parts.next()) {
        (Some(m), Some(t), Some(v)) if v.starts_with("HTTP/") && !m.is_empty() => (m, t),
        _ => return Err(ParseError::BadRequestLine),
    };
    let (path, query) = match target.split_once('?') {
        Some((p, q)) => (p.to_string(), parse_form(q)),
        None => (target.to_string(), HashMap::new()),
    };

    let mut headers = HashMap::new();
    loop {
        let mut h = String::new();
        let n = r.read_line(&mut h).map_err(|e| ParseError::Io(e.to_string()))?;
        let h = h.trim_end();
        if n == 0 || h.is_empty() {
            break;
        }
        let (k, v) = h.split_once(':').ok_or(ParseError::BadHeader)?;
        headers.insert(k.trim().to_ascii_lowercase(), v.trim().to_string());
    }

    let len: usize = headers.get("content-length").and_then(|v| v.parse().ok()).unwrap_or(0);
    if len > MAX_BODY {
        return Err(ParseError::BodyTooLarge);
    }
    let mut body = vec![0u8; len];
    r.read_exact(&mut body).map_err(|e: io::Error| ParseError::Io(e.to_string()))?;

    Ok(Request { method: method.to_string(), path, query, headers, body })
}

#[derive(Debug, PartialEq)]
pub struct Response {
    pub status: u16,
    pub content_type: &'static str,
    pub location: Option<String>,
    pub body: String,
}

impl Response {
    pub fn html(status: u16, body: String) -> Self {
        Response { status, content_type: "text/html; charset=utf-8", location: None, body }
    }
    pub fn text(status: u16, body: &str) -> Self {
        Response { status, content_type: "text/plain; charset=utf-8", location: None, body: body.to_string() }
    }
    pub fn redirect(to: &str) -> Self {
        Response { status: 303, content_type: "text/plain; charset=utf-8", location: Some(to.to_string()), body: String::new() }
    }
    pub fn reason(&self) -> &'static str {
        match self.status {
            200 => "OK",
            303 => "See Other",
            400 => "Bad Request",
            404 => "Not Found",
            405 => "Method Not Allowed",
            413 => "Payload Too Large",
            _ => "Unknown",
        }
    }
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut head = format!(
            "HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n",
            self.status,
            self.reason(),
            self.content_type,
            self.body.len()
        );
        if let Some(l) = &self.location {
            head.push_str(&format!("Location: {}\r\n", l));
        }
        head.push_str("\r\n");
        let mut v = head.into_bytes();
        v.extend_from_slice(self.body.as_bytes());
        v
    }
}

pub fn html_escape(s: &str) -> String {
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;")
}
