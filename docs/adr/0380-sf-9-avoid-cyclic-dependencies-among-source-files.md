# 0380. SF.9: Avoid cyclic dependencies among source files

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

Cycles complicate comprehension and slow down compilation. They also complicate
conversion to language-supported modules.

```cpp
// bad
// file1.h:
#include "file2.h"

// file2.h:
#include "file3.h"

// file3.h:
#include "file1.h"
```

Eliminate cycles; do not merely break them with include guards. A guard stops the
preprocessor recursing for ever, so the code compiles — but the cycle is still
there, and it now has a second symptom: the contents of `file1.h` are only
partly visible while `file3.h` is being processed, and which part depends on
which header the translation unit included first.

A cycle among files is nearly always a cycle among the ideas in them. Two
components that each need the other's declarations are not two components.

## Decision

Keep the include graph acyclic, and treat a cycle as a design defect rather than
a build problem.

- Break a cycle by moving the shared declaration into a third header that both
  sides include, rather than by adding a guard and moving on.
- Use a forward declaration where only a pointer or reference to a type is
  needed; this removes the include, and with it the edge in the graph.
- Where two types genuinely need each other, invert one direction with an
  abstract interface so that the dependency points one way only.
- Keep the dependency between components one-way as well: a lower-level component
  that includes a header from a higher-level one has created a cycle at the level
  that matters most, whatever the per-file graph looks like.

## Rationale

The compile-time argument is the least of it. A cycle means neither file can be
understood, tested, or reused without the other, so the boundary between them is
fictional — and the fix is nearly always the smaller design that was there all
along: a third file holding what both actually depend on.

The rule's insistence on eliminating rather than breaking cycles is the part
worth keeping. Include guards make a cyclic graph compile, which removes the
pressure to fix it and leaves behind an order-dependence that shows up much later
as a partially declared type.

## Consequences

### Positive

- Every file can be compiled, read, and reasoned about with a bounded set of
  dependencies.
- Order-dependent partial declarations, the characteristic symptom of a
  guard-broken cycle, cannot occur.
- The build parallelises better, and touching one header rebuilds less.

### Negative

- Breaking a cycle usually means creating another file — an interface header, or
  a common-types header — which adds a level of indirection that exists only to
  satisfy the graph.
- Forward declarations, the cheapest tool here, are declarations kept in step by
  hand, which
  [0374](0374-sf-3-use-h-files-for-all-declarations-used-in-multiple-source-files.md)
  is otherwise trying to avoid.
- Nothing in the toolchain reports cycles by default, so the rule is enforced by
  attention until something is set up to check it.

### Neutral

- The rule is about the graph, not about the number of edges; an acyclic graph can
  still be far too dense.
- A cycle confined to `.cpp` files calling each other is not an include cycle and
  is outside this rule, though it is often the same design smell.

## Enforcement

- Flag all cycles.

## Related

- [0381](0381-sf-10-avoid-dependencies-on-implicitly-included-names.md) — explicit
  includes are what make the real graph visible enough to check.
- [0379](0379-sf-8-use-include-guards-for-all-h-files.md) — guards make a cycle
  compile; they do not remove it.
- [0382](0382-sf-11-header-files-should-be-self-contained.md) — a header caught
  in a cycle cannot be self-contained.
- [0374](0374-sf-3-use-h-files-for-all-declarations-used-in-multiple-source-files.md)
  — where a shared declaration belongs once a cycle is broken.
