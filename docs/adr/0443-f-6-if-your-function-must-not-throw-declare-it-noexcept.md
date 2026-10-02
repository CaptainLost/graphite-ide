# 0443. F.6: If your function must not throw, declare it noexcept

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

If an exception is not supposed to be thrown, the program cannot be assumed to
cope with the error and should be terminated as soon as possible. Declaring a
function `noexcept` helps optimisers by reducing the number of alternative
execution paths, and it speeds up the exit after a failure.

Put `noexcept` on every function written completely in C, or in any other
language without exceptions. The C++ standard library does that implicitly for
all functions in the C standard library.

`noexcept` may be used even on functions that can throw:

```cpp
vector<string> collect(istream& is) noexcept
{
    vector<string> res;
    for (string s; is >> s;)
        res.push_back(s);
    return res;
}
```

If `collect()` runs out of memory the program crashes. Unless the program is
crafted to survive memory exhaustion that might be exactly right; `terminate()`
may generate useful log information, though after memory has run out it is hard
to do anything clever.

That is why the execution environment matters when deciding whether to tag a
function, especially over allocation. Code intended to be perfectly general —
the standard library and utility code of that sort — must support environments
where a `bad_alloc` could be handled meaningfully. Most programs and
environments cannot meaningfully handle a failure to allocate, and aborting is
the cleanest and simplest response there; if you know your application cannot
respond to an allocation failure, `noexcept` may be appropriate even on
functions that allocate.

Put another way: in most programs most functions can throw, because they use
`new`, call functions that do, or use library functions that report failure by
throwing. Do not sprinkle `noexcept` everywhere without considering whether the
possible exceptions could be handled. It is most useful, and most clearly
correct, on frequently used low-level functions. `constexpr` functions can throw
when evaluated at run time, so some of those need a conditional `noexcept`.
Destructors, swap functions, move operations and default constructors should
never throw.

## Decision

Declare a function `noexcept` when no exception escaping it is a condition the
program could survive anyway.

- Mark destructors, swap functions, move operations and default constructors
  `noexcept`; they must not throw.
- Mark every function whose body is entirely C, or otherwise cannot throw,
  `noexcept`.
- Mark low-level, frequently called functions `noexcept` where the promise is
  clearly true.
- Consider `noexcept` on functions that allocate only if this program could not
  meaningfully handle an allocation failure, and accept that the consequence is
  `terminate()`.
- Use a conditional `noexcept` where throwing depends on a template argument or
  on run-time evaluation of a `constexpr` function.
- Do not add `noexcept` to a function whose exceptions a caller could plausibly
  handle, and do not add it merely because nothing currently throws.

## Rationale

`noexcept` is not a claim that no exception occurs; it is a decision about what
happens if one does, and the answer is `terminate()`. That reframing is what
makes the rule usable: the question to ask is not "can this throw" but "is there
a caller who could do something useful about it", and where the answer is no, an
immediate crash with a stack still intact is strictly better than an exception
travelling through code that was never written to unwind. The optimiser benefit
is real but secondary.

## Consequences

### Positive

- The promise becomes machine-checked at the point that matters, rather than
  documented.
- The optimiser can discard unwinding paths, which shrinks code and can speed up
  the ordinary path.
- Standard-library containers select move operations over copies when the move
  is `noexcept`, so marking moves has a measurable effect.
- Failure arrives immediately, at the failing frame, instead of surfacing
  somewhere unrelated.

### Negative

- `noexcept` is part of the interface. A function that must later be allowed to
  throw cannot simply drop it without breaking callers who relied on the
  guarantee.
- Marking allocating functions `noexcept` converts a recoverable condition into
  a hard crash for every caller, including callers in contexts where recovery
  was possible.
- Getting it wrong is not a compile error; the mistake shows up as a
  `terminate()` in production.
- Conditional `noexcept` specifications are notoriously hard to write correctly
  for templates, and a wrong one is worse than none.

### Neutral

- `noexcept` is a statement about one function, not about a program's
  error-reporting strategy. Reporting failures as return values rather than by
  throwing raises the question less often but does not settle it: returning a
  failure as a value does not stop a function allocating, so whether the promise
  is true still has to be judged function by function.
- The rule does not require `noexcept` everywhere; it requires it where the
  no-throw property is a genuine part of the contract.

## Enforcement

- Flag functions that are not `noexcept` yet cannot throw.
- Flag throwing swap functions, move operations, destructors and default
  constructors.

## Related

- [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md)
  — which failures are reported by throwing in the first place.
- [0013](0013-p-7-catch-run-time-errors-early.md) — terminating at the failure is
  the extreme form of catching early.
- [0014](0014-p-8-do-not-leak-any-resources.md) — `terminate()` skips
  destructors, which is the cost this rule accepts.
