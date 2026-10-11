'use strict';
// Pure logic for three JavaScript30 exercises; the page (index.html) wires it to the DOM.

// Ex 1 (Drum Kit): map a keyboard keyCode to a sound/pad, ignoring unknown keys.
const PADS = { 65: 'clap', 83: 'hihat', 68: 'kick', 70: 'openhat', 71: 'boom', 72: 'ride', 74: 'snare', 75: 'tom', 76: 'tink' };
function padForKey(keyCode) {
  return Object.prototype.hasOwnProperty.call(PADS, keyCode) ? PADS[keyCode] : null;
}

// Ex 2 (JS + CSS Clock): rotation in degrees of each hand. The +90 offsets the
// CSS default where a rotated element starts pointing at 3 o'clock.
function clockAngles(date) {
  const s = date.getSeconds(), m = date.getMinutes(), h = date.getHours() % 12;
  return {
    second: (s / 60) * 360 + 90,
    minute: ((m + s / 60) / 60) * 360 + 90,
    hour: ((h + m / 60) / 12) * 360 + 90,
  };
}

// Ex 6 (Type Ahead): filter items by a case-insensitive pattern, and wrap matches.
function escapeRegExp(s) { return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'); }
function filterItems(items, query) {
  if (!query) return [];
  const re = new RegExp(escapeRegExp(query), 'i');
  return items.filter((it) => re.test(it));
}
function highlight(text, query) {
  if (!query) return text;
  return text.replace(new RegExp(escapeRegExp(query), 'gi'), (m) => `<span class="hl">${m}</span>`);
}
function withCommas(n) { return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ','); }

const api = { padForKey, clockAngles, filterItems, highlight, withCommas, escapeRegExp, PADS };
if (typeof module !== 'undefined') module.exports = api;
else window.Lib = api;
