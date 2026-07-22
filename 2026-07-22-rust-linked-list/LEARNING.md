# Linked List (Rust)

**Source:** [Learning Rust with Too Many Linked Lists](https://rust-unofficial.github.io/too-many-lists/),
from [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

The full book works through six list variants; this build scopes to the first one — a safe
singly-linked stack — plus its three iterator forms (`into_iter`, `iter`, `iter_mut`), since that
chapter is where almost all of the ownership/borrowing lessons live.

## What it is

A generic `List<T>` backed by `Option<Box<Node<T>>>` links, with `push`/`pop`/`peek`/`peek_mut`,
a hand-written iterative `Drop`, and three iterator types with different ownership over the
elements they yield. `src/main.rs` is a tiny demo; `src/lib.rs` has the implementation plus unit
tests, including one that pushes 200,000 elements to prove the `Drop` impl doesn't blow the stack.

## Run it

```powershell
# Windows 11 / PowerShell
winget install Rustlang.Rustup
rustup default stable
cargo --version   # confirm the toolchain installed
```

```bash
# macOS/Linux
cd 2026-07-22-rust-linked-list
cargo run           # builds and runs the demo binary
cargo test           # runs the unit tests
cargo clippy --all-targets   # lints
```

```
$ cargo run
pushing 1, 2, 3
peek: Some(3)
doubling every element via iter_mut
contents front-to-back via iter(): 6 4 2
draining via into_iter(): 6 4 2
```

## What it actually teaches

- **`Option<Box<Node<T>>>` as the null-safe substitute for a raw pointer.** There's no `null` in
  safe Rust, and a struct can't directly contain itself (the compiler can't compute a fixed size
  for an infinitely-nested type). `Box` gives an owned heap pointer with a fixed size, and wrapping
  it in `Option` gives "end of list" a real, checked representation instead of a null-pointer
  sentinel that every read has to trust.
- **`.take()` is how you move out of a struct field you don't own outright.** `self.head.take()`
  replaces `self.head` with `None` and hands back the original value, which is the pattern for
  restructuring a linked chain (push, pop, `Drop`) without fighting the borrow checker over
  partially-moved fields — you can't just write `self.head` by value while `self` is only borrowed.
- **Recursive `Drop` is a real stack-overflow risk, not a theoretical one.** The derived `Drop` for
  this struct would drop `head`, which drops its `next`, which drops *its* `next`, one stack frame
  per list node. The `long_list_drops_without_stack_overflow` test pushes 200,000 elements
  specifically to make that failure mode reproducible — the hand-written `Drop` avoids it by
  looping and calling `.take()` on each node's `next` before letting that node fall out of scope,
  so no node's destructor ever recurses into the next one.
- **`iter()` and `iter_mut()` need different variance than `into_iter()`, and the type signatures
  encode the difference.** `into_iter` owns the list and yields `T` by value (consuming it);
  `iter` borrows and yields `&'a T`; `iter_mut` borrows mutably and yields `&'a mut T`. Writing all
  three by hand — rather than only the consuming one — is what makes the borrow checker's role
  concrete: `Iter`/`IterMut` each carry a lifetime tied to the original list, so the compiler
  rejects, at compile time, any attempt to use the iterator after the list it points into has been
  dropped or mutated elsewhere.
- **`as_deref()` / `as_deref_mut()` turn `Option<Box<Node<T>>>` into `Option<&Node<T>>` /
  `Option<&mut Node<T>>` without consuming the `Option`.** This is what lets `iter`/`iter_mut`
  walk the chain by reference — `Iter.next` is reassigned from `node.next.as_deref()` on every
  step, converting one layer of ownership into a borrow at each hop instead of moving the node out.

## What I'd add next (stretch goals I skipped for scope)

- The book's later chapters: a persistent (immutable, `Rc`-shared) list, an unsafe queue with raw
  pointers, and a doubly-linked list with `RefCell` — each swaps in a different ownership model.
- A `Debug`/`Display` impl so a `List` can be printed directly instead of only iterated.
- `FromIterator`/`Extend` impls so `list.extend([1, 2, 3])` and `[1, 2, 3].into_iter().collect()`
  work like they do for `Vec`.
