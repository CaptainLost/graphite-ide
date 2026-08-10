# 0117. C.146: Use dynamic_cast where class hierarchy navigation is unavoidable

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

`dynamic_cast` is checked at run time. Where a program must ask whether an
object behind a base handle is in fact of some more derived type, it is the only
cast that answers truthfully.

```cpp
struct B {   // an interface
    virtual void f();
    virtual void g();
    virtual ~B();
};

struct D : B {   // a wider interface
    void f() override;
    virtual void h();
};

void user(B* pb)
{
    if (D* pd = dynamic_cast<D*>(pb)) {
        // ... use D's interface ...
    }
    else {
        // ... make do with B's interface ...
    }
}
```

Use of the other casts can violate type safety and cause a variable that is
actually of type `X` to be accessed as if it were of an unrelated type `Z`:

```cpp
void user2(B* pb)   // bad
{
    D* pd = static_cast<D*>(pb);    // I know that pb really points to a D; trust me
    // ... use D's interface ...
}

void user3(B* pb)    // unsafe
{
    if (some_condition) {
        D* pd = static_cast<D*>(pb);   // I know that pb really points to a D; trust me
        // ... use D's interface ...
    }
    else {
        // ... make do with B's interface ...
    }
}

void f()
{
    B b;
    user(&b);   // OK
    user2(&b);  // bad error
    user3(&b);  // OK *if* the programmer got the some_condition check right
}
```

Like other casts, `dynamic_cast` is overused. Prefer a virtual function to a
cast, and prefer static polymorphism to hierarchy navigation wherever that is
possible and reasonably convenient, since it needs no run-time resolution at
all.

Some people use `dynamic_cast` where `typeid` would have been more appropriate.
`dynamic_cast` is a general "is kind of" operation for discovering the best
interface to an object; `typeid` is a "give me the exact type of this object"
operation. The latter is inherently simpler and ought to be faster, and is
easily hand-crafted if necessary — for instance on a system where RTTI is for
some reason prohibited — whereas `dynamic_cast` is far harder to implement
correctly in general. Home-brewed RTTI is its own trap:

```cpp
struct B {
    const char* name {"B"};
    // if pb1->id() == pb2->id() *pb1 is the same type as *pb2
    virtual const char* id() const { return name; }
    // ...
};

struct D : B {
    const char* name {"D"};
    const char* id() const override { return name; }
    // ...
};

void use()
{
    B* pb1 = new B;
    B* pb2 = new D;
    cout << pb1->id(); // "B"
    cout << pb2->id(); // "D"
    if (pb1->id() == "D") {         // looks innocent
        D* pd = static_cast<D*>(pb1);
        // ...
    }
    // ...
}
```

The result of `pb2->id() == "D"` is implementation defined. Such code might work
as expected for years and then fail on a new machine, compiler, or linker that
does not unify character literals.

There is an exception for genuinely slow implementations. All workarounds that
cannot be statically resolved involve explicit casting, typically `static_cast`,
and are error-prone; in effect one ends up crafting a special-purpose
`dynamic_cast`. So first confirm that `dynamic_cast` really is as slow as
assumed, and that the use is really performance critical. In very rare cases —
where the overhead has been measured and found material, where there is some
other static guarantee that the downcast will succeed, and where no virtual
inheritance is involved — a tactical `static_cast` may be used, with a prominent
comment recording that human attention is needed under maintenance because the
type system cannot verify correctness. Even then, such "I know what I'm doing"
situations remain a known source of bugs.

## Decision

Where the type of an object behind a base handle must be discovered at run time,
use `dynamic_cast`.

- Never use `static_cast`, a C-style cast, or a `reinterpret_cast` to convert a
  base pointer or reference down to a derived one.
- Before reaching for any cast, ask whether a virtual function on the base would
  answer the question instead; add one if it would.
- Where the set of types is closed and known, prefer static polymorphism or a
  variant to hierarchy navigation, so there is nothing to discover at run time.
- Use `typeid` when the question is "what exactly is this", and `dynamic_cast`
  when it is "does this provide that interface".
- Do not invent a home-grown RTTI scheme based on comparing name pointers or
  string literals; pointer comparison of string literals is not a type test.
- Depart to `static_cast` only with a measurement showing the cost is material,
  a static guarantee that the cast succeeds, no virtual inheritance in play, and
  a comment at the site saying all of that.

## Rationale

Both `static_cast` and `dynamic_cast` express the same belief about the object;
only one of them checks it. `user3` above is correct exactly as long as
`some_condition` and the object's real type never disagree, and nothing in the
language ties those two together — the relationship is maintained by hand, in a
codebase that will be edited by people who did not write it. Paying for the
check buys the guarantee that a wrong belief becomes a null pointer or an
exception rather than an unrelated object read through the wrong type.

## Consequences

### Positive

- A downcast that is wrong fails visibly instead of producing an object of a
  type the program never had.
- The cast keeps working when the hierarchy is extended or rearranged during
  maintenance, which is when hand-maintained type tags break.
- Because `dynamic_cast` is conspicuous and costed, it discourages the hierarchy
  navigation it enables, which is the deeper point of the rule.

### Negative

- `dynamic_cast` requires RTTI, and some builds, platforms and embedded
  toolchains disable it; where it is off, this rule has no compliant answer.
- It costs run time, and the cost is not constant across implementations or
  hierarchies, so it is a real consideration in a hot path.
- The measured exception is a loophole with soft edges: "I have another static
  guarantee" is exactly what the author of `user2` believed.
- Making the checked cast easy makes hierarchy navigation easy, and a design
  full of correct `dynamic_cast`s is usually a design missing a virtual
  function.

### Neutral

- The rule governs how a downcast is spelled, not whether the design should have
  needed one.
- Upstream regards current `dynamic_cast` implementations as unnecessarily slow
  and expects that a fast constant-time form is achievable; compatibility, not
  feasibility, is what stands in the way.

## Enforcement

- Flag all uses of `static_cast` for downcasts, including C-style casts that
  perform a `static_cast`.
- This rule is part of the type-safety profile.

## Related

- [0124](0124-c-153-prefer-virtual-function-to-casting.md) — the rule that
  should usually be applied before this one.
- [0118](0118-c-147-use-dynamic-cast-to-a-reference-type-when-failure-to-find-the-required-class-is-considered-an-error.md)
  and
  [0119](0119-c-148-use-dynamic-cast-to-a-pointer-type-when-failure-to-find-the-required-class-is-considered-a-valid-alternative.md)
  — which form of the cast to use, once it is unavoidable.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — an
  unchecked downcast is the canonical breach.
- [0039](0039-i-30-encapsulate-rule-violations.md) — where a tactical
  `static_cast` is unavoidable, it belongs behind a named boundary.
