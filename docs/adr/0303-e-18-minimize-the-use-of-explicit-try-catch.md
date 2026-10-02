# 0303. E.18: Minimize the use of explicit try/catch

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

`try`/`catch` is verbose, and non-trivial uses are error-prone. Its presence is
often a sign of unsystematic or low-level resource management, or of
unsystematic error handling.

```cpp
void f(zstring s)
{
    Gadget* p;
    try {
        p = new Gadget(s);
        // ...
        delete p;
    }
    catch (Gadget_construction_failure) {
        delete p;
        throw;
    }
}
```

This code is messy. There could be a leak from the naked pointer in the `try`
block. Not all exceptions are handled. And deleting an object that failed to
construct is almost certainly a mistake — if `new Gadget(s)` threw, `p` was
never assigned, so the handler deletes an uninitialised pointer. Better:

```cpp
void f2(zstring s)
{
    Gadget g {s};
}
```

The second version is not merely shorter. It is correct on every path, including
the ones the first version's author was trying to cover and got wrong. That is
the shape of the argument: the handler was written precisely because the author
was thinking about failure, and it still contained a defect that the version
without a handler cannot contain.

The alternatives are proper resource handles with RAII, and `finally` for
cleanup that no handle covers.

## Decision

Prefer constructs that make handlers unnecessary to writing handlers.

- Replace a handler whose only purpose is cleanup with a resource handle. That
  is the `f`-to-`f2` transformation above.
- Where no handle fits, use a `finally`/`final_action` object rather than a
  `try`/`catch`; see
  [0304](0304-e-19-use-a-final-action-object-to-express-cleanup-if-no-suitable-resource-handle-is-available.md).
- Do not hold a raw resource inside a `try` block. If the handler has a `delete`
  in it, the resource was owned by the frame and should not have been.
- Never release an object whose construction failed.
- Handle all the exceptions a `try` block can produce, or none of them. A
  handler for one type in a block that can throw several is the `f` shape again.
- Where a `try`/`catch` is genuinely required, keep the block small and let it
  do one thing. A `try` wrapping a long body is a handler that cannot know which
  statement failed.

## Rationale

Error-handling code is the least exercised code in a program, so hand-written
handlers are where defects hide longest. The `f` example is the argument in
miniature: a careful author, thinking specifically about the failure path, wrote
a double bug into four lines, and the version with no handler at all has neither
bug.

The economy is that a destructor is written once and reviewed once, whereas a
handler is written at every site that acquires the resource. Reducing the number
of handlers reduces the number of independent opportunities to get the failure
path wrong from many to one.

## Consequences

### Positive

- Failure paths stop being hand-written per call site, so there are fewer of
  them to be wrong.
- Functions shrink to the work they actually do, which makes the work readable.
- Classes of defect visible in the `f` example — deleting after a failed
  construction, uninitialised pointers in the handler — become unexpressible.

### Negative

- "Minimize" is a direction, not a criterion. Nothing says how many is too many,
  and the guidelines offer no mechanical check.
- Pushing failure handling into destructors and handle types means the failure
  logic is no longer where the failure happens, which can make it harder to
  follow rather than easier.
- Fewer explicit handlers means more code through which an exception passes
  invisibly, and every one of those frames must be exception-safe whether or not
  its author thought about it.
- Wrapping a C API in a handle just to remove one handler can be more code
  overall, even though it is better code.

### Neutral

- The rule is about the volume of explicit handling, not about which
  error-handling mechanism is chosen.
- `finally` is endorsed here as an alternative, while
  [0304](0304-e-19-use-a-final-action-object-to-express-cleanup-if-no-suitable-resource-handle-is-available.md)
  records that it is a last resort relative to a proper handle.

## Enforcement

- No mechanical check is offered; detecting this reliably would need a
  heuristic.

## Related

- [0302](0302-e-17-dont-try-to-catch-every-exception-in-every-function.md) — the
  companion rule about where handling belongs.
- [0294](0294-e-6-use-raii-to-prevent-leaks.md) — the replacement for a
  cleanup-only handler.
- [0304](0304-e-19-use-a-final-action-object-to-express-cleanup-if-no-suitable-resource-handle-is-available.md)
  — what to use when no handle is available.
