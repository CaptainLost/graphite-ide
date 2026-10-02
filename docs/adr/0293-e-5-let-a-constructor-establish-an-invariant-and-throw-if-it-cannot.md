# 0293. E.5: Let a constructor establish an invariant, and throw if it cannot

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

Leaving an object without its invariant established is asking for trouble. Not
all member functions can be called on such an object, and nothing in the type
records which ones.

```cpp
class Vector {  // very simplified vector of doubles
    // if elem != nullptr then elem points to sz doubles
public:
    Vector() : elem{nullptr}, sz{0} {}
    Vector(int s) : elem{new double[s]}, sz{s} { /* initialize elements */ }
    ~Vector() { delete[] elem; }
    double& operator[](int s) { return elem[s]; }
    // ...
private:
    owner<double*> elem;
    int sz;
};
```

The class invariant — here stated as a comment — is established by the
constructors. `new` throws if it cannot allocate the required memory. The
operators, notably the subscript operator, rely on the invariant: `operator[]`
does not check `elem`, because the constructors guarantee it.

Throwing is what makes the constructor's refusal complete. An object whose
constructor threw never existed: no name is bound to it, no destructor runs for
it, and the members that were built before the throw are destroyed in reverse
order without anyone writing that code.

## Decision

Let a constructor establish the class invariant, and throw if it cannot.

- Put invariant-establishing work in the constructor, and write the invariant
  down on the class.
- Where the work cannot fail, use an ordinary constructor and let member
  functions assume the invariant unconditionally.
- Where the work can fail — allocating, opening a file, acquiring a handle —
  throw from the constructor rather than completing in a half-built state.
- Rely on the invariant in the member functions. `operator[]` does not check
  `elem`, because the constructors guarantee it.
- Do not add a `valid()` flag to a type merely so that its constructor can
  succeed unconditionally. That reintroduces exactly the two-state object the
  rule forbids, and every member function must then check it.
- A class with private state should have a constructor — public, protected or
  private. State that no constructor establishes has no invariant.

## Rationale

The economy of the rule is that the check happens once instead of at every use.
`Vector::operator[]` is a one-line function precisely because it may assume what
the constructor promised; had the constructor been allowed to leave `elem`
unset, every operator would open with a test, and the test would be a test for a
condition the type says cannot occur.

Throwing is not an incidental choice of mechanism here but the only one the
language offers a constructor. The alternatives are to leave a half-built object
and hope the caller checks, or to terminate. The first is what the rule exists to
forbid: an object that exists but must not be used is indistinguishable, at the
type level, from one that may be.

## Consequences

### Positive

- An object that exists is an object that may be used, so member functions do
  not open with validity checks.
- The failure is checked once, at the point where the resource is acquired,
  rather than at every subsequent call.
- Writing the invariant on the class makes the constructor's obligation
  reviewable.

### Negative

- A throwing constructor is a throwing constructor everywhere it appears. Such a
  type cannot be a member of a class whose own constructor must be `noexcept`,
  cannot be constructed in an environment where exceptions are unavailable, and
  cannot be built across a C ABI boundary without a wrapper that catches.
- The failure is invisible in the declaration. Nothing about `Vector v(n);` says
  that this line may not complete, so a reader has to know the type to know that
  the next statement is conditional on it.
- The rule pushes real work — allocation, acquisition, I/O — into constructors,
  which is precisely the work that can fail, so it concentrates the fallible
  operations at the points where the failure is least visible.
- Where a constructor cannot throw because the surrounding code forbids it, the
  rule offers nothing directly; the fallback is to simulate it, at the cost of
  the two-state object the rule was written against. See
  [0305](0305-e-25-if-you-cant-throw-exceptions-simulate-raii-for-resource-management.md).
- "Not all member functions can be called" is unenforceable on a type that does
  admit an empty state, such as the default-constructed `Vector` above; the
  invariant there is genuinely a disjunction and every operator must respect it.

### Neutral

- The default-constructed empty state is not a violation, provided the invariant
  admits it and is written that way.
- The rule does not say the invariant must be machine-checkable; a comment is
  the upstream example.

## Enforcement

- Flag classes with private state and no constructor, whether public, protected
  or private.

## Related

- [0292](0292-e-4-design-your-error-handling-strategy-around-invariants.md) — the
  invariant this record makes the constructor responsible for.
- [0290](0290-e-2-throw-an-exception-to-signal-that-a-function-cant-perform-its-assigned-task.md)
  — a constructor that cannot construct is upstream's canonical failure.
- [0305](0305-e-25-if-you-cant-throw-exceptions-simulate-raii-for-resource-management.md)
  — the `valid()` alternative where throwing is not available, and why it is
  second best.
