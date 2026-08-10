# 0197. ES.40: Avoid complicated expressions

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Complicated expressions are error-prone. The upstream rule collects the shapes
that go wrong:

```cpp
// bad: assignment hidden in subexpression
while ((c = getc()) != -1)

// bad: two non-local variables assigned in sub-expressions
while ((cin >> c1, cin >> c2), c1 == c2)

// better, but possibly still too complicated
for (char c1, c2; cin >> c1 >> c2 && c1 == c2;)

// OK: if i and j are not aliased
int x = ++i + ++j;

// OK: if i != j and i != k
v[i] = v[j] + v[k];

// bad: multiple assignments "hidden" in subexpressions
x = a + (b = f()) + (c = g()) * 7;

// bad: relies on commonly misunderstood precedence rules
x = a & b + c * d && e ^ f == 7;

// bad: undefined behavior
x = x++ + x++ + ++x;
```

Some of these are unconditionally bad, because they rely on undefined
behaviour. Others are simply so complicated or so unusual that even good
programmers could misunderstand them, or overlook a problem when in a hurry.
Note that two of the acceptable lines are acceptable only under a stated
assumption about aliasing, which the expression itself does not express.

C++17 tightened the rules for order of evaluation — left to right, except right
to left in assignments, with the order of evaluation of function arguments left
unspecified — but that does not change the fact that complicated expressions are
potentially confusing.

The rule cuts both ways. A programmer should know and use the basic rules for
expressions, and decomposing an ordinary expression into one operation per
statement is its own kind of damage:

```cpp
x = k * y + z;             // OK
auto t1 = k * y;           // bad: unnecessarily verbose
x = t1 + z;

if (0 <= x && x < max)   // OK
auto t1 = 0 <= x;        // bad: unnecessarily verbose
auto t2 = x < max;
if (t1 && t2)            // ...
```

## Decision

Write expressions that a reader can evaluate in their head without consulting
the precedence table or the sequencing rules.

- Do not hide an assignment inside a larger expression. Give the assignment its
  own statement, or use an `if`/`while` init-statement or condition declaration
  where the language provides one.
- Do not put side effects on more than one non-local variable in a single
  expression, and especially not in separate subexpressions of it.
- Do not write an expression whose meaning depends on a precedence rule you
  would have to look up; see
  [0198](0198-es-41-if-in-doubt-about-operator-precedence-parenthesize.md).
- Never read a variable twice in an expression that also writes it.
- Do not use the comma operator to sequence side effects inside a condition.
- Do not go the other way and split ordinary arithmetic or comparison into a
  chain of single-operation temporaries; that is also on the bad list.

## Rationale

The sharpest argument in the source is not about any single expression but about
the assumptions they carry. `v[i] = v[j] + v[k]` is fine only if `i`, `j` and
`k` differ, and `int x = ++i + ++j` is fine only if `i` and `j` are not aliased.
Neither expression states its own precondition, so the correctness of the line
lives outside the line.

A complicated expression also fails silently under change: it stays compiling
when someone makes two of its operands alias, or when the type of one operand
changes an operator's meaning. Simple expressions have fewer such hinges.

## Consequences

### Positive

- Fewer expressions whose behaviour depends on evaluation order or on aliasing
  assumptions that are not written down.
- Debugger and profiler output maps onto statements a reader recognises, because
  one statement is one step.
- Diffs get smaller: changing one operation touches one line rather than
  restructuring a nested expression.

### Negative

- "Complicated" has no threshold. The upstream Enforcement section admits this
  openly and ends in an unanswered question about how many operators are too
  many, so the rule cannot be applied mechanically and will be argued about.
- The rule contradicts itself in tone: hiding an assignment in a condition is
  bad, but so is introducing a named temporary for `k * y`. The line between
  them is taste.
- `while ((c = getc()) != -1)` is a widely recognised idiom, and the
  init-statement rewrite is longer. Rejecting it costs familiarity for a reader
  who has seen the idiom a thousand times.
- Splitting an expression into statements can change what the compiler is
  allowed to do with lifetimes and temporaries, so a "purely cosmetic" rewrite
  is not always purely cosmetic.

### Neutral

- The undefined-behaviour cases in the example list are not really a matter of
  taste at all; they are covered as their own rules by
  [0200](0200-es-43-avoid-expressions-with-undefined-order-of-evaluation.md) and
  [0201](0201-es-44-dont-depend-on-order-of-evaluation-of-function-arguments.md).

## Enforcement

This is genuinely hard to check. Things worth considering as signals: side
effects on multiple non-local variables, especially in separate subexpressions;
writes to aliased variables; more than some number of operators; reliance on
subtle precedence rules; and any reliance on undefined or implementation-defined
behaviour. Writing computations as statements with one operation each is also
confusing, so a checker that simply counts operators will make code worse.

## Related

- [0198](0198-es-41-if-in-doubt-about-operator-precedence-parenthesize.md) — the
  precedence half of this rule.
- [0200](0200-es-43-avoid-expressions-with-undefined-order-of-evaluation.md) —
  the `x = x++ + x++ + ++x` case as its own rule.
- [0009](0009-p-3-express-intent.md) — an expression that needs decoding does
  not express intent.
- [0241](0241-per-4-dont-assume-that-complicated-code-is-necessarily-faster-than-simple-code.md)
  — the usual excuse for a dense expression is speed, and it is usually wrong.
