# 0460. F.27: Use a shared_ptr<T> to share ownership

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

`std::shared_ptr` is the standard way to represent shared ownership: the last
owner deletes the object. Where several independent parties each need the object
to stay alive, and none of them can be nominated as the one that outlives the
rest, that is what shared ownership means.

```cpp
shared_ptr<const Image> im { read_image(somewhere) };

std::thread t0 {shade, args0, top_left, im};
std::thread t1 {shade, args1, top_right, im};
std::thread t2 {shade, args2, bottom_left, im};
std::thread t3 {shade, args3, bottom_right, im};

// detach threads
// last thread to finish deletes the image
```

Prefer a `unique_ptr` over a `shared_ptr` if there is never more than one owner
at a time; `shared_ptr` is for shared ownership, not for convenience. Pervasive
use of `shared_ptr` has a cost, because the atomic operations on the reference
count have a measurable aggregate cost.

There is an alternative worth considering before reaching for it at all: have a
single object own the shared object — a scoped object, say — and destroy that,
preferably implicitly, when all users have completed.

## Decision

Use `std::shared_ptr<T>` only where ownership is genuinely shared, and prefer a
single owner where one can be identified.

- Before choosing `shared_ptr`, look for a scope that outlives every user. If
  there is one, let it own the object and hand out `T&` or `T*` observers.
- Use `shared_ptr` when the object's lifetime is determined by the last of
  several independent users to finish, as with detached threads or an
  asynchronous continuation.
- Construct with `std::make_shared<T>(...)`, which puts the control block and
  the object in one allocation.
- Take a `shared_ptr` parameter by value only when the callee stores it and
  thereby becomes an owner. A function that merely uses the object takes `T&` or
  `const T&`.
- Prefer `shared_ptr<const T>` for a shared object that is only read, as in the
  example above.
- Use `weak_ptr` for a participant that must observe without extending the
  lifetime, and to break cycles.

## Rationale

The reason to be precise here is that `shared_ptr` answers a question most code
does not ask. Most lifetimes are nested, and a nested lifetime has an owner;
sharing is the answer only when the lifetimes genuinely are not nested, which is
comparatively rare and usually involves concurrency.

Used where it is not needed, `shared_ptr` costs an atomic increment and
decrement per copy and, worse, it removes the pressure to work out where the
object actually belongs. A reference count is a run-time answer to a question
that a scope answers at compile time.

## Consequences

### Positive

- Lifetime is correct without any party knowing which of them will finish last,
  which is the case that no scope-based scheme handles.
- Destruction is thread-safe with respect to the count, so the pattern in the
  example needs no further synchronisation for the lifetime itself.
- `weak_ptr` gives a checked way to hold a possibly-dead reference, which no raw
  pointer can offer.
- `shared_ptr<const T>` expresses read-only sharing exactly, and combines with
  [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md).

### Negative

- The reference count is atomic, and its cost is paid on every copy whether or
  not the program is concurrent. Pervasive use is measurable.
- It shares ownership of the object, not the object's contents. Two threads
  holding one `shared_ptr<T>` still need their own synchronisation for any
  mutation.
- Cycles leak silently, and finding them requires reasoning about the whole
  object graph rather than about one function.
- It makes destruction order non-local and non-obvious: the object dies in
  whichever thread happens to drop the last reference, which is a poor place for
  a destructor with side effects to run.
- It is habit-forming. Once a type is handed around as `shared_ptr`, the
  question of who owns it stops being asked at all.

### Neutral

- The upstream alternative — a single scoped owner destroyed when all users
  complete — is not a weaker option, only a different one; it needs the
  completion to be observable.
- `make_shared` keeps the object's storage alive as long as any `weak_ptr`
  survives, which is a trade against its single-allocation benefit.

## Enforcement

- (Not enforceable) This is too complex a pattern to detect reliably.

## Related

- [0459](0459-f-26-use-a-unique-ptr-t-to-transfer-ownership-where-a-pointer-is-needed.md)
  — the default to prefer when there is one owner at a time.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  why the ownership must be in the type at all.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the reference-count cost this
  record weighs.
- [0014](0014-p-8-do-not-leak-any-resources.md) — cycles are the leak this
  mechanism does not prevent.
