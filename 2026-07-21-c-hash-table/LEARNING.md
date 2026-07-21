# Hash Table (C)

**Source:** [Write a hash table in C](https://github.com/jamesroutley/write-a-hash-table), from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

Picked and built end-to-end in one sitting, so this folder contains the finished implementation
directly at the project root (no separate `reference/`).

## What it is

A hash table implemented in C99 against nothing but the standard library: `ht_insert`,
`ht_search`, and `ht_delete`, backed by an array of buckets sized to a prime number and grown or
shrunk automatically as load changes. `src/main.c` wraps it in a tiny REPL (`set k v`, `get k`,
`del k`), and `tests/test_hash_table.c` exercises insert/overwrite/delete plus both resize
directions.

## Run it

```powershell
# Windows 11 / PowerShell, via MSYS2/MinGW
winget install MSYS2.MSYS2
# then, from an MSYS2 MinGW64 shell:
pacman -S mingw-w64-x86_64-gcc make
gcc --version   # confirm the toolchain installed
```

```bash
# macOS/Linux
cd 2026-07-21-c-hash-table
make all                 # builds ./hash_table_demo
./hash_table_demo        # interactive REPL: set/get/del/quit

make test                # builds and runs ./run_tests
```

```
$ printf 'set name claude\nget name\ndel name\nget name\nquit\n' | ./hash_table_demo
ok
claude
ok
(nil)
```

## What it actually teaches

- **Double hashing for collision resolution.** Open addressing with a single hash function
  clusters badly (linear probing) or needs a second, independent probe sequence to spread out
  collisions. Double hashing computes `(hash_a(key) + i * (hash_b(key) + 1)) mod size` for probe
  attempt `i`, using two different prime multipliers (151, 163) in the same polynomial hash — the
  `+1` on `hash_b` exists specifically so a key that happens to hash to a zero step doesn't
  degenerate into linear probing or loop forever on a full table.
- **Tombstones, not real deletion.** Deleting a bucket in an open-addressed table can't just set
  it to `NULL` — probing for a *different* key that collided past it would stop early and report
  "not found" even though the key is still there, later in the probe sequence. The fix is a
  `HT_DELETED_ITEM` sentinel distinct from both `NULL` and a real item: search treats it as
  "keep probing," insert treats it as "this slot is reusable."
- **Why table size has to be prime.** `next_prime()` rounds every bucket-array size up to the
  nearest prime before allocating. With a composite size, the double-hash step size `(hash_b + 1)`
  can share a factor with the table size, so the probe sequence only ever visits a fraction of
  the buckets — silently wasting capacity or, in the worst case, failing to find a free slot at
  all. A prime size guarantees the probe sequence is a full permutation of `[0, size)`.
- **Resizing that swaps state instead of swapping pointers.** `ht_resize` builds a whole new
  table, re-inserts every live item into it (so items land in fresh, correctly-sized buckets),
  then swaps the `size`/`items` fields between the old and new struct rather than returning a new
  pointer — callers holding a `ht_hash_table*` from `ht_new()` keep a valid pointer across a
  resize. Growing at a 70% load factor and shrinking at 10% (with a floor at the initial size)
  keeps probe sequences short in both directions without thrashing on every insert/delete pair.

## What I'd add next (stretch goals I skipped for scope)

- Generic values (`void*` + a size, instead of hardcoding `char*`) so the table isn't
  string-to-string only.
- An iterator so a table's contents can be walked/dumped without knowing the internal layout.
- Load-factor and probe-length stats surfaced from the REPL, to see clustering behavior directly
  instead of taking the double-hashing argument on faith.
