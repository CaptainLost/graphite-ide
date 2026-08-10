# 0256. CP.1: Assume that your code will run as part of a multi-threaded program

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

It is hard to be certain that concurrency is not used now, or will not be used
at some point in the future. Code gets reused. Libraries that do not themselves
use threads get called from parts of a program that do. The rule applies most
urgently to library code and least urgently to a stand-alone application, but
over time code fragments turn up in unexpected places.

```cpp
double cached_computation(int x)
{
    // bad: these statics cause data races in multi-threaded usage
    static int cached_x = 0.0;
    static double cached_result = COMPUTATION_OF_ZERO;
    if (cached_x != x) {
        cached_x = x;
        cached_result = computation(x);
    }
    return cached_result;
}
```

`cached_computation` works perfectly in a single-threaded environment. In a
multi-threaded one the two statics are a data race and therefore undefined
behaviour. Moving the state into an object fixes it:

```cpp
struct ComputationCache {
    int cached_x = 0;
    double cached_result = COMPUTATION_OF_ZERO;
    double compute(int x) {
        if (cached_x != x) {
            cached_x = x;
            cached_result = computation(x);
        }
        return cached_result;
    }
};
```

The cache is now member data rather than shared static state, which delegates
the concern upward to the caller. A single-threaded program might still choose
one global `ComputationCache`; a multi-threaded program might have one per
thread, or one per "context" for any definition of context. The refactored
function no longer attempts to manage the allocation of `cached_x`, which makes
this an application of the single responsibility principle. In this example the
change also improved reusability in single-threaded programs: it is not hard to
imagine wanting two caches in different parts of one program without having
them overwrite each other's data.

There are other ways to make code written for a standard multi-threaded
environment — one where the only form of concurrency is `std::thread` — safe:
mark the state `thread_local` rather than `static`; protect the statics with a
`static std::mutex`; refuse to build or run in a multi-threaded environment; or
provide two implementations, one for each case.

The exception is code that is never run in a multi-threaded environment. Be
careful with it. There are many examples of code that was "known" never to run
in a multi-threaded program being run as part of one, often years later, and
such programs typically lead to a painful effort to remove data races. Code that
is never intended to run in a multi-threaded environment should be clearly
labelled as such, and ideally come with compile-time or run-time enforcement to
catch the misuse early.

## Decision

Write every function as though a second thread might call it at the same moment.

- Do not keep mutable function-local `static` state. Put the state in an object
  the caller owns and passes in.
- Where per-thread state really is wanted, say so with `thread_local` rather
  than relying on `static` and hoping.
- Where shared mutable state is genuinely required, give it a mutex and define
  the two together, rather than leaving the synchronisation to the caller's
  imagination.
- If a component is deliberately single-threaded, label it so in its interface
  documentation, and prefer an assertion or a build-time guard over a comment.
- Treat the rule as most binding on library code, which is the code most likely
  to be called from somewhere it was not written for.

## Rationale

The sharpest point in the rule is that the decision is not yours to make: the
code that turns your function into a race has not been written yet, and will be
written by someone who never reads it. A mutable `static` inside a function is a
global variable with the visibility of a local, which is exactly the property
that lets it survive review.

Refactoring the state outward is also not a pure tax. The `ComputationCache`
example is better single-threaded code as well, because it stops one cache from
being an unavoidable program-wide singleton.

## Consequences

### Positive

- Removes the most common accidental sharing — the function-local `static` —
  before any thread exists to expose it.
- Pushes lifetime and ownership of state to a place where the caller can see and
  choose it.
- Makes components reusable in more than one instance, which is a
  single-threaded benefit too.

### Negative

- Every cache, counter or memo becomes an object the caller must create, store
  and thread through call sites; the convenience of a hidden `static` is real
  and this rule spends it.
- Applied without judgement it produces parameter creep in code that will only
  ever be single-threaded, which fights the rule to keep argument counts low.
- "Assume multi-threaded" is a stance, not a check. Nothing verifies that the
  assumption was actually honoured, so the rule depends on the author's care.
- The exception has no test: a component labelled single-threaded stays correct
  only for as long as everyone reads the label.

### Neutral

- The four listed remedies — `thread_local`, a mutex, refusing to build, or two
  implementations — are alternatives, not a ranking. Which fits depends on the
  component.
- The urgency varies with the code's reach: highest for a library, lowest for a
  stand-alone application, but never zero, because code fragments travel.

## Related

- [0021](0021-i-2-avoid-non-const-global-variables.md) — the data-race argument
  against non-const globals; a function-local mutable `static` is the same
  hazard wearing a smaller scope.
- [0022](0022-i-3-avoid-singletons.md) — a hidden cache is a singleton by
  another name.
- [0257](0257-cp-2-avoid-data-races.md) — what happens when this assumption is
  not made.
- [0276](0276-cp-50-define-a-mutex-together-with-the-data-it-guards-use-synchronized-value-t-where-possible.md)
  — how to say that shared state is guarded, when it must be shared.
