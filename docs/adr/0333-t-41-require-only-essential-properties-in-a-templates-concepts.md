# 0333. T.41: Require only essential properties in a template's concepts

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Keep interfaces simple and stable. Consider a `sort` instrumented with
oversimplified debug support:

```cpp
void sort(sortable auto& s)  // sort sequence s
{
    if (debug) cerr << "enter sort( " << s <<  ")\n";
    // ...
    if (debug) cerr << "exit sort( " << s <<  ")\n";
}
```

Should this be rewritten to:

```cpp
template<sortable S>
    requires Streamable<S>
void sort(S& s)  // sort sequence s
{
    if (debug) cerr << "enter sort( " << s <<  ")\n";
    // ...
    if (debug) cerr << "exit sort( " << s <<  ")\n";
}
```

After all, there is nothing in `sortable` that requires iostream support. On the
other hand, there is nothing in the fundamental idea of sorting that says
anything about debugging.

If every operation used has to be listed among the requirements, the interface
becomes unstable: every time the debug facilities, the usage data gathering, the
testing support or the error reporting change, the definition of the template
would need to change and every use of the template would have to be recompiled.
That is cumbersome and in some environments infeasible.

Conversely, if an operation is used in the implementation that is not guaranteed
by concept checking, a late compile-time error may result. By not using concept
checking for properties of a template argument that are not considered essential,
checking is delayed until instantiation time. That is considered a worthwhile
trade-off.

Note also that using non-local, non-dependent names such as `debug` and `cerr`
introduces context dependencies that might lead to mysterious errors. It can be
hard to decide which properties of a type are essential and which are not.

Upstream gives no enforcement for this rule.

## Decision

Constrain a template on the properties its purpose requires, not on every
property its current body happens to use.

- Include in the constraints the operations that define what the template is
  for: comparison for a sort, iteration for a traversal, arithmetic for an
  accumulation.
- Leave out properties used only by instrumentation, logging, assertions,
  diagnostics or other incidental machinery, and accept that misuse of those
  surfaces at instantiation.
- Do not add a constraint because a new tracing statement needed it; adding one
  is an interface change that recompiles every caller.
- Watch the non-local names such an implementation pulls in — they are context
  dependencies whose failures are harder to read than a missing constraint.

## Rationale

The requirement set is part of the interface, so anything listed there is a
promise to callers that will be expensive to withdraw, and incidental machinery
is exactly the part most likely to change next week. The trade-off is stated
plainly upstream and is worth restating: leaving a property unconstrained buys
interface stability at the price of a late error, and a late error in
instrumentation code is cheaper than a churning public contract.

## Consequences

### Positive

- The template's declaration says what the template is about, without
  implementation detail leaking into it.
- Adding or removing debug support does not change the interface or force
  recompilation of callers.
- Callers with types that lack the incidental property can still use the
  template.

### Negative

- It deliberately gives up early checking for some operations, so a caller can
  satisfy every stated constraint and still fail to compile deep inside the body
  — the failure mode concepts were introduced to remove.
- "Essential" is undecidable in general, and upstream says so; two reviewers can
  disagree indefinitely.
- It pulls against
  [0316](0316-t-1-use-templates-to-raise-the-level-of-abstraction-of-code.md) and
  [0326](0326-t-21-require-a-complete-set-of-operations-for-a-concept.md), which
  argue for requiring more than the body strictly needs.
- The unconstrained operations are invisible to a reader of the declaration, so
  what a type must actually support to instantiate the template is discoverable
  only by trying.

### Neutral

- The rule constrains what goes into a concept list, not how the concepts
  themselves are defined.
- The example's reliance on a global `debug` flag is separately governed by
  [0021](0021-i-2-avoid-non-const-global-variables.md).

## Related

- [0321](0321-t-10-specify-concepts-for-all-template-arguments.md) — every
  parameter is still constrained; this record limits what the constraint says.
- [0316](0316-t-1-use-templates-to-raise-the-level-of-abstraction-of-code.md) —
  the opposing pressure, and the one that argues minimality is not the goal.
- [0326](0326-t-21-require-a-complete-set-of-operations-for-a-concept.md) — a
  concept is complete even when a given template needs only part of it.
- [0021](0021-i-2-avoid-non-const-global-variables.md) — the context
  dependencies the note warns about.
