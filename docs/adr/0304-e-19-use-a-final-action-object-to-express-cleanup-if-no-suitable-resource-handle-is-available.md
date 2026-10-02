# 0304. E.19: Use a final_action object to express cleanup if no suitable resource handle is available

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

`finally` from the Guidelines Support Library is less verbose and harder to get
wrong than `try`/`catch`.

```cpp
void f(int n)
{
    void* p = malloc(n);
    auto _ = gsl::finally([p] { free(p); });
    // ...
}
```

`finally` is not as messy as `try`/`catch`, but it is still ad hoc. Prefer
proper resource management objects; consider `finally` a last resort.

Its other use is as a systematic and reasonably clean alternative to the old
`goto exit;` technique for dealing with cleanup where resource management is not
systematic. That is the pattern found in C code and in code written to avoid
exceptions: a chain of allocations, a chain of labels at the bottom, and each
failure jumping to the right label. `finally` expresses the same intent without
requiring the labels to be maintained in the correct order, which is the part of
the `goto` idiom that breaks when a resource is added in the middle.

The ordering point is worth stating plainly, because it is the mechanical
advantage. `final_action` objects are destroyed in reverse order of
construction, automatically, so cleanup order follows acquisition order without
anyone maintaining it. In the `goto` version that ordering is written by hand in
a different part of the function from the acquisitions it mirrors.

The reason it remains a last resort is that a `final_action` is a local variable
whose name is never used. Nothing refers to it, so nothing breaks if it is
moved, renamed, shadowed, or deleted; and if it is constructed before the
resource it releases, or the lambda captures something that later changes, the
error is silent. A handle type has none of those properties, because the
resource and its release are the same object.

Nothing about `final_action` is specific to exceptions. It guards every exit
from the scope — a throw, an early return, or falling off the end — because all
three run the destructor of a local. That is why it is equally the answer to the
`goto exit;` idiom, which exists in code that never throws at all.

## Decision

Where cleanup is needed and no resource handle covers it, attach a
`final_action` object rather than repeating the cleanup or writing a handler.

- Reach for a proper handle type first. Use `final_action` only when writing one
  is genuinely impractical.
- Construct the `final_action` immediately after acquiring the thing it
  releases, so no exit path exists between the two.
- Keep the lambda small and make it incapable of failing; it runs during
  destruction and is bound by
  [0301](0301-e-16-destructors-deallocation-swap-and-exception-type-copy-move-construction-must-never-fail.md).
- Do not use `goto exit;` chains for cleanup. Where such a pattern exists,
  replace it with `final_action` objects, whose ordering is maintained by the
  language.
- Capture by value what the cleanup needs, and do not depend on locals that the
  body may reassign before the exit.
- Where a `final_action` is used repeatedly for the same resource, that is the
  signal to write the handle type after all.

## Rationale

`final_action` is the smallest construct that makes cleanup run on every exit
path, including the ones that do not exist yet. That is the same property RAII
provides, obtained without defining a type, which is why it is worth having at
all and also why it should not be the default: the property comes from a local
that nothing references.

Against the `goto exit;` alternative the case is stronger. Both run cleanup on
every path, but the `goto` version encodes the ordering in a sequence of labels
maintained separately from the acquisitions, and inserting a resource in the
middle means editing both places correctly. Destruction order is not something
anyone can get wrong.

## Consequences

### Positive

- Cleanup runs on every exit, including exits added later and exits inside
  callees.
- Ordering is handled by destruction order rather than by hand-maintained
  labels.
- It is far shorter than the `try`/`catch` it replaces, and cannot contain the
  defects of the `f` example in
  [0303](0303-e-18-minimize-the-use-of-explicit-try-catch.md).

### Negative

- It is an unused local. Nothing warns when it is removed, moved above its
  resource, or shadowed, and every one of those is silently wrong.
- It separates the resource from its release: the acquisition and the cleanup
  are two statements that must agree, unlike a handle where they are one object.
- It scales badly. Several resources means several unused locals with
  conventional names, and the function's shape stops communicating anything.
- It requires the GSL, or a local equivalent, to be available — a dependency
  taken on for a last-resort construct.

### Neutral

- The rule ranks it below a proper handle explicitly, so using it is an
  admission that no handle was practical.
- Nothing about it is specific to exceptions; it guards early returns equally.

## Enforcement

- Heuristic: detect `goto exit;`.

## Related

- [0294](0294-e-6-use-raii-to-prevent-leaks.md) — the preferred mechanism, of
  which this is the fallback.
- [0303](0303-e-18-minimize-the-use-of-explicit-try-catch.md) — the construct
  `final_action` displaces.
- [0298](0298-e-13-never-throw-while-being-the-direct-owner-of-an-object.md) —
  the leak this closes when no handle exists.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — `finally` comes
  from the GSL.
