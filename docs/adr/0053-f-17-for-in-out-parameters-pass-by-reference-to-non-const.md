# 0053. F.17: For in-out parameters, pass by reference to non-const

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

In-out parameters are rare here, because failures return `Result<T>` and results
are returned rather than written through a parameter. The clearest live example
is `Window::FramebufferSize(int& width, int& height)`, which is an out
parameter pair rather than in-out — and is therefore F.20's subject rather than
this rule's.

## Decision

Adopt F.17 as written. Transcribed in full:

> **Reason.** This makes it clear to callers that the object is assumed to be
> modified.

**Example.**

```cpp
void update(Record& r);  // assume that update writes to r
```

**Note.** Some user-defined and standard library types, such as `span<T>` or the
iterators are cheap to copy and may be passed by value, while doing so has
mutable (in-out) reference semantics:

```cpp
void increment_all(span<int> a)
{
  for (auto&& e : a)
    ++e;
}
```

**Note.** A `T&` argument can pass information into a function as well as out of
it. Thus `T&` could be an in-out-parameter. That can in itself be a problem and
a source of errors:

```cpp
void f(string& s)
{
    s = "New York";  // non-obvious error
}

void g()
{
    string buffer = ".................................";
    f(buffer);
    // ...
}
```

Here, the writer of `g()` is supplying a buffer for `f()` to fill, but `f()`
simply replaces it (at a somewhat higher cost than a simple copy of the
characters). A bad logic error can happen if the writer of `g()` incorrectly
assumes the size of the buffer.

**Enforcement.**

- (Moderate) ((Foundation)) Warn about functions regarding reference to
  non-`const` parameters that do not write to them.
- (Simple) ((Foundation)) Warn when a non-`const` parameter being passed by
  reference is moved.

## Rationale

Adopted as written. The rule's second Note is the interesting one and it is a
warning about the form the rule itself prescribes: `T&` does not distinguish
in-out from out-only, and a caller who supplied a buffer expecting it to be
filled may find it replaced.

That ambiguity is why this codebase has so few of these. Where a function
produces a value it returns it, per
[0054 onward](0054-f-18-for-will-move-from-parameters-pass-by-x-and-std-move-the-parameter.md)
and F.20; a non-`const` reference parameter is reserved for the case where the
callee genuinely modifies an object the caller already owns and cares about —
`SolutionEditor` operating on a loaded solution, for instance.

The first enforcement point is a useful review question in its own right: a
non-`const` reference the function never writes to should be `const`.

## Consequences

### Positive

- A non-`const` reference in a signature carries a promise, which makes the
  first enforcement point a real check on interface honesty.
- The `span` Note explains why a by-value `span` is not a contradiction of the
  rule, which is otherwise confusing.

### Negative

- The rule cannot distinguish in-out from out-only, and its own Note shows the
  bug that follows. Nothing in the type system fixes this; only a naming or
  documentation convention does.
- Applied literally it would encourage in-out parameters where a return value
  would be clearer — F.20 has to pull in the other direction.

### Neutral

- No inventory taken. In-out parameters are few enough that a list would be
  shorter than the effort of producing it reliably, and the out-parameter cases
  belong under F.20.

## Related

- [0052](0052-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md)
  — the previous rule.
- [0054](0054-f-18-for-will-move-from-parameters-pass-by-x-and-std-move-the-parameter.md)
  — the next rule.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — `span`, which
  this rule's first Note relies on.
