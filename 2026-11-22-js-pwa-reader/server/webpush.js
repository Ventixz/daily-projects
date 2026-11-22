// Web Push sender, written from the RFCs with only node:crypto.
//
//   RFC 8291  Message Encryption for Web Push (the key derivation)
//   RFC 8188  Encrypted Content-Encoding for HTTP ("aes128gcm", the framing)
//   RFC 8292  VAPID (proving to the push service who is sending)
//   RFC 8030  Generic Event Delivery Using HTTP Push (TTL, Urgency, Topic)
//
// The browser gives us a subscription: an endpoint URL on its vendor's push
// service, plus two keys. `p256dh` is the browser's P-256 public key and `auth`
// is a 16-byte shared secret. We encrypt so that only that browser can read
// the payload. The push service only relays opaque bytes.

import crypto from 'node:crypto';

export const RECORD_SIZE = 4096;
// The whole encrypted body must fit in 4096 bytes (RFC 8291 §4). The header is
// 16 (salt) + 4 (rs) + 1 (idlen) + 65 (key) = 86 bytes. AES-GCM adds a 16-byte
// tag. The padding delimiter takes 1 byte. That leaves 3993 bytes of plaintext.
export const MAX_PLAINTEXT = RECORD_SIZE - 86 - 16 - 1;

export function b64url(buf) {
  return Buffer.from(buf).toString('base64url');
}

export function fromB64url(str) {
  if (typeof str !== 'string' || !/^[A-Za-z0-9_-]*={0,2}$/.test(str)) {
    throw new TypeError('expected a base64url string');
  }
  return Buffer.from(str, 'base64url');
}

function hmac(key, data) {
  return crypto.createHmac('sha256', key).update(data).digest();
}

/**
 * The RFC 8291 §3.4 key schedule. It is exported so the tests can compare
 * every intermediate value with RFC 8291 Appendix A.
 */
export function deriveKeys({ ecdhSecret, authSecret, uaPublic, asPublic, salt }) {
  const keyInfo = Buffer.concat([Buffer.from('WebPush: info\0'), uaPublic, asPublic]);
  // HKDF with SHA-256, written out. Each output fits in one HMAC block, so
  // "expand" is a single HMAC over info || 0x01.
  const prkKey = hmac(authSecret, ecdhSecret);
  const ikm = hmac(prkKey, Buffer.concat([keyInfo, Buffer.from([1])]));
  const prk = hmac(salt, ikm);
  const cek = hmac(prk, Buffer.from('Content-Encoding: aes128gcm\0\x01')).subarray(0, 16);
  const nonce = hmac(prk, Buffer.from('Content-Encoding: nonce\0\x01')).subarray(0, 12);
  return { prkKey, ikm, prk, cek, nonce };
}

/**
 * Encrypt one push message payload for one subscription.
 *
 * `options.asPrivateKey` and `options.salt` exist only so the RFC test vector
 * can be reproduced. Normal callers leave them unset: a fresh ephemeral key
 * pair and a fresh salt are then used for every message. Reusing either one
 * would reuse the AES-GCM key and nonce, which breaks GCM completely.
 */
export function encrypt(payload, subscriptionKeys, options = {}) {
  const plaintext = Buffer.isBuffer(payload) ? payload : Buffer.from(String(payload), 'utf8');
  if (plaintext.length > MAX_PLAINTEXT) {
    throw new RangeError(`payload is ${plaintext.length} bytes; the limit is ${MAX_PLAINTEXT}`);
  }
  const uaPublic = fromB64url(subscriptionKeys.p256dh);
  const authSecret = fromB64url(subscriptionKeys.auth);
  if (uaPublic.length !== 65 || uaPublic[0] !== 0x04) {
    throw new TypeError('p256dh must be an uncompressed P-256 point (65 bytes, leading 0x04)');
  }
  if (authSecret.length !== 16) throw new TypeError('auth must be 16 bytes');

  const ecdh = crypto.createECDH('prime256v1');
  if (options.asPrivateKey) ecdh.setPrivateKey(fromB64url(options.asPrivateKey));
  else ecdh.generateKeys();
  const asPublic = ecdh.getPublicKey(); // uncompressed, 65 bytes
  // Throws if the browser's key is not on the curve.
  const ecdhSecret = ecdh.computeSecret(uaPublic);
  const salt = options.salt ? fromB64url(options.salt) : crypto.randomBytes(16);

  const { cek, nonce } = deriveKeys({ ecdhSecret, authSecret, uaPublic, asPublic, salt });

  // A single record. It is also the last record, so the padding delimiter is
  // 0x02 (RFC 8188 §2). We add no padding bytes after it.
  const cipher = crypto.createCipheriv('aes-128-gcm', cek, nonce);
  const ciphertext = Buffer.concat([cipher.update(plaintext), cipher.update(Buffer.from([2])), cipher.final()]);
  const tag = cipher.getAuthTag();

  const header = Buffer.alloc(16 + 4 + 1);
  salt.copy(header, 0);
  header.writeUInt32BE(RECORD_SIZE, 16);
  header.writeUInt8(asPublic.length, 20);

  return Buffer.concat([header, asPublic, ciphertext, tag]);
}

