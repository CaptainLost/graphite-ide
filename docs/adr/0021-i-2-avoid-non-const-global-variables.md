# 0021. I.2: Avoid non-const global variables

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Non-`const` global variables hide dependencies and make those dependencies
subject to unpredictable change:

```cpp
struct Data {
    // ... lots of stuff ...
} data;            // non-const data

void compute()     // don't
{
    // ... use data ...
}
void output()      // don't
{
    // ... use data ...
}
```

Who else might modify `data`?

The initialisation of global objects is not totally ordered. If a global object
is used, initialise it with a constant — and note that it is possible to get
undefined initialisation order even for `const` objects.

Two threads make it worse. If one thread can access non-local data, or data
passed by reference, while another thread executes the callee, there is a data
race. Every pointer or reference to mutable data is a potential data race. There
can be no race condition on immutable data.

Using global pointers or references to access and change non-`const`, and
otherwise non-global, data is not a better alternative: it does not solve the
hidden dependencies or the potential race conditions.

The rule is "avoid", not "don't use". There will be rare exceptions, such as
`cin`, `cout` and `cerr`. Global *constants* are useful. A global object is
often better than a singleton. And the rule applies to namespace-scope variables
as well as to truly global ones.

The alternative, where global data was being used to avoid copying, is to pass
the data as an object by reference to `const`, or to define the data as the
state of some object and the operations as member functions of it.

## Decision

Avoid non-`const` variables at namespace scope, including global pointers and
references to non-`const` data.

- Prefer passing the data explicitly, by reference to `const`, or making it the
  state of an object whose member functions are the operations on it.
- Where a namespace-scope object is genuinely warranted, make it `const` or
  `constexpr` and initialise it with a constant expression.
- Treat each remaining non-`const` global as an exception that is named and
  justified where it is declared, not as an ordinary choice.

## Rationale

The cost of a mutable global is paid by every reader of every function that
touches it: the set of things that can change it is the whole program, so no
local reasoning is possible. Concurrency turns that from a comprehension problem
into a correctness one, because a mutable global is shared by definition, and
sharing without synchronisation is a race.

Immutability is the cheap escape. A `const` global has none of these problems —
no race, no hidden mutation — which is why the rule is about non-`const`
globals specifically rather than about globals as such.

## Consequences

### Positive

- Dependencies become visible in signatures rather than discoverable by reading
  every file.
- A whole category of data race disappears, rather than being managed.
- Code becomes testable, because state can be constructed per test rather than
  reset between tests.

### Negative

- State that really is program-wide has to be threaded through call chains or
  owned by some object that must itself be reachable, which is often more
  ceremony than the global was.
- The obvious workaround — a class with static members, or an object handed
  around by reference — can reproduce every problem the rule was avoiding while
  satisfying its letter.
- "Avoid" is a judgement, so this rule cannot settle any specific case on its
  own.

### Neutral

- Global constants are explicitly fine, and encouraged.
- The initialisation-order hazard is only partly addressed here; see
  [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md).
- Preferring a global object to a singleton is stated here, and the singleton
  side of it is [0022](0022-i-3-avoid-singletons.md).

## Enforcement

- (Simple) Report all non-`const` variables declared at namespace scope, and
  global pointers or references to non-`const` data.

## Related

- [0020](0020-i-1-make-interfaces-explicit.md) — hidden dependencies are what
  globals are made of.
- [0022](0022-i-3-avoid-singletons.md) — the same object with a class around it.
- [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md) — the
  initialisation-order half.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — why `const` is
  the escape hatch here.
