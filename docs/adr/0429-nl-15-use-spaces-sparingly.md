# 0429. NL.15: Use spaces sparingly

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

Too much space makes the text larger and distracts.

```cpp
#include < map >
int main(int argc, char * argv [ ])
{
    // ...
}
```

```cpp
#include <map>
int main(int argc, char* argv[])
{
    // ...
}
```

The spaces in the first version separate tokens that belong together. `< map >`
splits a header name; `char * argv [ ]` splits a type into four pieces, so the
reader has to reassemble it. Upstream notes that some IDEs have their own
opinions and add distracting space of exactly this kind, and it balances the
rule with the observation that well-placed whitespace is a significant help to
readability — the instruction is not to overdo it, not to remove it.

This is marked as a recommendation for when there are no constraints or better
ideas, added after many requests for guidance.

## Decision

Space tokens to show what belongs together, and no more.

- Bind a declarator to its type rather than to its name: `char* argv[]`.
- Do not pad brackets, parentheses, angle brackets or subscripts with interior
  spaces, and do not split a header name inside `<>`.
- Keep the spaces that separate genuinely separate things — around binary
  operators, after commas, and between a control-flow keyword and its
  parenthesis.
- If an editor inserts space of its own, correct it rather than committing it.

## Rationale

Whitespace is the reader's grouping cue, and a space inside a construct
contradicts the grouping the grammar already imposes — `argv [ ]` invites the
eye to parse a subscript as three things when it is one. The rule is therefore
not about density for its own sake; it is about not using the strongest visual
separator available to split something indivisible.

## Consequences

### Positive

- Declarations read as a type followed by a name, which is what they are.
- Lines stay shorter, so more of a function fits on screen.
- The spacing rule is mechanical enough for a formatter to apply, so it need not
  be argued about case by case.

### Negative

- Binding `*` to the type produces `char* a, b;`, where `b` is not a pointer,
  and the spacing actively supports the misreading. That is one of the reasons
  [0435](0435-nl-21-declare-one-name-only-per-declaration.md) exists, and it is
  a genuine cost of this spelling.
- "Sparingly" has no definition, so beyond the clear cases the rule cannot
  decide anything; the remainder is preference dressed as guidance.
- Removing space is the kind of change that touches many lines for no
  behavioural gain, so applying the rule to existing code buries substantive
  diffs.

### Neutral

- Vertical space — blank lines between logical blocks — is not what this rule is
  about, and well-placed whitespace is explicitly valued.
- Aligning consecutive declarations or trailing comments into columns is a
  separate question that this rule does not settle.

## Related

- [0432](0432-nl-18-use-c-style-declarator-layout.md) — the same `*` and `&`
  placement question, argued from types rather than from density.
- [0431](0431-nl-17-use-k-r-derived-layout.md) — the rest of the layout
  question, offered in the same spirit as a suggestion.
- [0422](0422-nl-4-maintain-a-consistent-indentation-style.md) — horizontal
  space at the start of a line rather than within a construct.
- [0435](0435-nl-21-declare-one-name-only-per-declaration.md) — the rule that
  contains the damage from binding `*` to the type.
