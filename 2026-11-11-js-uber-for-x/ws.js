'use strict';

// A minimal RFC 6455 WebSocket implementation: enough for JSON text messages.
// Supports: handshake, masked client frames, 7/16/64-bit lengths, ping/pong,
// close, and fragmented (continuation) text messages.

const crypto = require('node:crypto');
const { EventEmitter } = require('node:events');

const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const OP = { CONT: 0x0, TEXT: 0x1, BINARY: 0x2, CLOSE: 0x8, PING: 0x9, PONG: 0xa };
const MAX_PAYLOAD = 1 << 20; // 1 MiB is plenty for dispatch messages

function acceptKey(key) {
  return crypto.createHash('sha1').update(key + GUID).digest('base64');
}

// Encode one unfragmented frame. Servers must not mask; clients must.
function encodeFrame(opcode, payload = Buffer.alloc(0), { mask = false } = {}) {
  const data = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  const len = data.length;
  let header;
  if (len < 126) {
    header = Buffer.alloc(2);
    header[1] = len;
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[1] = 126;
    header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(len), 2);
  }
  header[0] = 0x80 | opcode; // FIN + opcode
  if (!mask) return Buffer.concat([header, data]);
  header[1] |= 0x80;
  const key = crypto.randomBytes(4);
  const masked = Buffer.alloc(len);
  for (let i = 0; i < len; i++) masked[i] = data[i] ^ key[i & 3];
  return Buffer.concat([header, key, masked]);
}

// Try to decode one frame from the front of `buf`.
// Returns null if more bytes are needed, else { fin, opcode, payload, masked, length }.
function decodeFrame(buf) {
  if (buf.length < 2) return null;
  const fin = (buf[0] & 0x80) !== 0;
  if (buf[0] & 0x70) throw new Error('reserved bits set');
  const opcode = buf[0] & 0x0f;
  const masked = (buf[1] & 0x80) !== 0;
  let len = buf[1] & 0x7f;
  let offset = 2;
  if (len === 126) {
    if (buf.length < 4) return null;
    len = buf.readUInt16BE(2);
    offset = 4;
  } else if (len === 127) {
    if (buf.length < 10) return null;
    const big = buf.readBigUInt64BE(2);
    if (big > BigInt(MAX_PAYLOAD)) throw new Error('frame too large');
    len = Number(big);
    offset = 10;
  }
  if (len > MAX_PAYLOAD) throw new Error('frame too large');
  if (opcode >= 0x8 && (len > 125 || !fin)) throw new Error('invalid control frame');
  const keyLen = masked ? 4 : 0;
  if (buf.length < offset + keyLen + len) return null;
  let payload = buf.subarray(offset + keyLen, offset + keyLen + len);
  if (masked) {
    const key = buf.subarray(offset, offset + 4);
    const out = Buffer.alloc(len);
    for (let i = 0; i < len; i++) out[i] = payload[i] ^ key[i & 3];
    payload = out;
  } else {
    payload = Buffer.from(payload);
  }
  return { fin, opcode, masked, payload, length: offset + keyLen + len };
}

// Wraps an upgraded socket. Emits 'message' (string), 'close'.
class WebSocketConnection extends EventEmitter {
  constructor(socket) {
    super();
    this.socket = socket;
    this.buffer = Buffer.alloc(0);
    this.fragments = null;
    this.closed = false;
    socket.on('data', (chunk) => this._onData(chunk));
    socket.on('close', () => this._finish());
    socket.on('error', () => this._finish());
  }

  send(text) {
    if (this.closed) return;
    this.socket.write(encodeFrame(OP.TEXT, Buffer.from(String(text), 'utf8')));
  }

  sendJSON(obj) {
    this.send(JSON.stringify(obj));
  }

  close(code = 1000) {
    if (this.closed) return;
    const body = Buffer.alloc(2);
    body.writeUInt16BE(code, 0);
    this.socket.end(encodeFrame(OP.CLOSE, body));
    this._finish();
  }

  _finish() {
    if (this.closed) return;
    this.closed = true;
    this.emit('close');
  }

  _onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    try {
      let frame;
      while (!this.closed && (frame = decodeFrame(this.buffer))) {
        this.buffer = this.buffer.subarray(frame.length);
        // RFC 6455 §5.1: a server MUST close on unmasked client frames.
        if (!frame.masked) return this.close(1002);
        this._onFrame(frame);
      }
    } catch {
      this.close(1002); // protocol error
    }
  }

  _onFrame({ fin, opcode, payload }) {
    switch (opcode) {
      case OP.PING:
        this.socket.write(encodeFrame(OP.PONG, payload));
        return;
      case OP.PONG:
        return;
      case OP.CLOSE:
        this.close(1000);
        return;
      case OP.TEXT:
        if (this.fragments) throw new Error('new message during fragmented message');
        if (fin) return this.emit('message', payload.toString('utf8'));
        this.fragments = [payload];
        return;
      case OP.CONT:
        if (!this.fragments) throw new Error('unexpected continuation');
        this.fragments.push(payload);
        if (fin) {
          const msg = Buffer.concat(this.fragments).toString('utf8');
          this.fragments = null;
          this.emit('message', msg);
        }
        return;
      default:
        this.close(1003); // binary or unknown: unsupported data
    }
  }
}

// Perform the server side of the opening handshake on an http 'upgrade' event.
// Returns a WebSocketConnection, or null (and rejects) if the request is bad.
function upgrade(req, socket) {
  const key = req.headers['sec-websocket-key'];
  const isWs = String(req.headers.upgrade || '').toLowerCase() === 'websocket';
  if (!isWs || !key || req.headers['sec-websocket-version'] !== '13') {
    socket.end('HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n');
    return null;
  }
  socket.write(
    'HTTP/1.1 101 Switching Protocols\r\n' +
      'Upgrade: websocket\r\n' +
      'Connection: Upgrade\r\n' +
      `Sec-WebSocket-Accept: ${acceptKey(key)}\r\n\r\n`,
  );
  socket.setNoDelay(true);
  return new WebSocketConnection(socket);
}

module.exports = { acceptKey, encodeFrame, decodeFrame, upgrade, WebSocketConnection, OP };
