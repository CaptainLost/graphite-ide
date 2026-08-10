# 0412. NR.1: Don't insist that all declarations should be at the top of a function

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NR (Non-Rules and myths), transcribed 2026-08-10

## Context

The NR section collects rules that are popular somewhere but that the
guidelines deliberately do not recommend. They made sense in their time and
place, but in the style of programming these guidelines support they do harm.
This record therefore adopts the rejection, not the practice being rejected.

The practice here is the convention that every variable in a function must be
declared in a block at the top of it. It is a legacy of older languages that did
not allow a declaration to appear after a statement, and hence did not allow a
variable to be initialised where its initial value first became available. The
result is longer programs and more errors from uninitialised and wrongly
initialised variables:

```cpp
int use(int x)
{
    int i;
    char c;
    double d;
    // ... some stuff ...
    if (x < i) {
        // ...
        i = f(x, d);
    }
    if (i < x) {
        // ...
        i = g(x, c);
    }
    return i;
}
```

The larger the distance between the uninitialised variable and its use, the
larger the chance of a bug. Fortunately, compilers catch many "used before set"
errors. Unfortunately, they cannot catch all of them, and the bugs are not
always as easy to spot as in this small example.

## Decision

Do not impose an all-declarations-at-the-top convention, and do not follow one.

- Declare each variable at the point where it is first needed, and give it its
  initial value in the same statement.
- Where a value is not available until part-way through a function, that is
  where the variable is declared; do not reserve a name at the top and assign to
  it later.
- Where a computation is needed to produce the initial value, use an immediately
  invoked lambda or a helper function rather than a default-constructed variable
  followed by a fix-up.
- Do not reject a change on the grounds that a declaration appears in the middle
  of a function.

## Rationale

The convention does not merely fail to help — it manufactures the exact defect
it is supposed to prevent, by forcing a window between declaration and
initialisation in which the variable holds nothing meaningful and can be read.
C++ has allowed a declaration to be a statement since the beginning, so the
constraint has no basis in the language.

The compiler's "used before set" diagnostics are the practical argument for
declaring late. They are good but incomplete; the way to be safe is to leave no
gap for them to have to detect.

## Consequences

### Positive

- Declaration and initialisation coincide, so there is no interval during which
  a variable is live but meaningless.
- Each variable's scope shrinks to the region that actually uses it, which makes
  the function easier to read and to split.
- The type is chosen when the value is known, so it can be deduced rather than
  guessed in advance.

### Negative

- A reader who wants an inventory of a function's local state must now scan the
  whole body instead of one block at the top, which was the convention's real
  attraction.
- The rule pushes complexity into initialisers — lambdas and helper functions
  exist to avoid a later assignment — and those are not always simpler than the
  assignment they replace.
- It does not apply uniformly: a variable genuinely assigned by two branches, or
  one whose address is taken before it is filled, still has to be declared
  before it has a value.
- A codebase that has followed the old convention for years cannot be converted
  cheaply, so both styles coexist for a long time.

### Neutral

- Nothing here forbids a declaration at the top of a function when that is where
  the variable is first needed. The objection is to the insistence, not the
  position.
- This concerns local variables. Class member declarations are governed
  elsewhere.

## Related

- [0183](0183-es-20-always-initialize-an-object.md) — the positive rule this
  non-rule obstructs, and the alternative upstream offers.
- [0184](0184-es-21-dont-introduce-a-variable-or-constant-before-you-need-to-use-it.md)
  — the other alternative offered upstream.
- [0185](0185-es-22-dont-declare-a-variable-until-you-have-a-value-to-initialize-it-with.md)
  — the same point stated as a requirement.
- [0175](0175-es-5-keep-scopes-small.md) — declaring late is how scopes stay
  small.
