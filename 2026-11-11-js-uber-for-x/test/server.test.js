'use strict';
// End-to-end: real HTTP server, raw TCP sockets acting as WebSocket clients.
const test = require('node:test');
const assert = require('node:assert/strict');
const net = require('node:net');
const crypto = require('node:crypto');
const { createApp } = require('../server');
const { encodeFrame, decodeFrame, acceptKey, OP } = require('../ws');

function connect(port) {
  return new Promise((resolve, reject) => {
    const sock = net.connect(port, '127.0.0.1');
    const key = crypto.randomBytes(16).toString('base64');
    let buf = Buffer.alloc(0);
    let handshaken = false;
    const queue = [];
    const waiters = [];
    const deliver = (m) => (waiters.length ? waiters.shift()(m) : queue.push(m));
    sock.on('error', reject);
    sock.on('connect', () => {
      sock.write(
        'GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
          `Sec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`,
      );
    });
    sock.on('data', (chunk) => {
      buf = Buffer.concat([buf, chunk]);
      if (!handshaken) {
        const end = buf.indexOf('\r\n\r\n');
        if (end < 0) return;
        const head = buf.subarray(0, end).toString();
        buf = buf.subarray(end + 4);
        assert.match(head, /^HTTP\/1\.1 101/);
        assert.ok(head.includes(`Sec-WebSocket-Accept: ${acceptKey(key)}`));
        handshaken = true;
        resolve(client);
      }
      let f;
      while ((f = decodeFrame(buf))) {
        buf = buf.subarray(f.length);
        assert.equal(f.masked, false, 'server frames must be unmasked');
        deliver(f.opcode === OP.TEXT ? JSON.parse(f.payload.toString()) : { opcode: f.opcode, payload: f.payload });
      }
    });
    const client = {
      send: (obj) => sock.write(encodeFrame(OP.TEXT, Buffer.from(JSON.stringify(obj)), { mask: true })),
      raw: (frame) => sock.write(frame),
      next: () => new Promise((r) => (queue.length ? r(queue.shift()) : waiters.push(r))),
      close: () => sock.destroy(),
    };
  });
}

test('driver and rider complete a ride over WebSockets', async (t) => {
  const { server } = createApp({ maxKm: 5 });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const { port } = server.address();
  t.after(() => server.close());

  const driver = await connect(port);
  const rider = await connect(port);
  t.after(() => { driver.close(); rider.close(); });

  driver.send({ type: 'hello', role: 'driver', id: 'd1' });
  assert.deepEqual(await driver.next(), { type: 'welcome', role: 'driver', id: 'd1' });
  rider.send({ type: 'hello', role: 'rider', id: 'r1' });
  assert.equal((await rider.next()).type, 'welcome');

  // Ping is answered with a pong carrying the same payload.
  driver.raw(encodeFrame(OP.PING, Buffer.from('hi'), { mask: true }));
  const pong = await driver.next();
  assert.equal(pong.opcode, OP.PONG);
  assert.equal(pong.payload.toString(), 'hi');

  driver.send({ type: 'location', lat: 51.5, lng: -0.12 });
  rider.send({ type: 'request', lat: 51.501, lng: -0.121 });

  const offer = await driver.next();
  assert.equal(offer.type, 'offer');
  assert.ok(offer.distanceKm < 1);
  assert.equal((await rider.next()).ride.status, 'searching');

  driver.send({ type: 'accept', rideId: offer.ride.id });
  const accepted = await rider.next();
  assert.equal(accepted.ride.status, 'accepted');
  assert.equal(accepted.ride.driverId, 'd1');
  assert.deepEqual(accepted.driverLocation, { lat: 51.5, lng: -0.12 });
  assert.equal((await driver.next()).ride.status, 'accepted');

  // Live tracking.
  driver.send({ type: 'location', lat: 51.5005, lng: -0.1205 });
  const moved = await rider.next();
  assert.equal(moved.type, 'driver_location');
  assert.deepEqual(moved.location, { lat: 51.5005, lng: -0.1205 });

  driver.send({ type: 'complete', rideId: offer.ride.id });
  assert.equal((await rider.next()).ride.status, 'completed');
  assert.equal((await driver.next()).ride.status, 'completed');
});

test('protocol errors are reported, not fatal', async (t) => {
  const { server } = createApp();
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  t.after(() => server.close());
  const c = await connect(server.address().port);
  t.after(() => c.close());

  c.send({ type: 'request', lat: 0, lng: 0 });
  assert.match((await c.next()).message, /hello first/);
  c.raw(encodeFrame(OP.TEXT, Buffer.from('{not json'), { mask: true }));
  assert.match((await c.next()).message, /invalid JSON/);
  c.send({ type: 'hello', role: 'admin', id: 'x' });
  assert.match((await c.next()).message, /role/);
  c.send({ type: 'hello', role: 'rider', id: 'r1' });
  assert.equal((await c.next()).type, 'welcome');
  c.send({ type: 'accept', rideId: '1' });
  assert.match((await c.next()).message, /unknown message type for rider/);
  c.send({ type: 'request', lat: 0, lng: 0 });
  assert.equal((await c.next()).ride.status, 'no_drivers');
});
