# 0176. ES.6: Declare names in for-statement initializers and conditions to limit scope

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Declaring the name where the construct that uses it begins limits the loop
variable's visibility to the loop, prevents the loop variable being used for
some other purpose afterwards, and minimises resource retention.

```cpp
void use()
{
    for (string s; cin >> s;)
        v.push_back(s);

    for (int i = 0; i < 20; ++i) {   // good: i is local to for-loop
        // ...
    }

    if (auto pc = dynamic_cast<Circle*>(ps)) {   // good: pc is local to if-statement
        // ... deal with Circle ...
    }
    else {
        // ... handle error ...
    }
}
```

Against that:

```cpp
int j;                            // BAD: j is visible outside the loop
for (j = 0; j < 100; ++j) {
    // ...
}
// j is still visible here and isn't needed
```

C++17 and C++20 extend the same facility to `if`, `switch` and range-`for`,
which lets a name that exists only to be inspected by the condition die with the
statement that inspects it:

```cpp
map<int, string> mymap;
if (auto result = mymap.insert(value); result.second) {
    // insert succeeded, and result is valid for this block
    use(result.first);  // ok
    // ...
} // result is destroyed here
```

There is an optimisation argument as well as a readability one. Scoping the loop
variable to the loop body helps code optimisers considerably: recognising that
the induction variable is accessible only within the loop body unblocks
hoisting, strength reduction, loop-invariant code motion and similar
transformations.

The upstream examples write single-statement bodies without braces; that is
incidental to where the names are declared.

## Decision

Declare the name in the statement that uses it.

- Declare the loop variable in the `for`-statement's initialiser.
- Use the `for (T x; condition;)` form when the loop's working variable is
  refreshed each iteration, as with `for (string s; cin >> s;)`.
- Use the C++17 `if (init; condition)` and `switch (init; condition)` forms for
  a value that only the condition and the body need.
- Declare in an `if`-condition where the declared value is itself the test, as
  with `if (auto p = dynamic_cast<Circle*>(ps))`.
- Never declare a variable before a loop merely so that it survives the loop,
  unless the value after the loop is genuinely wanted.

## Rationale

This is the cheapest possible application of small-scope discipline: it costs
one keyword's worth of typing and it is a purely local edit, unlike the function
extraction that a wide scope usually demands. The optimiser argument is the part
worth remembering, because it is not obvious — a variable declared outside the
loop may be observed afterwards, so the compiler must keep it consistent, and a
variable declared in the initialiser need not be.

## Consequences

### Positive

- The loop variable cannot be reused after the loop, deliberately or by
  accident.
- The optimiser gets a stronger guarantee about the induction variable and can
  transform the loop more aggressively.
- A `dynamic_cast` or an `insert` result is confined to the branch that
  established it, so the failure branch cannot see it.

### Negative

- When the post-loop value genuinely is wanted — the index at which a search
  stopped — the declaration must move out again, and code that alternates
  between the two forms is less uniform.
- An `if`-with-initialiser puts two things on one line and can be harder to read
  than a declaration followed by a test, particularly when the initialiser is
  long.
- Declaring in an `if`-condition tests the value's conversion to `bool`, which
  silently does the wrong thing for a type whose "empty" state is truthy.

### Neutral

- The `if`/`switch` initialiser forms require C++17 or later; under an earlier
  standard the declaration has to sit in an enclosing block instead.
- The rule concerns where a name is introduced, not how many names a loop needs.

## Enforcement

- Warn when a variable modified inside a `for`-statement is declared outside the
  loop and not used outside the loop.
- (Hard) Flag loop variables declared before the loop and used after the loop
  for an unrelated purpose.
- With a C++17 or later compiler, extend both checks to selection statements:
  flag selection variables declared before the body and not used after it, and
  (hard) flag those used after the body for an unrelated purpose.

## Related

- [0175](0175-es-5-keep-scopes-small.md) — the general rule this one specialises.
- [0189](0189-es-26-dont-use-a-variable-for-two-unrelated-purposes.md) — the
  reuse this rule makes impossible.
