# 0060. C.37: Make destructors noexcept

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A destructor must not fail. If a destructor tries to exit with an exception, it
is a bad design error and the program had better terminate.

A destructor, whether user-defined or compiler-generated, is implicitly declared
`noexcept` — independently of what code is in its body — if all the members of
its class have `noexcept` destructors. By explicitly marking a destructor
`noexcept`, an author guards against the destructor becoming implicitly
`noexcept(false)` through the addition or modification of a class member.

Not all destructors are `noexcept` by default; one throwing member poisons the
whole class hierarchy:

```cpp
struct X {
    Details x;  // happens to have a throwing destructor
    // ...
    ~X() { }    // implicitly noexcept(false); aka can throw
};
```

So, if in doubt, declare a destructor `noexcept`.

Why not then declare all destructors `noexcept`? Because that would in many
cases — especially simple ones — be distracting clutter.

## Decision

Declare a destructor `noexcept` wherever the guarantee is not already obvious.

- Write `noexcept` on the destructor of any class that owns a resource, calls
  into external code during destruction, or has a member whose destructor is not
  plainly non-throwing.
- Do not add `noexcept` to a destructor that is trivially non-throwing and whose
  members are all standard-library or built-in types; the annotation there is
  noise.
- When in doubt, add it. The cost of the annotation is a word; the cost of an
  unnoticed `noexcept(false)` is an exception escaping destruction, and
  `std::terminate` if it escapes while the stack is already unwinding.
- Do not use `noexcept(false)` on a destructor to permit a throwing release.
  Handle the failure inside, per
  [0059](0059-c-36-a-destructor-must-not-fail.md).

## Rationale

The value of writing `noexcept` explicitly is that it converts a distant change
into a local diagnostic. A destructor's exception specification is derived from
its members, so adding a member with a throwing destructor to a class silently
makes that class's destructor throwing too, and every class that contains it,
transitively — the poisoning the example describes. An explicit `noexcept` stops
the propagation at the class that declared it, and turns the offending member
into an error there rather than a surprise somewhere else.

The guideline's refusal to require it everywhere is a deliberate trade of
completeness for readability, and it is worth honouring: a rule that puts
`noexcept` on every destructor in the codebase makes the annotation carry no
information at all.

## Consequences

### Positive

- A throwing member cannot silently make an owning class's destructor throwing.
- The intent of the type — destruction always succeeds — is stated where a
  reader looks for it.
- The standard library requires that the types it handles destroy without
  throwing; an explicit `noexcept` states that conformance rather than leaving
  it to be inferred from every member in turn.

### Negative

- The rule is deliberately partial: "wherever it is not obvious" is a judgement,
  so two reasonable authors will annotate different sets of classes, and the
  absence of `noexcept` then means nothing definite.
- An explicit `noexcept` converts a would-be exception into an immediate
  `std::terminate`, which turns a recoverable-looking situation into a crash
  with less diagnostic context than an unhandled exception would have carried.
- Adding `noexcept` to a destructor that legitimately calls fallible cleanup
  forces that cleanup to be restructured, which is real work the annotation
  makes mandatory rather than optional.

### Neutral

- Most destructors are already implicitly `noexcept`; the annotation documents
  and protects a guarantee rather than creating one.
- The rule concerns the specification, not the body.

## Enforcement

- (Simple) A destructor should be declared `noexcept` if it could throw.

## Related

- [0059](0059-c-36-a-destructor-must-not-fail.md) — the requirement this record
  makes mechanical.
- [0055](0055-c-31-all-resources-acquired-by-a-class-must-be-released-by-the-classs-destructor.md)
  — the classes most in need of the annotation.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — an
  explicit specification is a compile-time statement about run-time behaviour.
- [0058](0058-c-35-a-base-class-destructor-should-be-either-public-and-virtual-or-protected-and-non-virtual.md)
  — the other decision made when declaring a destructor.
