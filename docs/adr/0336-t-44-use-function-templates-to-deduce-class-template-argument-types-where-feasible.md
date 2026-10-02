# 0336. T.44: Use function templates to deduce class template argument types (where feasible)

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Writing the template argument types explicitly can be tedious and unnecessarily
verbose.

```cpp
tuple<int, string, double> t1 = {1, "Hamlet", 3.14};   // explicit type
auto t2 = make_tuple(1, "Ophelia"s, 3.14);         // better; deduced type
```

Note the use of the `s` suffix to ensure the string is a `std::string` rather
than a C-style string.

Since a `make_T` function can be written trivially, the compiler could do the
same, so `make_T` functions might become redundant in the future. That future has
arrived: class template argument deduction, introduced in C++17, lets the
arguments be deduced directly from the constructor arguments, which upstream
notes makes this rule largely redundant.

```cpp
tuple t1 = {1, "Hamlet"s, 3.14}; // deduced: tuple<int, string, double>
```

There is an exception either way. Sometimes there is no good way of getting the
template arguments deduced, and sometimes they should be specified explicitly:

```cpp
vector<double> v = { 1, 2, 3, 7.9, 15.99 };
list<Record*> lst;
```

## Decision

Let the compiler work out class template arguments instead of writing them.

- Prefer class template argument deduction — `std::pair p{1, 'a'}`,
  `std::lock_guard g{m}` — where the deduced type is the intended one.
- Use an existing `make_` function where deduction does not apply or where the
  factory does more than construct, such as `std::make_unique` and
  `std::make_shared`.
- Do not write a new `make_T` function for a type whose constructor deduction
  already works; add a deduction guide instead if deduction gets it wrong.
- Spell the arguments out where the deduced type would not be the wanted one: an
  empty container, a container of a base or pointer type, or a
  `vector<double>` initialised from a list of integers.

## Rationale

The type of an object built from its arguments is derivable from those arguments,
and writing it again is duplication that can disagree with itself — the
`vector<double>` example only works because the explicit argument overrides what
deduction would have produced, which is precisely why the exception has to be
stated. The rule's own note concedes that the compiler should do this, and under
C++20 it does, so what survives is the preference for deduction over repetition
rather than the specific `make_T` technique.

## Consequences

### Positive

- Declarations shorten and stop repeating information the initialiser already
  carries.
- Changing an argument's type does not require editing the declaration too.
- Wrong explicit arguments that would have silently converted are avoided.

### Negative

- A deduced type is invisible at the declaration, so the reader must infer it
  from the initialiser — the same objection
  [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md)
  answers for `auto`, and it applies here too.
- Deduction produces the type the arguments imply, not the one intended:
  `std::vector v{1, 2, 3}` is a `vector<int>` even when doubles were wanted, and
  a string literal deduces as `const char*` without the `s` suffix.
- Deduction guides are a separate mechanism that must be written and maintained
  for types whose constructors do not deduce correctly, and getting them wrong is
  subtle.
- `make_T` functions remain in the standard library for other reasons, so the
  codebase will contain both styles indefinitely.

### Neutral

- The rule as originally written recommends `make_T` functions; from C++17
  onwards the same intent is served by constructor deduction, and the record is
  transcribed with that noted.
- Nothing here is about deducing function template arguments, which has always
  worked.

## Enforcement

- Flag uses where an explicitly specified type exactly matches the types of the
  arguments used.

## Related

- [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md) —
  the same argument for variable types.
- [0121](0121-c-150-use-make-unique-to-construct-objects-owned-by-unique-ptrs.md)
  and
  [0122](0122-c-151-use-make-shared-to-construct-objects-owned-by-shared-ptrs.md)
  — factory functions that are still preferred, for reasons beyond deduction.
- [0323](0323-t-12-prefer-concept-names-over-auto-for-local-variables.md) — how
  to keep a deduced declaration informative.
- [0186](0186-es-23-prefer-the-initializer-syntax.md) — the initialisation the
  deduction reads.
