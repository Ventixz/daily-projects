"""Wire protocol: every message is a u32 big-endian length followed by that many bytes of JSON.

Requests look like {"api": "produce", "id": 7, ...}; responses echo "id" and carry
either the result fields or {"error": "<code>", "message": "..."}.
Keys and values are bytes, so they travel base64-encoded.
"""

from __future__ import annotations

import base64
import json
import socket
import struct

MAX_FRAME = 64 * 1024 * 1024
_LEN = struct.Struct(">I")


class ProtocolError(Exception):
    pass


class BrokerError(Exception):
    """An error the broker reported back (e.g. unknown_topic, rebalance_in_progress)."""

    def __init__(self, code: str, message: str = ""):
        super().__init__(f"{code}: {message}" if message else code)
        self.code = code


def _recv_exact(sock: socket.socket, n: int) -> bytes | None:
    chunks = []
    while n:
        chunk = sock.recv(min(n, 1 << 16))
        if not chunk:
            if chunks:
                raise ProtocolError("connection closed mid-frame")
            return None
        chunks.append(chunk)
        n -= len(chunk)
    return b"".join(chunks)


def send_frame(sock: socket.socket, obj: dict) -> None:
    data = json.dumps(obj, separators=(",", ":")).encode()
    if len(data) > MAX_FRAME:
        raise ProtocolError("frame too large")
    sock.sendall(_LEN.pack(len(data)) + data)


def recv_frame(sock: socket.socket) -> dict | None:
    """Return the next message, or None on a clean close between frames."""
    head = _recv_exact(sock, 4)
    if head is None:
        return None
    (length,) = _LEN.unpack(head)
    if length > MAX_FRAME:
        raise ProtocolError(f"frame of {length} bytes exceeds limit")
    body = _recv_exact(sock, length) if length else b""
    if body is None:
        raise ProtocolError("connection closed mid-frame")
    try:
        obj = json.loads(body)
    except ValueError as e:
        raise ProtocolError(f"bad JSON: {e}") from None
    if not isinstance(obj, dict):
        raise ProtocolError("frame is not a JSON object")
    return obj


def b64(data: bytes | None) -> str | None:
    return None if data is None else base64.b64encode(data).decode()


def unb64(text: str | None) -> bytes | None:
    if text is None:
        return None
    try:
        return base64.b64decode(text, validate=True)
    except (ValueError, TypeError):
        raise ProtocolError("bad base64") from None
