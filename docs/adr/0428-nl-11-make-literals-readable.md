# 0428. NL.11: Make literals readable

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

A long run of digits is unreadable and easy to mistype, and nothing in the
language checks it. Digit separators break the run into groups the eye can
count:

```cpp
auto c = 299'792'458; // m/s2
auto q2 = 0b0000'1111'0000'0000;
auto ss_number = 123'456'7890;
```

Literal suffixes say what a literal is where the bare form would be ambiguous:

```cpp
auto hello = "Hello!"s; // a std::string
auto world = "world";   // a C-style string
auto interval = 100ms;  // using <chrono>
```

Upstream adds the obvious caveat: literals should not be sprinkled all over the
code as magic constants in the first place. But making them readable where they
are defined is still worthwhile, precisely because it is so easy to make a typo
in a long string of integers.

## Decision

Write literals so a reader can check them.

- Group long digit sequences with `'` separators: thousands for decimal, four
  bits or a byte at a time for binary and hexadecimal, whichever matches what
  the number means.
- Use a literal suffix when the bare literal's type is not what a reader would
  assume, or when the unit matters — `100ms` rather than `100`, `"x"s` rather
  than `"x"` where a `std::string` is meant.
- Do not scatter literals through the code in the first place. Give a value a
  name at one definition point, and apply this rule there, where the digits are
  actually read.
- Group by meaning, not by habit. The grouping is a claim about the number's
  structure and should be one a reader can verify.

## Rationale

The digits in a long literal are the one part of a program that cannot be
checked by anything: the compiler accepts any value, the type system accepts any
value, and a transposition inside `299792458` produces a perfectly valid
constant that is simply wrong. Grouping does not check it either, but it makes
the number countable by eye, which is the only verification available.

## Consequences

### Positive

- A transposed or dropped digit becomes visible, because the groups stop having
  equal length.
- A suffix records a unit or a type at the point where the value is written,
  where a comment would have to be maintained separately.
- Applying the rule at a single named definition, rather than at each use,
  bounds the cost.

### Negative

- Separators can lie. `123'456'7890` above is grouped inconsistently and reads
  as a mistake even where the grouping is deliberate, so a wrong grouping
  actively misleads where none would merely have been dull.
- Suffixes such as `s` and `ms` require the corresponding literal operators to be
  visible, which usually means a `using namespace` directive of some kind — a
  cost paid in namespace hygiene for a gain in readability.
- The rule is close to unenforceable at its edges: upstream itself cannot say
  what counts as a long digit sequence, and guesses at seven.
- Grouping is untyped decoration. It says nothing about whether the value is
  correct, so it can make a wrong constant look carefully considered.

### Neutral

- Nothing here decides where constants live; that is
  [0202](0202-es-45-avoid-magic-constants-use-symbolic-constants.md)'s subject,
  and this rule applies wherever they end up.
- The comment `// m/s2` in upstream's example is a slip — the speed of light is
  in metres per second — and is transcribed only because the example is quoted
  verbatim.

## Enforcement

Flag long digit sequences. The difficulty is defining "long"; upstream suggests
around seven digits.

## Related

- [0202](0202-es-45-avoid-magic-constants-use-symbolic-constants.md) — name the
  constant first, then make its literal readable.
- [0193](0193-es-31-dont-use-macros-for-constants-or-functions.md) — a named
  constant is a `constexpr` object, not a macro.
- [0433](0433-nl-19-avoid-names-that-are-easily-misread.md) — the same concern
  about characters that are hard to tell apart, applied to identifiers.
- [0421](0421-nl-3-keep-comments-crisp.md) — a suffix or a separator often does
  the job a comment was about to be written for.
