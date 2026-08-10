# 0422. NL.4: Maintain a consistent indentation style

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

The reasons given are readability and the avoidance of "silly mistakes". The
mistake meant is this one:

```cpp
int i;
for (i = 0; i < max; ++i); // bug waiting to happen
if (i == j)
    return i;
```

The stray semicolon ends the loop, and the layout of the following lines tells
the reader a story the compiler does not agree with. Indentation is the part of
the text a reader trusts instinctively and the compiler ignores entirely, so
when the two diverge it is always the reader who is misled.

The accompanying note is that always indenting the statement after `if (...)`,
`for (...)` and `while (...)` is usually a good idea:

```cpp
if (i < 0) error("negative argument");
if (i < 0)
    error("negative argument");
```

Enforcement is a job for a tool.

Like the rest of the NL section, this is offered as a suggestion. The section's
stated aim is consistency rather than any particular rule set, and its authors
note that most real-world projects contain code from many sources, so
standardising a single style across everything is often impossible. What the
rule asks for is that a body of code agree with itself.

## Decision

Choose one indentation style and apply it uniformly.

- Fix the indentation unit once — a width, and whether it is spaces or tabs —
  and use it for every level in the code you own.
- Indent one level for each level of nesting, and give the body of an `if`, a
  `for` or a `while` its own indented line rather than trailing it on the
  header line.
- Do not mix tabs and spaces within a file; the result depends on the viewer's
  settings and will not look the same twice.
- Keep the indentation truthful. If a line is indented as though it were inside
  a construct, it must be inside that construct.
- Match the file when editing existing code. Indentation is not the place to
  make a point about style.

## Rationale

Indentation is the reader's model of the control flow, and it is a model
nothing verifies — which is what makes an inconsistent one dangerous rather than
merely untidy. Upstream's example is the sharpest form of the argument: the
program is well-formed, the compiler is silent, and the only thing wrong is that
the layout describes a structure the code does not have. Which style is chosen
matters far less than that every line agrees with every other.

## Consequences

### Positive

- A reader can infer nesting from the left margin without counting braces.
- A structural mistake shows up as a line that does not line up, which is the
  earliest and cheapest place to catch it.
- Diffs stay small, because reformatting is not mixed into substantive changes.

### Negative

- Indentation carries no meaning to the compiler, so a consistent style makes
  layout trustworthy without making it true — the misleading example above is
  consistently indented and still wrong.
- Uniformity is only achievable over code you control. Imported sources arrive
  with their own style, and reformatting them makes every future update from
  upstream conflict.
- Without a tool the rule holds by habit, and habit varies by editor; with a
  tool, the first run reformats everything at once and produces a commit no one
  can review.
- The rule is silent on which style to pick, so it cannot settle the argument it
  is most often invoked in.

### Neutral

- Indentation width and tab policy are matters of taste, and the guidelines
  decline to legislate them.
- Whether a control-flow body is braced is a separate question; this rule only
  asks that whatever body exists be indented.

## Enforcement

Use a tool. A formatter applies an indentation style mechanically, which is the
only way a large body of code stays consistent.

## Related

- [0431](0431-nl-17-use-k-r-derived-layout.md) — the brace-placement half of
  the same layout question.
- [0434](0434-nl-20-dont-place-two-statements-on-the-same-line.md) — the other
  layout rule that keeps one line meaning one thing.
- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — a formatter is
  exactly the kind of supporting tool that rule is about.
- [0421](0421-nl-3-keep-comments-crisp.md) — another rule aimed at what the
  reader has to hold in their head.
