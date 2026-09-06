// Pure part of the IndexedDB schema migration -- no `indexedDB` global
// touched here, so it's testable with plain arrays. db.js calls this from
// inside the versionchange transaction's onupgradeneeded handler.

import { initialKeys } from "../core/order.js";

// v1 records have no `order` field; the list's order was implicitly
// "whatever createdAt says". v2 adds an explicit fractional `order` key so
// drag-reorder can move one record without rewriting its neighbors. This
// migration has to run once, over every existing record, to backfill that
// key from the old implicit order -- a v1 record can't just default to
// order: null, because it would sort before (or after, arbitrarily) every
// record a user creates post-migration.
function migrateV1ToV2(records) {
  const sorted = [...records].sort((a, b) => a.createdAt - b.createdAt);
  const keys = initialKeys(sorted.length);
  return sorted.map((record, i) => ({ ...record, order: keys[i] }));
}

export { migrateV1ToV2 };
