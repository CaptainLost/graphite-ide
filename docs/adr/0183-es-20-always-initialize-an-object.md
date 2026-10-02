# 0183. ES.20: Always initialize an object

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Initialising every object avoids used-before-set errors and their associated
undefined behaviour, avoids the comprehension problems that complex
initialisation brings, and simplifies refactoring.

```cpp
void use(int arg)
{
    int i;   // bad: uninitialized variable
    // ...
    i = 7;   // initialize i
}
```

`i = 7` does not initialise `i`; it assigns to it. And `i` can be read in the
elided part before that assignment happens. Better:

```cpp
void use(int arg)   // OK
{
    int i = 7;   // OK: initialized
    string s;    // OK: default initialized
    // ...
}
```

The "always initialise" rule is deliberately stronger than the language's "an
object must be set before it is used" rule. The relaxed version catches the
technical bugs, but it leads to less readable code, encourages declaring names
in larger scopes than necessary, encourages complex code and the logic bugs that
come with it, and hampers refactoring. "Always initialise" is a style rule aimed
at maintainability as much as it is a rule protecting against used-before-set.

The example usually offered against the strong rule:

```cpp
widget i;    // "widget" a type that's expensive to initialize, possibly a large POD
widget j;
if (cond) {  // bad: i and j are initialized "late"
    i = f1();
    j = f2();
}
else {
    i = f3();
    j = f4();
}
```

This cannot trivially be rewritten with initialisers. Note that for a type with
a default constructor, postponing initialisation simply produces a default
initialisation followed by an assignment — so nothing is saved. The usual
justification is efficiency, but a compiler able to detect a used-before-set
error is also able to eliminate a redundant double initialisation.

If there is a logical connection between `i` and `j`, it should be expressed in
code:

```cpp
pair<widget, widget> make_related_widgets(bool x)
{
    return (x) ? {f1(), f2()} : {f3(), f4()};
}

auto [i, j] = make_related_widgets(cond);    // C++17
```

and if that function would otherwise be redundant, a lambda removes it:

```cpp
auto [i, j] = [x] { return (x) ? pair{f1(), f2()} : pair{f3(), f4()} }();    // C++17
```

Using a value that represents "uninitialised" is a symptom, not a solution:

```cpp
widget i = uninit;  // bad
widget j = uninit;
// ...
use(i);         // possibly used before set
// ...
if (cond) {     // bad: i and j are initialized "late"
    i = f1();
    j = f2();
}
else {
    i = f3();
    j = f4();
}
```

Now the compiler cannot even detect the used-before-set, and the state space of
`widget` has grown a value for which it is unclear which operations are valid.

Complex initialisation has been popular with clever programmers for decades. It
has also been a major source of errors and complexity, many of them introduced
during maintenance years after the original implementation.

The rule covers member variables:

```cpp
class X {
public:
    X(int i, int ci) : m2{i}, cm2{ci} {}
    // ...
private:
    int m1 = 7;
    int m2;
    int m3;
    const int cm1 = 7;
    const int cm2;
    const int cm3;
};
```

The compiler will flag the uninitialised `cm3` because it is `const`, but it
will not catch the missing initialisation of `m3`. A rare spurious member
initialisation is worth the absence of errors from missing initialisation, and
an optimiser can often eliminate an initialisation that occurs immediately
before an assignment.

The exception concerns objects about to be filled from input, where initialising
would be a double initialisation. Beware that this can leave uninitialised data
beyond what the input wrote, which has been a fertile source of errors and
security breaches:

```cpp
constexpr int max = 8 * 1024;
int buf[max];         // OK, but suspicious: uninitialized
f.read(buf, max);
```

```cpp
constexpr int max = 8 * 1024;
int buf[max] = {};   // zero all elements; better in some situations
f.read(buf, max);
```

Because of the restrictive initialisation rules for arrays and `std::array`,
they offer the most compelling examples of this exception. Where feasible, use a
library facility known not to overflow instead:

```cpp
string s;   // s is default initialized to ""
cin >> s;   // s expands to hold the string
```

