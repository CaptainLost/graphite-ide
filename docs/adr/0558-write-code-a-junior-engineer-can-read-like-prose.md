# 0558. Write code a junior engineer can read like prose

Status: accepted
Date: 2026-10-05

## Context

The code is careful but not easy. A survey of every source file scored it 3.7
out of 5 for a reader who knows C++ but not this codebase. What held it back was
not wrong code but code that has to be decoded: one-letter and abbreviated
names, `auto` in front of a `Result` whose emptiness means failure, conditions
of five clauses on one line, lambdas standing in for local functions, protocol
numbers written bare, and about sixteen thousand lines of comments explaining
what the code beside them did not say for itself. A hundred of those comments
had drifted from the code they described; one contradicted its own tests.

The records transcribed from the C++ Core Guidelines assume a reader fluent in
the standard library and its idioms. That assumption made several of them
favour density — short local names, `auto`, algorithms with inline lambdas,
comments kept brief — and the code followed them.

## Decision

Write every line so that a junior engineer can read it from top to bottom
without a comment beside it and without opening another file to learn what a
name means. Code that reader cannot follow unaided is unfinished.

The concrete rules are their own records:

- [0559](0559-name-every-variable-for-its-role-loop-indices-included.md) — names.
- [0560](0560-spell-out-a-variable-s-type-unless-the-same-line-already-states-it.md) — types.
- [0561](0561-write-searches-as-named-loops-and-give-every-predicate-a-name.md) — loops, algorithms and lambdas.
- [0562](0562-say-it-in-code-and-keep-a-comment-only-as-a-warning.md) — comments.
- [0563](0563-write-concrete-code-and-keep-templates-for-vocabulary-types.md) — templates.

Records already in force that say the same thing still apply in full: name a
constant rather than write a number
([0202](0202-es-45-avoid-magic-constants-use-symbolic-constants.md)), split a
complicated expression into named parts
([0197](0197-es-40-avoid-complicated-expressions.md)), package a meaningful
operation as a carefully named function
([0438](0438-f-1-package-meaningful-operations-as-carefully-named-functions.md)),
and avoid needless nesting
([0474](0474-f-56-avoid-unnecessary-condition-nesting.md)).

The rule applies to tests as much as to the sources they test.

**Where complexity stays.** Some code keeps its shape because simplifying it
would cost correctness or speed. It still gets descriptive names, but its
structure is not rewritten:

- **Hot paths keep their algorithm**: the binary searches over pieces, columns,
  lens rows and fitted text, the Fenwick tree in `RowMap`, the line and markdown
  caches, the run coalescing in the row painter, the lexer. A rewrite there adds
  no `std::function` call and no allocation.
- **The ImGui protocol is not negotiable**: every row or child ends on an item or
  a `Dummy`; every Push has its Pop and every Begin its End; ID stacks and `###`
  ids stay as they are; the order of items, which decides focus and hovering,
  stays as it is.
- **Shapes an API forces** stay: libgit2's C callbacks, GLFW's captureless
  callbacks, the `std::function` seams other code plugs into.
- **Persisted identities never change with a rename**: settings and scheme keys,
  `imgui.ini` window ids, the analysis record format, `.slnx` byte offsets — any
  string that reaches the disk or ImGui's id hash.
- **Order that threads and lifetimes depend on** stays: member declaration order,
  lock scopes, cancel-then-wait on overlapped I/O, by-value captures in jobs,
  generation checks.
- **An exhaustive `switch` with no `default`** stays exhaustive, so that a new
  enumerator is a build break
  ([0216](0216-es-70-prefer-a-switch-statement-to-an-if-statement-when-there-is-a-choice.md)).
- **Tables a specification defines** — the xterm palette ramp, the UTF-8 and
  UTF-16 bit layouts, the `CommandLineToArgvW` quoting rules — keep their layout
  and gain names for the numbers written inline.

## Rationale

Code is read far more often than it is written, and most often by someone who
did not write it — in this repository, usually a coding agent starting with no
memory of the last session. A reader who has to reconstruct what `at` holds or
what `if (!diff)` tests spends the effort the author saved, every time, and
sometimes reconstructs it wrongly. Writing for the least experienced competent
reader costs the author a few more characters once and costs every later
reader nothing.

## Consequences

### Positive

- A function can be understood where it stands, without its comments and
  without the declarations of the names it uses.
- Knowledge that lived in comments moves into names, functions, constants and
  tests, where the compiler or the suite holds it to the truth.
- The style is checkable: most of the rules below can be found by a search.

### Negative

- Code gets longer. Lines widen with full names, and loops replace one-line
  algorithm calls; a reader fluent in the standard library will find some of it
  wordy.
- It overrides several transcribed Core Guidelines records, so the directory now
  holds advice the code deliberately does not follow; the superseded records say
  so in their status.
- Bringing the existing code into line is a refactor of every file, done folder
  by folder, during which other work on the same files has to wait.

### Neutral

- Formatting is unchanged: [0479](0479-settle-one-house-style-for-naming-formatting-and-layout.md)'s
  tabs, braces, casing and prefixes stand.
- Splitting long functions into named steps is the same principle applied to
  structure, and is done as a separate stage after this one.

## Related

- [0479](0479-settle-one-house-style-for-naming-formatting-and-layout.md) — the
  house style this builds on.
- [0559](0559-name-every-variable-for-its-role-loop-indices-included.md),
  [0560](0560-spell-out-a-variable-s-type-unless-the-same-line-already-states-it.md),
  [0561](0561-write-searches-as-named-loops-and-give-every-predicate-a-name.md),
  [0562](0562-say-it-in-code-and-keep-a-comment-only-as-a-warning.md),
  [0563](0563-write-concrete-code-and-keep-templates-for-vocabulary-types.md) —
  the rules.
- [0202](0202-es-45-avoid-magic-constants-use-symbolic-constants.md),
  [0197](0197-es-40-avoid-complicated-expressions.md),
  [0438](0438-f-1-package-meaningful-operations-as-carefully-named-functions.md),
  [0474](0474-f-56-avoid-unnecessary-condition-nesting.md) — earlier records that
  say the same.
