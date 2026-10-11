# 0573. Describe settings pages as data and draw them with one layout

Status: accepted
Date: 2026-10-11

## Context

[0483](0483-show-settings-as-sections-inside-one-panel.md) put every settings screen into one panel as a `SettingsSection` that drew itself. Nine sections later the panel had three problems that came from that shape:

- **Each section drew its own way.** One put labels to the right of checkboxes, another used a table of labels, and some had dimmed text under a control while others did not.
- **Nothing outside a section knew what it held.** The panel could not search the settings, so finding "wrap" meant already knowing which section it was in.
- **A section was chosen by its index in a vector.** 0483 itself listed the consequence: inserting a section changed what the status bar's "open this language's settings" opened.

Settings had also been grouped by the class that stores them. The editor's section held the Explorer's and Git's options because `EditorSettings` stored them.

## Decision

- **A settings page describes itself as data.** `SettingsPage::Describe()` returns cards of `SettingsRow`s. A row is one of two things:
  - a title, a one-sentence description, a problem line, search keywords, and a function that draws its control;
  - for a table, a function that draws a block across the whole card.
- **`SettingsLayout` draws every card and every row.** The text goes on the left and the control on the right, or under the text when the card is narrower than 420 pixels. No page draws its own chrome.
- **`SettingsPanel` holds a `SettingsDirectory` of groups of pages.** It addresses a page by a path of stable ids, such as `{"languageServers", "CSharp"}`. A page may hold pages of its own, listed in an inner list beside them.
- **The search reads the rows.** It matches a row by its text and keywords, its card's heading and its page's title. It shows the matching rows of every page at once, drawn by the same layout, so a control works in the results as it does on its page. A block is found by its title, or by what it says it holds for the query, and opens on its own page.
- **A page lives under the part of the window it changes**, named as the View menu names that part. Editor holds every editor setting, Git's change markers included.

What 0483 decided and still holds carries over:

- a page is not a `Core::Panel` and never begins a window;
- events reach the open page only, and no page while a search is shown;
- `OnEvent` and `OnDeactivated` keep default bodies, departing from [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md) for 0483's reason.

## Rationale

- **Data a search can read, and a function a layout can place, is the smallest shape that gives both search and one look.** Drawing code alone gives neither without running it to find out what it holds.
- **One layout makes every page look the same by construction**, rather than by every section remembering a convention.
- **Paths of ids survive pages being added and reordered.**

## Consequences

### Positive

- Every setting is found by typing a word of it.
- A new setting is one row: a title, a sentence and a lambda. It looks like every other row without trying to.
- The status bar and the bar above the editor open a language's page by path, whatever order the sidebar is in.

### Negative

- **Rows are rebuilt on every frame the panel is drawn.** During a search, every page's rows are rebuilt. A few dozen rows make this cheap, but the work happens whether anything changed or not.
- **`Update` and `RenderDialogs` run for every page on every frame, not only for the open one.** A search can show a page's rows, and through them open its dialogs, while another page is open.
- **A row whose title changes while it is on screen needs a stable `Key`.** Without one, a click on its control is lost mid-press. The install row's title is the install's progress.
- **Hiding the panel deactivates every page, not only the open one.** A search may have left a question open on any of them.

### Neutral

- Per [0002](0002-keep-core-free-of-ide-knowledge.md), the page vocabulary stays in Graphite. Only the toggle switch, which knows nothing of settings, is in Core.

## Related

- [0483](0483-show-settings-as-sections-inside-one-panel.md) is superseded by this record.
- [0571](0571-choose-and-install-language-servers-from-settings-and-the-editor.md): its language tabs are now pages inside Language Servers.
- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md) is the rule `OnEvent` and `OnDeactivated` still depart from.
- [0516](0516-animate-the-ui-through-a-core-facade-over-imanim.md): the switch's slide goes through `Core::Animation`. It keeps its state in the window's storage rather than in ImAnim, because ImAnim would slide a switch first drawn on in from off.
- [0550](0550-lay-out-sources-by-feature-in-whole-word-folders-mirrored-by-the-tests.md) is why `Settings` split into `Panel` and `LanguageServers`.
