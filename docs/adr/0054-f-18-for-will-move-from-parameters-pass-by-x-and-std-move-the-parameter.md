# 0054. F.18: For will-move-from parameters, pass by X&& and std::move the parameter

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

This codebase takes the rule's Exception rather than its main clause: sink
parameters are passed **by value** and moved in the body. `Panel`'s constructor
taking `std::string id, std::string title` and moving them, and
`TextDocument::FromContent(std::string bytes, ...)`, are both that form.

## Decision

Adopt F.18 as written, including its Exception, which is the form used here.
Transcribed in full:

> **Reason.** It's efficient and eliminates bugs at the call site: `X&&` binds to
> rvalues, which requires an explicit `std::move` at the call site if passing an
> lvalue.

**Example.**

```cpp
void sink(vector<int>&& v)  // sink takes ownership of whatever the argument owned
{
    // usually there might be const accesses of v here
    store_somewhere(std::move(v));
    // usually no more use of v here; it is moved-from
}
```

Note that the `std::move(v)` makes it possible for `store_somewhere()` to leave
`v` in a moved-from state. That could be dangerous.

**Exception.** Unique owner types that are move-only and cheap-to-move, such as
`unique_ptr`, can also be passed by value which is simpler to write and achieves
the same effect. Passing by value does generate one extra (cheap) move
operation, but prefer simplicity and clarity first.

For example:

```cpp
template<class T>
void sink(std::unique_ptr<T> p)
{
    // use p ... possibly std::move(p) onward somewhere else
}   // p gets destroyed
```

**Exception.** If the "will-move-from" parameter is a `shared_ptr` follow R.34
and pass the `shared_ptr` by value.

**Enforcement.**

- Flag all `X&&` parameters (where `X` is not a template type parameter name)
  where the function body uses them without `std::move`.
- Flag access to moved-from objects.
- Don't conditionally move from objects.

## Rationale

Adopted with the by-value form as the default here, and that choice is worth
stating because it is a departure from the rule's headline.

The rule's main clause buys one move over the by-value form, at the cost of
forcing every lvalue caller to write `std::move` explicitly and of putting a
moved-from object in the caller's hands. The Exception says to prefer simplicity
and clarity first, and for `std::string` and `std::filesystem::path` — the two
types this actually concerns here — a move is cheap enough that the extra one
does not register.

So: **a sink parameter is taken by value and `std::move`d in the body.** `X&&`
is reserved for a type where the extra move is genuinely expensive, which would
need the measurement
[0051](0051-f-15-prefer-simple-and-conventional-ways-of-passing-information.md)
requires.

The third enforcement point — do not conditionally move — is the one worth
watching. A move inside an `if` leaves the object in one of two states
depending on a branch, which is exactly the kind of thing
[0013](0013-p-7-catch-run-time-errors-early.md) says makes errors hard to find.

## Consequences

### Positive

- One convention for sink parameters, applied consistently, and readable without
  knowing which of the two forms was chosen.
- Callers never hold a moved-from object they did not explicitly hand over.

### Negative

- The by-value form costs one move per call. It is cheap for strings and paths;
  it would not be for a large container passed in a loop, and nothing here would
  detect that.
- `std::move` in the body is easy to forget, and forgetting it silently turns a
  sink into a copy. No warning fires.

### Neutral

- The `shared_ptr` Exception is inert; shared ownership is unused.
- No deviation recorded: the by-value form is a deliberate reading of the rule's
  own Exception, not a violation of it.

## Related

- [0053](0053-f-17-for-in-out-parameters-pass-by-reference-to-non-const.md) — the
  previous rule.
- [0055](0055-f-19-for-forward-parameters-pass-by-tp-and-only-std-forward-the-parameter.md)
  — the next rule.
- [0051](0051-f-15-prefer-simple-and-conventional-ways-of-passing-information.md)
  — the measurement an `X&&` parameter would need.
