# 0417. NR.6: Don't place all cleanup actions at the end of a function and goto exit

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NR (Non-Rules and myths), transcribed 2026-08-10

## Context

The NR section collects rules that are popular somewhere but that the
guidelines deliberately do not recommend. This record adopts the rejection, not
the practice being rejected.

The practice here is the single-cleanup-block idiom: every failure path jumps to
a label at the end of the function, where all the releases are written once.
`goto` is error-prone, and the technique is a pre-exception way of getting
RAII-like resource and error handling in a language that had neither.

```cpp
void do_something(int n)
{
    if (n < 100) goto exit;
    // ...
    int* p = (int*) malloc(n);
    // ...
    if (some_error) goto exit;
    // ...
exit:
    free(p);
}
```

Upstream invites the reader to spot the bug, and there is one: the first `goto`
jumps forward past the declaration and initialization of `p` into the scope
where `free(p)` is written, so the cleanup would run on a pointer that was never
given a value. C++ will not compile a jump that bypasses an initialization, so
this particular function is rejected rather than run — which is itself the
point, since the idiom is inherited from a language that accepts it, and the
same mistake made with a resource acquired conditionally rather than declared
after the label compiles perfectly well. The idiom's whole promise is that
cleanup happens exactly once on every path, and the very first path breaks it.

The alternative upstream offers is RAII, with exceptions for the error path, and
for resources that are not owned by a handle, a final-action object.

## Decision

Do not route failure paths through a shared cleanup label, and do not use `goto`
for error handling.

- Give every resource an owner whose destructor releases it, so that cleanup
  happens on every exit path without being written on any of them.
- Report a failure by throwing, or by returning early, and let the destructors
  of the objects going out of scope be the cleanup mechanism.
- Where a resource has no suitable handle — a C library's handle, a global flag
  that must be restored — wrap it in a scope guard or final-action object rather
  than repeating the release, or jumping to it.
- Where a C API's acquire/release pair is used repeatedly, write the handle
  class once and use it, per
  [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md).
- Do not introduce a cleanup label in order to satisfy a single-return
  convention; that convention is itself rejected by
  [0413](0413-nr-2-dont-insist-to-have-only-a-single-return-statement-in-a-function.md).

## Rationale

The idiom is an attempt to make cleanup unconditional using a mechanism that is
entirely conditional. Its correctness depends on every jump reaching the label
with exactly the right set of resources acquired, and nothing checks that
invariant — which is why the four-line example is already wrong. A destructor
inverts the arrangement: the release is attached to the acquisition rather than
to the exit, so it is not possible to add a path that forgets it.

The idiom also fails for the exit path it cannot see. A `goto exit` chain
handles the returns it was written for; it does nothing about an exception
thrown by a call in between, and a destructor handles both without being told
which is which.

## Consequences

### Positive

- Cleanup cannot be skipped by a path added later, because no path names it.
- Adding a resource to a function is a one-line change instead of an edit to
  every jump and to the shared label.
- Functions get shorter, because the error paths stop carrying release code.

### Negative

- It requires a handle type per resource. For a C API used once, writing that
  class is more code than the `goto` version it replaces, and it is code that
  must itself be got right — copy and move semantics included.
- Destruction order is fixed by declaration order, so a release sequence that
  genuinely must differ from the reverse of acquisition is harder to express
  than it was with an explicit block.
- A destructor cannot report a failure, so a release that can fail — closing a
  file whose buffered write is lost — has nowhere to say so.
- The rule leaves `goto` in the language for the cases the guidelines do allow,
  so "no `goto` for cleanup" is a narrower statement than it first appears.

### Neutral

- The objection is to using `goto` as a resource-management strategy, not to
  concentrating error handling in one place; a function may still handle all its
  errors at one point, provided the resources are not released by hand there.
- Upstream's alternative pairs RAII with exceptions, but the argument does not
  depend on the reporting mechanism: an early return on failure needs RAII just
  as much as a throw does.

## Related

- [0222](0222-es-76-avoid-goto.md) — the general rule this non-rule violates.
- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — the alternative upstream offers.
- [0304](0304-e-19-use-a-final-action-object-to-express-cleanup-if-no-suitable-resource-handle-is-available.md)
  — the other alternative, for resources with no handle.
- [0413](0413-nr-2-dont-insist-to-have-only-a-single-return-statement-in-a-function.md)
  — the non-rule that leads to this one.
