# 0042. F.3: Keep functions short and simple

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

The rule that F.1 and F.2 defer their enforcement to, and the one with a number
attached: sixty lines. A scan of Core against that threshold finds several
functions well past it, including one of 214 lines.

## Decision

Adopt F.3 as written, with its sixty-line threshold as the working figure.
Transcribed in full:

> **Reason.** Large functions are hard to read, more likely to contain complex
> code, and more likely to have variables in larger than minimal scopes.
> Functions with complex control structures are more likely to be long and more
> likely to hide logical errors.

**Example.** Consider:

```cpp
double simple_func(double val, int flag1, int flag2)
    // simple_func: takes a value and calculates the expected ASIC output,
    // given the two mode flags.
{
    double intermediate;
    if (flag1 > 0) {
        intermediate = func1(val);
        if (flag2 % 2)
             intermediate = sqrt(intermediate);
    }
    else if (flag1 == -1) {
        intermediate = func1(-val);
        if (flag2 % 2)
             intermediate = sqrt(-intermediate);
        flag1 = -flag1;
    }
    if (abs(flag2) > 10) {
        intermediate = func2(intermediate);
    }
    switch (flag2 / 10) {
    case 1: if (flag1 == -1) return finalize(intermediate, 1.171);
            break;
    case 2: return finalize(intermediate, 13.1);
    default: break;
    }
    return finalize(intermediate, 0.);
}
```

This is too complex. How would you know if all possible alternatives have been
correctly handled? Yes, it breaks other rules also.

We can refactor:

```cpp
double func1_muon(double val, int flag)
{
    // ???
}

double func1_tau(double val, int flag1, int flag2)
{
    // ???
}

double simple_func(double val, int flag1, int flag2)
    // simple_func: takes a value and calculates the expected ASIC output,
    // given the two mode flags.
{
    if (flag1 > 0)
        return func1_muon(val, flag2);
    if (flag1 == -1)
        // handled by func1_tau: flag1 = -flag1;
        return func1_tau(-val, flag1, flag2);
    return 0.;
}
```

**Note.** "It doesn't fit on a screen" is often a good practical definition of
"far too large." One-to-five-line functions should be considered normal.

**Note.** Break large functions up into smaller cohesive and named functions.
Small simple functions are easily inlined where the cost of a function call is
significant.

**Enforcement.**

- Flag functions that do not "fit on a screen." How big is a screen? Try 60
  lines by 140 characters; that's roughly the maximum that's comfortable for a
  book page.
- Flag functions that are too complex. How complex is too complex? You could use
  cyclomatic complexity. Try "more than 10 logical paths through." Count a
  simple `switch` as one path.

## Rationale

Adopted with the sixty-line figure, because a rule with a number can be checked
and a rule without one cannot. The column half of the threshold is already
enforced: `.clang-format` sets `ColumnLimit: 110`.

The refactored example is worth noting for what it does *not* do: it does not
make the code shorter overall. It makes each piece separately readable. That is
the trade being accepted.

The functions listed below are not scheduled for a rewrite. Several are long for
a defensible reason — `ParseSln` is a single-pass parser whose states are
genuinely sequential, and splitting it would spread one algorithm across five
functions that only make sense together. The inventory records the fact; the fix
happens when the surrounding code is touched, as with
[0006](0006-require-braces-around-every-control-flow-body.md).

## Consequences

### Positive

- A checkable number, which makes this one of the few rules in the directory
  that a tool could enforce outright.
- The complexity half gives a second, independent test for functions that are
  short but tangled.

### Negative

- Splitting a long function often converts local variables into parameters, and
  a five-parameter helper trades one violation for another — see
  [0034](0034-i-23-keep-the-number-of-function-arguments-low.md).
- The threshold is arbitrary at the margin: a 65-line function is not worse than
  a 55-line one, and treating the number as a gate invites cosmetic splits.
- Nothing measures cyclomatic complexity here, so half the enforcement is
  unavailable.

### Neutral

- The 140-column half of the threshold is stricter in this repository already,
  at 110 columns, and is enforced by the formatter.

## Deviations to fix

Functions over the sixty-line threshold, measured against the tree at the time
of writing:

| Function | Where | Lines |
|---|---|---|
| `ParseSln` | `Core/src/Core/Sln/SlnParser.cpp:103` | 214 |
| `ImGuiRenderer::Init` | `Core/src/Core/Gui/ImGuiRenderer.cpp:53` | 142 |
| `TextDocument::FromContent` | `Core/src/Core/Text/TextDocument.cpp:93` | 116 |
| `DocumentSet` — the block at `:76` | `Core/src/Core/Text/DocumentSet.cpp:76` | 95 |
| `VcxprojReader` — the block at `:138` | `Core/src/Core/Sln/VcxprojReader.cpp:138` | 88 |
| `TextDocument` — the block at `:211` | `Core/src/Core/Text/TextDocument.cpp:211` | 87 |
| `Application::Run` | `Core/src/Core/App/Application.cpp:41` | 80 |
| `FileDialog` — the block at `:60` | `Core/src/Core/Platform/FileDialog.cpp:60` | 72 |
| `Encoding` — the block at `:79` | `Core/src/Core/Text/Encoding.cpp:79` | 70 |
| `DockSpaceHost` — the block at `:24` | `Core/src/Core/Gui/DockSpaceHost.cpp:24` | 67 |
| `DirectoryWatcher` — the block at `:150` | `Core/src/Core/Fs/DirectoryWatcher.cpp:150` | 210 |
| `SlnxEditor` — the block at `:509` | `Core/src/Core/Sln/SlnxEditor.cpp:509` | 65 |

The three named functions at the top were confirmed by reading their signatures
and closing braces. The remaining entries come from a brace-matching scan and
give the block, not a verified function boundary; treat their line counts as
approximate until checked.

## Related

- [0041](0041-f-2-a-function-should-perform-a-single-logical-operation.md) — the
  previous rule; `Application::Run` fails both.
- [0043](0043-f-4-if-a-function-might-have-to-be-evaluated-at-compile-time-declare-it-constexpr.md)
  — the next rule.
- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — the analyser that
  would make this rule enforceable.
