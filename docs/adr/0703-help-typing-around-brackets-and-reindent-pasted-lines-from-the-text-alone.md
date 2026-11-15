# 0703. Help typing around brackets and reindent pasted lines from the text alone

Status: proposed
Date: 2026-11-15

## Context

The editor typed exactly what was pressed. No bracket got its partner. Enter copied the line's whole indentation,
even with the caret inside it, so Enter at the start of an indented line doubled that line's indentation. Tab over a
selection inside one line indented the line rather than replacing the selection as typing would. A paste went in
as copied, so lines copied from one depth and pasted at another kept their old indentation.

Only C# has a syntax tree, and it is parsed off the UI thread; C++ has a line lexer. A key press has to be answered
in the frame it arrives in, from the text as it is now.

## Decision

- Put the typing help in Core (`Core/Text/Typing`, `InsertNewline`, `Indent`), working on the buffer's text with no
  language and no syntax tree, for every file type.
- An opening bracket `( [ {` gets its partner when what follows is whitespace, the line's end or one of
  `; : . , = ) ] } >`. A quote `" '` gets its partner under the same rule, and not after a letter, a digit or `_`,
  not after the same quote, and not inside an open string of its kind (an odd count of that quote, escapes left
  out, before the caret on the line).
- A closing bracket typed in front of the same bracket steps over it; a quote steps over the quote that ends its
  string. A bracket or quote typed over a selection goes around it and keeps the text selected. Backspace between a
  bracket or quote and its partner removes both.
- A closing bracket typed on a line holding only indentation takes the indentation of the line holding its opening
  partner. The partner is found by walking back up to 2000 lines and counting brackets outside quotes and `//`
  comments.
- Enter: inside the indentation, the line is only split. After text, the new line takes the line's indentation, one
  level deeper when the text before the caret ends in an opening bracket; between that bracket and its partner, the
  partner goes down to a line of its own at the line's indentation. The whitespace on both sides of the caret goes.
  A new level follows the indentation the line already has (a tab after tabs, spaces after spaces) and the setting
  only when the line has none.
- Tab: a selection inside one line that leaves some of its text out is replaced by an indent. A selection holding
  the whole of the line's text, or reaching another line, indents the lines. Spaces run to the next tab stop.
- A paste of several lines is reindented (`ReindentPasted`). The shallowest line moves to the landing indentation and
  the rest keep their depth relative to it. At a line's indentation the first line moves with the rest. After text,
  it continues the line as copied. An empty line takes the indentation the line above gives a new line. A paste is
  one undo step.

## Rationale

Text alone answers in the frame, works the same for every language and needs no tree that might be a few edits
behind. The heuristics fail in a small way when they fail: a bracket inside a block comment or a raw string costs a
closing brace its indentation, or a quote its partner. They never change text the user did not touch.

## Consequences

### Positive

- C++, C#, JSON, and anything else with brackets get pairs, Enter between braces, and an electric closing brace
  without a grammar.
- Pasted blocks land at the depth they are pasted at.

### Negative

- Strings and comments are guessed. `/* { */`, a C# verbatim or raw string, or an apostrophe inside a comment can
  make the help pair or reindent wrongly. The syntax tree could answer these, but only after a delay.
- No setting turns it off. Someone who dislikes paired brackets has no way out yet.
- A paste after text moves the lines under the first to the line's indentation. A continuation copied deeper than
  its first line, such as call arguments, loses that extra depth.

### Neutral

- Overtyping does not remember which closers the help inserted, so typing `)` before any `)` steps over it.

## Alternatives Considered

- **Use the syntax tree and the lexer's tokens to skip strings and comments.** Exact where a tree exists, but C#'s
  tree is parsed in the background, so the answer could lag the keystroke, and every other language would still
  need the text-only path.
- **Ask the language server for on-type formatting and range formatting.** Gives each language its own rules, but
  only with a server running and after a round trip. It also rewrites more than the user typed. It can still be added
  as a format command on top.
- **Leave paste as copied, as before.** Exact, but it is what prompted the change.

## Related

- [0705](0705-give-a-c-interpolation-s-braces-a-token-kind-of-their-own.md)
