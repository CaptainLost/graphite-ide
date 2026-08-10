# 0153. R.10: Avoid malloc() and free()

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

`malloc()` and `free()` do not support construction and destruction, and they do
not mix with `new` and `delete`.

```cpp
class Record {
    int id;
    string name;
    // ...
};
void use()
{
    // p1 might be nullptr
    // *p1 is not initialized; in particular,
    // that string isn't a string, but a string-sized bag of bits
    Record* p1 = static_cast<Record*>(malloc(sizeof(Record)));
    auto p2 = new Record;
    // unless an exception is thrown, *p2 is default initialized
    auto p3 = new(nothrow) Record;
    // p3 might be nullptr; if not, *p3 is default initialized
    // ...
    delete p1;    // error: cannot delete object allocated by malloc()
    free(p2);    // error: cannot free() object allocated by new
}
```

In some implementations that `delete` and that `free()` might appear to work, or
they might cause run-time errors. Neither outcome is a diagnostic.

There are applications and sections of code where exceptions are not acceptable
— life-critical hard-real-time code is the best example — and the failure mode
of `new` is where `malloc` is usually reached for. In such cases, use the
`nothrow` form of `new`, which returns a null pointer instead of throwing while
still constructing the object. Many bans on exception use are based on
superstition, or on concern for older code bases with unsystematic resource
management.

## Decision

Do not use `malloc()` and `free()` to obtain storage for C++ objects.

- Allocate through a resource handle: `make_unique`, `make_shared`, or a
  container. Where storage really must be obtained directly, use `new` and
  `delete`.
- Never release with `free()` what was obtained from `new`, and never `delete`
  what came from `malloc()`.
- Where allocation must not throw, use `new(std::nothrow)` and check the result,
  rather than dropping to `malloc` to avoid the exception.
- Calling a C library that allocates with `malloc` and hands the pointer back is
  not a violation of this rule; wrap the pointer in a handle whose destructor
  calls the library's matching release function, and confine the raw pair to
  that wrapper.

## Rationale

The decisive difference is not performance and not the null return: it is that
`malloc` produces storage, and a C++ object is not storage. A `Record` obtained
from `malloc` has a `string` member that was never constructed, so the first
operation on it is undefined behaviour that no compiler will mention.
Mismatching the pair afterwards compounds it, and the standard says nothing
about what happens, which means implementations are free to make it work today
and fail after a toolchain upgrade.

## Consequences

### Positive

- Objects are always constructed before use and destroyed before their storage
  is released.
- Allocation failure has one representation rather than two, so callers do not
  need to know which one a given function used.
- Mismatched allocate/release pairs, whose symptoms are heap corruption long
  after the fact, stop being possible.

### Negative

- `realloc` has no equivalent, so code that grows a raw buffer in place must be
  restructured around a container that reallocates and moves.
- Interoperating with a C library that allocates and expects you to `free`, or
  that expects a `malloc`ed buffer, still requires the raw pair somewhere; the
  rule confines it rather than removing it.
- The failure channel changes: `malloc` reports exhaustion by returning a null
  pointer, `new` by throwing `std::bad_alloc`. Code that tested the return value
  has to be restructured around an exception, and callers that cannot let one
  propagate must catch it at the boundary.
- `new(std::nothrow)` returns a pointer that is easy to use unchecked, so the
  exception-free route trades a thrown error for a silently ignored one.

### Neutral

- A custom allocator or arena is not forbidden by this rule; it must simply
  construct and destroy the objects it hands out.
- This rule settles which allocator is used, not how allocation failure should
  be reported onwards; the throwing and `nothrow` forms of `new` both satisfy
  it.

## Enforcement

- Flag explicit use of `malloc` and `free`.

## Related

- [0154](0154-r-11-avoid-calling-new-and-delete-explicitly.md) — the next step:
  avoid the C++ pair too.
- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — where the wrapper for a C allocator belongs.
- [0158](0158-r-15-always-overload-matched-allocation-deallocation-pairs.md) —
  the same pairing requirement, one level down.
