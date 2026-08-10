# 0283. CP.101: Distrust your hardware/compiler combination

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

The low-level hardware interfaces used by lock-free programming are among the
hardest to implement well, and among the areas where the most subtle portability
problems occur. If you are doing lock-free programming for performance, you need
to check for regressions — the reason you wrote the code is itself a property of
a particular platform, and it does not travel.

Instruction reordering, both static and dynamic, makes it hard to think
effectively at this level, and especially so if you use relaxed memory models.
Experience, (semi)formal models and model checking can be useful here. Testing,
often to an extreme extent, is essential. As the guidelines put it: "Don't fly
too close to the sun."

## Decision

Treat the compiler, the standard library, the operating system and the processor
as parts of the program that can change under you.

- Assume nothing about a lock-free construct that is not guaranteed by the
  standard, however reliably the current toolchain behaves.
- Re-run the concurrency tests on every change of hardware, operating system,
  compiler or library — not only on changes to your own code.
- Keep the performance measurement that justified the lock-free code, and re-run
  it as a regression test; if the margin has gone, the justification has gone
  with it.
- Where a relaxed ordering is used, back it with something stronger than
  reading: a model, a checker, or an amount of testing that would be
  disproportionate anywhere else.

## Rationale

Lock-free code is written against a machine, not against the language, so its
correctness and its speed are both hostage to a combination of parts that no
single change of your own touches. That is what makes the failure mode
particular: the code does not break when it is edited, it breaks when the
compiler is upgraded, and there is nothing in the diff to look at.

## Consequences

### Positive

- Toolchain and platform upgrades become deliberate events with a test gate,
  rather than something that silently changes program semantics.
- Keeping the original benchmark means the code's justification stays checkable
  instead of becoming folklore.
- Naming the platform assumptions makes the portability cost of lock-free code
  visible before it is paid.

### Negative

- Testing "often to an extreme extent" is an unbounded demand; it has no
  completion criterion, and concurrency tests that pass prove very little.
- Re-testing on every compiler, library and OS change is an expensive standing
  obligation, and in practice it slows down toolchain upgrades that are wanted
  for other reasons.
- Formal models and model checking are specialist tools with their own learning
  cost, and the rule recommends them without saying when they are warranted.
- Distrust is not actionable on its own: it tells you the platform may betray
  you but not which of its guarantees to stop relying on.

### Neutral

- The rule assumes lock-free code already exists;
  [0282](0282-cp-100-dont-use-lock-free-programming-unless-you-absolutely-have-to.md)
  is the earlier decision about whether to write any.
- The re-testing discipline is useful beyond concurrency, but this rule claims
  only the concurrency case.

## Enforcement

- Have strong rules for re-testing in place that cover any change in hardware,
  operating system, compiler, and libraries.

## Related

- [0282](0282-cp-100-dont-use-lock-free-programming-unless-you-absolutely-have-to.md)
  — the rule this one qualifies: if you write it anyway, distrust the platform.
- [0284](0284-cp-102-carefully-study-the-literature.md) — the other half of the
  competence requirement.
- [0008](0008-p-2-write-in-iso-standard-c.md) — portability is the guarantee
  being relied on, and lock-free code is where it is thinnest.
- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — the model checkers
  and test harnesses this rule leans on.
