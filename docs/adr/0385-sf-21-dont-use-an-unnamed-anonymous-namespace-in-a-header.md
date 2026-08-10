# 0385. SF.21: Don't use an unnamed (anonymous) namespace in a header

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

It is almost always a bug to mention an unnamed namespace in a header file.

```cpp
// file foo.h:
namespace
{
    const double x = 1.234;  // bad
    double foo(double y)     // bad
    {
        return y + x;
    }
}

namespace Foo
{
    const double x = 1.234; // good
    inline double foo(double y)        // good
    {
        return y + x;
    }
}
```

An unnamed namespace gives its contents internal linkage. In a header that is the
opposite of what is wanted: every translation unit that includes the header gets
its own private copy of `x` and its own private `foo`. The header appears to
declare one entity and in fact declares as many as there are includers. Addresses
compare unequal between translation units, any mutable state is per-file rather
than shared, and the duplicated code is emitted once per object file. None of
this is diagnosed, because each translation unit is individually correct.

The named-namespace version says the intended thing. `inline` gives `foo` a
single definition shared by every translation unit, which is what makes a
function definition in a header legal at all under
[0373](0373-sf-2-a-h-file-must-not-contain-object-definitions-or-non-inline-function-definitions.md).
The constant is the subtler case: a namespace-scope `const` variable already has
internal linkage unless it is declared `extern`, so `Foo::x` is still one object
per translation unit. What the named namespace buys there is a name that says
where the constant belongs and cannot be confused with an interface; making it a
single object as well takes `inline constexpr`.

## Decision

Never open an unnamed namespace in a header.

- Put header-resident definitions in a named namespace, marking functions
  `inline` and constants `constexpr` — `inline constexpr` where the constant
  must be one object rather than one per includer.
- Where something really is internal to one translation unit, define it in the
  `.cpp` and put it in an unnamed namespace there, per
  [0386](0386-sf-22-use-an-unnamed-anonymous-namespace-for-all-internal-non-exported-entities.md).
- Do not use an unnamed namespace in a header as a way of avoiding a
  duplicate-symbol link error; the error means the definition does not belong in
  the header.
- The rule covers any header, including one included only by generated or
  template code.

## Rationale

This is a rule about a construct that quietly does something other than it
appears to. Internal linkage in a header does not scope anything — it multiplies
it — and because every copy is valid, nothing in the build ever reports the
mistake. The failure surfaces as a pointer comparison that should have been true,
or as state that mysteriously fails to propagate, long after the header was
written.

## Consequences

### Positive

- An `inline` definition in a named namespace is one entity with one address,
  rather than one silently created per includer.
- Code is not duplicated once per object file for want of a keyword.
- The header says what it means: a named namespace with `inline` definitions.

### Negative

- Getting it right requires understanding `inline` and linkage, which is more
  than the rule asks for on its face; substituting a named namespace without
  marking the function `inline` trades a silent bug for a link error.
- It removes the one easy way to keep a helper out of a header's public surface,
  so a helper that has to live in the header is visible to everyone who includes
  it.
- The rule cannot be applied to headers you do not own, and a third-party header
  that breaks it produces exactly the same silent duplication in your build.

### Neutral

- Nothing here restricts unnamed namespaces in a `.cpp`, which is where they
  belong.
- Duplication of an `inline` function across translation units is a different
  thing: the language requires those copies to be one entity.

## Enforcement

- Flag any use of an anonymous namespace in a header file.

## Related

- [0386](0386-sf-22-use-an-unnamed-anonymous-namespace-for-all-internal-non-exported-entities.md)
  — where the unnamed namespace does belong.
- [0373](0373-sf-2-a-h-file-must-not-contain-object-definitions-or-non-inline-function-definitions.md)
  — what a header may define, and why `inline` is the answer.
- [0378](0378-sf-7-dont-write-using-namespace-at-global-scope-in-a-header-file.md)
  — the other namespace construct banned from headers.
- [0384](0384-sf-20-use-namespaces-to-express-logical-structure.md) — the named
  namespace to use instead.
