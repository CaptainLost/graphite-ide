# 0632. Keep a short history of each language server and show it in the status bar's tooltip

Status: proposed
Date: 2026-10-21

## Context

The status bar names the active document's server and its state, "C++: clangd
indexing 42%", with a coloured dot
([0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md)).
Its tooltip repeated the state's detail: the command line while starting, the
server's name and version once ready, the reason after a failure. A reader
hovering it to learn why the editor was slow found nothing more: not what the
server was working on, not since when, and not that it had failed and restarted
a minute before.

The facts were there or nearly. `$/progress` brings a title and a message with
every report ("Background index", "120/450"), which the client dropped, keeping
only the percentage. `LanguageServices` sees every change of state as it drains
a client, and starts, restarts and searches for projects itself. Only the
Console kept any of it, as log lines with no server named in them.

## Decision

- **Activity.** The client keeps each running progress's title and latest
  message, and `ServerStatus::Activity` is the one begun last that has not
  ended, as "Title: message".
- **History.** Each managed server keeps its last `kServerHistoryLength` (20)
  events, each a wall-clock time, a few words and, where it ended a stretch, how
  long that stretch took: starting, looking for projects, found N projects,
  cannot start, indexing or loading a solution, ready, finished indexing in N,
  limited by a project problem, failed and why, restarting. It also keeps when
  it entered its state, on the services' clock.
- **Report.** `LanguageServices::ReportFor` hands the status, the root, the
  number of open documents, the requests in flight, how long the server has been
  in its state and the history to whoever asks; `GraphiteLayer` asks for the
  active document each frame.
- **Tooltip.** `ServerReportText` words the report and `ChooseServerLine` sets it
  below the state's detail, so the bar stays a reader of strings: what the
  server works on, the folder, how busy it is, how long in its state, and the
  last eight events at the local time of day, oldest first.

## Rationale

The tooltip is where a reader already looks for more about the server, and
keeping a few events per server costs a short list of strings. Times of day
read like a log and match the Console; durations measured on the services'
clock stay testable.

## Consequences

### Positive

- Hovering the server says what it is doing now and what happened to it lately.
- Progress titles and messages reach the interface for the first time.

### Negative

- The history is per server and in memory: it ends with the server's entry and
  with the session, and a server reaped for being idle takes its history with it.
- The report is built every frame the status bar is drawn, a handful of short
  strings.

### Neutral

- The Console's log lines are unchanged.
- The tooltip is still ImGui's plain one; it cannot be scrolled or clicked.

## Alternatives Considered

- **A server log panel.** It would keep everything, stderr too, but it is a
  view of its own to open and size, for a question the tooltip answers.
- **Read the history out of the Console's log.** The lines name no server and
  scroll out of the ring buffer, so the tooltip could show another root's server
  or nothing.

## Related

- [0548](0548-draw-interactive-tooltips-as-non-focusing-windows-rather-than-imgui-tooltips.md):
  the window a scrollable history would need.
- [0565](0565-keep-a-session-history-of-toasts-behind-a-bell-in-the-status-bar.md):
  where the servers' error and warning messages are kept.
- [0620](0620-show-problems-and-the-server-s-health-as-glyphs-and-colours-in-the-status-bar.md):
  the server item this tooltip belongs to.
