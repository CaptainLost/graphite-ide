# 0639. Highlight the other occurrences of a selected word, in the text and either side of the scrollbar's centre strip

Status: proposed
Date: 2026-10-25

## Context

Double-clicking a field such as `_steam` selects it, and the next question is
nearly always where else the file uses it. Until now the answer took the find
bar: open it, accept the query, read the matches, close it again. The find bar's
matches are case-insensitive substrings by default, so `_steamReader` and
`_Steam` came up alongside `_steam`.

The editor already paints many ranges on a line at once (the find bar's
matches, through `MatchColumnsOnLine`). Its vertical scrollbar carries the git
change marks in a 4 px strip down its centre. That strip is exactly as wide as
the grab at the default scrollbar width, and the caret's mark spans it too.
0527 left open how the track would be shared once a second kind of mark was
wanted. This is that second kind.

## Decision

- When the selection is exactly one whole word, paint every other place that
  word stands whole, in the same case, in the new editor colour
  `selectionOccurrence`. A word is what `Core::IsWordByte` calls one, the same
  test the find bar's whole-word option and the double-click use. The way the
  selection was made does not matter. A partial word, two words, punctuation or
  an empty selection paint nothing, and neither does a word found only where it
  is selected.
- Leave the selected occurrence itself to the selection colour. It is not
  painted twice.
- Take a double-clicked word from the character under the pointer, not from the
  caret position the click rounds to. Rounded, the right half of a word's last
  character selected whatever followed the word.
- Tick each line holding an occurrence on the vertical scrollbar, in the margins
  either side of the grab, 1 px clear of it. The centre strip is never wider
  than the grab, so this clears both. Use the occurrence colour at full
  opacity. Ticks that touch merge. No tick is shorter than the change marks'
  2 px least height, and a tick on the last line is pushed back inside the
  track rather than cut short, as change marks are.
- Find the lines a merged tick swallows by binary search, since their places on
  the track rise with the line. That bounds the work per frame by the track's
  height rather than the number of occurrences: 200,000 occurrence lines cost
  0.05 ms a frame, where visiting each line cost 6 ms.
- Show nothing while the find bar is open with matches of its own. Its matches
  hold the highlight then.
- Search once per change of selection or buffer revision. The result is kept in
  an `OccurrenceMemo` on the view's `EditorWidgetState`. Searching and placing
  the ticks are pure functions in `Editor/Find/SelectionOccurrences`, tested
  without ImGui.
- Put the behaviour behind a setting, Editor › Text › *Highlight occurrences*,
  stored as `highlightOccurrences`. It is on by default.
- Give `selectionOccurrence` a lower alpha than `selection` in every built-in
  scheme. The derived schemes take it from their accent, at 45 on dark and 30
  on light.

## Rationale

The word test is the one the double-click selects by, so what a double-click
selects is always something this highlights. Case and whole-word matching keep
`_steamReader` and `_Steam` out. Those were exactly the false hits that made the
find bar a poor tool for this.

Ticking beside the grab shares the track without moving anything already
there. The grab and the change marks keep the middle. The ticks use the margins
the grab never covers, so neither hides the other at any scrollbar width a
scheme allows. At the default width the grab is exactly as wide as the strip.

## Consequences

### Positive

- One double-click shows every use of a name in the file, with no query to
  type and no false hits from longer names.
- The scrollbar shows the uses beyond the screen, so a long file needs no
  scrolling to find them.

### Negative

- A selection change in a large file scans the whole document once, line by
  line, on the UI thread. A common short word in a file of hundreds of
  thousands of lines can cost a visible frame. The find bar pays the same on
  every query change.
- With the find bar open and showing matches, a double-click shows no
  occurrences. Someone who forgot the bar was open may read that as the
  feature not working.
- The ticks are thin. The grab is inset 4 px from each side of the bar, so a
  tick is 3 px wide, and 2 px on the narrowest scrollbar a scheme allows.
- The text test is lexical. A name in a comment or a string is highlighted like
  a use, and two different symbols that share a name are not told apart.

### Neutral

- Diagnostics on the scrollbar, if they come, will need their own place on the
  track. The centre is taken by changes and the margins by occurrences.

## Alternatives Considered

- **The language server's document highlights.** They tell a use from a
  same-named symbol and skip comments. They lost because they need a running,
  trusted server and a round trip per selection, and they do nothing for a
  language Graphite has no server for. The lexical rule works in every file at
  once.
- **Any selected text, matched as a substring.** It would highlight inside
  longer names, which is the find bar's behaviour and the noise this record
  removes.
- **Ticks across the full track width.** Simpler, but they would cover the grab
  and the change marks wherever an occurrence sits beside them. 0527 rejected
  that for change marks for the same reason.

## Related

- 0527 Show change markers on the editor's vertical scrollbar
- 0612 Draw scrollbars as a grab in the text colour over no track
- 0573 Describe settings pages as data and draw them with one layout
