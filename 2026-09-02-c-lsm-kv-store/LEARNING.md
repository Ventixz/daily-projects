# Implementing a Key-Value Store (C)

**Source:** ["iKVS: Implementing a Key-Value Store"](https://codecapsule.com/2012/11/07/ikvs-implementing-a-key-value-store-table-of-contents/)
by Emmanuel Goossaert, from the C section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting, so this folder contains the finished implementation
directly at the project root (no separate `reference/`). Zero external dependencies — standard
library only (`stdio`, `dirent`, `sys/stat`).

## What it is

An LSM-tree-style key-value store, the same family of design as LevelDB/RocksDB/Bitcask, scoped
down to something buildable in a day:

- `src/memtable.c` — the mutable, in-memory layer. A sorted array (binary-search insert/lookup)
  of live entries; a `NULL` value marks a tombstone (delete) rather than removing the key, which
  matters once older on-disk data could otherwise resurface.
- `src/record.c` — the one on-disk record format (`op | keylen | key | vallen | val`) shared
  verbatim by the WAL and every sstable, so a WAL file and an sstable file are byte-for-byte the
  same kind of thing: an append-only sequence of these records.
- `src/wal.c` — the write-ahead log. Every `PUT`/`DEL` is appended and `fflush`'d *before* it
  touches the memtable, and `wal_replay` rebuilds a memtable by reading one back top-to-bottom.
- `src/sstable.c` — immutable sorted runs flushed from a memtable. `sstable_open` reads a file
  once at startup to build an in-memory index of `(key, offset)` pairs, so a later `sstable_get`
  is a binary search plus one `fseek`+read, never a scan. `sstable_compact` merges N sstables into
  one by replaying each (reusing `wal_replay`, since the format is identical) and re-applying
  every entry oldest-to-newest into a fresh memtable — `memtable_put`'s overwrite-on-duplicate
  behavior is exactly "newest version wins," for free.
- `src/store.c` — ties it together: `kv_open` scans the directory for `NNNNNN.sst` files, sorts
  them oldest-first, and replays the WAL; `kv_put`/`kv_get`/`kv_del` route through memtable →
  newest-to-oldest sstables; crossing `KV_MEMTABLE_FLUSH_THRESHOLD` triggers a flush, and crossing
  `KV_COMPACT_THRESHOLD` sstables triggers a compaction, both synchronously and automatically.
- `src/main.c` — a REPL (`put`, `get`, `del`, `flush`, `compact`, `stats`) so flushes and
  compactions are directly observable instead of happening silently inside a benchmark.
- `tests/test_kv.c` — 8 tests: memtable semantics, the memtable→sstable flush path, cross-sstable
  shadowing (newest write wins), tombstone-dropping compaction, and two tests that specifically
  reopen the store to check what survives — one simulating a crash before any flush (WAL replay),
  one after a clean flush (sstables reload, WAL is empty).

## Run it

```bash
cd 2026-09-02-c-lsm-kv-store
make test    # builds and runs ./run_tests -- 8 tests
make all     # builds ./kv_demo
./kv_demo kvdata
```

Actual session (thresholds set low — flush at 4 memtable entries, compact at 3 sstables — so
both trigger during a short demo instead of needing thousands of writes):

```
$ ./kv_demo kvdata
kv> opened 'kvdata' (memtable flushes at 4 entries, compacts at 3 sstables)
kv> put lang c
ok
kv> put year 2026
ok
kv> put a 1
ok
kv> put b 2
ok
kv> put c 3
ok
kv> stats
memtable: 1 live entry
sstables (1, oldest first):
  000000.sst  4 records
kv> put d 4
ok
kv> del a
ok
kv> put e 5
ok
kv> put f 6
ok
kv> stats
memtable: 1 live entry
sstables (2, oldest first):
  000000.sst  4 records
  000001.sst  4 records
kv> quit
```

Then reopening the *same* directory in a fresh process (nothing here was flushed on exit — the
one live memtable entry, `f`, only exists in the WAL) and pushing past the compaction threshold:

```
$ ./kv_demo kvdata
kv> opened 'kvdata' (memtable flushes at 4 entries, compacts at 3 sstables)
kv> stats
memtable: 1 live entry
sstables (2, oldest first):
  000000.sst  4 records
  000001.sst  4 records
kv> get f
6
kv> put g 7
ok
kv> put h 8
ok
kv> put i 9
ok
kv> stats
memtable: 0 live entries
sstables (1, oldest first):
  000003.sst  10 records
kv> get a
(not found)
kv> get f
6
kv> get g
7
```

`get f` on the very first line after reopening proves WAL replay worked — `f` was never flushed,
only logged. The third `put` (of `g`/`h`/`i`) crosses the 4-entry flush threshold, which pushes
the sstable count to 3 and triggers an automatic compaction: `000000.sst`, `000001.sst`, and the
new flush collapse into a single `000003.sst` with 10 live records (11 keys ever written, minus
`a`, which was deleted and whose tombstone compaction then dropped entirely — `get a` correctly
still returns not found).

## What it actually teaches

- **A WAL and an sstable are the same file format wearing two different hats.** Both are just
  "sequence of length-prefixed records." The only real difference is a lifecycle one — a WAL is
  appended to and eventually truncated, an sstable is written once and never touched again — and
  even the tooling to make sense of one works on the other: `sstable_compact` rebuilds a memtable
  from an sstable file by calling `wal_replay` directly. Realizing that let one function do the
  work that two nearly-identical ones otherwise would have.
- **A flush needs to close and reopen the WAL's `FILE*`, not just truncate the path.** The first
  version called `wal_truncate` (a fresh `fopen(path, "wb")`) while the store still held the old
  handle open in append mode. That handle's underlying file offset had no idea the file had been
  replaced out from under it — the next append landed at the old (now-stale) end-of-file position
  instead of byte 0, so the "empty" WAL wasn't actually empty. The fix is to close the store's WAL
  handle before truncating and reopen it after — a small, easy-to-miss lesson about `FILE*`
  offsets not being tied to what's actually on disk once another open on the same path changes it.
- **Tombstones can only be dropped once compaction has nothing left to hide behind.** A tombstone
  isn't "no data," it's "positive evidence of a delete" — if compaction merges sstable A (has
  `a=1`) and sstable B (has `a=<tombstone>`, written later) but *not* older sstable C, dropping
  the tombstone during that merge would let `a=1` (which C might also have, or an even older
  layer might) resurface as if never deleted. This implementation sidesteps the problem instead of
  solving it: `kv_compact` always merges *every* sstable at once, so "nothing older is left" is
  always true when a tombstone is dropped. A real leveled LSM (RocksDB, LevelDB) has to solve the
  harder version, because it only ever compacts adjacent levels, not the whole tree.
- **Binary-search insert into a sorted array is a fine memtable for a demo, and a real bottleneck
  for anything else.** `memtable_put`'s `memmove` on every insert is O(n) per write, which is
  invisible at the thresholds this demo uses (4 entries) and would dominate at real ones (real
  memtables use a skip list specifically to get O(log n) insert *and* keep sorted iteration, which
  a hash map can't do and a plain sorted array can't do cheaply).
- **The read path's correctness is entirely about direction: newest first, and stop at the first
  hit.** `kv_get` checks the memtable, then sstables from `sstable_count - 1` down to `0`. Get
  that loop backwards (oldest sstable first) and every test still compiles, `get` on any key that
  was only ever written once still passes — and the one behavior that's actually the whole point
  of an LSM tree (a later write shadowing an earlier one) silently breaks. That's exactly the kind
  of bug lightweight testing without `test_store_newer_overrides_older_across_sstables` would
  have missed.

## Deliberate scope cuts

- **No `fsync`.** Every WAL append is `fflush`'d (survives a crashed *process* — verified by
  `test_wal_replay_recovers_unflushed_writes`, which reopens the store having never called
  `kv_flush`, relying on WAL replay alone to recover both writes) but never `fsync`'d, so it does
  not survive a real power loss or OS crash — the data can still be sitting in the OS page cache,
  not on the platter/flash. A real WAL needs `fsync` (or at least a documented, configurable
  tradeoff) to make a durability claim that means anything beyond "this process didn't crash
  weirdly."
