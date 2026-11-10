# 0700. Bucket session usage by UTC hour in the summary and price it at display

Status: proposed
Date: 2026-11-10

## Context

The sessions dashboard ([0699](0699-derive-the-sessions-dashboard-from-the-transcripts-alone-and-keep-no-history-of-its-own.md))
draws cost per day, a 13-week map of days, the hours of the week a user works in, and a
session's cost on a given day. The summary of [0517](0517-read-claude-transcripts-through-a-lazy-index-with-cached-summaries.md)
held one total of tokens per model for the whole session, which can answer none of those:
a session often runs across midnight, and when the dashboard was designed, 72 of the 80
sessions then on this machine had changed their working folder at least once. The
measurements below were taken later, over the 77 sessions on disk by then.

Days and hours must be the user's local ones, but a summary is cached on disk and read back
after the time zone, or daylight saving time, has changed. Dollars must follow the prices in
Settings > Claude, which the user edits and which change with each new model; a session tab
already prices its tokens when it draws them.

## Decision

- Give `SessionSummary` a `SessionActivity`: for the main transcript and the subagent
  transcripts separately, a map from the UTC hour (`std::chrono::sys_time<std::chrono::hours>`)
  to that hour's tokens per model (`UsageByModel`), requests per model and prompts. Add the
  main thread's tokens per working folder, tool and failed-tool counts, skill calls with the
  last call's time, compactions and active time.
- Count a request once, by `UsageTally`'s rule, in the hour and folder of its first record
  with usage, so the buckets add up to the session's totals.
- Store tokens only. Price them with `Pricing::CostOf` when the dashboard is built, and
  convert hours to local days and hours with the current time zone at the same moment.
- Raise `kSummaryCacheVersion` to 3; an older cache is discarded and rebuilt by one scan,
  with no migration.
- Keep only hours from 1 January 2025, before Claude Code's first release, to the end of
  tomorrow, when a transcript is read and when the cache is loaded, and clamp the dashboard's
  days to the same range. The floor is a fixed day, not a span back from today, so an old
  transcript never ages out of the dashboard. A
  record dated by a wrong clock counts in no hour, as one without a timestamp does, rather
  than stretch the run of days without end.

Measured on the machine this was built on, 2026-11-10, Release build: 77 sessions in
1,134 transcript files (3.8 GB, subagent files included). A cold scan with an empty cache
took 38.1 s, from the start of indexing to the first frame that saw the scan done, timed
with `steady_clock` around the panel's indexing. The resulting `sessions-cache.json` is
207,477 bytes (203 KB), written in 7 ms; the UI kept drawing at a median 4.2 ms a frame
throughout the scan.

## Rationale

An hour is the smallest bucket that still places work in the hour of the week and survives
a move between time zones whole-hour apart; a day bucket would be wrong after any such
move, and per-record timestamps would make the cache as large as the transcripts. Keeping
tokens rather than dollars means one source of prices: a price changed in Settings, or a
model priced for the first time, reprices every day at once and agrees with the session
tabs, which price the same tokens the same way.

## Consequences

### Positive

- Cost per day, the map, the punch card and a session's cost on a day all come from one
  structure, and agree with the session's own tab to the cent.
- A summary stays right across time-zone and daylight-saving changes.
- The cache stays small: a few kilobytes a session.

### Negative

- Every dashboard rebuild prices every hour of every session. The rebuild runs on the UI
  thread, coalesced to at most twice a second during a scan, and it is work a stored total
  would not need.
- A time zone whose offset is not a whole hour puts each request in the local hour the UTC
  hour starts in, up to 30 or 45 minutes off.
- A record without a timestamp has no hour, so its usage is in the session's totals but on
  no day.

### Neutral

- The first start after this change rescans every transcript, since the old cache is dropped.

## Alternatives Considered

- **Dollars in the cache**: cheaper to sum, but a price edited in Settings, or added for a
  new model, would leave old days at the old price and disagree with the session tabs.
  Rejected: prices change.
- **Buckets per local day**: smaller still, but wrong after a change of time zone or
  daylight saving time until the transcript is read again, and no hour of the week.
- **Reading the transcripts again on every rebuild**: no cache growth, but seconds of
  parsing per rebuild where the dashboard needs milliseconds.

## Related

- [0517](0517-read-claude-transcripts-through-a-lazy-index-with-cached-summaries.md) — the summary cache this extends
- [0699](0699-derive-the-sessions-dashboard-from-the-transcripts-alone-and-keep-no-history-of-its-own.md) — the dashboard these buckets feed
