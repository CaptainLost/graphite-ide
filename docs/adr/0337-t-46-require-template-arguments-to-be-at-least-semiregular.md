# 0337. T.46: Require template arguments to be at least semiregular

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Readability, prevention of surprises and errors, and the observation that most
uses support it anyway.

```cpp
class X {
public:
    explicit X(int);
    X(const X&);            // copy
    X operator=(const X&);
    X(X&&) noexcept;        // move
    X& operator=(X&&) noexcept;
    ~X();
    // ... no more constructors ...
};

X x {1};              // fine
X y = x;              // fine
std::vector<X> v(10); // error: no default constructor
```

`semiregular` requires default constructibility, along with copy and move
construction and assignment, destruction, and swappability. A type that has
everything else but no default constructor is rejected by a great deal of
generic code, of which the sized `vector` constructor above is only the most
visible example.

## Decision

Require template arguments to model `std::semiregular`, and design types so they
can.

- Constrain a template's type parameters with `std::semiregular`, or a concept
  that subsumes it, unless there is a reason to accept less.
- Give a value-like type a default constructor, even when it exists only to
  produce an empty or zero state; see
  [0064](0064-c-43-ensure-that-a-copyable-class-has-a-default-constructor.md).
- Keep the default constructor simple and non-throwing, per
  [0065](0065-c-44-prefer-default-constructors-to-be-simple-and-non-throwing.md),
  so that being semiregular costs nothing at runtime.
- Where a type genuinely cannot have a meaningful default state, accept that it
  will not be usable as an argument to templates that need one, and do not
  invent a default that violates the type's invariant.

## Rationale

Semiregularity is not an arbitrary bar: it is the set of operations that generic
code performs on values without thinking — creating one to assign to, copying one
into a container, swapping two during a permutation — so a type that lacks any of
them fails at an unpredictable distance from the code that chose it. Upstream's
practical argument carries most of the weight: most types support this anyway, so
the requirement excludes very little while removing a whole class of surprises.

## Consequences

### Positive

- The type will work with containers and algorithms that create, copy or swap
  values, without each one needing its own workaround.
- Failures move to the declaration, naming a concept, instead of appearing at a
  `vector` constructor deep inside a call.
- Stating the requirement documents what a caller's type must provide.

### Negative

- It conflicts directly with
  [0061](0061-c-40-define-a-constructor-if-a-class-has-an-invariant.md) for types
  whose invariant admits no empty state; supplying a default constructor then
  means inventing a valid-but-meaningless value and carrying it forever.
- It requires copyability, which excludes move-only types — `std::unique_ptr`
  among them — so the constraint is wrong for any template that only needs to
  move its arguments.
- It is more than many templates need, which is
  [0333](0333-t-41-require-only-essential-properties-in-a-templates-concepts.md)'s
  objection.
- Enforcing it wholesale would flag every use of a deliberately non-default-
  constructible type, so the check needs judgement to be useful.

### Neutral

- `semiregular` is the floor, not the target: a value type should generally be
  `regular`, adding equality, per
  [0049](0049-c-11-make-concrete-types-regular.md).
- The rule says nothing about non-type template parameters.

## Enforcement

- Flag types used as template arguments that are not at least semiregular.

## Related

- [0049](0049-c-11-make-concrete-types-regular.md) — regularity, of which
  semiregularity is the weaker half.
- [0064](0064-c-43-ensure-that-a-copyable-class-has-a-default-constructor.md) —
  the default constructor this rule depends on.
- [0061](0061-c-40-define-a-constructor-if-a-class-has-an-invariant.md) — the
  rule that pushes back where an invariant admits no default state.
- [0318](0318-t-3-use-templates-to-express-containers-and-ranges.md) — the
  element requirement a container states.
