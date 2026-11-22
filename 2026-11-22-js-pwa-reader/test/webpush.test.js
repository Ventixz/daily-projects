// Tests for server/webpush.js. They check against the RFC 8291 Appendix A
// test vector, against a receiver written separately here (the browser's
// side), and check that VAPID JWTs verify.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {
  encrypt, deriveKeys, vapidAuthorization, generateVapidKeys, sendNotification,
  b64url, fromB64url, MAX_PLAINTEXT, RECORD_SIZE,
} from '../server/webpush.js';

// RFC 8291 Appendix A
const V = {
  plaintext: 'When I grow up, I want to be a watermelon',
  asPrivate: 'yfWPiYE-n46HLnH0KqZOF1fJJU3MYrct3AELtAQ-oRw',
  asPublic: 'BP4z9KsN6nGRTbVYI_c7VJSPQTBtkgcy27mlmlMoZIIgDll6e3vCYLocInmYWAmS6TlzAC8wEqKK6PBru3jl7A8',
  uaPrivate: 'q1dXpw3UpT5VOmu_cf_v6ih07Aems3njxI-JWgLcM94',
  uaPublic: 'BCVxsr7N_eNgVRqvHtD0zTZsEc6-VV-JvLexhqUzORcxaOzi6-AYWXvTBHm4bjyPjs7Vd8pZGH6SRpkNtoIAiw4',
  salt: 'DGv6ra1nlYgDCS1FRnbzlw',
  auth: 'BTBZMqHH6r4Tts7J_aSIgg',
  ecdhSecret: 'kyrL1jIIOHEzg3sM2ZWRHDRB62YACZhhSlknJ672kSs',
  prkKey: 'Snr3JMxaHVDXHWJn5wdC52WjpCtd2EIEGBykDcZW32k',
  ikm: 'S4lYMb_L0FxCeq0WhDx813KgSYqU26kOyzWUdsXYyrg',
  prk: '09_eUZGrsvxChDCGRCdkLiDXrReGOEVeSCdCcPBSJSc',
  cek: 'oIhVW04MRdy2XN9CiKLxTg',
  nonce: '4h_95klXJ5E_qnoN',
  body: 'DGv6ra1nlYgDCS1FRnbzlwAAEABBBP4z9KsN6nGRTbVYI_c7VJSPQTBtkgcy27mlmlMoZIIgDll6e3vCYLocInmYWAmS6TlzAC8wEqKK6PBru3jl7A_yl95bQpu6cVPTpK4Mqgkf1CXztLVBSt2Ks3oZwbuwXPXLWyouBWLVWGNWQexSgSxsj_Qulcy4a-fN',
};

/**
 * The browser's side of RFC 8291, written separately from the sender. It
 * parses the aes128gcm header, does ECDH with its own private key, derives
 * the same keys, decrypts, and strips the padding.
 */
function receive(body, { uaPrivate, auth }) {
  const salt = body.subarray(0, 16);
  const rs = body.readUInt32BE(16);
  const idlen = body[20];
  const asPublic = body.subarray(21, 21 + idlen);
  const record = body.subarray(21 + idlen);
  assert.ok(record.length <= rs, 'single record must fit in rs');
  const ecdh = crypto.createECDH('prime256v1');
  ecdh.setPrivateKey(fromB64url(uaPrivate));
  const ecdhSecret = ecdh.computeSecret(asPublic);
  const { cek, nonce } = deriveKeys({ ecdhSecret, authSecret: fromB64url(auth), uaPublic: ecdh.getPublicKey(), asPublic, salt });
  const decipher = crypto.createDecipheriv('aes-128-gcm', cek, nonce);
  decipher.setAuthTag(record.subarray(record.length - 16));
  const padded = Buffer.concat([decipher.update(record.subarray(0, record.length - 16)), decipher.final()]);
  // Strip trailing zero padding, then the delimiter, which must be 0x02 in
  // the last record.
  let end = padded.length - 1;
  while (end >= 0 && padded[end] === 0) end--;
  assert.equal(padded[end], 2, 'last-record delimiter');
  return padded.subarray(0, end);
}

function makeBrowserKeys() {
  const ecdh = crypto.createECDH('prime256v1');
  ecdh.generateKeys();
  const auth = crypto.randomBytes(16);
  return {
    subscriptionKeys: { p256dh: b64url(ecdh.getPublicKey()), auth: b64url(auth) },
    privateKeys: { uaPrivate: b64url(ecdh.getPrivateKey()), auth: b64url(auth) },
  };
}

