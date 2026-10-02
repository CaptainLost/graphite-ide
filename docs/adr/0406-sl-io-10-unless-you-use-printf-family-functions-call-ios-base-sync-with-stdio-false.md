# 0406. SL.io.10: Unless you use printf-family functions call ios_base::sync_with_stdio(false)

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Synchronising iostreams with `printf`-style I/O can be costly. `cin` and `cout`
are by default synchronised with `printf`.

```cpp
int main()
{
    ios_base::sync_with_stdio(false);
    // ... use iostreams ...
}
```

The default exists so that a program can mix `printf` and `cout` and see the
output interleaved in the order the calls were made. Paying for that guarantee
means the standard streams cannot maintain their own buffers independently, and
on a typical implementation each character or each operation goes through the C
stdio layer. A program that never calls a `printf`-family function pays that
cost for a guarantee it does not use.

Turning the synchronisation off is not a formatting change or an optimisation
flag; it changes what the standard streams are permitted to do with buffering,
which is why it must happen before any I/O occurs.

## Decision

Turn off the synchronisation in a program that uses only iostreams.

- Call `std::ios_base::sync_with_stdio(false)` early in `main`, before any
  input or output has taken place.
- Do not call it in a program that also uses `printf`, `scanf`, `puts` or any
  other stdio function on the standard streams, and do not call it in code that
  cannot know whether such a program will link it in.
- Once it has been called, do not mix the two families on the standard streams
  at all; the relative order of their output is no longer defined by anything.
- Treat this as a whole-program decision made in one place, not as something
  each translation unit does for itself.

## Rationale

This is the rare case where a single call removes a cost that no amount of local
care can address, because the cost is imposed by a compatibility guarantee
rather than by the code doing the I/O.

The condition attached to the rule is the important half. The call is safe
exactly when the guarantee it discards is unused, so the decision reduces to a
factual question about the program — does anything here use stdio on the
standard streams? — rather than to a judgement about performance.

## Consequences

### Positive

- Removes a per-operation cost from every use of the standard streams, for one
  line of code.
- The standard streams may maintain their own buffers, which is where the gain
  actually comes from.
- The precondition is a property of the whole program and can be checked once.

### Negative

- It is global and order-dependent: if any I/O has already been performed on the
  standard streams, the effect of the call is implementation-defined, and
  nothing diagnoses that.
- A library cannot make this call safely, because it cannot know what the
  program that links it does. That confines the rule to application code, which
  the rule itself does not say.
- Once set, a later addition of a single `printf` — or of a third-party library
  that uses one — reintroduces interleaving bugs that appear as
  mysteriously reordered output, far from the call that caused them.
- The benefit is implementation-dependent, and on implementations where the
  standard streams are already efficient it may be nil.
- Upstream gives no enforcement, and the condition is not mechanically
  checkable in the presence of separate compilation.

### Neutral

- The call affects only the standard streams' relationship with stdio; file and
  string streams are unaffected.
- The rule assumes iostreams are being used at all, which is
  [0405](0405-sl-io-3-prefer-iostreams-for-i-o.md).

## Related

- [0405](0405-sl-io-3-prefer-iostreams-for-i-o.md) — establishes the premise
  under which this call is safe.
- [0407](0407-sl-io-50-avoid-endl.md) — the other cheap, unconditional cost
  removal in stream output.
- [0240](0240-per-3-dont-optimize-something-thats-not-performance-critical.md) —
  worth remembering before restructuring a program around this call.
- [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md) — the
  reason this belongs at the top of `main` rather than in a static initialiser.
