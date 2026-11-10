# 0699. Derive the sessions dashboard from the transcripts alone and keep no history of its own

Status: proposed
Date: 2026-11-10

## Context

The Claude Sessions panel was a list with a cost per row and a total per folder. It answered
"what did this session cost" but never "how much went where, and on what": no total, no
time axis, no split by model, project, subagent or tool. The user called it empty. The
panel gained an Overview tab — a dashboard of totals, a 13-week map of days, cost per day,
models, projects, sessions, tools, skills, the hours of work and the cache — and every
number on it needs data from before today.

Everything the dashboard shows is already in the transcripts Claude Code writes under
`~/.claude/projects`, and the panel already reads them through the index and summary cache
of [0517](0517-read-claude-transcripts-through-a-lazy-index-with-cached-summaries.md).
But the transcripts do not last: Claude Code deletes a session's files once they are older
than `cleanupPeriodDays`, 30 days by default. A dashboard drawn from them alone forgets a
day a month after it happened.

## Decision

- Derive every number on the dashboard from the summaries of the transcripts on disk and
  from the prices in Settings > Claude, through the same `UsageTally` and `Pricing` the
  session tabs use. Store nothing else.
- Keep `sessions-cache.json` what 0517 made it: a cache of summaries keyed by each
  transcript's fingerprint, extended with the fields the dashboard needs
  (`kSummaryCacheVersion` 3) and never more. When a transcript leaves the disk its summary
  leaves the cache, and its numbers leave the dashboard on the next rebuild.
- Show the loss rather than hide it. The 13-week map hatches the days older than the
  oldest transcript, so "nothing left" reads differently from "no work"; a hatched day's
  tooltip and the line under the map say that Claude Code deletes transcripts after
  `cleanupPeriodDays` and that Graphite keeps no copy.
- Rebuild the dashboard's aggregates when summaries arrive or leave, when prices change,
  when the workspace toggle or the day changes — never per frame. During the first scan,
  coalesce the rebuilds to at most two a second, and rebuild at once when a summary is
  removed.

## Rationale

One source means the dashboard and a session's own tab can never disagree: both are the
same tally of the same records, priced the same way. A store of our own would be a second
truth to keep in step with the transcripts, with its own format, migration and repair, for
days whose transcripts the user — or Claude Code on their behalf — has already chosen to
delete. A user who wants a longer view raises `cleanupPeriodDays`, and the dashboard
reaches back as far as the transcripts do.

## Consequences

### Positive

- Every number agrees with the session tabs and with the transcripts the user can open.
- Nothing new to version, migrate or clean up; deleting a transcript removes it everywhere.
- A price changed in Settings reprices the whole dashboard, past days included.

### Negative

- With Claude Code's default settings the dashboard covers about 30 days; the map's other
  ten weeks are hatched, and a month's totals are gone a month later.
- Counting and pricing hours on every rebuild costs more than reading stored totals would;
  the rebuilds are coalesced during a scan to keep it off the frame.

### Neutral

- The cache grows with the hourly buckets of [0700](0700-bucket-session-usage-by-utc-hour-in-the-summary-and-price-it-at-display.md),
  but stays a cache: discarding it costs one full scan and loses nothing.

## Alternatives Considered

- **A daily store of our own**, appended as days pass and kept after the transcripts go:
  the dashboard could show a year. Rejected by the user — it is history Graphite would
  own and have to keep right, and a second source for numbers the session tabs also show.
- **Keeping copies of deleted transcripts**: would undo the user's own retention setting and
  hold gigabytes for a few totals.
- **Showing old days as empty instead of hatched**: simpler, but reads as "no work" on days
  that simply have no transcript left.

## Related

- [0517](0517-read-claude-transcripts-through-a-lazy-index-with-cached-summaries.md) — the index and the summary cache this builds on
- [0700](0700-bucket-session-usage-by-utc-hour-in-the-summary-and-price-it-at-display.md) — what the summary carries for the dashboard
- [0522](0522-persist-session-state-in-two-scopes-global-window-and-panels-per-workspace-tabs.md) — where the panel's tab and workspace toggle are kept
