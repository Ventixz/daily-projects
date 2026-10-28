//! TCP accept loop. One thread per connection; the store sits behind a Mutex.

use std::io::{BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use crate::app::handle;
use crate::http::{parse_request, ParseError, Response};
use crate::store::Store;

pub fn serve_connection(stream: TcpStream, store: &Mutex<Store>) {
    let _ = stream.set_read_timeout(Some(Duration::from_secs(5)));
    let mut writer = match stream.try_clone() {
        Ok(w) => w,
        Err(_) => return,
    };
    let mut reader = BufReader::new(stream);
    let resp = match parse_request(&mut reader) {
        Ok(req) => {
            // A poisoned lock only means another handler panicked; the data is still usable.
            let mut guard = store.lock().unwrap_or_else(|e| e.into_inner());
            handle(&mut guard, &req)
        }
        Err(ParseError::TooLarge) => Response::html(413, "<h1>413 Payload Too Large</h1>"),
        Err(ParseError::Malformed(m)) => Response::html(400, format!("<h1>400 {}</h1>", m)),
        Err(ParseError::Io(_)) => return,
    };
    let _ = writer.write_all(&resp.to_bytes());
}

/// Accept connections forever.
pub fn run(listener: TcpListener, store: Arc<Mutex<Store>>) {
    for conn in listener.incoming() {
        if let Ok(stream) = conn {
            let store = Arc::clone(&store);
            thread::spawn(move || serve_connection(stream, &store));
        }
    }
}
