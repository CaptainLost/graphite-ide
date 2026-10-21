# 0631. Draw every diff with one set of options kept in the settings

Status: proposed
Date: 2026-10-21

## Context

A diff is drawn by a `DiffView` in three places: a diff tab (`DiffWindows`), a
branch review's tab (`ReviewWindows`) and the Git panel's preview pane. Its
toolbar offers four choices: side by side or unified, hunks only or the whole
file, syntax colour on or off, and word wrap.

Word wrap was one setting for every diff, kept in `graphite.json` as
`diffWordWrap`. The other three were members of each `DiffView` and started at
their defaults in every new one. Clicking another file in Changes closes the
working-tree tab and opens a new one, so a reader who switched to unified saw
the next file side by side again; the reader reported it as each window having
"separate settings rather than shared ones".

## Decision

Hold all four as one `DiffViewOptions` in `EditorSettings`, saved in
`graphite.json` beside `diffWordWrap`, and draw every diff with it.

- `DiffViewOptions` (`Git/Diff/DiffViewOptions.h`) is the mode, the scope,
  syntax colour and word wrap. `DiffViewMode` and `DiffScope` move there from
  `DiffRows.h`.
- The keys are `diffLayout` (`sideBySide` or `unified`), `diffScope` (`hunks`
  or `wholeFile`) and `diffColourSyntax`, on when absent. `diffWordWrap` keeps
  its name and meaning. A value not understood reads as the default.
- `DiffView::Render` takes the options; the view keeps none of its own. A
  toolbar choice is drawn from that frame on and handed to `OnSetOptions`,
  which every owner points at `EditorSettings::SetDiffViewOptions`, so the
  next frame of every diff, open or opened later, draws with it.
- The Git settings page's word wrap switch writes the same options.

## Rationale

How a reader likes to see diffs is a preference, not a property of one file's
tab: it should survive the tab, the next file and a restart, as word wrap
already did. Keeping the options out of the view leaves one place that says
how diffs look, so the three owners cannot drift apart.

## Consequences

### Positive

- A layout picked once holds for every diff tab, review and preview, and after
  a restart.
- `DiffView` loses three setters only the tests called.

### Negative

- Two diffs can no longer be compared in two layouts side by side; switching
  one switches all.

### Neutral

- An open diff that another diff's choice redraws keeps the reader's line,
  through the layout change it already detects for word wrap.

## Alternatives Considered

- **Copy the last choice into each new view, but keep each view's own.** Open
  diffs would disagree with each other and with the settings, and the choice
  would still be lost on restart unless saved anyway.
- **Keep the options per tab and restore them with the tab.** It fits a reader
  who wants two layouts at once, but the report asked for the opposite, and a
  new tab would still start at the defaults.

## Related

- [0522](0522-persist-session-state-in-two-scopes-global-window-and-panels-per-workspace-tabs.md):
  where state that outlives a session is kept.
- [0543](0543-load-the-settings-file-once-and-merge-its-changes-into-it-at-the-end-of-a-frame.md):
  how a change reaches `graphite.json`.
