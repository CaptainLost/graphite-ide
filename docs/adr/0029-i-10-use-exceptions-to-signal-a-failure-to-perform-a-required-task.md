# 0029. I.10: Use exceptions to signal a failure to perform a required task

Status: rejected
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The first rule in this directory that is transcribed and then declined. This
repository decided the opposite in
[0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md):
fallible operations return `core::Result<T>` and exceptions do not cross a
module boundary.

The status here is `rejected` rather than `accepted` — the rule is recorded so
that the departure is visible and argued, not so that it is followed.

## Decision

Do not adopt I.10. Transcribed in full so that what is being declined is on the
record:

> **Reason.** It should not be possible to ignore an error because that could
> leave the system or a computation in an undefined (or unexpected) state. This
> is a major source of errors.

**Example.**

```cpp
int printf(const char* ...);    // bad: return negative number if output fails

template<class F, class ...Args>
// good: throw system_error if unable to start the new thread
explicit thread(F&& f, Args&&... args);
```

**Note.** What is an error?

An error means that the function cannot achieve its advertised purpose
(including establishing postconditions). Calling code that ignores an error
could lead to wrong results or undefined systems state. For example, not being
able to connect to a remote server is not by itself an error: the server can
refuse a connection for all kinds of reasons, so the natural thing is to return
a result that the caller should always check. However, if failing to make a
connection is considered an error, then a failure should throw an exception.

**Exception.** Many traditional interface functions (e.g., UNIX signal handlers)
use error codes (e.g., `errno`) to report what are really status codes, rather
than errors. You don't have a good alternative to using such, so calling these
does not violate the rule.

Alternative: If you can't use exceptions (e.g., because your code is full of
old-style raw-pointer use or because there are hard-real-time constraints),
consider using a style that returns a pair of values:

```cpp
int val;
int error_code;
tie(val, error_code) = do_something();
if (error_code) {
    // ... handle the error or exit ...
}
// ... use val ...
```

This style unfortunately leads to uninitialized variables. Since C++17 the
"structured bindings" feature can be used to initialize variables directly from
the return value:

```cpp
auto [val, error_code] = do_something();
if (error_code) {
    // ... handle the error or exit ...
}
// ... use val ...
```

**Note.** We don't consider "performance" a valid reason not to use exceptions.

- Often, explicit error checking and handling consume as much time and space as
  exception handling.
- Often, cleaner code yields better performance with exceptions (simplifying the
  tracing of paths through the program and their optimization).
- A good rule for performance critical code is to move checking outside the
  critical part of the code.
- In the longer term, more regular code gets better optimized.
- Always carefully measure before making performance claims.

See also: I.5 and I.7 for reporting precondition and postcondition violations.

**Enforcement.**

- (Not enforceable) This is a philosophical guideline that is infeasible to
  check directly.
- Look for `errno`.

## Rationale

The rule's stated goal — an error that cannot be ignored — is one this project
accepts entirely. It is the mechanism that is declined, because `Result<T>` is
`[[nodiscard]]` and the build uses `fatalwarnings { "All" }`, so ignoring a
failure is a build error rather than a silent drop. The property the rule wants
is met by other means.

Two of the rule's own passages support the departure more than they oppose it:

- Its Note defining an error draws the line at "cannot achieve its advertised
  purpose" and gives the connection-refused case as one where returning a
  checked result is "the natural thing". Nearly every failure in this codebase
  is of that kind: a file that is not there, a `.slnx` that does not parse, a
  directory that cannot be watched. These are expected outcomes of asking about
  a filesystem the user controls, not violated contracts.
- Its Alternative anticipates a value-plus-error return, and `Result<T>` is that
  alternative in its better form — a `variant`, so there is no uninitialized
  value and no way to read the value on the error path.

Where the rule and this project genuinely disagree is the performance Note, and
this record does not rest on it. Performance was not the reason; the reasons are
in [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md)
— visibility in the signature, and a UI thread with nowhere to unwind to.

## Consequences

### Positive

- The departure is written down where a reader who knows the guidelines will
  look for it, instead of being discovered as an apparent mistake.
- Naming the rule's goal separately from its mechanism keeps the obligation:
  errors here must still be impossible to ignore.

### Negative

- This codebase is idiosyncratic to a C++ programmer who follows the
  guidelines, and that cost is paid at every reading, not once.
- Declining the rule means declining what it buys: constructors that can fail,
  and error propagation that does not need every intermediate signature to
  mention it.
- Third-party code still throws. Exceptions must be caught and converted at each
  boundary, and nothing checks that this was actually done.

### Neutral

- `errno` is not read anywhere, so the one mechanical enforcement point passes
  trivially.
- If the language floor moves to C++23, `std::expected` would make this
  departure more conventional rather than less — see
  [0005](0005-target-c-20-with-headers-not-modules.md).

## Related

- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  the decision this rule loses to.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — the previous rule.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the next rule.
- [0020](0020-i-1-make-interfaces-explicit.md) — where the same substitution is
  made for "throw an exception".
