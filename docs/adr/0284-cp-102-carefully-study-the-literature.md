# 0284. CP.102: Carefully study the literature

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

With the exception of atomics and a few other standard patterns, lock-free
programming is really an expert-only topic. Become an expert before shipping
lock-free code for others to use.

The guidelines do not leave "become an expert" as an instruction without a
route. They give a reading list, which is the substance of this rule:

- Anthony Williams, *C++ Concurrency in Action*, Manning Publications.
- Boehm and Adve, "You Don't Know Jack About Shared Variables or Memory Models",
  *Communications of the ACM*, February 2012.
- Boehm, "Threads Basics", HPL TR 2009-259.
- Adve and Boehm, "Memory Models: A Case for Rethinking Parallel Languages and
  Hardware", *Communications of the ACM*, August 2010.
- Boehm and Adve, "Foundations of the C++ Concurrency Memory Model", PLDI 08.
- Batty, Owens, Sarkar, Sewell and Weber, "Mathematizing C++ Concurrency", POPL
  2011.
- Dechev, Pirkelbauer and Stroustrup, "Understanding and Effectively Preventing
  the ABA Problem in Descriptor-based Lock-free Designs", IEEE ISORC 2010.
- Dechev and Stroustrup, "Scalable Non-blocking Concurrent Objects for Mission
  Critical Code", ACM OOPSLA'09.
- Dechev, Pirkelbauer, Rouquette and Stroustrup, "Semantically Enhanced
  Containers for Concurrent Real-Time Systems", IEEE ECBS 2009.
- Herlihy, Shavit, Luchangco and Spear, *The Art of Multiprocessor Programming*,
  2nd edition, September 2020.

## Decision

Read before you write, and do not ship lock-free code for others to use on the
strength of a plausible-looking algorithm.

- Treat atomics under the default ordering, and the small set of standard
  patterns, as the part of this subject that ordinary code may use.
- For anything beyond that, work from the literature above rather than from a
  blog post or a remembered idiom.
- Be able to name the published algorithm a piece of lock-free code implements,
  and say in a comment which one it is, so the next reader has the same route
  into it that you had.
- Where you cannot cite what the code is based on, that is the signal to fall
  back on
  [0282](0282-cp-100-dont-use-lock-free-programming-unless-you-absolutely-have-to.md)
  and use a mutex or a library.

## Rationale

The distinguishing property of this area is that intuition is actively
misleading: the memory model is not the machine, the machine is not the source,
and the failures are invisible to testing. The reading list exists because the
knowledge required is not derivable from the language reference, and a citation
in the code is the only durable evidence that the author had it.

## Consequences

### Positive

- The reading list turns "be an expert" from an attitude into a finite,
  checkable action.
- A named algorithm in a comment survives the author leaving, and gives review a
  standard to compare the code against.
- The bar applies specifically to code shipped for others to use, which is where
  a defect is hardest to withdraw.

### Negative

- The cost is measured in weeks of study, and it falls before any code is
  written — an unattractive prospect against a mutex that works today.
- Parts of the list are more than a decade old and describe a memory model that
  has been revised since; the reader has to reconcile the papers with the
  current standard themselves.
- The literature is written about idealised machines, so it does not settle what
  a particular compiler and processor will do, which is exactly what
  [0283](0283-cp-101-distrust-your-hardware-compiler-combination.md) says to
  distrust.
- There is no enforcement, and no test distinguishes an expert from someone who
  believes they are one.

### Neutral

- The rule bounds itself: atomics and a few standard patterns are excluded, so
  it does not apply to most concurrent code.
- Nothing here says the reading must precede using a library that others have
  already written to this standard.

## Related

- [0282](0282-cp-100-dont-use-lock-free-programming-unless-you-absolutely-have-to.md)
  — the default this rule qualifies, and the ABA problem the reading list
  addresses.
- [0283](0283-cp-101-distrust-your-hardware-compiler-combination.md) — knowledge
  of the model is necessary but not sufficient; the platform still has to be
  measured.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — using someone
  else's implementation is the alternative to acquiring the expertise.
