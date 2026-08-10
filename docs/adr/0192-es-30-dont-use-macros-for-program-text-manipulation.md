# 0192. ES.30: Don't use macros for program text manipulation

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Macros are a major source of bugs. They do not obey the usual scope and type
rules. They ensure that the human reader sees something different from what the
compiler sees. And they complicate tool building.

```cpp
#define Case break; case   /* BAD */
```

This innocuous-looking macro turns a single lower-case `c` instead of a `C` into
a flow-control bug: a `case` written by hand no longer breaks, and the code that
reads as identical to its neighbours behaves differently. Nothing at the use
site indicates that a substitution is happening.

The rule does not ban the use of macros for configuration control in `#ifdef`
and similar. It is also meant to discourage `#` for stringification and `##` for
concatenation. As with macros generally, some uses are mostly harmless, but even
those create problems for tools such as auto-completers, static analysers and
debuggers. Often the desire to use fancy macros is a sign of an overly complex
design, and `#` and `##` encourage the definition and use of more macros:

```cpp
#define CAT(a, b) a ## b
#define STRINGIFY(a) #a

void f(int x, int y)
{
    string CAT(x, y) = "asdf";   // BAD: hard for tools to handle (and ugly)
    string sx2 = STRINGIFY(x);
    // ...
}
```

Workarounds exist for low-level string manipulation:

```cpp
string s = "asdf" "lkjh";   // ordinary string literal concatenation

enum E { a, b };

template<int x>
constexpr const char* stringify()
{
    switch (x) {
    case a: return "a";
    case b: return "b";
    }
}

void f(int x, int y)
{
    string sx = stringify<x>();
    // ...
}
```

That is not as convenient as a macro to define, but it is as easy to use, has
zero overhead, and is typed and scoped. Two language developments are expected
to remove the remaining reasons for the preprocessor: modules for configuration
control, and static reflection for program text manipulation.

## Decision

Do not generate program text with the preprocessor.

- Do not define a macro that expands to a keyword, an operator, a partial
  statement, or any fragment that changes how surrounding code parses.
- Do not use `#` to stringify or `##` to build identifiers. Use ordinary string
  literal concatenation, a `constexpr` function, or a template returning a name.
- Do not use a macro to declare entities whose names are assembled from its
  arguments; a reader cannot search for a name that does not appear in the
  source.
- Configuration control with `#ifdef`, `#if defined(...)` and include guards
  remains permitted; that is not what this rule is about.
- Where a third-party header does this, do not extend the pattern into code you
  control; wrap it at the boundary per
  [0039](0039-i-30-encapsulate-rule-violations.md).

## Rationale

The specific harm here is a divergence between the text a human reads and the
text a compiler compiles, and everything else follows from it. A reviewer
reviews the source; a debugger steps the expansion; an analyser sees the
expansion; a search tool sees the source. Once a name is assembled by `##`, no
tool and no reader can find its definition by looking for it. The `Case` macro
is the compact demonstration: correct-looking code, one character from a
flow-control bug, no diagnostic anywhere.

## Consequences

### Positive

- What is written is what is compiled, so review, search, navigation and
  debugging all agree.
- Names remain findable, which is the property that makes a codebase navigable
  at all.
- Replacements — `constexpr`, templates, literal concatenation — are typed and
  scoped, and cost nothing at run time.

### Negative

- The typed replacements are more work to write. A `constexpr` stringify
  function is several lines where `#a` is two characters, and it must be
  extended for each new case.
- Some things the preprocessor does have no replacement in C++20. Reproducing
  the text of a failed assertion's condition requires `#`, since only the
  preprocessor sees the expression as text; generating parallel enum/name
  tables is painful without reflection. (Capturing the call site is not one of
  these: `std::source_location` covers what `__FILE__` and `__LINE__` were
  used for.)
- The boundary between "program text manipulation" and "configuration control"
  is not sharp; a macro that selects between two implementations sits on it.
- The upstream escape routes are future language features, which is no help to
  code being written now.

### Neutral

- `#ifdef` and friends for configuration are explicitly outside this rule.
- The rule is about the preprocessor's use, not about whether a given macro is
  well written; a well-written text-manipulation macro is still in scope.

## Enforcement

Scream when you see a macro that is not being used for source control, for
example `#ifdef`.

## Related

- [0193](0193-es-31-dont-use-macros-for-constants-or-functions.md) — the same
  argument for the other common use of macros.
- [0194](0194-es-32-use-all-caps-for-all-macro-names.md) — how the macros that
  survive this rule are named.
