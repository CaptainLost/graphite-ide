# 0683. Clear libgit2's last error by hand before a call whose failure is judged

Status: accepted
Date: 2026-11-08

## Context

`IsMissingObject(int)` tells a missing object (a blobless clone) from any other
failure by the result code and, failing that, by the class of libgit2's last
error on the thread. libgit2 leaves that error in place when a call succeeds,
so a call whose failure is judged must be preceded by `git_error_clear()`, or an
earlier, harmless miss reads as its own. About twenty places in `Core/Git` do
this by hand. The architecture review of 2026-11-08 proposed one helper that
clears, calls and maps the failure to an `Error`, so the rule would hold by
construction.

## Decision

Keep clearing by hand, right before each call whose failure is passed to
`IsMissingObject` or `ObjectReadFailure`. Do not wrap the calls.

## Rationale

The clear has to come before the call, so a helper that guarantees it has to
make the call itself: either a template taking the call, which
[0563](0563-write-concrete-code-and-keep-templates-for-vocabulary-types.md)
rules out, or a `std::function` lambda around every libgit2 call, which reads
worse than the line it replaces. The places that judge a failure then do
different things with it — answer Unknown, return NotFound, skip a tag — so the
helper would hand back the result code anyway. What a missed clear costs is
narrow: a failure that sets no error of its own after an earlier ODB miss is
reported as NotFound rather than IoFailure.

## Consequences

### Positive

- Every libgit2 call reads as itself, with the clear beside it.

### Negative

- A new judged call without its clear compiles and passes until an earlier miss
  on the same thread meets a failure that sets no error.

### Neutral

- `IsMissingObject`'s comment in `Libgit2Support.h` stays the place the rule is
  written.

## Alternatives Considered

- **Clear after reading an error instead of before a call.** Leaves the errors of
  failures handled elsewhere in place, so it does not make the rule hold either.

## Related

- [0563](0563-write-concrete-code-and-keep-templates-for-vocabulary-types.md) is
  why the calls are not wrapped.
