# 0021. I.2: Avoid non-const global variables

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

This repository has two: `g_monoFont` and `g_monoItalicFont`, file-scope
pointers in `Core/src/Core/Gui/ImGuiRenderer.cpp`, returned by accessor
functions. They exist because ImGui's font atlas is built once at startup and
every panel that wants a monospaced font needs to reach it.

## Decision

Adopt I.2 as written. Transcribed in full:

> **Reason.** Non-const global variables hide dependencies and make the
> dependencies subject to unpredictable changes.

**Example.**

```cpp
struct Data {
    // ... lots of stuff ...
} data;            // non-const data

void compute()     // don't
{
    // ... use data ...
}

void output()     // don't
{
    // ... use data ...
}
```

Who else might modify `data`?

**Warning.** The initialization of global objects is not totally ordered. If you
use a global object initialize it with a constant. Note that it is possible to
get undefined initialization order even for `const` objects.

**Exception.** A global object is often better than a singleton.

**Note.** Global constants are useful.

**Note.** The rule against global variables applies to namespace scope variables
as well.

Alternative: If you use global (more generally namespace scope) data to avoid
copying, consider passing the data as an object by reference to `const`. Another
solution is to define the data as the state of some object and the operations as
member functions.

**Warning.** Beware of data races: If one thread can access non-local data (or
data passed by reference) while another thread executes the callee, we can have
a data race. Every pointer or reference to mutable data is a potential data
race.

Using global pointers or references to access and change non-`const`, and
otherwise non-global, data isn't a better alternative to non-`const` global
variables since that doesn't solve the issues of hidden dependencies or
potential race conditions.

**Note.** You cannot have a race condition on immutable data.

**Note.** The rule is "avoid", not "don't use." Of course there will be (rare)
exceptions, such as `cin`, `cout`, and `cerr`.

**Enforcement.** (Simple) Report all non-`const` variables declared at namespace
scope and global pointers/references to non-`const` data.

## Rationale

Adopted as written, including the "avoid, not don't use" framing, because the
two globals this codebase has are close to the exception the rule allows: they
are set once during startup and only read afterwards, and they hold pointers
into ImGui's own atlas, which is itself global state this program does not own.

They are still recorded as a deviation, because the rule's warning applies
literally: nothing in the type system says they are write-once, and a second
`ImGuiRenderer` — which nothing forbids — would overwrite them.

Note the rule's Exception, which matters for the next record: a global object is
often better than a singleton. That is an argument for keeping these as globals
rather than "fixing" them into a singleton wrapper.

## Consequences

### Positive

- Sets a low ceiling: two known globals, both in one file, both with a stated
  reason.
- The threading warning is live here — the watcher thread exists — so the rule
  is not theoretical.

### Negative

- The alternative the rule offers, passing the data as an object, means
  threading a font handle through every panel that draws text. That is a real
  cost for something read in a dozen places and written in one.

### Neutral

- Global constants are unaffected; the codebase uses them freely, which the rule
  explicitly permits.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| `g_monoFont`, `g_monoItalicFont` — non-const namespace-scope pointers, set at startup, read from anywhere | `Core/src/Core/Gui/ImGuiRenderer.cpp:40`, `:45` | Write-once by convention only; nothing enforces it |

## Related

- [0020](0020-i-1-make-interfaces-explicit.md) — the previous rule.
- [0022](0022-i-3-avoid-singletons.md) — the next rule, and the alternative this
  one warns against.
- [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md) — I.22, on
  the initialization order this rule warns about.
