# 0217. ES.71: Prefer a range-for-statement to a for-statement when there is a choice

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Readability, error prevention, efficiency. The rule's example also marks out
exactly where it stops applying:

```cpp
for (gsl::index i = 0; i < v.size(); ++i)   // bad
    cout << v[i] << '\n';

for (auto p = v.begin(); p != v.end(); ++p)   // bad
    cout << *p << '\n';

for (auto& x : v)    // OK
    cout << x << '\n';

for (gsl::index i = 1; i < v.size(); ++i) // touches two elements: can't be a range-for
    cout << v[i] + v[i - 1] << '\n';

for (gsl::index i = 0; i < v.size(); ++i) // possible side effect: can't be a range-for
    cout << f(v, &v[i]) << '\n';

for (gsl::index i = 0; i < v.size(); ++i) { // body messes with loop variable: can't be a range-for
    if (i % 2 != 0)
        cout << v[i] << '\n'; // output odd elements
}
```

A human or a good static analyser might determine that there really is no side
effect on `v` in `f(v, &v[i])`, in which case that loop can be rewritten too.
Messing with the loop variable in the body of a loop is typically best avoided
in any case.

Do not use expensive copies of the loop variable of a range-for:

```cpp
for (string s : vs) // ...
```

copies each element of `vs` into `s`. Better:

```cpp
for (string& s : vs) // ...
```

Better still, if the loop variable is neither modified nor copied:

```cpp
for (const string& s : vs) // ...
```

The upstream examples are written without braces around single-statement
bodies; that is orthogonal to the choice between the two loop forms.

## Decision

Iterate the sequence, not an index into it, whenever the loop simply visits each
element.

- Replace an index loop or an explicit iterator loop with a range-for where the
  body needs neither the index nor more than one element at a time.
- Choose the loop variable's form deliberately: `const auto&` to read, `auto&`
  to modify in place, and a value only when a copy is actually wanted.
- Do not use a range-for where the body needs the index, touches more than one
  element per iteration, iterates part of the range, or steps by more than one;
  those loops keep an explicit form.
- Do not modify the container being iterated inside a range-for; the range and
  its iterators are captured before the first iteration.
- Do not modify the loop variable to control the iteration in any loop form.
- Where the loop exists only to visit every element and do one thing, consider a
  standard algorithm instead, per
  [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md).

## Rationale

A range-for removes the two things an index loop can get wrong that nothing
checks: the bound and the increment. `i < v.size()` versus `i <= v.size()`, and
`++i` versus a forgotten increment, are the classic defects, and neither exists
in a form that has no index.

The efficiency claim is also real and often overlooked in the other direction:
`for (string s : vs)` copies every element, and it looks like the recommended
form. The rule's note about the loop variable's declaration is not a stylistic
footnote — it is where the performance goes.

## Consequences

### Positive

- The bound and the increment are supplied by the container, so they cannot
  disagree with it.
- The loop reads as "for each element", which is what the code means.
- The compiler sees the whole range, which gives it more to work with than an
  index whose bound it must re-derive.

### Negative

- There is no index. Loops that need the position must either be left in the
  older form or carry a separate counter, which reintroduces the increment that
  the range-for was removing. `std::views::enumerate`, which supplies the index
  without the increment, is a C++23 facility and so is out of reach in C++20.
- `for (auto x : v)` copies silently, and the shortest, most natural spelling is
  the one with the hidden cost.
- The range expression is evaluated once, so a range-for over a temporary
  subrange or over the result of a chained call can dangle; C++20's
  init-statement form mitigates this but does not remove it.
- The rule's own examples show that three common loop shapes — neighbouring
  elements, possible side effects on the container, and a body that manipulates
  the index — are outside its scope, so it governs less code than its phrasing
  suggests.

### Neutral

- C++20 permits an init-statement in a range-for, which covers some of the cases
  that previously needed an outer scope.
- Iterating a subrange with `std::span` or a view keeps the range-for form where
  only part of a container is wanted.

## Enforcement

Look at loops: if a traditional loop just looks at each element of a sequence,
and there are no side effects on what it does with the elements, rewrite the
loop as a range-for.

## Related

- [0208](0208-es-55-avoid-the-need-for-range-checking.md) — the range-for is the
  main way that rule is satisfied.
- [0220](0220-es-74-prefer-to-declare-a-loop-variable-in-the-initializer-part-of-a-for-statement.md)
  — what to do in the loops that still need an index.
- [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md)
  — often the loop should not be written at all.
