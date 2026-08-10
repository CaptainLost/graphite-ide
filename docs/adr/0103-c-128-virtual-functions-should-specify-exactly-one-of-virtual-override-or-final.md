# 0103. C.128: Virtual functions should specify exactly one of virtual, override, or final

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Readability, and detection of mistakes. Writing an explicit `virtual`,
`override` or `final` is self-documenting and enables the compiler to catch
mismatches of types or names between base and derived classes. Writing more than
one of the three is both redundant and a potential source of errors. The scheme
is simple and clear: `virtual` means exactly and only "this is a new virtual
function"; `override` means exactly and only "this is a non-final overrider";
`final` means exactly and only "this is a final overrider".

```cpp
// bad
struct B {
    void f1(int);
    virtual void f2(int) const;
    virtual void f3(int);
    // ...
};

struct D : B {
    void f1(int);        // bad (hope for a warning): D::f1() hides B::f1()
    void f2(int) const;  // bad (but conventional and valid): no explicit override
    void f3(double);     // bad (hope for a warning): D::f3() hides B::f3()
    // ...
};
```

```cpp
// good
struct Better : B {
    void f1(int) override;        // error (caught): Better::f1() hides B::f1()
    void f2(int) const override;
    void f3(double) override;     // error (caught): Better::f3() hides B::f3()
    // ...
};
```

Two particular classes of error are being eliminated. The first is implicit
virtual: the programmer intended a function to be implicitly virtual and it is,
but readers cannot tell; or intended it to be virtual and it is not, because of
a subtle parameter-list mismatch; or did not intend it to be virtual and it is,
because it happens to have the same signature as a virtual in the base. The
second is implicit override, with the same three variants — intended and is, but
invisibly; intended and is not, because of a mismatch; not intended and is,
because the signature happens to match. That last variant arises whether or not
the function is explicitly declared `virtual`, because the programmer might have
meant to create either a new virtual function or a new non-virtual one.

On a class defined as `final`, it does not matter whether `override` or `final`
is put on an individual virtual function. Use `final` on functions sparingly: it
does not necessarily lead to optimisation, and it precludes further overriding.

## Decision

Write exactly one of `virtual`, `override` or `final` on every virtual function
declaration.

- Write `virtual` in the base, on the declaration that introduces the function,
  and nowhere else.
- Write `override` on an overrider that is still open to further overriding, and
  do not also write `virtual`.
- Write `final` on an overrider that closes the chain, and do not also write
  `override` or `virtual`.
- Never leave an overrider unmarked, even though the language permits it and it
  was the pre-C++11 convention.
- Use `final` on individual functions sparingly, and only where further
  overriding is genuinely wrong.

## Rationale

The keyword is not documentation, it is a check. `void f3(double) override;`
does not merely record an intention — it fails to compile if the intention is
false, which is precisely what `void f3(double);` does not do. The bad example
compiles cleanly, and two of its three declarations hide a base function instead
of overriding it: `D::f1(int)` because `B::f1` was never virtual, and
`D::f3(double)` because the parameter type does not match `B::f3(int)`.

Forbidding the combination is the less obvious half. Writing `virtual void f()
override;` is not harmless verbosity: it means two different things at once, so
a reader can no longer take any one of the three keywords as a reliable
statement of what the declaration is.

## Consequences

### Positive

- Signature mismatches between base and derived become compile errors rather
  than silent name hiding.
- Whether a declaration introduces, continues or ends a virtual chain is
  readable without consulting the base.
- The three keywords retain distinct meanings, so each one is informative.

### Negative

- Adding `override` to an existing codebase turns latent bugs into build
  failures, which is the point but is also unschedulable work.
- Marking a function `final` closes a door and gives little back: the
  optimisation it is often justified by frequently does not materialise.
- The rule is a convention the compiler enforces only partially — a missing
  `override` is legal C++, so the check depends on a warning being enabled.

### Neutral

- On a `final` class the distinction between `override` and `final` on a member
  is immaterial.
- Destructors are conventionally exempt from `override` in practice, though the
  rule as stated does not carve them out.

## Enforcement

- Compare virtual function names in base and derived classes, and flag uses of
  the same name that do not override.
- Flag overriders that specify neither `override` nor `final`.
- Flag function declarations that use more than one of `virtual`, `override` and
  `final`.

## Related

- [0114](0114-c-139-use-final-on-classes-sparingly.md) — the same caution about
  `final`, applied to whole classes.
- [0113](0113-c-138-create-an-overload-set-for-a-derived-class-and-its-bases-with-using.md)
  — the other name-hiding trap in a derived class.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) —
  `override` converts a run-time mistake into a compile-time one.
- [0009](0009-p-3-express-intent.md) — the keyword states what the declaration
  is for.
