'use strict';

const { EventEmitter } = require('node:events');
const { nearest, isValidPoint } = require('./geo');

// Ride lifecycle:
//   searching -> offered -> accepted -> completed
//        ^          |
//        +-declined/timeout (next nearest driver)
//   searching/offered -> no_drivers | cancelled
class Dispatcher extends EventEmitter {
  constructor({ maxKm = 10, offerTimeoutMs = 15000, setTimer = setTimeout, clearTimer = clearTimeout } = {}) {
    super();
    this.maxKm = maxKm;
    this.offerTimeoutMs = offerTimeoutMs;
    this.setTimer = setTimer;
    this.clearTimer = clearTimer;
    this.drivers = new Map(); // id -> { id, location, available }
    this.rides = new Map();   // id -> ride
    this.nextRideId = 1;
  }

  upsertDriver(id, location) {
    if (!isValidPoint(location)) throw new Error('invalid location');
    const d = this.drivers.get(id) || { id, available: true, location };
    d.location = { lat: location.lat, lng: location.lng };
    this.drivers.set(id, d);
    this.emit('driver:moved', { driverId: id, location: d.location });
    return d;
  }

  removeDriver(id) {
    const d = this.drivers.get(id);
    if (!d) return;
    this.drivers.delete(id);
    // A driver vanishing mid-offer counts as a decline.
    for (const ride of this.rides.values()) {
      if (ride.status === 'offered' && ride.driverId === id) this._advance(ride);
    }
  }

  requestRide(riderId, pickup) {
    if (!isValidPoint(pickup)) throw new Error('invalid pickup');
    const ride = {
      id: String(this.nextRideId++),
      riderId,
      pickup: { lat: pickup.lat, lng: pickup.lng },
      status: 'searching',
      driverId: null,
      tried: new Set(),
      timer: null,
    };
    this.rides.set(ride.id, ride);
    this._offerNext(ride);
    return ride;
  }

  accept(rideId, driverId) {
    const ride = this._offeredTo(rideId, driverId);
    this._clear(ride);
    ride.status = 'accepted';
    this.emit('ride:accepted', this.view(ride));
    return ride;
  }

  decline(rideId, driverId) {
    const ride = this._offeredTo(rideId, driverId);
    this._advance(ride);
    return ride;
  }

  complete(rideId, driverId) {
    const ride = this.rides.get(rideId);
    if (!ride || ride.status !== 'accepted' || ride.driverId !== driverId) {
      throw new Error('ride not in progress for this driver');
    }
    ride.status = 'completed';
    const d = this.drivers.get(driverId);
    if (d) d.available = true;
    this.emit('ride:completed', this.view(ride));
    return ride;
  }

  cancel(rideId, riderId) {
    const ride = this.rides.get(rideId);
    if (!ride || ride.riderId !== riderId) throw new Error('no such ride');
    if (!['searching', 'offered', 'accepted'].includes(ride.status)) throw new Error('ride already finished');
    this._clear(ride);
    this._releaseDriver(ride);
    ride.status = 'cancelled';
    this.emit('ride:cancelled', this.view(ride));
    return ride;
  }

  // Public, serialisable snapshot (no timers or Sets).
  view(ride) {
    const { id, riderId, pickup, status, driverId } = ride;
    return { id, riderId, pickup, status, driverId };
  }

  _offeredTo(rideId, driverId) {
    const ride = this.rides.get(rideId);
    if (!ride || ride.status !== 'offered' || ride.driverId !== driverId) {
      throw new Error('no pending offer for this driver');
    }
    return ride;
  }

  _advance(ride) {
    this._clear(ride);
    this._releaseDriver(ride);
    this._offerNext(ride);
  }

  _offerNext(ride) {
    const free = [...this.drivers.values()].filter((d) => d.available && !ride.tried.has(d.id));
    const [best] = nearest(free, ride.pickup, { maxKm: this.maxKm, limit: 1 });
    if (!best) {
      ride.status = 'no_drivers';
      ride.driverId = null;
      this.emit('ride:no_drivers', this.view(ride));
      return;
    }
    const driver = best.item;
    driver.available = false; // reserve while the offer is open
    ride.tried.add(driver.id);
    ride.driverId = driver.id;
    ride.status = 'offered';
    ride.timer = this.setTimer(() => {
      ride.timer = null;
      if (ride.status === 'offered' && ride.driverId === driver.id) this._advance(ride);
    }, this.offerTimeoutMs);
    this.emit('ride:offered', { ...this.view(ride), distanceKm: best.distanceKm });
  }

  _releaseDriver(ride) {
    const d = ride.driverId && this.drivers.get(ride.driverId);
    if (d) d.available = true;
    ride.driverId = null;
  }

  _clear(ride) {
    if (ride.timer) this.clearTimer(ride.timer);
    ride.timer = null;
  }
}

module.exports = { Dispatcher };
