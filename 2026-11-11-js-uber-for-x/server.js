'use strict';

const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const { Dispatcher } = require('./dispatch');
const { upgrade } = require('./ws');

const INDEX = path.join(__dirname, 'public', 'index.html');

function createApp(options = {}) {
  const dispatcher = new Dispatcher(options);
  const drivers = new Map(); // driverId -> conn
  const riders = new Map();  // riderId -> conn

  const toDriver = (id, msg) => drivers.get(id)?.sendJSON(msg);
  const toRider = (id, msg) => riders.get(id)?.sendJSON(msg);

  dispatcher.on('ride:offered', (r) => {
    toDriver(r.driverId, { type: 'offer', ride: strip(r), distanceKm: round(r.distanceKm) });
    toRider(r.riderId, { type: 'ride', ride: { ...strip(r), driverId: null, status: 'searching' } });
  });
  dispatcher.on('ride:accepted', (r) => {
    const d = dispatcher.drivers.get(r.driverId);
    toRider(r.riderId, { type: 'ride', ride: r, driverLocation: d?.location });
    toDriver(r.driverId, { type: 'ride', ride: r });
  });
  dispatcher.on('ride:no_drivers', (r) => toRider(r.riderId, { type: 'ride', ride: r }));
  dispatcher.on('ride:completed', (r) => {
    toRider(r.riderId, { type: 'ride', ride: r });
    toDriver(r.driverId, { type: 'ride', ride: r });
  });
  dispatcher.on('ride:cancelled', (r) => toRider(r.riderId, { type: 'ride', ride: r }));
  // Live tracking: tell the rider where their assigned driver is.
  dispatcher.on('driver:moved', ({ driverId, location }) => {
    for (const ride of dispatcher.rides.values()) {
      if (ride.status === 'accepted' && ride.driverId === driverId) {
        toRider(ride.riderId, { type: 'driver_location', rideId: ride.id, location });
      }
    }
  });

  const handlers = {
    driver: {
      location: (id, m) => dispatcher.upsertDriver(id, { lat: m.lat, lng: m.lng }),
      accept: (id, m) => dispatcher.accept(String(m.rideId), id),
      decline: (id, m) => dispatcher.decline(String(m.rideId), id),
      complete: (id, m) => dispatcher.complete(String(m.rideId), id),
    },
    rider: {
      request: (id, m) => dispatcher.requestRide(id, { lat: m.lat, lng: m.lng }),
      cancel: (id, m) => dispatcher.cancel(String(m.rideId), id),
    },
  };

  function onConnection(conn) {
    let role = null;
    let id = null;

    conn.on('message', (text) => {
      let msg;
      try {
        msg = JSON.parse(text);
      } catch {
        return conn.sendJSON({ type: 'error', message: 'invalid JSON' });
      }
      try {
        if (msg.type === 'hello') {
          if (role) throw new Error('already identified');
          if (!['driver', 'rider'].includes(msg.role)) throw new Error('role must be driver or rider');
          if (typeof msg.id !== 'string' || !/^[\w-]{1,64}$/.test(msg.id)) throw new Error('invalid id');
          const registry = msg.role === 'driver' ? drivers : riders;
          if (registry.has(msg.id)) throw new Error('id already connected');
          role = msg.role;
          id = msg.id;
          registry.set(id, conn);
          return conn.sendJSON({ type: 'welcome', role, id });
        }
        if (!role) throw new Error('send hello first');
        const handler = handlers[role][msg.type];
        if (!handler) throw new Error(`unknown message type for ${role}: ${msg.type}`);
        handler(id, msg);
      } catch (err) {
        conn.sendJSON({ type: 'error', message: err.message });
      }
    });

    conn.on('close', () => {
      if (role === 'driver') {
        drivers.delete(id);
        dispatcher.removeDriver(id);
      } else if (role === 'rider') {
        riders.delete(id);
      }
    });
  }

  const server = http.createServer((req, res) => {
    if (req.method === 'GET' && req.url === '/health') {
      const body = JSON.stringify({ ok: true, drivers: dispatcher.drivers.size, rides: dispatcher.rides.size });
      res.writeHead(200, { 'Content-Type': 'application/json' });
      return res.end(body);
    }
    if (req.method === 'GET' && req.url === '/') {
      return fs.readFile(INDEX, (err, html) => {
        if (err) {
          res.writeHead(500);
          return res.end('missing index.html');
        }
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
        res.end(html);
      });
    }
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('not found');
  });

  server.on('upgrade', (req, socket) => {
    if (req.url !== '/ws') return socket.end('HTTP/1.1 404 Not Found\r\n\r\n');
    const conn = upgrade(req, socket);
    if (conn) onConnection(conn);
  });

  return { server, dispatcher };
}

function strip(r) {
  const { id, riderId, pickup, status, driverId } = r;
  return { id, riderId, pickup, status, driverId };
}
const round = (n) => Math.round(n * 100) / 100;

if (require.main === module) {
  const port = Number(process.env.PORT) || 3000;
  createApp().server.listen(port, () => console.log(`dispatch listening on http://localhost:${port}`));
}

module.exports = { createApp };
