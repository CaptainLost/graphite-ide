# 0014. P.8: Do not leak any resources

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

Even a slow growth in resource use will, over time, exhaust the availability of
those resources. This matters particularly for long-running programs, but it is
an essential piece of responsible programming behaviour regardless.

The failure is almost always an early return past a manual release:

```cpp
void f(char* name)
{
    FILE* input = fopen(name, "r");
    // ...
    if (something) return;   // bad: if something == true, a file handle is
                             // leaked
    // ...
    fclose(input);
}
```

RAII removes the possibility rather than the instance:

```cpp
void f(char* name)
{
    ifstream input {name};
    // ...
    if (something) return;   // OK: no leak
    // ...
}
```

A leak is colloquially "anything that isn't cleaned up". The more important
classification is "anything that can no longer be cleaned up" — for example,
allocating an object on the heap and then losing the last pointer to that
allocation. The rule should not be read as requiring that allocations within
long-lived objects be returned during program shutdown: relying on
system-guaranteed cleanup, such as file closing and memory deallocation on
process shutdown, can simplify code. However, relying on abstractions that
implicitly clean up can be just as simple, and is often safer.

Enforcing the lifetime safety profile eliminates leaks. Combined with the
resource safety provided by RAII, it eliminates the need for garbage collection,
by generating no garbage. Combined further with the type and bounds profiles, it
yields complete type- and resource-safety, guaranteed by tools.

## Decision

Do not leak any resources. Own every resource — memory, file handles, sockets,
locks, GPU objects, OS handles — through an object whose destructor releases it.

- Acquire a resource in a constructor and release it in a destructor; do not
  pair an acquisition with a release statement that control flow can skip.
- Prefer standard-library resource handles to hand-written ones, and a
  hand-written RAII type to a raw acquire/release pair.
- Where a raw owning pointer is unavoidable — at a C boundary, say — mark it as
  an owner rather than leaving ownership to convention.
- Treat "the process will clean it up" as acceptable only for genuinely
  process-lifetime state, and never as a reason to leave ownership unclear.

## Rationale

The distinction the upstream note draws is the useful one: the problem is not
that memory is still held at exit, it is that the program has lost the ability
to release it. Ownership that lives in a type is ownership a later edit cannot
accidentally drop, and every early return, every exception and every new branch
is exactly such an edit.

That is also why the rule is phrased as an absolute despite the shutdown
carve-out. "Leak nothing" is checkable and teachable; "leak only what does not
matter" requires a judgement at every site.

## Consequences

### Positive

- Early returns and exceptions stop being leak opportunities.
- The lifetime of a resource is visible in the code as the lifetime of an
  object, which is a thing readers and tools already track.
- No garbage collector, and no periodic leak-hunting, because there is no
  garbage.

### Negative

- Wrapping a C-style resource means writing a small RAII type, with the copy and
  move operations thought through, for something that was two calls.
- RAII fixes destruction order to scope exit, which is occasionally not what is
  wanted, and working around it — release, reset, deferred destruction — is
  fiddlier than the manual version would have been.
- The rule as stated is stricter than the note that follows it, so a reader can
  reasonably arrive at different answers about shutdown-time cleanup.

### Neutral

- Which specific handle type is used for which resource is not decided here.
- The lifetime, type and bounds profiles are mentioned as the mechanism that
  would make this tool-enforceable; adopting them is a separate decision, and
  none is enabled by this record.

## Enforcement

- Look at pointers: classify them into non-owners (the default) and owners.
  Where feasible, replace owners with standard-library resource handles.
  Alternatively, mark an owner as such using `owner` from the GSL.
- Look for naked `new` and `delete`.
- Look for known resource-allocating functions returning raw pointers, such as
  `fopen`, `malloc` and `strdup`.

## Related

- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — returning an object that owns *and* knows its size.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the other half of "this is
  C++": correctness of resource use and economy of it.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — where the remaining raw acquisition code belongs.
