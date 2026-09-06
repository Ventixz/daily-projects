// IndexedDB adapter. Everything that can be pure lives in migrations.js and
// core/*.js; this file is only the impure shell around indexedDB itself, so
// the interesting logic stays testable without a browser.

import { migrateV1ToV2 } from "./migrations.js";

const DB_NAME = "todo-app";
const DB_VERSION = 2;
const STORE = "todos";

function openDb(dbName = DB_NAME) {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(dbName, DB_VERSION);

    req.onupgradeneeded = (event) => {
      const db = req.result;
      const oldVersion = event.oldVersion;
      const store =
        oldVersion < 1
          ? db.createObjectStore(STORE, { keyPath: "id" })
          : req.transaction.objectStore(STORE);

      if (oldVersion < 2) {
        if (!store.indexNames.contains("by_order")) {
          store.createIndex("by_order", "order", { unique: false });
        }
        // Backfill `order` on every record written under the old schema.
        // Must happen inside this same versionchange transaction: IndexedDB
        // has no separate "migration transaction" concept, and the upgrade
        // transaction auto-commits once every request chained off it settles.
        const getAllReq = store.getAll();
        getAllReq.onsuccess = () => {
          const migrated = migrateV1ToV2(getAllReq.result);
          for (const record of migrated) store.put(record);
        };
      }
    };

    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

function storeFor(db, mode) {
  return db.transaction(STORE, mode).objectStore(STORE);
}

function requestToPromise(req) {
  return new Promise((resolve, reject) => {
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

function getAll(db) {
  return requestToPromise(storeFor(db, "readonly").getAll());
}

function put(db, record) {
  return requestToPromise(storeFor(db, "readwrite").put(record));
}

function remove(db, id) {
  return requestToPromise(storeFor(db, "readwrite").delete(id));
}

function deleteDatabase(dbName = DB_NAME) {
  return new Promise((resolve, reject) => {
    const req = indexedDB.deleteDatabase(dbName);
    req.onsuccess = () => resolve();
    req.onerror = () => reject(req.error);
    req.onblocked = () => resolve();
  });
}

export { openDb, getAll, put, remove, deleteDatabase, DB_NAME };
