# 0701. Fill the sessions dashboard's height with a day-by-day timeline of sessions

Status: proposed
Date: 2026-11-11

## Context

The sessions dashboard ([0699](0699-derive-the-sessions-dashboard-from-the-transcripts-alone-and-keep-no-history-of-its-own.md))
lays its tiles out from the panel's width alone. Every tile's height comes from the width
and the font size, never from a figure, so no tile moves when a total changes. In a full-width
panel about 950 px tall, the rows ended about 150 px above the bottom edge and left an empty band.

A tile with a fixed height only moves the gap. In a shorter window the panel scrolls, and in a
taller one the empty band comes back under the new tile. Rows that grow, with longer lists and
taller charts, fill any height. But a taller bar or dot shows nothing new, and the map's square
cells do not grow at all.

The summary already keeps each session's usage by UTC hour ([0700](0700-bucket-session-usage-by-utc-hour-in-the-summary-and-price-it-at-display.md)).
That is enough to say when each session ran on each local day. The dashboard showed this only
as weekday and hour totals.

## Decision

- Put a full-width "Day by day" tile last in every layout. Let the last row grow so the
  dashboard ends at the panel's bottom. In a panel too short for every tile, the row keeps its
  least height (three quiet days) and the dashboard scrolls. Decide the scrollbar from the
  frame's own room, so the tiles never reflow a frame after the panel changes height.
- Draw one row per local day, today first. Show as many days as fit in the tile, at most 91,
  the map's 13 weeks. Days older than the oldest transcript on disk are hatched, as on the map.
- In a row, draw each run of a session's consecutive active local hours as a bar on a 0–24 h
  track. Colour the bar by the session's main model family: the family it cost the most in, or
  the one with the most requests when it cost nothing. Its brightness is one of four levels: the
  quarter of the shown runs' costs per hour that the run falls in. Count a subagent's hours as
  its session's own. Mark a bar's cost with "?" when one of its hours used a model without a price.
- Give sessions that run at the same time their own lanes, at most four. Draw every bar at the
  same thickness, and make a day's row as tall as its lanes need. A session keeps one lane from
  its first hour to its last that day. A fifth session at once takes the lane that frees soonest
  and overlaps it.
- Hovering a bar shows the session's tip card, with the day, hours and cost of that run as its
  banner. A click opens the session. Hovering a day off its bars shows the day's tip card, and a
  click shows the day's sessions.
- Keep each session's cost per local hour (`SessionFacts::CostByHour`) and its hours without a
  price in the dashboard model. Build the 91 days' bars and lanes once, with the model
  (`DashboardModel::Timeline`), and only lay out the rows when the tile is drawn.

## Rationale

Only a tile whose content grows with the height fills the panel at every height. It must also show
more when it gets more room, not larger marks. Days are such content: a taller window shows more of
them. The timeline also answers a question no other tile does: when each session ran, and next to
which others.

## Consequences

### Positive

- The dashboard has no empty band at any panel height tall enough for every tile.
- Parallel sessions, long sessions and breaks within a session show at a glance.
- The tile needs no new data in the summary or the cache.

### Negative

- The bars have hour precision. A session that ran from 9:50 to 10:05 draws as 9:00–11:00.
- Past four sessions at once, bars overlap, and the top one hides the others' hours.
- A busy day takes up to about two and a half quiet rows, so a week of parallel work shows
  fewer days.
- Brightness compares a run only with the others shown, so the same run can change level
  when the panel shows more days.

### Neutral

- How many days the tile shows depends on the panel's height and on how busy the days are.
  Nothing else on the dashboard changes with a figure, and the tile itself never moves.

## Alternatives Considered

- **Rows that grow with the height.** Taller charts and longer lists of sessions, tools and skills.
  Rejected: taller bars and dots carry no more information, the map cannot grow, and lists past
  their eighth row add little.
- **A new row of fixed tiles.** A 14-day timeline, the month's cumulative cost with a projection,
  and sessions by cost. Rejected: fixed heights fit one window height only, so the gap or a
  scrollbar returns at every other.
- **One track per day, with parallel sessions drawn over each other.** Rejected: the user often
  runs several sessions at once, and the top bar would hide the rest.
- **Rows of one height, split into thinner lanes on a busy day.** Rejected: four lanes left bars
  3 px thick, hard to hover, and the busiest days looked the faintest.
- **Brightness straight from the cost per hour.** Rejected: one dear hour made almost every other
  bar look the same on a dark theme.

## Related

- [0699](0699-derive-the-sessions-dashboard-from-the-transcripts-alone-and-keep-no-history-of-its-own.md): the dashboard and its tiles.
- [0700](0700-bucket-session-usage-by-utc-hour-in-the-summary-and-price-it-at-display.md): the hourly buckets the timeline reads.
