'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { Dispatcher } = require('../dispatch');

// Fake timers so offer timeouts are deterministic.
function fakeClock() {
  const timers = new Set();
  return {
    setTimer: (fn) => { const t = { fn }; timers.add(t); return t; },
    clearTimer: (t) => timers.delete(t),
    fireAll: () => { for (const t of [...timers]) { timers.delete(t); t.fn(); } },
    get pending() { return timers.size; },
  };
}

function setup(opts = {}) {
  const clock = fakeClock();
  const d = new Dispatcher({ maxKm: 10, ...clock, ...opts });
  const events = [];
  for (const e of ['ride:offered', 'ride:accepted', 'ride:no_drivers', 'ride:completed', 'ride:cancelled']) {
    d.on(e, (r) => events.push([e, r.driverId]));
  }
  d.upsertDriver('near', { lat: 0, lng: 0.01 });
  d.upsertDriver('mid', { lat: 0, lng: 0.05 });
  d.upsertDriver('far', { lat: 0, lng: 1 }); // outside 10 km
  return { d, clock, events };
}

test('offers the nearest available driver, then accept and complete', () => {
  const { d, events } = setup();
  const ride = d.requestRide('r1', { lat: 0, lng: 0 });
  assert.equal(ride.status, 'offered');
  assert.equal(ride.driverId, 'near');
  assert.equal(d.drivers.get('near').available, false);
  d.accept(ride.id, 'near');
  assert.equal(ride.status, 'accepted');
  d.complete(ride.id, 'near');
  assert.equal(ride.status, 'completed');
  assert.equal(d.drivers.get('near').available, true);
  assert.deepEqual(events.map((e) => e[0]), ['ride:offered', 'ride:accepted', 'ride:completed']);
});

test('decline cascades to the next driver, then no_drivers past the radius', () => {
  const { d, events } = setup();
  const ride = d.requestRide('r1', { lat: 0, lng: 0 });
  d.decline(ride.id, 'near');
  assert.equal(ride.driverId, 'mid');
  assert.equal(d.drivers.get('near').available, true);
  d.decline(ride.id, 'mid');
  assert.equal(ride.status, 'no_drivers'); // 'far' is beyond maxKm
  assert.deepEqual(events, [
    ['ride:offered', 'near'], ['ride:offered', 'mid'], ['ride:no_drivers', null],
  ]);
});

test('an unanswered offer times out and moves on', () => {
  const { d, clock } = setup();
  const ride = d.requestRide('r1', { lat: 0, lng: 0 });
  assert.equal(clock.pending, 1);
  clock.fireAll();
  assert.equal(ride.driverId, 'mid');
  d.accept(ride.id, 'mid');
  assert.equal(clock.pending, 0, 'accepting clears the timeout');
});

test('busy drivers are skipped for concurrent requests', () => {
  const { d } = setup();
  const a = d.requestRide('r1', { lat: 0, lng: 0 });
  const b = d.requestRide('r2', { lat: 0, lng: 0 });
  assert.equal(a.driverId, 'near');
  assert.equal(b.driverId, 'mid');
  const c = d.requestRide('r3', { lat: 0, lng: 0 });
  assert.equal(c.status, 'no_drivers');
});

test('only the offered driver can accept, and only once', () => {
  const { d } = setup();
  const ride = d.requestRide('r1', { lat: 0, lng: 0 });
  assert.throws(() => d.accept(ride.id, 'mid'), /no pending offer/);
  d.accept(ride.id, 'near');
  assert.throws(() => d.accept(ride.id, 'near'), /no pending offer/);
  assert.throws(() => d.complete(ride.id, 'mid'), /not in progress/);
});

test('driver disconnecting mid-offer re-dispatches; rider cancel frees the driver', () => {
  const { d, clock } = setup();
  const ride = d.requestRide('r1', { lat: 0, lng: 0 });
  d.removeDriver('near');
  assert.equal(ride.driverId, 'mid');
  d.accept(ride.id, 'mid');
  d.cancel(ride.id, 'r1');
  assert.equal(ride.status, 'cancelled');
  assert.equal(d.drivers.get('mid').available, true);
  assert.equal(clock.pending, 0);
  assert.throws(() => d.cancel(ride.id, 'r1'), /already finished/);
});

test('rejects invalid coordinates', () => {
  const { d } = setup();
  assert.throws(() => d.requestRide('r1', { lat: 200, lng: 0 }), /invalid pickup/);
  assert.throws(() => d.upsertDriver('x', { lat: 0 }), /invalid location/);
});
