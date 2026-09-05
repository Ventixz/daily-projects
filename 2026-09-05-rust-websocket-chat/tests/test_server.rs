//! Exercises the actual server over real TCP sockets: two raw clients perform the HTTP
//! handshake by hand (no WebSocket library — this crate doesn't have a client, only a server),
//! then one sends a masked text frame and the other must receive it broadcast back unmasked,
//! while the sender itself must NOT get an echo of its own message.

use std::io::{BufRead, BufReader, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::thread;
use std::time::Duration;

fn start_server() -> std::net::SocketAddr {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let addr = listener.local_addr().unwrap();
    thread::spawn(move || {
        wschat::server::serve(listener).unwrap();
    });
    addr
}

/// Connects, sends a minimal upgrade request with a fixed test key, and reads back the
/// handshake response. Returns the raw stream, positioned right after the response so the
/// caller can start exchanging WebSocket frames on it.
fn connect_and_handshake(addr: std::net::SocketAddr) -> TcpStream {
    let mut stream = TcpStream::connect(addr).unwrap();
    stream.set_read_timeout(Some(Duration::from_secs(3))).unwrap();

    let request = "GET / HTTP/1.1\r\n\
                    Host: localhost\r\n\
                    Upgrade: websocket\r\n\
                    Connection: Upgrade\r\n\
                    Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\
                    Sec-WebSocket-Version: 13\r\n\r\n";
    stream.write_all(request.as_bytes()).unwrap();

    let mut reader = BufReader::new(stream.try_clone().unwrap());
    let mut status = String::new();
    reader.read_line(&mut status).unwrap();
    assert!(
        status.starts_with("HTTP/1.1 101"),
        "expected a 101 upgrade, got: {status}"
    );

    let mut accept_line = String::new();
    loop {
        let mut line = String::new();
        reader.read_line(&mut line).unwrap();
        if line == "\r\n" {
            break;
        }
        if line.to_ascii_lowercase().starts_with("sec-websocket-accept:") {
            accept_line = line;
        }
    }
    // Matches the RFC 6455 worked example for this exact key, so a wrong accept value fails
    // loudly here instead of surfacing as a mysterious client-side rejection later.
    assert!(
        accept_line.contains("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="),
        "unexpected accept value: {accept_line}"
    );

    stream
}

fn send_masked_text(stream: &mut TcpStream, text: &str) {
    let mask = [0x12u8, 0x34, 0x56, 0x78];
    let payload: Vec<u8> = text
        .bytes()
        .enumerate()
        .map(|(i, b)| b ^ mask[i % 4])
        .collect();

    let mut frame = vec![0x81, 0x80 | (payload.len() as u8)];
    frame.extend_from_slice(&mask);
    frame.extend_from_slice(&payload);
    stream.write_all(&frame).unwrap();
}

fn read_unmasked_text(stream: &mut TcpStream) -> String {
    let mut header = [0u8; 2];
    stream.read_exact(&mut header).unwrap();
    let len = (header[1] & 0x7F) as usize;
    let mut payload = vec![0u8; len];
    stream.read_exact(&mut payload).unwrap();
    String::from_utf8(payload).unwrap()
}

#[test]
fn broadcasts_to_others_but_not_back_to_sender() {
    let addr = start_server();

    let mut alice = connect_and_handshake(addr);
    let mut bob = connect_and_handshake(addr);
    // Give the server a moment to register both clients before Alice speaks — the handshake
    // response already round-tripped, but the registry insert happens right after it.
    thread::sleep(Duration::from_millis(100));

    send_masked_text(&mut alice, "hi bob");

    bob.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
    assert_eq!(read_unmasked_text(&mut bob), "hi bob");

    // Alice should not see her own message echoed back. A short read timeout turns "it hangs
    // forever" into a fast, clear failure if broadcast ever starts including the sender.
    alice
        .set_read_timeout(Some(Duration::from_millis(300)))
        .unwrap();
    let mut probe = [0u8; 1];
    let result = alice.read(&mut probe);
    assert!(
        matches!(&result, Err(e) if e.kind() == std::io::ErrorKind::WouldBlock || e.kind() == std::io::ErrorKind::TimedOut),
        "sender unexpectedly received data: {result:?}"
    );
}

#[test]
fn disconnected_client_is_dropped_and_does_not_break_broadcast() {
    let addr = start_server();

    let alice = connect_and_handshake(addr);
    let mut bob = connect_and_handshake(addr);
    thread::sleep(Duration::from_millis(100));

    drop(alice); // simulate a client vanishing mid-session

    let mut carol = connect_and_handshake(addr);
    thread::sleep(Duration::from_millis(100));

    send_masked_text(&mut carol, "still works");
    bob.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
    assert_eq!(read_unmasked_text(&mut bob), "still works");
}
