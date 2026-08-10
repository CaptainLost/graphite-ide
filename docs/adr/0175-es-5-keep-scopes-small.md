# 0175. ES.5: Keep scopes small

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Three separate benefits follow from a small scope: readability, minimised
resource retention, and the impossibility of accidentally misusing a value after
its useful life. An alternative formulation of the same rule is: do not declare
a name in an unnecessarily large scope.

```cpp
void use()
{
    int i;    // bad: i is needlessly accessible after loop
    for (i = 0; i < 20; ++i) { /* ... */ }
    // no intended use of i here
    for (int i = 0; i < 20; ++i) { /* ... */ }  // good: i is local to for-loop
    if (auto pc = dynamic_cast<Circle*>(ps)) {  // good: pc is local to if-statement
        // ... deal with Circle ...
    }
    else {
        // ... handle error ...
    }
}
```

The resource-retention case is the more expensive one:

```cpp
void use(const string& name)
{
    string fn = name + ".txt";
    ifstream is {fn};
    Record r;
    is >> r;
    // ... 200 lines of code without intended use of fn or is ...
}
```

This function is by most measures too long anyway, but the point stands
independently: the memory held by `fn` and the file handle held by `is` are
retained for far longer than they are needed, and unanticipated uses of `is` and
`fn` can happen anywhere in the remaining two hundred lines. Factoring out the
read fixes both at once:

```cpp
Record load_record(const string& name)
{
    string fn = name + ".txt";
    ifstream is {fn};
    Record r;
    is >> r;
    return r;
}

void use(const string& name)
{
    Record r = load_record(name);
    // ... 200 lines of code ...
}
```

## Decision

Declare every name in the smallest scope in which it is used.

- Declare a loop variable in the loop's initialiser, not before the loop.
- Use the `if`, `switch` and range-`for` initialiser forms to confine a name
  that exists only to be tested.
- Introduce a bare `{ ... }` block when a value is needed for part of a function
  and not the rest, rather than letting it live to the closing brace.
- When a resource — a file handle, a lock, a large buffer — is used only at the
  start of a long function, extract that part into its own function so the
  resource is released when it stops being needed.
- Do not keep a name alive merely because it might be convenient later.

## Rationale

A scope is a claim about lifetime and a claim about reachability at the same
time, and the second is the one that bites. A variable still in scope is a
variable a later edit can reach, and the bug that results — reusing a value left
over from an earlier phase — is invisible at the point where it is written,
because the code compiles and the name means something. Shrinking the scope
removes the possibility instead of relying on nobody taking it.

## Consequences

### Positive

- Resources are released at the point they stop being needed, not at the end of
  the function.
- A name that is out of scope cannot be misused, so a whole class of stale-value
  bugs stops being expressible.
- Small scopes push functions towards being short, because a long function makes
  small scopes awkward.

### Negative

- Applied honestly, the rule is a demand to split functions, which is a larger
  change than moving a declaration and is not always wanted.
- A value genuinely needed in two branches must be declared in the enclosing
  scope anyway, and pushing this rule too hard produces artificial helper
  functions or repeated computation.
- Extracting a block into a function to shrink a scope adds a name, a signature,
  and a place to look; that is not always a net gain for four lines.
- Nested blocks introduced purely for scoping add indentation, which costs some
  of the readability the rule was after.

### Neutral

- The rule says nothing about how long a function may be; it merely makes long
  functions uncomfortable.
- Where a lock's scope is deliberately wider than its first use — to cover a
  whole critical section — that width is the point, not a violation.

## Enforcement

- Flag a loop variable declared outside a loop and not used after the loop.
- Flag when expensive resources, such as file handles and locks, are not used
  for some suitable number of lines.

## Related

- [0176](0176-es-6-declare-names-in-for-statement-initializers-and-conditions-to-limit-scope.md)
  — the concrete form of this rule for loops and conditions.
- [0184](0184-es-21-dont-introduce-a-variable-or-constant-before-you-need-to-use-it.md)
  — the same rule along the time axis rather than the nesting axis.
- [0189](0189-es-26-dont-use-a-variable-for-two-unrelated-purposes.md) — the
  misuse a wide scope makes possible.
- [0182](0182-es-12-do-not-reuse-names-in-nested-scopes.md) — the other half of
  keeping nested scopes comprehensible.