test('RFC 8291 Appendix A: every intermediate key matches', () => {
  const ecdh = crypto.createECDH('prime256v1');
  ecdh.setPrivateKey(fromB64url(V.asPrivate));
  assert.equal(b64url(ecdh.getPublicKey()), V.asPublic);
  const ecdhSecret = ecdh.computeSecret(fromB64url(V.uaPublic));
  assert.equal(b64url(ecdhSecret), V.ecdhSecret);
  const k = deriveKeys({
    ecdhSecret, authSecret: fromB64url(V.auth), uaPublic: fromB64url(V.uaPublic),
    asPublic: fromB64url(V.asPublic), salt: fromB64url(V.salt),
  });
  assert.equal(b64url(k.prkKey), V.prkKey);
  assert.equal(b64url(k.ikm), V.ikm);
  assert.equal(b64url(k.prk), V.prk);
  assert.equal(b64url(k.cek), V.cek);
  assert.equal(b64url(k.nonce), V.nonce);
});

test('RFC 8291 Appendix A: the encrypted body matches byte for byte', () => {
  const body = encrypt(V.plaintext, { p256dh: V.uaPublic, auth: V.auth }, { asPrivateKey: V.asPrivate, salt: V.salt });
  assert.equal(b64url(body), V.body);
});

test('the RFC 8291 test-vector body decrypts with the receiver', () => {
  assert.equal(receive(fromB64url(V.body), { uaPrivate: V.uaPrivate, auth: V.auth }).toString(), V.plaintext);
});

test('round trip with fresh keys, and the aes128gcm header layout', () => {
  const { subscriptionKeys, privateKeys } = makeBrowserKeys();
  const msg = JSON.stringify({ title: 'New article', body: 'Héllo 🌍', url: '/articles/x' });
  const body = encrypt(msg, subscriptionKeys);
  assert.equal(body.readUInt32BE(16), RECORD_SIZE);
  assert.equal(body[20], 65);
  assert.equal(body[21], 0x04, 'keyid is the uncompressed sender public key');
  assert.equal(receive(body, privateKeys).toString(), msg);
});

test('each message uses a fresh salt and sender key', () => {
  const { subscriptionKeys } = makeBrowserKeys();
  const a = encrypt('same', subscriptionKeys);
  const b = encrypt('same', subscriptionKeys);
  assert.notDeepEqual(a.subarray(0, 16), b.subarray(0, 16), 'salt');
  assert.notDeepEqual(a.subarray(21, 86), b.subarray(21, 86), 'ephemeral public key');
});

test('tampering with the ciphertext is detected by the GCM tag', () => {
  const { subscriptionKeys, privateKeys } = makeBrowserKeys();
  const body = encrypt('secret', subscriptionKeys);
  body[body.length - 20] ^= 1;
  assert.throws(() => receive(body, privateKeys), /unable to authenticate|auth/i);
});

test('the wrong auth secret cannot decrypt', () => {
  const { subscriptionKeys, privateKeys } = makeBrowserKeys();
  const body = encrypt('secret', subscriptionKeys);
  assert.throws(() => receive(body, { ...privateKeys, auth: b64url(crypto.randomBytes(16)) }));
});

test('payload size limit: exactly MAX_PLAINTEXT fits in 4096 bytes; one more byte is rejected', () => {
  const { subscriptionKeys, privateKeys } = makeBrowserKeys();
  const body = encrypt(Buffer.alloc(MAX_PLAINTEXT, 0x61), subscriptionKeys);
  assert.equal(body.length, 4096);
  assert.equal(receive(body, privateKeys).length, MAX_PLAINTEXT);
  assert.throws(() => encrypt(Buffer.alloc(MAX_PLAINTEXT + 1), subscriptionKeys), RangeError);
});

