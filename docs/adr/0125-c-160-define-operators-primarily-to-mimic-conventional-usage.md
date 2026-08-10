# 0125. C.160: Define operators primarily to mimic conventional usage

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The reason is to minimise surprises. An operator carries expectations that a
named function does not: readers, and generic code, assume the conventional
meaning without checking.

```cpp
class X {
public:
    // ...
    X& operator=(const X&); // member function defining assignment
    friend bool operator==(const X&, const X&); // == needs access to representation
                                                // after a = b we have a == b
    // ...
};
```

Here the conventional semantics is maintained, including the expectation that
copies compare equal. The failure case needs no elaboration:

```cpp
X operator+(X a, X b) { return a.v - b.v; }   // bad: makes + subtract
```

Non-member operators should be either friends or defined in the same namespace
as their operands, and binary operators should treat their operands
equivalently.

## Decision

Define an operator only where the operation it denotes matches what that
operator conventionally means.

- Preserve the relationships readers rely on: after `a = b`, `a == b`; `a != b`
  is `!(a == b)`; `a += b` leaves `a` as `a + b` would have produced; `++a`
  advances by one.
- Make `==` an equivalence relation and the ordering operators a consistent
  order, so that generic algorithms and containers behave.
- Give `+`, `-`, `*` and `/` value semantics: they produce a result and do not
  modify their operands, while the compound assignments modify the left operand
  and return a reference to it.
- Make a binary operator treat its operands equivalently, which in practice
  means defining it as a non-member.
- Give a non-member operator access by declaring it a friend, or define it in
  the namespace of its operands.
- If the operation does not fit any of these conventions, give it a name
  instead.

## Rationale

An operator is a promise about behaviour made in advance of the reader looking
at its definition, and most readers never do look. That is the whole value of
using one — `a + b` is understood at a glance — and it is why an unconventional
operator is worse than a badly named function rather than merely as bad: the
misunderstanding is silent and confident. Generic code makes the same assumption
without a reader involved at all, so a `+` that subtracts corrupts every
algorithm it is instantiated into.

## Consequences

### Positive

- Code using the type reads as arithmetic, comparison or assignment really does,
  with no per-type learning.
- Types that keep the conventions work in standard algorithms and containers
  without adaptation.
- The invariants named above are the ones generic code depends on, so honouring
  them is what makes a type substitutable.

### Negative

- Convention is not checkable: the compiler accepts `operator+` that subtracts,
  so this rule rests entirely on review.
- Domains do exist where the conventional reading is wrong — `<<` for streams
  and `+` for string concatenation are both departures the standard library
  itself made — so the rule has soft edges and cannot be applied literally.
- Maintaining the full set of consistent relationships is real work: a type that
  defines `==` and `+` acquires an obligation to define `!=` and `+=`
  compatibly, and to keep them so.
- Following the rule can mean not defining an operator that would have been
  convenient, leaving a more verbose named function in its place.

### Neutral

- The rule says which operators may mean what; it does not require that a type
  define any operators at all.
- C++20's rewritten comparisons and `operator<=>` remove much of the mechanical
  work of keeping the comparison family consistent with itself.

## Enforcement

- Possibly impossible.

## Related

- [0132](0132-c-167-use-an-operator-for-an-operation-with-its-conventional-meaning.md)
  — the converse: when an operation has a conventional operator, use it.
- [0126](0126-c-161-use-non-member-functions-for-symmetric-operators.md) — how a
  binary operator comes to treat its operands equivalently.
- [0133](0133-c-168-define-overloaded-operators-in-the-namespace-of-their-operands.md)
  — where the non-member operator goes.
- [0009](0009-p-3-express-intent.md) — an operator is the strongest claim about
  intent the language offers, and therefore the easiest to make falsely.
