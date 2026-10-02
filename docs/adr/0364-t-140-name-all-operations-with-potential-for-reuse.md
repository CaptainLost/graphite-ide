# 0364. T.140: Name all operations with potential for reuse

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Naming an operation buys documentation, readability and the opportunity for
reuse. Consider a search over records, written with the whole comparison inline:

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

There is a useful function lurking here — case-insensitive string comparison —
as there often is when lambda arguments get large:

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

Or, if you prefer to avoid the implicit name binding to `n`:

```cpp
auto cmp_to_n = [&n](const string& a) { return compare_insensitive(a, n); };

auto x = find_if(vr.begin(), vr.end(),
    [&](const Rec& r) { return cmp_to_n(r.name); }
);
```

The rule applies whether the operation is a function, a lambda or an operator.
The exceptions are lambdas used only locally — an argument to `for_each` and
similar control-flow algorithms — and lambdas used as initializers.

## Decision

Give a name to any operation that could be used more than once.

- When a lambda body grows past a line or two, extract the reusable part into a
  named function and call it from the lambda.
- Name the operation for what it computes, not for the call site that prompted
  it.
- Where the operation must capture, bind the captures in a named lambda rather
  than repeating the capture list at every use.
- Leave unnamed only lambdas that are genuinely local and single-use: a
  predicate handed straight to an algorithm, or an initializer.

## Rationale

A large lambda hides a general operation inside a particular call. Naming it
separates the two questions a reader has — what is being computed, and what is
being searched — and turns the first into something that can be tested on its
own, which an inline lambda cannot be. The duplication argument follows: an
operation nobody named is one nobody can find, so the second person to need it
writes it again.

## Consequences

### Positive

- The general operation becomes testable, documentable and findable.
- The call site shrinks to the thing it is actually about.
- Duplicated logic becomes visible, because two call sites now share a name
  rather than two similar bodies.

### Negative

- Naming is hard, and a badly named extracted function is worse than the inline
  code it replaced, since it now misleads at every call.
- Extraction costs locality: the reader of the algorithm call must jump
  elsewhere to see what the predicate does.
- Captured state has to become parameters, so the extracted function often
  takes more arguments than the lambda needed, and the call site regains some
  of the noise removed.
- The judgement "potential for reuse" is speculative; naming an operation that
  is never reused adds a public name for no return.

### Neutral

- The rule covers functions, lambdas and operators alike.
- It says nothing about where the named operation should live; scoping it is a
  separate decision.

## Enforcement

- (Hard) Flag similar lambdas, which are candidates for replacement by a named
  function or a named lambda.

## Related

- [0438](0438-f-1-package-meaningful-operations-as-carefully-named-functions.md)
  — the general rule, of which this is the lambda-shaped case.
- [0365](0365-t-141-use-an-unnamed-lambda-if-you-need-a-simple-function-object-in-one-place-only.md)
  — the counterweight, and where the exceptions come from.
- [0174](0174-es-3-dont-repeat-yourself-avoid-redundant-code.md) — the
  duplication that unnamed operations cause.
- [0468](0468-f-50-use-a-lambda-when-a-function-wont-do-to-capture-local-variables-or-to-write-a-local-function.md)
  — when the operation should be a lambda at all.
