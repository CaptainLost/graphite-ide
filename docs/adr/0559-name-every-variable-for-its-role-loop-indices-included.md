# 0559. Name every variable for its role, loop indices included

Status: accepted
Date: 2026-10-05

## Context

[0177](0177-es-7-keep-common-and-local-names-short-and-keep-uncommon-and-non-local-names-longer.md)
and [0424](0424-nl-7-make-the-length-of-a-name-roughly-proportional-to-the-length-of-its-scope.md)
priced a name by the distance to its declaration: `i`, `p`, `s` inside a few
lines, descriptive names further out. The code followed them, and the survey
behind [0558](0558-write-code-a-junior-engineer-can-read-like-prose.md) found
about six hundred places where a short name made the reader stop: `at` holding a
word's end, `c` for a cell, `r` for a row, `it` for an entry, `ec` for an error
code, `.first` and `.second` for a path and its status. Worse were names that
said the opposite of what they held — the return code of
`git_reference_name_to_id` called `found`, so that `found == GIT_ENOTFOUND` reads
as a contradiction. In the diff, one word, "line", meant four different things:
a file's line number, an index into a hunk, a wrapped screen line and a selected
row.

Call sites hid meaning the same way. `PendingAction{Kind::AddProject, {}, path, {}}`
does not say which field `path` fills; `RequestOpen(path, true)` does not say
what `true` asks for; a function returning `std::pair<std::size_t, bool>` leaves
the caller to remember which half is which.

## Decision

Every name states the role of what it names, whatever its scope.

- **No one-letter names and no abbreviations**, loop indices included:
  `lineIndex`, not `i`; `wordEnd`, not `at`; `error`, not `ec`; `onFinished`, not
  `cb`; `entry` or what the entry is, not `it`.
- **The exception is geometry**, where the conventional name is the role: `x`,
  `y`, `width`, `height`, `left`, `top`, `right`, `bottom`.
- **A variable holding a status code is named as a code** — `lookupResult`,
  `writeStatus` — never after the success it may report.
- **One word means one thing within a feature.** Where a word has come to mean
  several things, each meaning gets its own word.
- **`.first` and `.second` are not read.** Decompose a pair with a structured
  binding that names both halves, or return a named struct instead of the pair.
  A `std::pair` or `std::tuple` whose halves mean different things does not
  cross a function boundary.
- **Every argument is legible at the call site.** An aggregate is built with
  designated initialisers (`PendingAction{.Action = Kind::AddProject, .Folder = path}`).
  A bare `true`, `false`, `0`, `{}` or `""` argument is first given a named
  variable; where the function is ours and the flag chooses between two
  behaviours, the flag becomes an enum.

## Rationale

A name is the one piece of documentation that travels with every use and that
the compiler keeps honest when the code changes. Short names were defended on
the grounds that the declaration is close; but the reader's attention is on the
use, and a name that needs its declaration to be understood has moved work from
the writer to every reader. Long lines cost nothing here: the house style has no
column limit.

## Consequences

### Positive

- A line can be understood without scrolling to the declarations of its names.
- Misleading names — codes named like successes, one word for four things —
  have nowhere to hide.
- Call sites state what each argument means, so a reordered parameter list or a
  wrong flag is visible in review.

### Negative

- Loops over an index are longer, and nested index loops much longer;
  `cells[rowIndex][columnIndex]` takes more room than `cells[r][c]`.
- Replacing a bool parameter with an enum, or a pair with a struct, changes a
  signature and every caller with it.
- "Role" is a judgement; two writers can name the same thing differently, which
  review has to settle.

### Neutral

- Casing and prefixes are [0479](0479-settle-one-house-style-for-naming-formatting-and-layout.md)'s
  and are unchanged.
- [0423](0423-nl-5-avoid-encoding-type-information-in-names.md) still holds: a
  name states a role, not a type; `m_` and `s_` remain the only prefixes.

## Related

- [0558](0558-write-code-a-junior-engineer-can-read-like-prose.md) — the
  principle this applies.
- Supersedes [0177](0177-es-7-keep-common-and-local-names-short-and-keep-uncommon-and-non-local-names-longer.md)
  and [0424](0424-nl-7-make-the-length-of-a-name-roughly-proportional-to-the-length-of-its-scope.md).
- Amends [0423](0423-nl-5-avoid-encoding-type-information-in-names.md).
- [0178](0178-es-8-avoid-similar-looking-names.md) and
  [0433](0433-nl-19-avoid-names-that-are-easily-misread.md) — still in force,
  and easier to satisfy with long names.
