# 0165. R.31: If you have non-std smart pointers, follow the basic pattern from std

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

The smart pointer rules also work for third-party and custom smart pointers, and
are useful for diagnosing the common smart pointer errors that cause performance
and correctness problems. You want the rules to work on all the smart pointers
you use.

Any type, including a primary template or a specialisation, that overloads unary
`*` and `->` is considered a smart pointer. If it is copyable, it is recognised
as a reference-counted shared pointer. If it is not copyable, it is recognised
as a unique pointer.

```cpp
// use Boost's intrusive_ptr
#include <boost/intrusive_ptr.hpp>
void f(boost::intrusive_ptr<widget> p)  // error under rule 'sharedptrparam'
{
    p->foo();
}
// use Microsoft's CComPtr
#include <atlbase.h>
void f(CComPtr<widget> p)               // error under rule 'sharedptrparam'
{
    p->foo();
}
```

Both are errors under the shared-pointer parameter guideline. `p` is a shared
pointer, but nothing about its sharedness is used here, and passing it by value
is a silent pessimisation. These functions should accept a smart pointer only if
they need to participate in the widget's lifetime management. Otherwise they
should accept a `widget*`, if it can be `nullptr`; otherwise, and ideally, the
function should accept a `widget&`. Because these smart pointers match the
shared pointer concept, the enforcement rules work on them out of the box and
expose the common pessimisation.

## Decision

Give any custom or wrapped smart pointer the shape the standard ones have, so
that the smart pointer rules apply to it.

- Overload unary `*` and `->`, so that the type is recognised as a smart pointer
  at all.
- Make a reference-counted, shared-ownership pointer copyable, and make an
  exclusively-owning pointer move-only. Do not make a move-only type copyable
  for convenience, and do not give a shared type a deleted copy constructor for
  safety; either miscommunicates ownership to every reader and every tool.
- Apply the parameter rules to the custom type exactly as to `std::shared_ptr`
  and `std::unique_ptr`: pass it only where lifetime participation is intended,
  and pass `widget&` or `widget*` otherwise.
- Where a third-party pointer cannot be changed — a COM pointer, an intrusive
  pointer from a library — treat it as whichever standard type its copyability
  implies, and keep it out of signatures that only use the object.

## Rationale

The copyable/non-copyable distinction is doing the work here, and it is not
arbitrary: copyability is exactly the observable difference between an ownership
that may be duplicated and one that may not. A type that follows the convention
gets every existing guideline, checker and reader intuition for free; one that
does not gets a bespoke set of habits that nobody will learn, and analysis tools
will classify it anyway — just wrongly.

## Consequences

### Positive

- Existing checkers and existing reader knowledge transfer to custom smart
  pointers with no configuration.
- The by-value pessimisation is caught in third-party pointer types as well as
  standard ones.
- Ownership semantics become inferable from copyability, uniformly across the
  codebase.

### Negative

- The heuristic misclassifies types that are legitimately copyable without being
  reference-counted, such as a deep-copying pointer or a non-owning handle with
  pointer syntax, and the resulting diagnostics are wrong in a confusing way.
- Following the pattern constrains the design of a custom pointer: a type that
  wants copy semantics for an unrelated reason must either give them up or
  accept being read as shared ownership.
- Types imposed by a platform — `CComPtr` and friends — cannot be changed at
  all, so the rule reduces to "be careful with them" exactly where care is
  hardest.
- Any type overloading `*` and `->` is caught, including iterators,
  optional-like types and proxy references that own nothing, which produces
  false positives.

### Neutral

- The rule does not say whether a custom smart pointer should exist; it says
  what shape it should have if one does.
- Wrapping a foreign pointer in a thin type of your own is a legitimate way to
  comply, at the cost of one more type.

## Related

- [0164](0164-r-30-take-smart-pointers-as-parameters-only-to-explicitly-express-lifetime-semantics.md)
  — the parameter rule whose substance this record's source states.
- [0168](0168-r-34-take-a-shared-ptr-widget-parameter-to-express-shared-ownership.md)
  — the shared-pointer parameter guideline these examples violate.
- [0159](0159-r-20-use-unique-ptr-or-shared-ptr-to-represent-ownership.md) — the
  standard types being taken as the pattern.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — a type
  whose shape misstates its semantics is a mistyped interface.
