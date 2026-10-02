# 0472. F.54: If you capture this, capture all variables explicitly (no default capture)

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Default capture in a member function is confusing. Writing `[=]` appears to
capture by value, but actually captures data members by reference, because what
it captures by value is the invisible `this` pointer. If that is what was meant,
`this` should be written explicitly.

```cpp
class My_class {
    int x = 0;
    // ...

    void f()
    {
        int i = 0;
        // ...

        auto lambda = [=] { use(i, x); };   // BAD: "looks like" copy/value capture
        // [&] has identical semantics and copies the this pointer under the current rules
        // [=,this] and [&,this] are not much better, and confusing

        x = 42;
        lambda(); // calls use(0, 42);
        x = 43;
        lambda(); // calls use(0, 43);

        // ...

        auto lambda2 = [i, this] { use(i, x); }; // ok, most explicit and least confusing
        // ...
    }
};
```

The two calls to `lambda` demonstrate the problem exactly: `i` was copied and
stays `0`, while `x` follows the object and changes between the calls, although
both were captured by the same `[=]`. Nothing in the capture list distinguishes
them.

This was under active discussion in standardisation when the rule was written,
and might be addressed in a future version of the standard by adding a new
capture mode, or by adjusting the meaning of `[=]`. For now, be explicit.

## Decision

Do not use a default capture in a lambda that captures `this`.

- Write out every capture by name in a lambda inside a member function:
  `[i, this]`, not `[=]` or `[&]`.
- Write `this` explicitly when the lambda needs access to the object's members,
  so that the reader sees a pointer capture rather than inferring one.
- Capture a member by value with an init-capture — `[x = x]` — when a snapshot
  of the member is what is wanted, since no default capture will give you one.
- Do not rely on `[=, this]` or `[&, this]` to make the intent clear; they are
  explicit about `this` and still leave the rest of the capture set implicit.
- Treat a captured `this` as subject to
  [0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md):
  it is a raw pointer, and a lambda holding one must not outlive the object.

## Rationale

The argument is the discrepancy the example makes visible. A capture list is
read as a summary of what the lambda depends on and how; `[=]` in a member
function makes that summary wrong for exactly the subset of names that are data
members, and the reader has no way to tell which names those are without
knowing the class.

That the two default captures — `[=]` and `[&]` — have identical semantics with
respect to `this` is the second half of it. When the two opposite-looking
spellings mean the same thing, the spelling has stopped carrying information,
and the only remedy is to stop using it.

## Consequences

### Positive

- The capture list becomes an accurate list of what the lambda depends on and
  how each dependency is held.
- The lifetime question becomes visible: a written `this` prompts the question
  of whether the object outlives the lambda, and an implicit one does not.
- It survives whatever standardisation does to `[=]`, because explicit captures
  mean the same thing under every proposal.

### Negative

- Explicit capture lists are longer and must be maintained; adding a use of a
  member or a local inside the lambda now requires a matching edit at the top,
  and forgetting it is a compile error rather than a silent success.
- For a lambda that legitimately uses many members, the explicit form is
  substantially more ceremonious than `[this]` would be, and the rule offers no
  relief short of listing them.
- The rule addresses the confusion, not the danger. `[i, this]` is exactly as
  capable of dangling as `[=]`; it is only more honest about it.
- C++20 deprecates implicit `this` capture via `[=]`, so on a conforming C++20
  compiler part of this rule is already a warning, and the record's remaining
  content is the narrower objection to `[=, this]`.

### Neutral

- The rule constrains lambdas inside member functions. A lambda at namespace or
  block scope with no `this` in reach is not affected.
- Init-capture, `[x = x]`, is the C++14 feature that makes a genuine by-value
  member capture expressible at all; without it the rule would have no answer
  for that case.

## Enforcement

- Flag any lambda capture list that specifies a default capture and also
  captures `this`, whether explicitly or via the default capture.

## Related

- [0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md)
  — a captured `this` is a reference to an object that may not outlive the
  lambda.
- [0470](0470-f-52-prefer-capturing-by-reference-in-lambdas-that-will-be-used-locally-including-passed-to-algorithms.md)
  — the by-reference default that this rule stops short of endorsing wholesale.
- [0468](0468-f-50-use-a-lambda-when-a-function-wont-do-to-capture-local-variables-or-to-write-a-local-function.md)
  — capture is the reason to write a lambda, so it deserves to be written out.
- [0009](0009-p-3-express-intent.md) — a capture list that misstates what is
  captured expresses the wrong intent.
