# 0407. SL.io.50: Avoid endl

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

The `endl` manipulator is mostly equivalent to `'\n'` and `"\n"`; as most
commonly used it simply slows down output by doing redundant `flush()`es. This
slowdown can be significant compared to `printf`-style output.

```cpp
cout << "Hello, World!" << endl;    // two output operations and a flush
cout << "Hello, World!\n";          // one output operation and no flush
```

For `cin`/`cout` and equivalent interaction there is no reason to flush, because
that is done automatically. For writing to a file there is rarely a need to
flush. For string streams, and specifically `ostringstream`, inserting an `endl`
is entirely equivalent to inserting a `'\n'` character, but even there `endl`
might be significantly slower.

`endl` does not take care of producing a platform-specific end-of-line sequence
such as `"\r\n"` on Windows. For a string stream, `s << endl` just inserts a
single character, `'\n'`. This is the misconception the manipulator's name
invites and the reason it is so widely used: many people believe it is the
portable way to end a line, and it is not.

Apart from the occasionally important issue of performance, the choice between
`'\n'` and `endl` is almost completely aesthetic.

## Decision

End a line with a newline character, not with a manipulator.

- Write `'\n'` at the end of an output expression, or include it in the string
  literal being written.
- Do not use `endl` to obtain a platform-specific line ending; it does not
  provide one, and the translation to `"\r\n"` if any is done by the stream in
  text mode regardless of which of the two is written.
- Where output genuinely must reach its destination before the next statement —
  a progress indicator, a log written before a possible crash, output consumed
  by another process in lockstep — flush deliberately with `std::flush`, so that
  the flush is visible as a decision.
- Do not use `endl` in string streams, where the flush accomplishes nothing at
  all.

## Rationale

`endl` is the wrong default because it silently does two things and its name
advertises one of them. The flush is not merely superfluous — it defeats the
stream's buffering, turning a buffered write into a system call per line, which
is exactly the cost the buffer exists to avoid.

Separating the two operations also makes the rare genuine flush legible. In a
codebase where every line ends with `endl`, no reader can tell which flushes
were intended; in one where lines end with `'\n'`, a `std::flush` means
something.

## Consequences

### Positive

- Buffered output stays buffered, which can be a large difference on
  line-oriented output.
- A deliberate flush becomes visible, because it is now the unusual thing.
- `'\n'` inside an existing string literal costs no output operation at all,
  where `endl` always costs a separate one.

### Negative

- Output that is not flushed may be lost if the program terminates abnormally,
  and that is precisely when the last few lines are most wanted; `endl` was
  accidentally providing crash-resilient logging that this rule removes.
- Interactive prompts written without a trailing newline still need an explicit
  flush, and forgetting it produces a program that appears to hang — a worse
  failure than a slow one.
- The rule contradicts an idiom taught in a great many introductory texts, so it
  is contradicted by the first example any new C++ programmer ever saw.
- The saving is invisible without measurement, so the rule reads as
  micro-optimisation to anyone who has not seen the system call count.

### Neutral

- The two forms are otherwise equivalent; upstream describes the choice as
  almost completely aesthetic once performance is set aside.
- Upstream gives no enforcement for this rule.

## Related

- [0405](0405-sl-io-3-prefer-iostreams-for-i-o.md) — the library this rule
  refines the use of.
- [0406](0406-sl-io-10-unless-you-use-printf-family-functions-call-ios-base-sync-with-stdio-false.md)
  — the other cheap removal of unnecessary I/O cost.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — a flush per line is waste of
  exactly the kind that record describes.
- [0240](0240-per-3-dont-optimize-something-thats-not-performance-critical.md) —
  the counterweight: on output that is not hot, this is a style choice.