test('malformed subscription keys are rejected', () => {
  const { subscriptionKeys } = makeBrowserKeys();
  assert.throws(() => encrypt('x', { ...subscriptionKeys, auth: b64url(Buffer.alloc(8)) }), /auth/);
  assert.throws(() => encrypt('x', { ...subscriptionKeys, p256dh: b64url(Buffer.alloc(33, 2)) }), /p256dh/);
  assert.throws(() => encrypt('x', { ...subscriptionKeys, p256dh: 'not base64!' }), /base64url/);
  // A 65-byte "point" that is not on the curve.
  const offCurve = Buffer.alloc(65, 1);
  offCurve[0] = 4;
  assert.throws(() => encrypt('x', { ...subscriptionKeys, p256dh: b64url(offCurve) }));
});

function verifyVapid(header, publicKey) {
  const m = header.match(/^vapid t=([^,]+), k=(.+)$/);
  assert.ok(m, 'header shape');
  const [h, c, s] = m[1].split('.');
  const pub = fromB64url(m[2]);
  assert.equal(m[2], publicKey);
  const key = crypto.createPublicKey({
    key: { kty: 'EC', crv: 'P-256', x: b64url(pub.subarray(1, 33)), y: b64url(pub.subarray(33)) },
    format: 'jwk',
  });
  const valid = crypto.verify('sha256', Buffer.from(`${h}.${c}`), { key, dsaEncoding: 'ieee-p1363' }, fromB64url(s));
  return { valid, header: JSON.parse(fromB64url(h)), claims: JSON.parse(fromB64url(c)), sigLength: fromB64url(s).length };
}

test('VAPID: ES256 JWT that verifies, audience is the endpoint origin, expiry under 24h', () => {
  const keys = generateVapidKeys();
  const now = Date.UTC(2026, 10, 22, 12, 0, 0);
  const auth = vapidAuthorization('https://fcm.googleapis.com/fcm/send/abc123', { ...keys, subject: 'mailto:ops@example.org' }, now);
  const v = verifyVapid(auth, keys.publicKey);
  assert.equal(v.valid, true);
  assert.equal(v.sigLength, 64, 'raw r||s, not DER');
  assert.deepEqual(v.header, { typ: 'JWT', alg: 'ES256' });
  assert.equal(v.claims.aud, 'https://fcm.googleapis.com');
  assert.equal(v.claims.sub, 'mailto:ops@example.org');
  assert.ok(v.claims.exp > now / 1000 && v.claims.exp <= now / 1000 + 86400);
});

test('VAPID: a JWT signed by a different key does not verify', () => {
  const a = generateVapidKeys();
  const b = generateVapidKeys();
  const auth = vapidAuthorization('https://push.example/x', { ...a, subject: 'mailto:a@example.org' });
  const forged = auth.replace(/k=.*/, `k=${b.publicKey}`);
  assert.equal(verifyVapid(forged, b.publicKey).valid, false);
});

test('VAPID: the subject must be mailto: or https:', () => {
  const keys = generateVapidKeys();
  assert.throws(() => vapidAuthorization('https://push.example/x', { ...keys, subject: 'ops@example.org' }), /subject/);
});

test('sendNotification: request shape, and 410 reported as gone', async () => {
  const { subscriptionKeys, privateKeys } = makeBrowserKeys();
  const vapid = { ...generateVapidKeys(), subject: 'mailto:a@example.org' };
  const seen = [];
  const fakeFetch = async (url, init) => { seen.push({ url, init }); return { status: seen.length === 1 ? 201 : 410 }; };
  const sub = { endpoint: 'https://push.example/send/1', keys: subscriptionKeys };

  const first = await sendNotification(sub, 'hello', { vapid, fetchImpl: fakeFetch, ttl: 60, urgency: 'high', topic: 'new-article' });
  assert.deepEqual(first, { status: 201, gone: false });
  const { url, init } = seen[0];
  assert.equal(url, sub.endpoint);
  assert.equal(init.method, 'POST');
  assert.equal(init.headers['Content-Encoding'], 'aes128gcm');
  assert.equal(init.headers.TTL, '60');
  assert.equal(init.headers.Urgency, 'high');
  assert.equal(init.headers.Topic, 'new-article');
  assert.equal(verifyVapid(init.headers.Authorization, vapid.publicKey).valid, true);
  assert.equal(receive(init.body, privateKeys).toString(), 'hello');

  assert.deepEqual(await sendNotification(sub, 'again', { vapid, fetchImpl: fakeFetch }), { status: 410, gone: true });
  await assert.rejects(sendNotification(sub, 'x', { vapid, fetchImpl: fakeFetch, topic: 'has spaces' }), /topic/);
});
