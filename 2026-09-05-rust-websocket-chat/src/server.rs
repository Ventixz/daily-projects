//! The chat service itself: one thread per connected client, a shared registry of write
//! handles, and a broadcast that fans a text frame out to everyone except its sender.

use crate::websocket::{self, Opcode};
use std::collections::HashMap;
use std::io::{self, BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;

type Clients = Arc<Mutex<HashMap<u64, TcpStream>>>;

/// Binds `addr` and serves forever. Thin wrapper around [`serve`] so tests can bind their
/// own ephemeral-port listener and hand it to `serve` directly.
pub fn run(addr: &str) -> io::Result<()> {
    serve(TcpListener::bind(addr)?)
}

pub fn serve(listener: TcpListener) -> io::Result<()> {
    let clients: Clients = Arc::new(Mutex::new(HashMap::new()));
    let next_id = Arc::new(AtomicU64::new(1));

    for stream in listener.incoming() {
        let stream = match stream {
            Ok(s) => s,
            Err(_) => continue,
        };
        let clients = Arc::clone(&clients);
        let id = next_id.fetch_add(1, Ordering::SeqCst);
        thread::spawn(move || {
            if let Err(e) = handle_client(id, stream, clients.clone()) {
                eprintln!("client {id}: {e}");
            }
            clients.lock().unwrap().remove(&id);
        });
    }
    Ok(())
}

fn handle_client(id: u64, stream: TcpStream, clients: Clients) -> io::Result<()> {
    stream.set_nodelay(true).ok();
    let write_half = stream.try_clone()?;
    let mut reader = BufReader::new(stream);

    let key = read_handshake_key(&mut reader)?;
    let accept = websocket::accept_key(&key);
    let response = format!(
        "HTTP/1.1 101 Switching Protocols\r\n\
         Upgrade: websocket\r\n\
         Connection: Upgrade\r\n\
         Sec-WebSocket-Accept: {accept}\r\n\r\n"
    );
    (&write_half).write_all(response.as_bytes())?;

    clients.lock().unwrap().insert(id, write_half.try_clone()?);

    loop {
        let frame = websocket::read_frame(&mut reader)?;
        match frame.opcode {
            Opcode::Text => broadcast(&clients, id, &frame.payload),
            Opcode::Ping => {
                let pong = websocket::encode_frame(Opcode::Pong, &frame.payload);
                (&write_half).write_all(&pong)?;
            }
            Opcode::Close => break,
            _ => {}
        }
    }
    Ok(())
}

/// Reads HTTP request lines until the blank line that ends the header block, and pulls out
/// `Sec-WebSocket-Key`. Returns an error (which drops the connection) if it's missing —
/// there's no valid WebSocket session without it.
fn read_handshake_key<R: BufRead>(reader: &mut R) -> io::Result<String> {
    let mut request = String::new();
    loop {
        let mut line = String::new();
        if reader.read_line(&mut line)? == 0 {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "connection closed during handshake",
            ));
        }
        if line == "\r\n" || line == "\n" {
            break;
        }
        request.push_str(&line);
    }
    websocket::parse_websocket_key(&request)
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "missing Sec-WebSocket-Key"))
}

/// Sends `payload` as a text frame to every connected client except `sender_id`. A write
/// failure means that peer is gone; it's dropped from the registry rather than left to poison
/// every future broadcast.
fn broadcast(clients: &Clients, sender_id: u64, payload: &[u8]) {
    let frame = websocket::encode_frame(Opcode::Text, payload);
    let mut guard = clients.lock().unwrap();
    let dead: Vec<u64> = guard
        .iter_mut()
        .filter(|(&cid, _)| cid != sender_id)
        .filter_map(|(&cid, stream)| stream.write_all(&frame).err().map(|_| cid))
        .collect();
    for cid in dead {
        guard.remove(&cid);
    }
}
