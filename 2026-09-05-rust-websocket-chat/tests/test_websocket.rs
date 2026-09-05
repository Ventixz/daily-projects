use std::io::Cursor;
use wschat::websocket::{accept_key, encode_frame, parse_websocket_key, read_frame, Opcode};

#[test]
fn accept_key_matches_rfc6455_worked_example() {
    // RFC 6455 section 1.3's own handshake example.
    assert_eq!(
        accept_key("dGhlIHNhbXBsZSBub25jZQ=="),
        "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
    );
}

#[test]
fn parses_key_out_of_raw_header_block() {
    let request = "GET /chat HTTP/1.1\r\n\
                    Host: example.com\r\n\
                    Upgrade: websocket\r\n\
                    Connection: Upgrade\r\n\
                    Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\
                    Sec-WebSocket-Version: 13\r\n";
    assert_eq!(
        parse_websocket_key(request).as_deref(),
        Some("dGhlIHNhbXBsZSBub25jZQ==")
    );
}

#[test]
fn missing_key_returns_none() {
    let request = "GET / HTTP/1.1\r\nHost: example.com\r\n";
    assert_eq!(parse_websocket_key(request), None);
}

#[test]
fn decodes_rfc6455_masked_client_frame() {
    // RFC 6455 section 5.7's worked example: a client sending unmasked text "Hello",
    // masked on the wire with key 37 fa 21 3d.
    let wire: &[u8] = &[0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58];
    let mut cursor = Cursor::new(wire);
    let frame = read_frame(&mut cursor).unwrap();

    assert!(frame.fin);
    assert_eq!(frame.opcode, Opcode::Text);
    assert_eq!(frame.payload, b"Hello");
}

#[test]
fn decodes_rfc6455_unmasked_server_frame() {
    // Same section: the server's reply, sent unmasked.
    let wire: &[u8] = &[0x81, 0x05, b'H', b'e', b'l', b'l', b'o'];
    let mut cursor = Cursor::new(wire);
    let frame = read_frame(&mut cursor).unwrap();

    assert!(frame.fin);
    assert_eq!(frame.opcode, Opcode::Text);
    assert_eq!(frame.payload, b"Hello");
}

#[test]
fn encode_frame_matches_rfc6455_unmasked_example() {
    assert_eq!(
        encode_frame(Opcode::Text, b"Hello"),
        vec![0x81, 0x05, b'H', b'e', b'l', b'l', b'o']
    );
}

#[test]
fn encode_then_decode_round_trips_for_longer_payloads() {
    // 126 spills into the 16-bit extended length, 70_000 past 0xFFFF spills into the 64-bit form.
    for len in [0, 1, 125, 126, 300, 70_000] {
        let payload: Vec<u8> = (0..len).map(|i| (i % 256) as u8).collect();
        let wire = encode_frame(Opcode::Binary, &payload);
        let mut cursor = Cursor::new(wire.as_slice());
        let frame = read_frame(&mut cursor).unwrap();

        assert_eq!(frame.opcode, Opcode::Binary);
        assert_eq!(frame.payload, payload, "round trip failed for len={len}");
    }
}
