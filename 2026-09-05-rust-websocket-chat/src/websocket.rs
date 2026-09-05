//! RFC 6455 handshake and frame (de)coding — the two protocol pieces underneath every
//! WebSocket library. Deliberately narrow: single-frame messages only (no continuation
//! frames), which covers every message a browser or a simple client actually sends for a
//! chat line.

use crate::{base64, sha1};
use std::io::{self, Read};

const WS_GUID: &str = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/// Computes the `Sec-WebSocket-Accept` value the server hands back during the handshake:
/// SHA-1 of the client's key concatenated with the protocol's fixed magic GUID, base64-encoded.
pub fn accept_key(client_key: &str) -> String {
    let mut combined = Vec::with_capacity(client_key.len() + WS_GUID.len());
    combined.extend_from_slice(client_key.as_bytes());
    combined.extend_from_slice(WS_GUID.as_bytes());
    base64::encode(&sha1::sha1(&combined))
}

/// Pulls `Sec-WebSocket-Key` out of the raw HTTP upgrade request (header lines only, no
/// leading request line needed since we don't care about the path).
pub fn parse_websocket_key(request: &str) -> Option<String> {
    request.lines().find_map(|line| {
        let (name, value) = line.split_once(':')?;
        name.trim()
            .eq_ignore_ascii_case("Sec-WebSocket-Key")
            .then(|| value.trim().to_string())
    })
}

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum Opcode {
    Continuation,
    Text,
    Binary,
    Close,
    Ping,
    Pong,
    Other(u8),
}

impl Opcode {
    fn from_u8(b: u8) -> Opcode {
        match b {
            0x0 => Opcode::Continuation,
            0x1 => Opcode::Text,
            0x2 => Opcode::Binary,
            0x8 => Opcode::Close,
            0x9 => Opcode::Ping,
            0xA => Opcode::Pong,
            other => Opcode::Other(other),
        }
    }

    fn to_u8(self) -> u8 {
        match self {
            Opcode::Continuation => 0x0,
            Opcode::Text => 0x1,
            Opcode::Binary => 0x2,
            Opcode::Close => 0x8,
            Opcode::Ping => 0x9,
            Opcode::Pong => 0xA,
            Opcode::Other(b) => b,
        }
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct Frame {
    pub fin: bool,
    pub opcode: Opcode,
    pub payload: Vec<u8>,
}

/// Reads one frame off `r`. Per RFC 6455 section 5.1, every frame a *client* sends MUST be
/// masked; this unmasks it transparently so callers only ever see plain payload bytes.
pub fn read_frame<R: Read>(r: &mut R) -> io::Result<Frame> {
    let mut header = [0u8; 2];
    r.read_exact(&mut header)?;

    let fin = header[0] & 0x80 != 0;
    let opcode = Opcode::from_u8(header[0] & 0x0F);
    let masked = header[1] & 0x80 != 0;
    let mut len = (header[1] & 0x7F) as u64;

    if len == 126 {
        let mut ext = [0u8; 2];
        r.read_exact(&mut ext)?;
        len = u16::from_be_bytes(ext) as u64;
    } else if len == 127 {
        let mut ext = [0u8; 8];
        r.read_exact(&mut ext)?;
        len = u64::from_be_bytes(ext);
    }

    let mask_key = if masked {
        let mut key = [0u8; 4];
        r.read_exact(&mut key)?;
        Some(key)
    } else {
        None
    };

    let mut payload = vec![0u8; len as usize];
    r.read_exact(&mut payload)?;
    if let Some(key) = mask_key {
        for (i, byte) in payload.iter_mut().enumerate() {
            *byte ^= key[i % 4];
        }
    }

    Ok(Frame {
        fin,
        opcode,
        payload,
    })
}

/// Encodes a single unfragmented, unmasked frame — what a server is required to send back
/// (RFC 6455 5.1: "a server MUST NOT mask any frames it sends to the client").
pub fn encode_frame(opcode: Opcode, payload: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(payload.len() + 10);
    out.push(0x80 | opcode.to_u8()); // FIN=1, no fragmentation support needed for chat lines

    let len = payload.len();
    if len <= 125 {
        out.push(len as u8);
    } else if len <= 0xFFFF {
        out.push(126);
        out.extend_from_slice(&(len as u16).to_be_bytes());
    } else {
        out.push(127);
        out.extend_from_slice(&(len as u64).to_be_bytes());
    }

    out.extend_from_slice(payload);
    out
}
