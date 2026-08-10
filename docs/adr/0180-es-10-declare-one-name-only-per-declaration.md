# 0180. ES.10: Declare one name (only) per declaration

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

One declaration per line increases readability and avoids mistakes arising from
the C and C++ declarator grammar. It also leaves room for a descriptive
end-of-line comment.

```cpp
char *p, c, a[7], *pp[7], **aa[10];   // yuck!
```

The declarator syntax distributes over the declarators in a way almost nobody
holds in their head correctly: `p` is a pointer, `c` is a `char`, `a` is an
array, `pp` is an array of pointers, `aa` is an array of pointers to pointers.
The type on the left is `char`, and everything else is attached to the
individual names.

The same problem in its ordinary form:

```cpp
int a = 10, b = 11, c = 12, d, e = 14, f = 15;
```

In a long list of declarators it is easy to overlook an uninitialised variable —
`d`, here.

There are two exceptions. A function declaration can of course contain several
function argument declarations:

```cpp
template<class InputIterator, class Predicate>
bool any_of(InputIterator first, InputIterator last, Predicate pred);
```

or, better, using concepts:

```cpp
bool any_of(input_iterator auto first, input_iterator auto last, predicate auto pred);
```

A structured binding is the other exception; it is specifically designed to
introduce several variables at once:

```cpp
auto [iter, inserted] = m.insert_or_assign(k, val);
if (inserted) { /* new entry was inserted */ }
```

The one-per-line habit also pays off in parameter lists that need commentary.
These are all acceptable, in increasing order of clarity:

```cpp
double scalbn(double x, int n);   // OK: x * pow(FLT_RADIX, n); FLT_RADIX is usually 2
```

```cpp
double scalbn(    // better: x * pow(FLT_RADIX, n); FLT_RADIX is usually 2
    double x,     // base value
    int n         // exponent
);
```

```cpp
// better: base * pow(FLT_RADIX, exponent); FLT_RADIX is usually 2
double scalbn(double base, int exponent);
```

The last is best because the names carry what the comments were carrying.

## Decision

Declare one variable per declaration statement.

- Never write a declaration with more than one declarator. One name, one line.
- In particular, never mix pointer, array and plain declarators in a single
  declaration.
- Give each declaration its own initialiser, so that a missing one is visible
  rather than buried in a list.
- Function parameter lists and structured bindings are exempt; both are single
  constructs that happen to introduce several names.
- Where a parameter needs explaining, prefer a better parameter name to a
  per-parameter comment, and a one-per-line layout to a comment at the end of a
  long signature.

## Rationale

The declarator grammar binds `*` and `[]` to the name rather than to the type,
which means a multi-declarator declaration does not mean what its left-hand side
suggests. This is not an obscure corner: `char* p, q;` declares one pointer and
one `char`, and reads to almost everyone as declaring two pointers. The rule is
cheap and removes the entire category, along with the separate benefit that an
uninitialised name in a list stops being camouflaged.

## Consequences

### Positive

- The type written on the left is the type of the name on the right, always.
- An uninitialised declaration stands alone on a line, where it is visible to a
  reviewer and to
  [0183](0183-es-20-always-initialize-an-object.md)'s enforcement.
- Each declaration gets its own line for a comment, its own diff line, and its
  own place in a blame view.

### Negative

- Vertical space: six related short declarations become six lines, and a group
  of closely related variables can read as less obviously a group.
- The rule slightly discourages declaring several variables at once even when
  that is genuinely what is happening, which pushes towards structured bindings
  or a small struct — usually good, occasionally over-engineering.
- It says nothing about the underlying problem, which is that the declarator
  grammar is confusing; a reader still has to know that `char* p` and `char *p`
  are the same declaration.

### Neutral

- Structured bindings and parameter lists are explicit exceptions, so the rule
  does not conflict with modern idioms that introduce several names.
- Nothing here constrains where the `*` or `&` is placed relative to the type
  name.

## Enforcement

Flag variable and constant declarations with multiple declarators, for example
`int* p, q;`.

## Related

- [0183](0183-es-20-always-initialize-an-object.md) — the uninitialised `d` in
  the example is the bug this rule makes visible.
- [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md) —
  `auto` puts the declared name in a fixed position, which is the same goal.
- [0175](0175-es-5-keep-scopes-small.md) — one declaration per line makes it
  easier to move each one to where it belongs.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — the concepts form of `any_of` in the example.