// ---------------------------------------------------------------- VAPID

export function generateVapidKeys() {
  const ecdh = crypto.createECDH('prime256v1');
  ecdh.generateKeys();
  return { publicKey: b64url(ecdh.getPublicKey()), privateKey: b64url(ecdh.getPrivateKey()) };
}

function vapidKeyObject({ publicKey, privateKey }) {
  const pub = fromB64url(publicKey);
  const priv = fromB64url(privateKey);
  if (pub.length !== 65 || priv.length !== 32) throw new TypeError('malformed VAPID key pair');
  return crypto.createPrivateKey({
    key: { kty: 'EC', crv: 'P-256', d: b64url(priv), x: b64url(pub.subarray(1, 33)), y: b64url(pub.subarray(33)) },
    format: 'jwk',
  });
}

/**
 * Build the VAPID Authorization header value (RFC 8292 §3). The JWT is signed
 * with ES256. Its audience is the origin of the push endpoint, not the full
 * URL. RFC 8292 caps the expiry at 24 hours, and we use 12.
 */
export function vapidAuthorization(endpoint, vapid, now = Date.now()) {
  if (!/^(mailto:|https:)/.test(vapid.subject || '')) {
    throw new TypeError('VAPID subject must be a mailto: or https: URI');
  }
  const header = b64url(JSON.stringify({ typ: 'JWT', alg: 'ES256' }));
  const claims = b64url(JSON.stringify({
    aud: new URL(endpoint).origin,
    exp: Math.floor(now / 1000) + 12 * 3600,
    sub: vapid.subject,
  }));
  const signingInput = `${header}.${claims}`;
  // JWS needs the raw r||s signature (64 bytes), not DER, so ask for IEEE P1363.
  const signature = crypto.sign('sha256', Buffer.from(signingInput), {
    key: vapidKeyObject(vapid),
    dsaEncoding: 'ieee-p1363',
  });
  return `vapid t=${signingInput}.${b64url(signature)}, k=${vapid.publicKey}`;
}

// ---------------------------------------------------------------- sending

/**
 * POST one encrypted message to a subscription's push service.
 *
 * Resolves to `{ status, gone }`. `gone` is true for 404 and 410, which mean
 * the subscription no longer exists and the caller should delete it.
 */
export async function sendNotification(subscription, payload, { vapid, ttl = 86400, urgency = 'normal', topic, fetchImpl = fetch } = {}) {
  if (!subscription || typeof subscription.endpoint !== 'string') throw new TypeError('subscription.endpoint is required');
  const body = encrypt(payload, subscription.keys || {});
  const headers = {
    'Content-Type': 'application/octet-stream',
    'Content-Encoding': 'aes128gcm',
    TTL: String(ttl),
    Urgency: urgency,
    Authorization: vapidAuthorization(subscription.endpoint, vapid),
  };
  // A Topic replaces any undelivered message with the same topic (RFC 8030
  // §5.4). It must be at most 32 base64url characters.
  if (topic) {
    if (!/^[A-Za-z0-9_-]{1,32}$/.test(topic)) throw new TypeError('topic must be 1-32 base64url characters');
    headers.Topic = topic;
  }
  const res = await fetchImpl(subscription.endpoint, { method: 'POST', headers, body });
  return { status: res.status, gone: res.status === 404 || res.status === 410 };
}