- **Full compaction only, no leveling.** `kv_compact` always merges *every* sstable into one. That
  makes tombstone-dropping trivially safe (see above) but means each compaction rewrites the
  entire dataset — fine at demo scale, but the reason real LSM trees use leveled or size-tiered
  compaction (merge only same-sized/adjacent groups) is that full-rewrite compaction is O(total
  data size) every time it runs, not O(what changed).
- **No Bloom filters.** A `get` for a key that doesn't exist anywhere still does a binary search
  against every sstable's index before giving up — cheap here (in-memory index, one `fseek` in the
  worst case per table) but the reason real stores put a Bloom filter in front of each sstable is
  to skip that entirely for the common case of "this key isn't in this table."
- **No range scans.** The read/write paths are point `get`/`put`/`del` only, even though the
  memtable and every sstable are already kept in sorted order — the data structure supports
  ordered iteration for free, the API just doesn't expose it yet.
- **No concurrency.** One process, no locks, no readers-during-compaction story. `kv_compact`
  deletes the old sstable files as soon as the merge finishes; a concurrent reader mid-lookup
  against one of those files would just get an `fopen` failure, not a consistent snapshot.

## What I'd add next

- **Leveled or size-tiered compaction**, replacing "merge everything" with "merge same-sized
  groups," which is the change that would make compaction cost proportional to what changed
  instead of to the whole dataset — and would require actually solving the tombstone-safety
  problem this version dodges by compacting everything at once.
- **Bloom filters per sstable**, since the index-and-`fseek` path already isolates exactly where a
  filter would slot in: check the filter before touching `sstable_get` at all.
- **`fsync` on WAL append (or a batched/configurable version of it)** and atomic sstable creation
  (write to a `.tmp` path, `fsync`, then `rename()`) to make the durability guarantees actually
  hold across a real crash, not just a clean process restart.
- **A range/prefix scan API.** Both the memtable and sstable index are already sorted arrays;
  exposing an iterator that merges across them (memtable + all sstables, newest-wins, like `get`
  but for a key range) is a natural extension of machinery that already exists.
