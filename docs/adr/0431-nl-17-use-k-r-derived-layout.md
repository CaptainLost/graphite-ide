# 0431. NL.17: Use K&R-derived layout

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

This is the original C and C++ layout. It preserves vertical space well, and it
distinguishes different language constructs — functions and classes in
particular — from one another. In a C++ setting the style is often called
"Stroustrup". Upstream is explicit that this is a recommendation for when there
are no constraints or better ideas, and that it was added after many requests
for guidance rather than out of a wish to legislate layout.

```cpp
struct Cable {
    int x;
    // ...
};
double foo(int x)
{
    if (0 < x) {
        // ...
    }
    switch (x) {
    case 0:
        // ...
        break;
    case amazing:
        // ...
        break;
    default:
        // ...
        break;
    }
    if (0 < x)
        ++x;
    if (x < 0)
        something();
    else
        something_else();
    return some_value;
}
```

Note the space between `if` and `(` — the keyword is not a function name and is
not written as though it were. The remaining notes attached to the rule are
these: use separate lines for each statement, for the branches of an `if`, and
for the body of a `for`; the `{` of a class or a struct stays on the line that
opens it, whereas the `{` of a function goes on a line of its own; capitalise
the names of your own types so they are distinguishable from standard-library
types; and do not capitalise function names.

## Decision

Lay code out in the K&R-derived style where no other style is already in force.

- Put the opening brace of a class or struct on the same line as the
  introducer, and the opening brace of a function definition on the next line
  by itself.
- Indent the contents of a block one level; do not indent `case` labels beyond
  the `switch` that introduces them.
- Write a space between a control-flow keyword and its parenthesis, and none
  between a function name and its parenthesis.
- Give each statement, each branch of an `if`, and the body of a loop its own
  line.
- Capitalise the names of your own types; leave function names in lower case.
- Where surrounding code already uses another layout, follow that instead; this
  rule is a default for code with no constraint on it.

## Rationale

The claim being made is narrow and worth separating from taste. Placing a
function's brace on its own line while a class's brace stays inline makes the
two constructs distinguishable at a glance when skimming a file, which is the
one thing the style buys that a purely aesthetic choice would not. Everything
else in the rule is convention, and the guidelines say as much: the aim is that
a file be laid out one way, not that it be laid out this way.

## Consequences

### Positive

- Function definitions are visually separable from type definitions when
  scrolling, because their braces sit differently.
- The style is compact vertically, so more of a function fits on a screen than
  under layouts that give every brace its own line.
- It is the layout the language's own literature is written in, so most
  programmers can read it without adjustment.

### Negative

- The brace rule is not uniform — inline for a class, on its own line for a
  function — so it is a special case to remember rather than a principle to
  apply, and a reader cannot derive it.
- The rule bundles unrelated decisions together. Brace placement, the space
  after `if`, and the capitalisation of type names have nothing to do with each
  other, and adopting the layout means adopting all of them.
- Upstream's own example writes single-statement `if` and `else` bodies without
  braces, which is directly at odds with any convention requiring braces on
  every control-flow body. The two rules must be reconciled by hand.
- Nothing about the style is checkable by argument; upstream's enforcement
  advice is to hand the problem to a reformatting tool, which means the layout
  is only as consistent as everyone's tooling.

### Neutral

- The capitalisation notes overlap with the naming rules and could as easily
  have been stated there.
- Layout has no effect on the meaning of the program; nothing here changes what
  the compiler sees.

## Enforcement

If you want enforcement, use an editor or IDE to reformat.

## Related

- [0422](0422-nl-4-maintain-a-consistent-indentation-style.md) — the underlying
  requirement that this rule supplies one concrete answer to.
- [0429](0429-nl-15-use-spaces-sparingly.md) — the companion rule about
  horizontal space, including the space after `if`.
- [0432](0432-nl-18-use-c-style-declarator-layout.md) — the layout of
  declarations, offered in the same spirit and with the same hedging.
- [0434](0434-nl-20-dont-place-two-statements-on-the-same-line.md) — the
  one-statement-per-line note, stated as a rule of its own.
