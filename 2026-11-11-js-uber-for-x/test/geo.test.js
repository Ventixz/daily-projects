'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { haversineKm, nearest, isValidPoint } = require('../geo');

test('haversine matches known distances', () => {
  const london = { lat: 51.5074, lng: -0.1278 };
  const paris = { lat: 48.8566, lng: 2.3522 };
  assert.ok(Math.abs(haversineKm(london, paris) - 343.5) < 1.5);
  assert.equal(haversineKm(london, london), 0);
  // Antipodes: half the circumference.
  const d = haversineKm({ lat: 0, lng: 0 }, { lat: 0, lng: 180 });
  assert.ok(Math.abs(d - Math.PI * 6371) < 1e-6);
});

test('nearest sorts by distance and applies radius and limit', () => {
  const origin = { lat: 0, lng: 0 };
  const items = [
    { id: 'far', location: { lat: 0, lng: 1 } },     // ~111 km
    { id: 'near', location: { lat: 0, lng: 0.01 } }, // ~1.1 km
    { id: 'mid', location: { lat: 0.05, lng: 0 } },  // ~5.6 km
    { id: 'bad', location: { lat: 999, lng: 0 } },
  ];
  assert.deepEqual(nearest(items, origin).map((r) => r.item.id), ['near', 'mid', 'far']);
  assert.deepEqual(nearest(items, origin, { maxKm: 10 }).map((r) => r.item.id), ['near', 'mid']);
  assert.deepEqual(nearest(items, origin, { limit: 1 }).map((r) => r.item.id), ['near']);
});

test('isValidPoint rejects out-of-range and non-numeric input', () => {
  assert.ok(isValidPoint({ lat: -90, lng: 180 }));
  assert.ok(!isValidPoint({ lat: 91, lng: 0 }));
  assert.ok(!isValidPoint({ lat: '1', lng: 0 }));
  assert.ok(!isValidPoint({ lat: NaN, lng: 0 }));
  assert.ok(!isValidPoint(null));
});
