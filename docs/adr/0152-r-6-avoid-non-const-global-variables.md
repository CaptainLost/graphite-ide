# 0152. R.6: Avoid non-const global variables

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

R.6 carries no text of its own. It appears in the resource management section as
a cross-reference to the interface rule of the same name, which is transcribed
here as [0021](0021-i-2-avoid-non-const-global-variables.md): non-`const`
variables at namespace scope hide dependencies, make those dependencies subject
to unpredictable change, have unordered initialisation, and are a data race
waiting for a second thread.

Its presence in this section is the point worth recording, because resource
management adds arguments the interface rule does not make. The aim of this
section is to leak nothing and to hold nothing longer than needed. A non-`const`
global that owns a resource holds it for the whole run of the program by
construction, which fails the second half of that aim even when it never leaks.
A global that owns a resource is also destroyed at an unspecified point relative
to other translation units' globals, so a global whose destructor uses another
global's resource has an ordering problem at shutdown that no local reasoning
will reveal.

Reseating such a global is the sharper hazard: assigning to a global smart
pointer releases whatever it held, and anything that had borrowed a pointer or
reference from it is now dangling. That is the failure R.37 is entirely about.

## Decision

Avoid non-`const` variables at namespace scope, and in particular do not make
one the owner of a resource.

- Follow [0021](0021-i-2-avoid-non-const-global-variables.md) for the general
  rule; nothing here relaxes it.
- Do not give a resource — a file handle, a socket, a lock, a heap allocation —
  to a namespace-scope owner. Give it to an object that is created and destroyed
  at a point the program controls.
- Where a namespace-scope object is genuinely warranted, make it `const` or
  `constexpr` and initialise it with a constant expression, so that it has no
  destruction order to get wrong.
- Never take a pointer or reference out of a mutable global smart pointer and
  pass it down a call chain; take a local copy first.

## Rationale

The interface argument against a mutable global is about reasoning: the set of
things that can change it is the whole program. The resource argument is about
lifetime, and it is worse, because the consequence is not confusion but a
dangling pointer or a shutdown-order crash. A global that owns something is a
lifetime that no scope delimits, which is precisely what this section exists to
avoid.

## Consequences

### Positive

- Resource lifetimes stay bounded by scopes the program chooses, rather than by
  process lifetime.
- Shutdown-order hazards between owning globals do not arise.
- The aliasing hazard that R.37 addresses loses its most common source.

### Negative

- Genuinely process-wide resources — a log sink, a graphics device — have to be
  owned by something that is threaded through the program or reachable from a
  root object, which is more ceremony than the global was.
- The usual workaround, a function-local static handed out by reference, still
  has process lifetime and an unspecified destruction order relative to other
  statics; it satisfies the letter of the rule and keeps most of the problem.
- "Avoid" is a judgement, so this record cannot settle a specific case on its
  own.

### Neutral

- Global constants are explicitly fine, and hold no resource to release.
- This record adds the resource-lifetime argument;
  [0021](0021-i-2-avoid-non-const-global-variables.md) remains the governing
  statement of the rule and its exceptions.

## Enforcement

- (Simple) Report all non-`const` variables declared at namespace scope, and
  global pointers or references to non-`const` data. This is inherited from the
  interface rule.

## Related

- [0021](0021-i-2-avoid-non-const-global-variables.md) — the rule itself; R.6 is
  a pointer to it.
- [0171](0171-r-37-do-not-pass-a-pointer-or-reference-obtained-from-an-aliased-smart-pointer.md)
  — the dangling-reference failure a mutable global smart pointer causes.
- [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md) — the
  initialisation-order half of the same problem.
- [0151](0151-r-5-prefer-scoped-objects-dont-heap-allocate-unnecessarily.md) —
  where "scoped" includes globals, and this record is the limit on that.
