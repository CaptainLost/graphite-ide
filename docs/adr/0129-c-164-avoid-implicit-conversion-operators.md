# 0129. C.164: Avoid implicit conversion operators

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Implicit conversions can be essential, as `double` to `int` is, but they often
cause surprises, as `String` to C-style string does. Prefer explicitly named
conversions until a serious need is demonstrated. By "serious need" is meant a
reason that is fundamental in the application domain — such as an integer to
complex number conversion — and frequently needed. Do not introduce implicit
conversions, whether through conversion operators or non-explicit constructors,
just to gain a minor convenience.

```cpp
struct S1 {
    string s;
    // ...
    operator char*() { return s.data(); }  // BAD, likely to cause surprises
};

struct S2 {
    string s;
    // ...
    explicit operator char*() { return s.data(); }
};

void f(S1 s1, S2 s2)
{
    char* x1 = s1;     // OK, but can cause surprises in many contexts
    char* x2 = s2;     // error (and that's usually a good thing)
    char* x3 = static_cast<char*>(s2); // we can be explicit (on your head be it)
}
```

The surprising and potentially damaging implicit conversion can occur in
arbitrarily hard-to-spot contexts:

```cpp
S1 ff();

char* g()
{
    return ff();
}
```

The string returned by `ff()` is destroyed before the returned pointer into it
can be used. Nothing in `g` mentions a conversion, a pointer into a buffer, or a
lifetime; the return statement looks like a return statement.

## Decision

Do not give a type an implicit conversion operator.

- Declare every conversion operator `explicit`, so that a conversion appears at
  the point where it happens.
- Prefer a named member function — `to_string()`, `c_str()`, `value()` — to a
  conversion operator, particularly where the result borrows from the object.
- Declare single-argument constructors `explicit` too; they are the other half
  of the same hazard.
- Introduce an implicit conversion only where it is fundamental to the domain
  and frequently needed, not to spare a call site a few characters.
- Never provide an implicit conversion to a pointer or reference into the
  object's own storage, since the result outlives nothing and the call site
  cannot see the lifetime.

## Rationale

An implicit conversion is the only operation in C++ that can be inserted into
code that does not mention it, at a point the author did not choose. That is
what makes the `g()` example unanswerable: the bug is a dangling pointer created
by a conversion which appears nowhere in the source, in a function whose entire
body is one return statement. `explicit` does not make the conversion safer — it
makes it visible, which is the only property that lets a reader or a reviewer
have an opinion about it.

## Consequences

### Positive

- Every conversion is written where it occurs, so it can be seen, reviewed and
  searched for.
- Overload resolution has fewer viable candidates, so calls resolve to what they
  appear to resolve to.
- The lifetime hazard of returning a borrowed pointer becomes a visible cast
  rather than an invisible one.

### Negative

- Call sites get longer and noisier, and the noise falls on code that was
  correct, not just on code that was risky.
- `explicit` on a conversion to `bool` interacts specially with contextual
  conversion, so the rule's uniform advice has a case where the reasoning is
  subtler than it appears.
- Types designed to interoperate with a C API or a legacy interface become
  tedious to use, and the pressure to relax the rule falls exactly there.
- The permitted exception rests on "fundamental in the application domain and
  frequently needed", which no tool can evaluate and no two reviewers will draw
  identically.

### Neutral

- The rule bans neither conversion nor convenience; it moves both into the
  caller's sight.
- Standard-library types are full of implicit conversions this rule would not
  have allowed, so interoperating with them still means living with theirs.

## Enforcement

- Flag all non-explicit conversion operators.

## Related

- [0126](0126-c-161-use-non-member-functions-for-symmetric-operators.md) — the
  member-operator asymmetry that implicit conversions expose.
- [0125](0125-c-160-define-operators-primarily-to-mimic-conventional-usage.md) —
  a conversion operator is an operator, and surprise is the cost in both rules.
- [0020](0020-i-1-make-interfaces-explicit.md) — a conversion the call site does
  not mention is an implicit part of the interface.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — implicit
  conversions dissolve exactly the type distinctions strong typing establishes.
