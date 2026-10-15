# 0593. Load more history only on request, from a status line under the list

Status: proposed
Date: 2026-10-15

## Context

The Git window's history shows the newest 500 commits of a ref and used to fetch a page twice as long once the reader scrolled to the bottom. The list grew under the reader without being asked, the scrollbar's thumb jumped, and nothing said how much history was left.

Every query also stops after visiting 20,000 commits (`LogRequest::MaxWalk`), and load-more raised only the page size, so a history longer than that could never be read past its newest 20,000 commits.

## Decision

Nothing loads more history but a click. Under the commit list, one line says how much is shown and offers the next step:

- With no filter: `500 of 1,739 commits`, with **Load 500 more** and **Load all**. `500 commits · counting…` until the total arrives; `All 1,739 commits` and no buttons once the page is complete.
- With a text or path filter: `37 matching commits in the newest 20,000 of 50,000`, with **Search further** and **Search all**; `58 matching commits in all 50,000` once the walk reached the root.
- While a query runs, `Loading…` or `Searching…` stands in for the buttons. A stash shows no line.

Each click asks again from the newest commit with a longer reach:

- Load 500 more: 500 more commits, and a walk ceiling never below the page size.
- Search further: 500 more matches or 20,000 more commits walked, whichever comes first.
- Load all and Search all: no limit on either.
- Another ref or another filter starts again at 500 commits and 20,000 walked. A refresh keeps the reach the reader asked for.

`LogPage::Walked` says how many commits the walk visited. The total is `Repository::CountCommits`, counted in the background beside the log and again only when the ref's tip has moved.

## Rationale

The reader decides when the list grows, so the list never changes under them unasked, and the line says what is left before they ask. Counting is keyed on the tip because the model asks for the log again on every file save, and a count of a long history is a walk of all of it.

## Consequences

### Positive

- The list grows only when the reader asks, and never past what they asked for.
- A history longer than the walk ceiling can be read to its root.
- The reader sees how far a filter searched before deciding to search further.

### Negative

- Reading a long history takes a click per 500 commits, or one Load all.
- Load all has no limit: on a history of a million commits it is about a minute of background work and several hundred megabytes, and every refresh walks the whole history again.
- Counting walks the whole history once per tip move, in the background.

### Neutral

- Each request walks again from the newest commit; a longer page is not appended to the one on screen.

## Alternatives Considered

- **Load more when the reader reaches the bottom.** What the list did before: no clicks, but it grew unasked and its scrollbar jumped.
- **A button at the end of the list.** Explicit too, but says nothing about how much history is left until the reader is at the end.
- **A scrollbar sized to the whole history, filled as the reader scrolls.** No jump, but it still loads unasked and needs the count before it can draw the list.
- **Count with every log query.** Simpler, but a long history would be walked whole on every file save.

## Related

- [0496](0496-filter-the-history-in-the-walk-not-in-the-page.md): the filter runs in the walk. Its consequence that load-more asks for twice the matches on screen no longer holds; Search further asks for 500 more or 20,000 deeper.
- [0576](0576-draw-a-loading-indicator-through-a-core-facade-over-imspinner.md): the loader's delay.
- [0530](0530-draw-a-frame-only-when-there-is-reason-to.md): frames are drawn only on a reason.