Simple variables that are targets for input are not exceptions:

```cpp
int i;   // bad
// ...
cin >> i;
```

```cpp
int i2 = 0;   // better, assuming that zero is an acceptable value for i2
// ...
cin >> i2;
```

In the not uncommon case where the input target and the input operation become
separated — as they should not — the possibility of used-before-set opens up. A
good optimiser knows about input operations and eliminates the redundant store.

Finally, a lambda can sometimes serve as an initialiser and remove the
uninitialised variable entirely:

```cpp
error_code ec;
Value v = [&] {
    auto p = get_value();   // get_value() returns a pair<error_code, Value>
    ec = p.first;
    return p.second;
}();
```

## Decision

Give every object a value at the point it is declared.

- Declare a local with an initialiser, or not at all until one is available.
- Initialise every non-static data member, either with a default member
  initialiser or in every constructor's member initialiser list. `const` members
  are checked by the compiler; ordinary members are not, so this is on the
  programmer.
- Do not invent an "uninitialised" sentinel value for a type. It enlarges the
  state space and disables the compiler's own used-before-set analysis.
- When two values are computed together by the same condition, return them
  together — from a function or an immediately invoked lambda — and bind them
  with a structured binding, rather than declaring both empty and assigning in
  each branch.
- Where a buffer is about to be filled from input, either zero-initialise it or
  use a library type that sizes itself. A raw uninitialised buffer is permitted
  only when the cost is measured and the write covers it.
- Efficiency is not an argument for leaving an object uninitialised without a
  measurement, since the redundant store is exactly the kind the optimiser
  removes.

## Rationale

The sharpest point in the source is that the sentinel-value workaround is
strictly worse than the problem it addresses. An uninitialised variable is at
least *detectable* — by the compiler, by a sanitiser, by a static analyser —
whereas `widget i = uninit;` is a well-defined program that no tool can complain
about and that has a new invalid state for every operation on `widget` to
consider. That asymmetry is what makes "always initialise" the right default
rather than "set before use".

## Consequences

### Positive

- Used-before-set, and the undefined behaviour it produces, stops being
  expressible for the objects covered.
- The declaration states the object's initial value, so a reader does not have
  to trace control flow to learn it.
- Refactoring is safer: moving or reordering statements cannot open a window in
  which a variable is live but unset.

### Negative

- For expensive types with a default constructor, insisting on an initialiser
  can mean a default construction that is genuinely thrown away, and the
  optimiser only *usually* removes it.
- The restructuring the rule asks for — a factory function or an immediately
  invoked lambda to compute two related values — is a real cost in ceremony for
  cases that a pair of assignments expressed adequately.
- The rule cannot be enforced for members: the compiler catches missing `const`
  member initialisation and silently accepts everything else.
- The input-buffer exception is exactly the case with the worst consequences
  when it is wrong, and the rule can only advise suspicion about it.

### Neutral

- Default initialisation of a class type with a default constructor satisfies
  the rule; `string s;` is initialised.
- Enforcement deliberately does not flag user-defined types with default
  constructors, so the rule bites mainly on built-ins and aggregates.

## Enforcement

- Flag every uninitialised variable. Do not flag variables of user-defined types
  with default constructors.
- Check that an uninitialised buffer is written into immediately after its
  declaration. Passing an uninitialised variable as a reference to a non-`const`
  parameter can be assumed to be a write into the variable.

## Related

- [0185](0185-es-22-dont-declare-a-variable-until-you-have-a-value-to-initialize-it-with.md)
  — the same rule stated as a constraint on when to declare.
- [0191](0191-es-28-use-lambdas-for-complex-initialization-especially-of-const-variables.md)
  — the mechanism for the cases this rule makes awkward.
- [0186](0186-es-23-prefer-the-initializer-syntax.md) — how the initialiser
  should be written.
- [0188](0188-es-25-declare-an-object-const-or-constexpr-unless-you-want-to-modify-its-value-later-on.md)
  — an object initialised once is usually an object that can be `const`.
