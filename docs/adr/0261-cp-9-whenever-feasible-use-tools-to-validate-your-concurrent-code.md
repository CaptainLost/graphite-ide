# 0261. CP.9: Whenever feasible use tools to validate your concurrent code

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Experience shows that concurrent code is exceptionally hard to get right, and
that compile-time checking, run-time checks and testing are all less effective
at finding concurrency errors than they are at finding errors in sequential
code. Subtle concurrency errors can have dramatically bad effects, including
memory corruption, deadlocks and security vulnerabilities.

Thread safety is challenging and often gets the better of experienced
programmers, so tooling is an important strategy for mitigating the risk. There
are many tools available, both commercial and open-source, both research and
production. People's needs and constraints differ so dramatically that the
guidelines decline to make specific recommendations, but they name two
categories.

Static enforcement tools: both clang and some older versions of GCC have support
for static annotation of thread-safety properties. Consistent use of this
technique turns many classes of thread-safety error into compile-time errors.
The annotations are generally local — marking a particular member variable as
guarded by a particular mutex — and are usually easy to learn. As with many
static tools, they can present false negatives: cases that should have been
caught but were allowed.

Dynamic enforcement tools: clang's ThreadSanitizer is a powerful example. It
changes the build and execution of the program to add bookkeeping on memory
access, absolutely identifying data races in a given execution of the binary.
The cost is both memory, typically five to ten times, and CPU slowdown, two to
twenty times. Tools like this are best applied to integration tests, canary
pushes, or unit tests that operate on multiple threads. Workload matters: when
ThreadSanitizer reports a problem it is effectively always a real data race, but
it can only identify races that occur in the execution it observed.

## Decision

Do not rely on review and testing alone to establish that concurrent code is
correct.

- Where a toolchain in use supports thread-safety annotations, apply them: mark
  each guarded member with the mutex that guards it, and treat the resulting
  diagnostics as errors.
- Run the concurrent parts of the test suite under a dynamic race detector such
  as ThreadSanitizer, rather than under the ordinary build only.
- Apply the dynamic detector where it earns its cost — integration tests and
  multi-threaded unit tests — rather than to every build, given the memory and
  CPU multipliers.
- Treat a race reported by a dynamic detector as a confirmed defect, and treat a
  clean run as evidence about the executions observed, not as a proof.
- Design the multi-threaded tests to exercise the interleavings you care about,
  because the detector only sees what the workload produces.

## Rationale

The reason to spend build time on this is asymmetric evidence. A dynamic race
detector has effectively no false positives, so a report is a defect and needs
no argument; but it has no power over paths the test did not run, so a clean run
proves only what it observed. Knowing which half you are holding is what makes
the tool usable.

That asymmetry also decides where the tools go. The static annotations are cheap
and belong in the source permanently; the dynamic detector is expensive and
belongs on the tests that actually run several threads.

## Consequences

### Positive

- Catches races that review and ordinary testing demonstrably miss.
- A report from a dynamic detector is actionable without further argument,
  because false positives are effectively absent.
- Static annotations document the intended mutex-to-data relationship in the
  source, whether or not the checker is enabled.

### Negative

- The dynamic tools cost five to ten times the memory and two to twenty times
  the CPU, which rules them out of an ordinary edit-build-test loop.
- Coverage is only as good as the workload; an untaken interleaving is an
  unchecked one, so a green run tempts a conclusion it does not support.
- Static annotations have false negatives, so a clean static build is also
  weaker evidence than it looks.
- Availability is compiler-dependent: the annotations exist on some toolchains
  and not others, and the dynamic detector usually needs its own instrumented
  build, so adopting this rule can mean maintaining a second configuration.

### Neutral

- The guidelines deliberately name no specific tool as required; which tools
  are worth their cost is a per-application judgement.
- Using several tools catches more than using one, which is a matter of budget
  rather than of principle.

## Enforcement

- It is up to the application builder to choose which support tools are valuable
  for a particular application.

## Related

- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — the general rule
  this one specialises to concurrency.
- [0257](0257-cp-2-avoid-data-races.md) — the defect these tools look for, and
  the enforcement note that lists the same trade-offs.
- [0256](0256-cp-1-assume-that-your-code-will-run-as-part-of-a-multi-threaded-program.md)
  — the assumption that makes the tooling worth running at all.
- [0276](0276-cp-50-define-a-mutex-together-with-the-data-it-guards-use-synchronized-value-t-where-possible.md)
  — pairing a mutex with its data is what the static annotations express.
