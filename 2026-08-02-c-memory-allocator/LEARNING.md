# Memory Allocators 101 (C)

**Source:** [Memory Allocators 101 - Write a simple memory allocator](https://arjunsreedharan.org/post/148675821737/memory-allocators-101-write-a-simple-memory)
by Arjun Sreedharan, from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning) (C section).

Picked and built end-to-end in one sitting, so this folder contains the finished implementation
directly at the project root (no separate `reference/`).

## What it is

A drop-in replacement for `malloc`/`free`/`calloc`/`realloc` (`my_malloc` etc., so it can link
against a normal libc in the same binary) built on nothing but `sbrk(2)`:

- `src/allocator.c` / `allocator.h` — the allocator. One singly-linked list of `header_t` blocks,
  each header immediately preceding the memory handed back to the caller. `get_free_block` does a
  first-fit scan; a fresh allocation extends the heap with `sbrk`; freeing the *last* block on the
  heap shrinks the break back instead of just flagging it free.
- `src/main.c` — a REPL (`alloc`, `calloc`, `realloc`, `free`, `write`, `read`, `list`) so the
  block list is directly observable while poking at it.
- `tests/test_allocator.c` — 11 assertions covering reuse, tail-shrink, overflow, and realloc
  grow/shrink semantics.

## Run it

```bash
cd 2026-08-02-c-memory-allocator
make all                 # builds ./alloc_demo
make test                # builds and runs ./run_tests

./alloc_demo
alloc> alloc 32
id 0 -> 0x... (32 bytes)
alloc> write 0 hello claude
alloc> read 0
hello claude
alloc> list
head = 0x..., tail = 0x...
addr = 0x..., size = 32, is_free = 0, next = (nil)
alloc> free 0
alloc> quit
```

## What it actually teaches

- **A header is the only metadata the allocator gets.** `header_t` is a `union` of the real fields
  (`size`, `is_free`, `next`) and a `long double`, purely so the struct's alignment is wide enough
  for whatever the caller stores right after it — there's no separate bookkeeping table anywhere.
  `my_malloc` returns `header + 1`; `my_free` recovers the header with `(header_t *)block - 1`.
  Corrupt one byte before the pointer a caller was given and you've corrupted the allocator's own
  linked list, which is exactly the class of bug (heap corruption from a buffer underflow) that
  `-fsanitize=address` exists to catch and this design has zero defense against.
- **First-fit means "free" and "gone" are different states, and only one of them gives memory back
  to the OS.** `my_free` only calls `sbrk` with a negative increment when the freed block is the
  *tail* — `(char *)block + header->s.size == sbrk(0)` — because that's the only block whose memory
  is actually at the edge of the heap; anything earlier in the list has other live blocks after it
  holding the break up. Free a non-tail block and it just gets `is_free = 1` and stays exactly
  where it is, waiting for `get_free_block` to hand it back on a same-or-smaller request. The demo
  transcript in `main.c`'s usage shows this directly: after `alloc 32; calloc 4 4; realloc 0 64`,
  the original 32-byte block ends up flagged free but still in the list — freeing it earlier
  wouldn't have shrunk anything, because by then it was buried under the calloc'd block.
- **"Shrink the tail" is O(n) by construction, and that's a real cost, not a nitpick.** Finding the
  *new* tail after removing the old one means walking the list from `head` until `next == tail`,
  because the list is singly-linked — there's no `prev` pointer to jump back with. `test_free_of_tail_shrinks_list`
  exercises exactly this path (`mem_block_count()` drops by one each free) and it stays cheap only
  because the test list is short; a long-running program that mostly frees in FIFO order would pay
  for this walk on every free. A doubly-linked list would make this O(1) at the cost of one more
  pointer per header — the classic allocator-design trade of metadata size against operation cost.
- **`calloc`'s overflow check has to happen before the multiply, not after.** `my_calloc(num, nsize)`
  rejects the call when `num > (size_t)-1 / nsize` — division, not `num * nsize > SIZE_MAX` — because
  the multiplication itself is what would silently wrap on overflow, making the "check" a no-op if
  it ran after. `test_calloc_overflow_returns_null` calls `my_calloc((size_t)-1, 2)`: without the
  guard this wraps to a tiny allocation that `memset(block, 0, total)` would then zero far too
  little of, while the caller believes it has a huge zeroed buffer.
- **`realloc`'s "shrink" and "grow" paths are asymmetric, and the asymmetry is the whole point of
  the function.** Shrinking never moves anything — `if (header->s.size >= size) return block` — since
  the existing block already has the space and there's nothing to gain by relocating. Growing
  always calls `my_malloc` for a fresh block, `memcpy`s the *old* size (never the new one, or it'd
  read past the original allocation), then frees the old block. `test_realloc_shrink_keeps_pointer`
  and `test_realloc_grows_and_preserves_data` each pin down one half of that asymmetry.

## What I'd add next (stretch goals I skipped for scope)

- **Coalescing adjacent free blocks.** Right now two neighboring free blocks stay two separate list
  entries forever — a large-then-freed-then-split usage pattern fragments the heap with no way to
  merge back into one bigger reusable chunk.
- **Best-fit or a segregated free list.** First-fit is simple but can hand out a much-larger block
  for a small request when a closer-sized free block exists later in the list.
- **A `prev` pointer** to make tail-shrink (and general list surgery) O(1) instead of the O(n) walk
  described above.
- **Thread-safety test coverage.** The `pthread_mutex_lock` around every public function is in
  place, but nothing here actually exercises it from multiple threads.
