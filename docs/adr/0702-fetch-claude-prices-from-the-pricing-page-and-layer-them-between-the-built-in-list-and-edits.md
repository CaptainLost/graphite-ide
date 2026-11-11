# 0702. Fetch Claude prices from the pricing page and layer them between the built-in list and edits

Status: proposed
Date: 2026-11-11

## Context

The sessions views cost a transcript's tokens with `Pricing`. Its list was built into the code, and a price
could be edited in Settings > Claude Sessions (`claude.price.<model>`). A new model, or a price change, stayed
"?" or wrong until the next build or a hand edit. Older models were left out of the list on purpose, because a
guessed price shows a wrong number with nothing to say it was guessed.

Anthropic publishes no price API: `/v1/models` gives ids, context windows and capabilities, no prices. Its
documentation serves the pricing page as markdown at
`https://platform.claude.com/docs/en/about-claude/pricing.md`. The page's "Model pricing" table has exactly the five
prices `Price` holds (base input, 5m and 1h cache writes, cache hits, output) in dollars per million tokens, one row
per model, under display names ("Claude Opus 5.5"). The page is written for people; its layout is no contract.

## Decision

- Read the "Model pricing" table of that page (`ReadPricingPage`). A display name becomes an id by lower-casing it
  and turning spaces and dots into dashes, after dropping a note in brackets. Of two rows for one model (Haiku 5.5's
  dearer tier for long prompts) keep the first. A row whose name makes no `claude-` id is skipped. A missing section,
  a header other than the five prices in their order, a price that does not read as `$N / MTok`, or a table with no
  model is an error, never an empty or partial list.
- Keep the fetched list in global settings under keys of its own, `claude.fetchedPrice.<model>`, with the day of
  the fetch in `claude.pricesFetchedOn`; a fetch replaces the whole fetched list. Prices stay global, not per
  folder: they do not depend on the project, and the dashboard sums every project.
- Layer the prices: built-in, then fetched, then edited, the later winning (`PriceSource`). An edit outlives a fetch;
  Reset drops the edit and shows the fetched price, or the built-in one when the page does not name the model.
- Fetch with no schedule, on three triggers only: at start when the page was never fetched; when the sessions panel
  takes in a session with tokens under a model that has no price, at most once a run and not on a day the page was
  already fetched; and from the Refresh button on the Claude Sessions settings page, which also shows the day of the
  last fetch and why the last one failed. A failed fetch keeps the prices there were.
- Fetch on a one-worker queue of its own through `Core::LatestAnswer`; the UI thread takes the answer once a frame.

## Rationale

The page is the one place Anthropic keeps the prices current, and its markdown table maps onto `Price` column for
column, so reading it costs one request and a small parser. Failing closed on any change to the table's shape means a
redesigned page can stop the updates but cannot price anything wrong. Keeping the fetched list apart from edits is
what lets Reset mean "back to the published price" and lets settings say where each price came from.

## Consequences

### Positive

- A new model is priced without a new build, and models the built-in list leaves out (Opus 4.5, 4.1, Sonnet 4.5, …)
  are priced once the page names them.
- The settings page says whether the prices are built in or fetched, and on which day; edited rows are marked.

### Negative

- The parser depends on the page's wording: a renamed column or section stops the updates until the code follows.
  The settings page shows the error, and the last good list stays.
- The fetch cannot be cancelled (`HttpClient::Fetch` takes no token), so closing the app while it waits on a dead
  network can wait out WinHTTP's 30-second stage timeouts, as an install's version check already can.
- Models whose id does not follow the display name, such as the Claude 3 generation (`claude-3-5-haiku`), are not
  matched. The page no longer lists them, so they stay "?" as before.
- Haiku 5.5's dearer tier for prompts past 100K tokens is read and dropped, as the built-in list drops it.

### Neutral

- An unpriced model in an old transcript asks for one fetch a day at most.

## Alternatives Considered

- **A price list kept by a third party (a JSON file of model prices on GitHub).** Machine-readable, but it trails the
  page it is copied from, and it would make Graphite depend on someone else's upkeep for figures Anthropic publishes
  itself.
- **Fetching once, when the settings file is first created.** Simplest, but a model released later would stay "?"
  for good; the unpriced-model trigger and the Refresh button cover that at the cost of two more triggers.
- **Writing the fetched prices into `claude.price.*`.** One list, but a fetched price would read as an edit: Reset
  would remove it, and settings could no longer say which prices the user set.
- **Fetching on a schedule (daily).** Fresher, but a request on most starts for prices that change a few times a year.

## Related

- [0700](0700-bucket-session-usage-by-utc-hour-in-the-summary-and-price-it-at-display.md): tokens are priced at
  display, so a fetched list re-prices every view without touching the summaries.
- [0699](0699-derive-the-sessions-dashboard-from-the-transcripts-alone-and-keep-no-history-of-its-own.md)
