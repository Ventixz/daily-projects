use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::RwLock;

#[derive(Debug, PartialEq, Eq)]
pub enum PutOutcome {
    Created,
    Replaced,
}

/// A concurrent, in-memory key-value store. Every value is treated as an
/// opaque byte string -- the store has no idea it's holding JSON, and
/// doesn't need to.
pub struct Store {
    items: RwLock<HashMap<u64, Vec<u8>>>,
    next_id: AtomicU64,
}

impl Default for Store {
    fn default() -> Self {
        Self::new()
    }
}

impl Store {
    pub fn new() -> Self {
        Store {
            items: RwLock::new(HashMap::new()),
            next_id: AtomicU64::new(1),
        }
    }

    /// Every read/write helper recovers a poisoned lock instead of
    /// unwrapping straight into a panic. A `RwLock` poisons itself the
    /// moment any thread panics while holding a guard, and after that every
    /// future `.read()`/`.write()` on the *same* lock returns `Err` forever
    /// -- one bad request would otherwise take the whole store down for
    /// every client, not just the one that hit the bug. `into_inner()` on
    /// the `PoisonError` hands back the guard anyway; the data underneath
    /// is still structurally valid HashMap state (the panic happened around
    /// the map, not while it was left half-mutated by a `HashMap` method
    /// unwinding mid-insert).
    fn read(&self) -> std::sync::RwLockReadGuard<'_, HashMap<u64, Vec<u8>>> {
        self.items
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    fn write(&self) -> std::sync::RwLockWriteGuard<'_, HashMap<u64, Vec<u8>>> {
        self.items
            .write()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    /// Assigns a fresh id via `fetch_add`, which is the whole reason this is
    /// safe under concurrent callers: allocating the id and reserving it are
    /// the same atomic step, so two threads calling `create` at once can
    /// never walk away with the same id. Reading `next_id` and writing it
    /// back as two separate steps -- or deriving an id from `map.len()` --
    /// both have a window where two threads read the same value before
    /// either writes it back.
    pub fn create(&self, value: Vec<u8>) -> u64 {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        self.write().insert(id, value);
        id
    }

    pub fn get(&self, id: u64) -> Option<Vec<u8>> {
        self.read().get(&id).cloned()
    }

    /// Creates `id` if it's not already present, replaces it if it is.
    ///
    /// `id` comes from the client here, not from `next_id` -- PUT is
    /// idempotent-by-design, unlike POST. That means a client can hand this
    /// store an id `next_id` hasn't reached yet, and a `fetch_max` bump is
    /// what keeps the two id sources from ever colliding down the line:
    /// without it, a client that PUTs `/items/500` today could find a later
    /// POST silently overwriting it once the counter climbs to 500.
    pub fn put(&self, id: u64, value: Vec<u8>) -> PutOutcome {
        self.next_id.fetch_max(id + 1, Ordering::Relaxed);
        match self.write().insert(id, value) {
            Some(_) => PutOutcome::Replaced,
            None => PutOutcome::Created,
        }
    }

    pub fn delete(&self, id: u64) -> bool {
        self.write().remove(&id).is_some()
    }

    pub fn list(&self) -> Vec<(u64, Vec<u8>)> {
        let mut items: Vec<_> = self
            .read()
            .iter()
            .map(|(id, value)| (*id, value.clone()))
            .collect();
        items.sort_by_key(|(id, _)| *id);
        items
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;
    use std::panic;
    use std::sync::Arc;
    use std::thread;

    #[test]
    fn create_then_get_round_trips_the_value() {
        let store = Store::new();
        let id = store.create(b"hello".to_vec());
        assert_eq!(store.get(id), Some(b"hello".to_vec()));
    }

    #[test]
    fn get_on_a_missing_id_is_none() {
        let store = Store::new();
        assert_eq!(store.get(404), None);
    }

    #[test]
    fn put_on_a_missing_id_creates_it() {
        let store = Store::new();
        assert_eq!(store.put(42, b"seed".to_vec()), PutOutcome::Created);
        assert_eq!(store.get(42), Some(b"seed".to_vec()));
    }

    #[test]
    fn put_on_an_existing_id_replaces_it() {
        let store = Store::new();
        store.put(42, b"seed".to_vec());
        assert_eq!(store.put(42, b"grown".to_vec()), PutOutcome::Replaced);
        assert_eq!(store.get(42), Some(b"grown".to_vec()));
    }

    #[test]
    fn delete_reports_whether_anything_was_removed() {
        let store = Store::new();
        let id = store.create(b"x".to_vec());
        assert!(store.delete(id));
        assert!(!store.delete(id));
        assert_eq!(store.get(id), None);
    }

    #[test]
    fn list_is_sorted_by_id() {
        let store = Store::new();
        let c = store.create(b"c".to_vec());
        let a = store.put(1_000, b"a".to_vec());
        assert_eq!(a, PutOutcome::Created);
        let items = store.list();
        let ids: Vec<u64> = items.iter().map(|(id, _)| *id).collect();
        let mut expected = vec![1_000, c];
        expected.sort();
        assert_eq!(ids, expected);
    }

    #[test]
    fn a_put_id_reserves_the_id_space_against_future_creates() {
        let store = Store::new();
        store.put(500, b"seed".to_vec());
        let id = store.create(b"next".to_vec());
        assert!(id > 500, "expected an id past 500, got {id}");
    }

    #[test]
    fn concurrent_creates_never_hand_out_the_same_id() {
        let store = Arc::new(Store::new());
        let handles: Vec<_> = (0..200)
            .map(|i| {
                let store = Arc::clone(&store);
                thread::spawn(move || store.create(format!("item-{i}").into_bytes()))
            })
            .collect();

        let ids: HashSet<u64> = handles.into_iter().map(|h| h.join().unwrap()).collect();
        assert_eq!(ids.len(), 200, "expected 200 distinct ids, got collisions");
    }

    #[test]
    fn write_operations_still_work_after_a_writer_panics() {
        let store = Arc::new(Store::new());
        let id = store.create(b"before".to_vec());

        let poisoner = Arc::clone(&store);
        let result = panic::catch_unwind(panic::AssertUnwindSafe(|| {
            let _guard = poisoner.items.write().unwrap();
            panic!("simulated failure while the write lock is held");
        }));
        assert!(result.is_err());
        assert!(store.items.is_poisoned());

        // The lock is poisoned now, but Store's own methods recover it
        // instead of propagating the PoisonError, so ordinary traffic
        // keeps working.
        assert_eq!(store.get(id), Some(b"before".to_vec()));
        let new_id = store.create(b"after poison".to_vec());
        assert_eq!(store.get(new_id), Some(b"after poison".to_vec()));
    }
}
