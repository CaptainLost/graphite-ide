# 0377. SF.6: Use using namespace directives for transition, for foundation libraries (such as std), or within a local scope (only)

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

`using namespace` can lead to name clashes, so it should be used sparingly.
However, it is not always possible to qualify every name from a namespace in user
code — during a transition, for one — and sometimes a namespace is so fundamental
and so prevalent in a code base that consistent qualification would be verbose and
distracting.

```cpp
#include <string>
#include <vector>
#include <iostream>
#include <memory>
#include <algorithm>

using namespace std;
// ...
```

Here the standard library is used pervasively and apparently no other library is
used, so requiring `std::` everywhere could be distracting.

The risk is real, though. `using namespace std;` leaves the programmer open to a
clash with a name from the standard library:

```cpp
#include <cmath>

using namespace std;

int g(int x)
{
    int sqrt = 7;
    // ...
    return sqrt(x); // error
}
```

This is not particularly likely to lead to a resolution that is not an error, and
people who write `using namespace std` are supposed to know about `std` and about
this risk.

A `.cpp` file is a form of local scope. There is little difference in the
opportunities for name clashes between an N-line `.cpp` containing a
`using namespace X`, an N-line function containing one, and M functions each
containing one with N lines of code between them.

The one place the directive is never acceptable is a header at global scope; that
is
[0378](0378-sf-7-dont-write-using-namespace-at-global-scope-in-a-header-file.md).

## Decision

Restrict `using namespace` to the three cases the rule permits.

- Inside a function or another local scope, where the exposure is bounded by the
  scope itself.
- At namespace scope in a `.cpp` file, for a foundation library such as `std`
  that is used pervasively throughout that file.
- Temporarily, during a transition — a namespace being introduced or renamed —
  with the directive removed when the transition finishes.

Beyond that:

- Never at global scope in a header.
- Do not have two `using namespace` directives for different namespaces in one
  source file; that is the configuration in which clashes stop being predictable.
- Prefer a `using` declaration for the individual name, or a namespace alias,
  when what is wanted is one name rather than a whole namespace.

## Rationale

The cost of a `using namespace` is proportional to the amount of code it covers
and to the number of namespaces it merges, which is why the rule is drawn around
scope rather than around the directive itself. One namespace pulled into one file
is a bounded, inspectable risk; two namespaces pulled into a header is an
unbounded one, because the set of affected translation units is not known at the
point the directive is written.

The `sqrt` example is the honest version of the danger: it is almost always a
compile error rather than a silent change of meaning. That is a good outcome, and
it is why the rule permits the directive at all instead of banning it.

## Consequences

### Positive

- Pervasive standard-library use stays readable without `std::` on every line.
- The blast radius of any directive is a single file at most.
- A transition can proceed without a mechanical rename of every use site.

### Negative

- It sits directly against the argument for always-qualified names, which make
  the origin of every identifier visible at the point of use; this rule trades
  that away for brevity.
- "Foundation library" and "transition" are judgement calls, so the rule is not
  self-applying — and a transition directive left in place looks exactly like a
  permitted one.
- Even a file-scope directive changes overload resolution for the whole file, so
  adding an unrelated include can change which function a call resolves to.

### Neutral

- `using namespace std::literals` is a narrower case with its own reasoning; see
  [0378](0378-sf-7-dont-write-using-namespace-at-global-scope-in-a-header-file.md).
- A `using` declaration naming one entity is a different construct and is not
  restricted by this rule.

## Enforcement

- Flag multiple `using namespace` directives for different namespaces in a single
  source file.

## Related

- [0378](0378-sf-7-dont-write-using-namespace-at-global-scope-in-a-header-file.md)
  — the case this rule excludes outright.
- [0384](0384-sf-20-use-namespaces-to-express-logical-structure.md) — the
  namespaces a directive would flatten.
- [0175](0175-es-5-keep-scopes-small.md) — the general principle that bounds the
  permitted uses.
- [0182](0182-es-12-do-not-reuse-names-in-nested-scopes.md) — the clash this rule
  is trying to avoid, in its other form.
