# 0672. Open a branch's history on its own commits with its line's newer commits folded into one row

Status: proposed
Date: 2026-11-04

## Context

A branch's history used to be the branch's whole walk with a separator above the first commit its base already had ("Earlier history · already in main"), and every row under the separator dimmed. What a reader opens a branch for is its own commits and how far its line has moved on since it forked. The line model ([0663](0663-compare-each-branch-with-the-line-it-forked-from.md)) says what each branch is compared with and in which state it is (not merged, merged by history, without commits of its own), but it is read on a worker after the branches, so it is not there when the window first draws. The user decided that the history of a branch waits behind that one read rather than drift onto the screen piece by piece, and that Local changes work at once.

## Decision

- **Rows.** HEAD's history and the checked-out branch's, of the whole tree, start with a Local changes row: "Local changes" and "2 staged · 3 not staged" ("nothing to commit"; an ellipsis while the status is unread). A file's history has no such row. Choosing it shows the Changes view, its file list beside the diff, in the preview pane; choosing a commit leaves it.
- **Folded, the default for a branch that is not merged.** The log is the branch alone. With it the model asks the branch's own commits (`Ref` = the branch, `Exclude` = its line's tip, first parents as the switch says), and one fold row stands before the first page commit that is not the branch's own: "80 newer commits on develop since this branch forked · Show them". The count is the rows "Show them" adds: the line read's behind distance, along the line's first parents in a Merges history (`LineDistance::BehindOnFirstParents`); a line with nothing newer gives no fold row. The first time a history's rows are shown they wait for that count, and for the distance the header's vs label counts, so the rows, the fold row, the label and the banner arrive together at their final widths; `…` is left only as a fallback, and a count asked again later leaves the drawn row and the drawn label as they were until it lands. Rows under the fold row are drawn as usual.
- **Shown.** "Show them" walks the branch and its line's tip together (`AlsoFrom`) and asks the line's newer commits (`Ref` = the line, `Exclude` = the branch's tip), which are drawn dimmed; the status line adds "develop's newer commits shown, dimmed · Fold them". The choice is kept per branch, by full name, until the folder changes.
- **Words.** A log narrowed by words has no fold row, so the branch's own commits are not asked for it; the line's newer commits still are, so they stay dimmed and the status line still offers "Fold them".
- **Combined.** A branch merged by history and one with no commits of its own are shown as Shown, without the offer to fold. A base, an old copy and a branch compared with nothing, with no common history, cut off or with its tip missing show the reference alone — but a base or an old copy the user picked a line for is folded against that line as a branch that is not merged is.
- **The banner** (merged, merged by content, no own commits, old copy, cut off, no common history) is one row of the list under the Local changes row, cut to the row and whole in its tooltip.
- **Merged by content** ([0676](0676-find-branches-merged-by-content-in-the-background-and-keep-the-verdicts-per-repository.md)). A branch not merged by history whose check found it merged into its line is labelled `merged into <line> · squash` (or `· rebase`, the kind dim after the line's name) in place of its counts. Its banner says `Merged into <line> by a squash in <hash>: its changes are in <line>, its commits are not.` (without `in <hash>` when no landing was found) or `Merged into <line> by a rebase: each of its commits has a copy in <line>.` The history stays folded on the branch's own commits, as for a branch that is not merged. The header's words, the banner and a first showing's rows of such a branch also wait for its check, as they wait for its distance, while the window is on screen: an unknown or failed check releases them, and a verdict in the cache is there in the first frame.
- **The marks land with their page.** The own or newer commits are one `MarkedCommitsRequest` asked with the log and landed in the same update; the fold row and the dimming are drawn from what landed, never from the line as it is now, and a read that moves the line asks both again.
- **The wait.** While no line read holds the shown reference, a branch's or HEAD's log is not asked at all: the list is the Local changes row (when it has one) over one waiting row, "Reading the branches' lines…", drawn as a row of the list. HEAD's history waits the same way after a checkout, while the read still names the branch checked out before and a read is coming, and is shown alone when none is coming. Once the read is in the log is asked, and the rows, the fold row and the header's label and banner appear in the update its page (and the counts) are in; a history's first rows come only from a page asked with the line now shown. A stash, a file's history and a history whose read failed do not wait.
- **Opening.** Opening a reference selects Local changes for the checked-out branch, else its first own commit (or its first that is not one of the line's newer ones), else its first row, and scrolls there once; pages that land later never scroll.

## Rationale

Folding the line's newer commits keeps a branch's own work on top however far its line has moved, and the count in the fold row says how far without walking those commits. Asking the marks with the log, and the log only once the line is known, is what keeps the fold row and the dimmed rows from appearing in a list already on screen.

## Consequences

### Positive

- A branch opens on its own commits, and the line's progress is one row with a count, whatever its length.
- The history's rows, its fold row, its dimming and its banner arrive together, once.

### Negative

- On a first opening without the line read, a branch's history shows nothing but its waiting row until the read and then its log are in.
- A history that shows its line's newer commits counts no total: the commit count is of the branch alone.
- A folded history's first rows wait for the count as well as the page; the count is asked with the log, and is an in-memory walk, so the wait is short.
- A banner is one row high, so a long sentence is cut and said whole only in its tooltip.

### Neutral

- A folded history narrowed by words asks no marks and has no fold row.

## Alternatives Considered

- **Keep the separator over the branch's whole walk.** The line's newer commits are not in a branch's walk at all, so the separator could not say how far the line had gone, and a branch far behind showed its own commits above a long stretch of dimmed history.
- **Show the log at once and add the fold row when the line read lands.** The list would gain a row and its rows would change colour after they were on screen, which the window's own loading must never do.
- **Fold the newer commits into the page itself and drop them from the layout.** The graph would join lanes across the gap, and expanding the fold would lay the page out again with commits the walk never read.

## Related

- [0663](0663-compare-each-branch-with-the-line-it-forked-from.md) — what a branch is compared with, and its states.
- [0671](0671-show-a-branch-s-history-by-first-parents-by-default.md) — the Merges / All commits switch the marked commits follow.
- `docs/superpowers/specs/2026-11-03-git-window-lines-design.md` §4.4 and §6.
