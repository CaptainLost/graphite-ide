# 0574. List what a settings block holds as rows of its own in the search

Status: accepted
Date: 2026-10-11

## Context

[0573](0573-describe-settings-pages-as-data-and-draw-them-with-one-layout.md) found a block by its title, or by "what it says it holds for the query". A search for "save" showed one row, "3 commands match", and the user still had to open Key Bindings and read the table to find the command. Colours were the same: "comment" showed the card, not the colour.

## Decision

- **A block names the things inside it that match a query.** `SettingsRow::FindInside` takes the query's lowered words and returns one row for each thing that matches every word. A block that finds nothing is matched by its own title, description and keywords as before.
- **The search shows each as a row of its own.** A found row with a control, such as a colour's swatch, is drawn with that control and works in the results. One without, such as a command, is drawn with an Open button that opens the page and hands it the row's `OpenWith`.
- **At most eight rows per block.** The rest become one row, "N more matches", whose Open hands the page the whole query.
- **Matching is word by word, case-insensitive, in the search and in a block's own filter alike.** Every word must be found in the thing's name or in the text it is found by, such as a command's shortcuts.

## Rationale

- **The thing the user typed is the thing on screen.** A count sends them to look again; a row is the answer.
- **One matcher, `EveryWordFound`, keeps a filter and the search from disagreeing** about what "save ctrl" finds.

## Consequences

### Positive

- A command or a colour is found by its name, and a command by a word of its shortcut, without opening its page.
- A new kind of block gets the same behaviour by supplying one function.

### Negative

- **A block's findings are built on every frame of a search.** Every command and every colour is tested against the query each time; the registry and the colour groups are small, but the work is not cached.
- **Eight is a limit chosen by feel.** A query that matches a hundred commands shows eight and a pointer to the page, not the hundred.

### Neutral

- A page's own filter still shows its whole table; only the search limits what it lists.

## Related

- [0573](0573-describe-settings-pages-as-data-and-draw-them-with-one-layout.md): refines its sentence "A block is found by its title, or by what it says it holds for the query".
