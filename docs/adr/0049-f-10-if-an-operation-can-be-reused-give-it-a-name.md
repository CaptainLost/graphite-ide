# 0049. F.10: If an operation can be reused, give it a name

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

F.1's lambda Note, promoted to its own rule. The relevant code here is the
ImGui-facing panel code, where predicates and small callbacks are written inline
at the call, and `Fs`, which already has a named `EqualsIgnoringCase` helper —
the exact function this rule's example arrives at.

## Decision

Adopt F.10 as written. Transcribed in full:

> **Reason.** Documentation, readability, opportunity for reuse.

**Example.**

```cpp
struct Rec {
    string name;
    string addr;
    int id;         // unique identifier
};

bool same(const Rec& a, const Rec& b)
{
    return a.id == b.id;
}

vector<Rec*> find_id(const string& name);    // find all records for "name"

auto x = find_if(vr.begin(), vr.end(),
    [&](Rec& r) {
        if (r.name.size() != n.size()) return false; // name to compare to is in n
        for (int i = 0; i < r.name.size(); ++i)
            if (tolower(r.name[i]) != tolower(n[i])) return false;
        return true;
    }
);
```

There is a useful function lurking here (case insensitive string comparison), as
there often is when lambda arguments get large.

```cpp
bool compare_insensitive(const string& a, const string& b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) if (tolower(a[i]) != tolower(b[i])) return false;
    return true;
}

auto x = find_if(vr.begin(), vr.end(),
    [&](Rec& r) { return compare_insensitive(r.name, n); }
);
```

Or maybe (if you prefer to avoid the implicit name binding to `n`):

```cpp
auto cmp_to_n = [&n](const string& a) { return compare_insensitive(a, n); };

auto x = find_if(vr.begin(), vr.end(),
    [](const Rec& r) { return cmp_to_n(r.name); }
);
```

**Note.** whether functions, lambdas, or operators.

**Exception.**

- Lambdas logically used only locally, such as an argument to `for_each` and
  similar control flow algorithms.
- Lambdas as initializers

**Enforcement.**

- (hard) flag similar lambdas
- `???`

## Rationale

Adopted as written. The example is not hypothetical here: `FileOperations.cpp`
already contains `EqualsIgnoringCase`, arrived at for the same reason — a
case-insensitive comparison needed in more than one place, extracted and named.

The rule's Exception is what keeps it from being a nuisance. Immediately-invoked
initializer lambdas and small `for_each`-style bodies stay anonymous; the target
is a non-trivial predicate that has appeared twice.

Its enforcement is marked hard and carries a `???`, so this is a review rule
rather than a checkable one. The practical trigger is duplication: the second
time a lambda is written, name it.

## Consequences

### Positive

- A clear trigger — the second occurrence — rather than a matter of taste.
- Named predicates are separately testable, which an inline lambda is not.

### Negative

- Extracting a lambda that captured local state turns captures into parameters,
  which can produce a helper with more parameters than the lambda had captures.
- "Similar" is doing a lot of work: two predicates that differ in one comparison
  may or may not be the same operation.

### Neutral

- No inventory. Finding near-identical lambdas is the enforcement the rule
  itself calls hard, and a manual pass would produce a list nobody could trust.

## Related

- [0048](0048-f-9-unused-parameters-should-be-unnamed.md) — the previous rule.
- [0050](0050-f-11-use-an-unnamed-lambda-if-you-need-a-simple-function-object-in-one-place-only.md)
  — the next rule, which is this one's converse.
- [0040](0040-f-1-package-meaningful-operations-as-carefully-named-functions.md)
  — F.1, where this rule starts as a Note.
