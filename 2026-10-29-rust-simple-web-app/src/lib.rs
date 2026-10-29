pub mod app;
pub mod http;

use std::io::{BufReader, Write};
use std::net::TcpStream;
use std::sync::{Arc, Mutex};

use app::App;
use http::{parse_request, ParseError, Response};

pub fn serve_connection(stream: TcpStream, app: &Arc<Mutex<App>>) {
    let _ = stream.set_read_timeout(Some(std::time::Duration::from_secs(5)));
    let mut reader = BufReader::new(&stream);
    let resp = match parse_request(&mut reader) {
        Ok(req) => app.lock().unwrap().handle(&req),
        Err(ParseError::Empty) => return,
        Err(ParseError::BodyTooLarge) => Response::text(413, "payload too large"),
        Err(_) => Response::text(400, "bad request"),
    };
    let mut out = &stream;
    let _ = out.write_all(&resp.to_bytes());
}
