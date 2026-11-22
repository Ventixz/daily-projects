// Entry point: `npm start`. Loads or creates the VAPID key pair, then serves.
//
//   PORT          port to listen on (default 8080)
//   DATA_DIR      where store.json and vapid.json live (default ./data)
//   ADMIN_TOKEN   bearer token for publishing (a random one is printed if unset)
//   VAPID_SUBJECT contact URI sent to push services (default mailto:admin@example.invalid)
//
// Service workers only run in a secure context. http://localhost counts as
// one, so local development works without TLS. Deploy behind HTTPS.

import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import { createServer } from './server.js';
import { generateVapidKeys } from './webpush.js';

const port = Number(process.env.PORT || 8080);
const dataDir = path.resolve(process.env.DATA_DIR || 'data');
await fs.mkdir(dataDir, { recursive: true });

// The VAPID key pair must stay the same for the life of the deployment.
// Every existing subscription is bound to this public key. Change it and
// every subscriber silently stops getting pushes.
const vapidFile = path.join(dataDir, 'vapid.json');
let keys;
try {
  keys = JSON.parse(await fs.readFile(vapidFile, 'utf8'));
} catch (err) {
  if (err.code !== 'ENOENT') throw err;
  keys = generateVapidKeys();
  await fs.writeFile(vapidFile, JSON.stringify(keys, null, 2), { mode: 0o600 });
  console.log(`generated VAPID keys in ${vapidFile}`);
}

let adminToken = process.env.ADMIN_TOKEN;
if (!adminToken) {
  adminToken = crypto.randomBytes(18).toString('base64url');
  console.log(`ADMIN_TOKEN not set; using ${adminToken} for this run`);
}

const server = await createServer({
  dataDir,
  vapid: { ...keys, subject: process.env.VAPID_SUBJECT || 'mailto:admin@example.invalid' },
  adminToken,
});
server.listen(port, () => console.log(`Offline Reader on http://localhost:${port}`));
