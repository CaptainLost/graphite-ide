# 0045. F.6: If your function must not throw, declare it noexcept

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

`noexcept` appears 77 times across 31 files, mostly on accessors and on
`Result<T>`'s observers. The rule matters more here than in a typical codebase,
because [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md)
already says exceptions do not cross a module boundary — `noexcept` is where
that intent becomes a compiler-visible guarantee rather than a convention.

## Decision

Adopt F.6 as written. Transcribed in full:

> **Reason.** If an exception is not supposed to be thrown, the program cannot be
> assumed to cope with the error and should be terminated as soon as possible.
> Declaring a function `noexcept` helps optimizers by reducing the number of
> alternative execution paths. It also speeds up the exit after failure.

**Example.** Put `noexcept` on every function written completely in C or in any
other language without exceptions. The C++ Standard Library does that implicitly
for all functions in the C Standard Library.

**Note.** `constexpr` functions can throw when evaluated at run time, so you
might need conditional `noexcept` for some of those.

**Example.** You can use `noexcept` even on functions that can throw:

```cpp
vector<string> collect(istream& is) noexcept
{
    vector<string> res;
    for (string s; is >> s;)
        res.push_back(s);
    return res;
}
```

If `collect()` runs out of memory, the program crashes. Unless the program is
crafted to survive memory exhaustion, that might be just the right thing to do;
`terminate()` might generate suitable error log information (but after memory
runs out it is hard to do anything clever).

**Note.** You must be aware of the execution environment that your code is
running when deciding whether to tag a function `noexcept`, especially because
of the issue of throwing and allocation. Code that is intended to be perfectly
general (like the standard library and other utility code of that sort) needs to
support environments where a `bad_alloc` exception could be handled
meaningfully. However, most programs and execution environments cannot
meaningfully handle a failure to allocate, and aborting the program is the
cleanest and simplest response to an allocation failure in those cases. If you
know that your application code cannot respond to an allocation failure, it
could be appropriate to add `noexcept` even on functions that allocate.

Put another way: In most programs, most functions can throw (e.g., because they
use `new`, call functions that do, or use library functions that report failure
by throwing), so don't just sprinkle `noexcept` all over the place without
considering whether the possible exceptions can be handled.

`noexcept` is most useful (and most clearly correct) for frequently used,
low-level functions.

**Note.** Destructors, swap functions, move operations, and default constructors
should never throw. See also C.44.

**Note.** Care must be taken on base virtual functions and functions part of a
public interface because declaring a function `noexcept` is establishing a
guarantee that all current and future implementations must abide by. For virtual
function, all overriders must also be `noexcept` and removing `noexcept` from a
function could break calling functions.

**Enforcement.**

- (hard) Flag low-level functions that are not `noexcept`, yet cannot throw.
- Flag throwing swap, move, destructors, and default constructors.

## Rationale

Adopted as written. The rule's central caution — do not sprinkle it — is the one
this codebase has to hold onto, because
[0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md)
makes `noexcept` look universally applicable when it is not. A function that
returns `Result<T>` still allocates: it builds strings, grows vectors, and reads
files. Those can throw `bad_alloc`, and marking such a function `noexcept` turns
an allocation failure into `std::terminate`.

This project accepts that trade in the shape the rule's Note describes: an IDE
cannot meaningfully recover from memory exhaustion, so terminating is a
defensible response. But it is a decision to make per function, not a blanket
one, and this record does not authorise adding `noexcept` to everything that
returns `Result<T>`.

The last Note is the one that binds hardest here. `Window`, `Panel`,
`SolutionEditor` and `ProjectContentReader` are virtual interfaces; `noexcept`
on a virtual function is a promise every future implementation must keep, and
withdrawing it is a breaking change. Interface virtuals are therefore left
without `noexcept` unless the guarantee is genuinely permanent.

## Consequences

### Positive

- Existing use — accessors, observers, `Result<T>`'s `HasValue` and
  `operator bool` — is exactly the "frequently used, low-level functions" case
  the rule calls most clearly correct.
- Gives a written reason not to add `noexcept` to virtual interface functions,
  which would otherwise look like an omission.

### Negative

- The rule requires a judgement per function about whether allocation failure
  can be handled, and that judgement is invisible in the code afterwards — a
  `noexcept` that was thought about looks identical to one that was not.
- Getting it wrong is severe: a throw from a `noexcept` function is
  `std::terminate`, with no unwinding and no chance to save the user's work.

### Neutral

- The first enforcement point is marked hard and nothing here runs it. Whether
  a low-level function that cannot throw is missing `noexcept` is unchecked.
- Destructors and move operations are implicitly `noexcept` in the common cases,
  so the second enforcement point mostly passes without effort.

## Related

- [0044](0044-f-5-if-a-function-is-very-small-and-time-critical-declare-it-inline.md)
  — the previous rule.
- [0046](0046-f-7-for-general-use-take-t-or-t-arguments-rather-than-smart-pointers.md)
  — the next rule.
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  why this rule looks more applicable here than it is.
