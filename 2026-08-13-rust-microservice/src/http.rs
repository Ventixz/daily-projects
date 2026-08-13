use std::fmt;
use std::io::{self, BufRead, Write};

#[derive(Debug)]
pub struct Request {
    pub method: String,
    pub path: String,
    pub headers: Vec<(String, String)>,
    pub body: Vec<u8>,
}

#[derive(Debug)]
pub enum ParseError {
    Io(io::Error),
    Malformed(&'static str),
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ParseError::Io(e) => write!(f, "io error: {e}"),
            ParseError::Malformed(reason) => write!(f, "malformed request: {reason}"),
        }
    }
}

impl From<io::Error> for ParseError {
    fn from(e: io::Error) -> Self {
        ParseError::Io(e)
    }
}

impl Request {
    /// Reads exactly one HTTP request off `reader`.
    ///
    /// Returns `Ok(None)` if the connection was closed before a single byte
    /// arrived -- a client that opens a socket and hangs up is not a
    /// malformed request, so it must not be answered with a 400.
    ///
    /// `reader` has to be the same buffered stream the caller keeps using
    /// afterward (not a fresh wrapper around the underlying `TcpStream`):
    /// `read_line` pulls in more than just the line it returns, and the
    /// body bytes that follow the blank line are often already sitting in
    /// that buffer. Reading the body through a second, unbuffered handle to
    /// the same socket would silently drop whatever the header read had
    /// already buffered.
    pub fn parse(reader: &mut impl BufRead) -> Result<Option<Request>, ParseError> {
        let mut request_line = String::new();
        if reader.read_line(&mut request_line)? == 0 {
            return Ok(None);
        }

        let mut parts = request_line.trim_end().splitn(3, ' ');
        let method = parts
            .next()
            .filter(|s| !s.is_empty())
            .ok_or(ParseError::Malformed("missing method"))?
            .to_string();
        let path = parts
            .next()
            .filter(|s| !s.is_empty())
            .ok_or(ParseError::Malformed("missing path"))?
            .to_string();
        parts
            .next()
            .filter(|s| !s.is_empty())
            .ok_or(ParseError::Malformed("missing version"))?;

        let mut headers = Vec::new();
        loop {
            let mut line = String::new();
            reader.read_line(&mut line)?;
            let line = line.trim_end();
            if line.is_empty() {
                break;
            }
            let (name, value) = line
                .split_once(':')
                .ok_or(ParseError::Malformed("header missing colon"))?;
            headers.push((name.trim().to_ascii_lowercase(), value.trim().to_string()));
        }

        let content_length = headers
            .iter()
            .find(|(name, _)| name == "content-length")
            .map(|(_, value)| value.parse::<usize>())
            .transpose()
            .map_err(|_| ParseError::Malformed("content-length is not a number"))?
            .unwrap_or(0);

        let mut body = vec![0u8; content_length];
        reader.read_exact(&mut body)?;

        Ok(Some(Request {
            method,
            path,
            headers,
            body,
        }))
    }
}

pub struct Response {
    pub status: u16,
    pub reason: &'static str,
    pub headers: Vec<(String, String)>,
    pub body: Vec<u8>,
}

impl Response {
    pub fn new(status: u16, reason: &'static str) -> Self {
        Response {
            status,
            reason,
            headers: Vec::new(),
            body: Vec::new(),
        }
    }

    pub fn with_body(mut self, body: Vec<u8>) -> Self {
        self.body = body;
        self
    }

    pub fn with_header(mut self, name: &str, value: impl Into<String>) -> Self {
        self.headers.push((name.to_string(), value.into()));
        self
    }

    /// Writes the status line, headers, and body to `w`.
    ///
    /// `content-length` is always computed here from `self.body.len()`,
    /// never taken from a header a caller might have set -- the same rule
    /// this repo's Java HTTP server project settled on, for the same
    /// reason: a handler that hand-sets the length and a body that
    /// disagrees with it is a lie the client has no way to detect.
    pub fn write(&self, w: &mut impl Write) -> io::Result<()> {
        write!(w, "HTTP/1.1 {} {}\r\n", self.status, self.reason)?;
        for (name, value) in &self.headers {
            if name.eq_ignore_ascii_case("content-length") {
                continue;
            }
            write!(w, "{name}: {value}\r\n")?;
        }
        write!(w, "content-length: {}\r\n", self.body.len())?;
        write!(w, "connection: close\r\n")?;
        write!(w, "\r\n")?;
        w.write_all(&self.body)?;
        w.flush()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    #[test]
    fn parses_method_path_headers_and_body() {
        let raw = b"POST /items HTTP/1.1\r\nContent-Length: 5\r\nX-Trace: abc\r\n\r\nhello";
        let mut reader = Cursor::new(&raw[..]);
        let request = Request::parse(&mut reader).unwrap().unwrap();

        assert_eq!(request.method, "POST");
        assert_eq!(request.path, "/items");
        assert_eq!(request.body, b"hello");
        assert!(request
            .headers
            .contains(&("x-trace".to_string(), "abc".to_string())));
    }

    #[test]
    fn header_names_are_case_folded() {
        let raw = b"GET / HTTP/1.1\r\nCoNtEnT-LeNgTh: 0\r\n\r\n";
        let mut reader = Cursor::new(&raw[..]);
        let request = Request::parse(&mut reader).unwrap().unwrap();
        assert!(request
            .headers
            .iter()
            .any(|(name, _)| name == "content-length"));
    }

    #[test]
    fn missing_content_length_means_an_empty_body() {
        let raw = b"GET / HTTP/1.1\r\n\r\n";
        let mut reader = Cursor::new(&raw[..]);
        let request = Request::parse(&mut reader).unwrap().unwrap();
        assert!(request.body.is_empty());
    }

    #[test]
    fn clean_eof_before_any_bytes_is_none_not_an_error() {
        let raw: &[u8] = b"";
        let mut reader = Cursor::new(raw);
        assert!(Request::parse(&mut reader).unwrap().is_none());
    }

    #[test]
    fn a_request_line_missing_the_version_is_malformed() {
        let raw = b"GET /\r\n\r\n";
        let mut reader = Cursor::new(&raw[..]);
        let err = Request::parse(&mut reader).unwrap_err();
        assert!(matches!(err, ParseError::Malformed(_)));
    }

    #[test]
    fn a_header_without_a_colon_is_malformed() {
        let raw = b"GET / HTTP/1.1\r\nbroken header\r\n\r\n";
        let mut reader = Cursor::new(&raw[..]);
        let err = Request::parse(&mut reader).unwrap_err();
        assert!(matches!(err, ParseError::Malformed(_)));
    }

    #[test]
    fn write_recomputes_content_length_from_the_actual_body() {
        let response = Response::new(200, "OK")
            .with_header("content-length", "999")
            .with_body(b"hi".to_vec());
        let mut out = Vec::new();
        response.write(&mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("content-length: 2\r\n"));
        assert!(!text.contains("content-length: 999"));
        assert!(text.ends_with("hi"));
    }
}
