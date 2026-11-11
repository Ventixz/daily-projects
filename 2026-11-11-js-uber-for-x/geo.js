'use strict';

const EARTH_RADIUS_KM = 6371;
const toRad = (deg) => (deg * Math.PI) / 180;

function isValidPoint(p) {
  return (
    p != null &&
    Number.isFinite(p.lat) && Number.isFinite(p.lng) &&
    p.lat >= -90 && p.lat <= 90 &&
    p.lng >= -180 && p.lng <= 180
  );
}

// Great-circle distance between two {lat, lng} points, in kilometres.
function haversineKm(a, b) {
  const dLat = toRad(b.lat - a.lat);
  const dLng = toRad(b.lng - a.lng);
  const h =
    Math.sin(dLat / 2) ** 2 +
    Math.cos(toRad(a.lat)) * Math.cos(toRad(b.lat)) * Math.sin(dLng / 2) ** 2;
  return 2 * EARTH_RADIUS_KM * Math.asin(Math.min(1, Math.sqrt(h)));
}

// Return up to `limit` candidates within `maxKm` of `origin`, nearest first.
// Each candidate must have a `location` field. This is the job MongoDB's
// $near / 2dsphere index does in the original tutorial; a linear scan is
// fine for a few thousand drivers.
function nearest(candidates, origin, { maxKm = Infinity, limit = Infinity } = {}) {
  const out = [];
  for (const c of candidates) {
    if (!isValidPoint(c.location)) continue;
    const distanceKm = haversineKm(origin, c.location);
    if (distanceKm <= maxKm) out.push({ item: c, distanceKm });
  }
  out.sort((x, y) => x.distanceKm - y.distanceKm);
  return out.slice(0, limit);
}

module.exports = { haversineKm, nearest, isValidPoint, EARTH_RADIUS_KM };
