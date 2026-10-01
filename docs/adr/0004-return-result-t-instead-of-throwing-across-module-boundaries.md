# 0004. Return Result<T> instead of throwing across module boundaries

Status: accepted
Date: 2026-08-09

## Context

`Core/Base/Result.h` defines `core::Result<T>` as a `[[nodiscard]]` wrapper over
`std::variant<T, Error>`, with a `Result<void>` specialisation holding an
`std::optional<Error>`. `Error` is an `ErrorCode` enum — `None`, `NotFound`,
`AccessDenied`, `InvalidArgument`, `IoFailure`, `Unsupported` — and a message
string. Reading the value of an error result trips `CORE_ASSERT`, so the mistake
is caught in Debug rather than returning garbage.

It is used, not aspired to. Every fallible module returns it: `Fs`
(`FileSystem`, `FileOperations`, `FileTree`, `DirectoryWatcher`), `Sln` (all
four readers, `SlnxEditor`, `SolutionEditor`), `Git` (`Repository`), `Text`
(`Codec`, `FileProbe`, `TextDocument`), `Platform` (`FileDialog`, `Process`,
`Shell`, `Window`), plus `Application` and `ImGuiRenderer`. On the Graphite side
`Workspace` and `SolutionTreeView` consume it.

The C++ Core Guidelines take the opposite position. E.2 says to throw an
exception to signal that a function cannot perform its assigned task, and E.3
says to use exceptions for error handling only. This record is a deliberate
departure from both.

## Decision

An operation that can fail returns `core::Result<T>`. Exceptions do not cross a
module boundary. `main` may catch as a last-resort net so that an unexpected
throw becomes a logged shutdown rather than a silent one.

`Result<T>` is `[[nodiscard]]`, so ignoring a failure is a compiler
diagnostic — and because Core and Graphite build with `warnings "High"` and
`fatalwarnings { "All" }`, it is a build failure.

Third-party code that throws is wrapped at the point it enters Core, and the
throw is converted to an `Error` there. libgit2 is C and returns codes, so
`Repository` is already such a wrapper.

`CORE_ASSERT` is for programmer error — a precondition the caller was supposed
to meet — not for conditions the environment can produce. A missing file is an
`Error`; calling `Value()` on an error result is an assert.

## Rationale

A failure that a caller must handle belongs in the signature, where it cannot be
overlooked; with exceptions it is invisible at the call site and the compiler
has nothing to say about it. The UI thread makes this sharper: a frame being
rendered has no meaningful place to unwind to, and the honest response to "this
file could not be read" is to show it in a panel, not to abandon the frame.

## Consequences

### Positive

- The set of things that can go wrong is readable from the header.
- `[[nodiscard]]` plus warnings-as-errors turns an ignored failure into a build
  break rather than a runtime surprise.
- Errors are values, so tests assert on them directly — which is why the `Sln`
  and `Fs` failure paths have test coverage at all.

### Negative

- Every call site handles or propagates by hand. There is no `TRY`-style macro
  in this codebase, so a five-call chain is five `if (!result)` blocks, and the
  code that does the work is outnumbered by the code that checks it.
- `Result<T>` composes poorly through deep call stacks: an error raised four
  levels down is threaded back up through four signatures that would otherwise
  have nothing to say about it.
- Constructors cannot return one. A type that can fail to build needs a static
  factory returning `Result<T>`, which is a shape the language fights.
- It departs from E.2 and E.3, so a reader who knows the guidelines and not this
  record will read the codebase as wrong.

### Neutral

- `Error` carries a message string, so a failure allocates. Irrelevant at the
  rate errors actually occur here.
- The `ErrorCode` set is small and closed. Widening it is cheap and is not a
  decision that needs a record.

## Alternatives Considered

### Exceptions, as the C++ Core Guidelines recommend

The mainstream position, and the one E.2 and E.3 argue for: the happy path
stays uncluttered, errors propagate without every intermediate frame naming
them, and constructors can fail. Rejected because the two properties that
matter most here are the ones exceptions give up — a fallible operation being
visible in its signature, and a compiler-enforced obligation to handle it. The
UI-thread argument reinforces it: unwinding out of a half-drawn frame is not a
recovery, it is an abandonment.

### Error codes without a value wrapper

Return an `ErrorCode` and write the result through an out-parameter. Simplest
possible mechanism, no templates. Rejected: it separates the value from the
verdict, so a caller can read an uninitialised out-parameter after a failure,
and the return value is trivially ignorable — exactly the mistake `[[nodiscard]]`
exists to stop.

### `std::expected`

The same shape as `Result<T>`, standardised. It is C++23; this project targets
C++20 by [0005](0005-target-c-20-with-headers-not-modules.md), so it was not
available. Worth revisiting when the language floor moves — that would be a new
record superseding this one, not an edit to it.

## Related

- [0001](0001-record-architecture-decisions.md)
- [0002](0002-keep-core-free-of-ide-knowledge.md) — the boundary these errors
  cross.
- [0005](0005-target-c-20-with-headers-not-modules.md) — why `std::expected` was
  not on the table.
- [0537](0537-mark-result-nodiscard-on-the-type-not-on-functions.md) — amends
  this record: the attribute sits on the type, and a deliberate discard says why.
