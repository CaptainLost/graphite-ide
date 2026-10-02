# 0465. F.46: int is the return type for main()

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

That `main` returns `int` is a language rule, but it is violated through
"language extensions" often enough to be worth mentioning. Declaring `main` —
the one global `main` of a program — as `void` limits portability.

```cpp
void main() { /* ... */ };  // bad, not C++
```

```cpp
int main()
{
    std::cout << "This is the way to do it\n";
}
```

The rule is mentioned only because of the persistence of this error in the
community. Note that despite its non-`void` return type, the `main` function
does not require an explicit `return` statement; falling off the end of `main`
is defined to return zero.

## Decision

Declare the program entry point as `int main()` or
`int main(int argc, char* argv[])`.

- Never declare `main` as `void`, whatever a compiler extension permits.
- Use no other parameter list. `main` has exactly the two standard forms; a
  platform's additional form, such as an environment-pointer third parameter, is
  an extension.
- Return zero, or `EXIT_SUCCESS`, for success and a non-zero value for failure,
  and let the value mean something to whatever runs the program.
- Do not add an explicit `return 0;` for its own sake, but do not remove one
  that documents the success path of a function with several exits.
- Where a platform imposes a different entry point, such as `wWinMain` on
  Windows, treat it as a platform boundary and keep it thin, so that the
  portable program is not written in terms of it.

## Rationale

This is a rule about staying inside the standard, which
[0008](0008-p-2-write-in-iso-standard-c.md) already requires; the reason it gets
its own record is that the violation is invited by tooling rather than chosen.
Compilers accept `void main` and then the code does not move.

The cost of compliance is one keyword, and the benefit is that the exit status
remains available at all — a program whose `main` returns `void` cannot tell a
shell, a test runner, or a build system that it failed, which is the one thing
a process is always able to say.

## Consequences

### Positive

- The program is portable to any conforming implementation without an edit.
- The exit status stays usable, so the program composes with scripts, CI, and
  process supervisors.
- The rule is trivially checkable, and any conforming compiler enforces it for
  free.

### Negative

- The record is nearly content-free for a codebase that has never had the
  problem, so it is a rule whose upkeep cost is not obviously repaid.
- It does not settle the harder adjacent questions — what the non-zero values
  mean, whether they are documented anywhere, whether a failing path exits or
  returns — and a correct `int main` that always returns zero is no better than
  `void main` in practice.
- Platform entry points such as `WinMain` are outside the rule's reach, so on
  Windows the actual entry point may not be a `main` at all and the guidance
  applies only by analogy.

### Neutral

- Omitting the final `return` from `main` is permitted and idiomatic; this is
  the sole function in the language for which that is true.
- Whether `EXIT_SUCCESS` or `0` is written is a style choice with no behavioural
  difference.

## Enforcement

- The compiler should do it.
- If the compiler does not do it, let tools flag it.

## Related

- [0008](0008-p-2-write-in-iso-standard-c.md) — the general rule against relying
  on compiler extensions, of which this is one instance.
- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — the case for letting
  a tool catch this rather than a reviewer.
