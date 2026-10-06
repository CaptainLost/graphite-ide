# 0530. Draw a frame only when there is reason to

Status: proposed
Date: 2026-09-29

## Context

The frame loop polled for events and drew a full frame every refresh, held
back only by vsync. With nothing happening the IDE still ran every layer's
`OnUpdate` and rebuilt the whole ImGui frame 60 to 240 times a second. A Debug
build spent most of a core on it at idle, 6.7% of a 16-core machine, and
Release 0.2%. A minimized window was worse than an idle one: drawing was
skipped but the buffers were still swapped, and on Windows a minimized
window's swap does not wait for vsync, so the loop spun.

Much of the program relies on frames arriving without input:

- Worker threads hand results to the UI thread through inboxes drained once
  per frame. These are the git and change-marker models, search, workspace
  analysis, the session index and live transcripts, the language-server
  readers, the directory watchers, the terminal reader and system dialogs.
- Timers are advanced by the frame's `Timestep` or read off
  `ImGui::GetTime()`. They include the git debounce, the watcher's settle
  period, the state keeper's delayed write, the caret blink, the transient
  status message and the language-server retries.
- ImGui itself needs frames after input settles: tooltip delays, hover fades,
  a scroll or focus request that takes effect on the next frame.

## Decision

- Let `Core::FramePacer` decide how long `Application::Run` may sleep before
  the next frame. After input, draw every refresh for
  `FramePacer::kInputWindow` (1 s). After a `Core::Redraw::Request`, draw
  `FramePacer::kRequestFrames` (3) frames. Keep drawing for as long as a mouse
  button is held. Otherwise sleep in `glfwWaitEventsTimeout` until the earliest
  requested deadline, and never longer than `FramePacer::kIdleTick` (1 s).
- Count as input an event reaching `Application::OnEvent` and input left in
  ImGui's queue, which covers every viewport.
- Call `Core::Redraw::Request()` from any code that changes what the screen
  should show without an input event, from whatever thread it runs on, right
  after the result is published. Requests coalesce: only the one that raises
  the flag wakes the loop. A request still standing when the loop is about to
  wait stops it from waiting, because something on the UI thread may have
  pumped the message that carried the wake.
- Call `Core::Redraw::RequestIn(delay)` on the UI thread for a frame needed at
  a known time. A deadline lasts for one wait, so code that draws every frame
  asks again every frame. Ask for one wherever a result or an input sets off
  something that finishes later: the caret's next flip, a hover fade still
  under way, the watcher's quiet period (asked by `DirectoryWatcher::TakeSettled`),
  the git and change-marker debounces and the change markers' edit quiet.
- Neither draw nor swap while minimized. Wait for events and the idle tick
  instead.

## Rationale

Input and results are paced differently because they arrive differently.
Input comes from a person, in bursts, and what follows it is mostly ImGui's own
timing: tooltip and hover delays, a scroll request that lands a frame later.
None of that asks for frames, and a second of full-rate drawing after the last
keystroke covers all of it. Results arrive without anyone at the keyboard,
sometimes several a second for as long as another process writes into a
watched folder, such as a Claude session appending to its transcript or a
build writing into `bin/`. A second of frames for each would keep the loop
drawing throughout. So a result buys the few frames it takes to show it, and
the few timers a result can start ask for their own frame. The idle tick
bounds the damage of a producer that forgets to call `Request`: its result
shows up to a second late rather than never.

## Consequences

### Positive

- Idle cost after startup drops to about 0.01% of the machine in Debug and to
  nothing measurable in Release. A minimized window costs next to nothing.
- A process writing into a watched folder twice a second costs about 27 frames
  a second instead of the refresh rate. What remains is the work each write
  sets off, such as refreshing the tree and asking git for status. That work
  is the same as before.
- While anything is happening the loop behaves as before: every refresh, held
  back by vsync.

### Negative

- A new producer of results has to call `Redraw::Request`, and nothing checks
  that it does. If it doesn't, its results appear only on the next event or
  the next idle tick, up to a second late. Tests cannot see this; only using
  the application can.
- Timers longer than the input window and without a deadline fire up to one
  idle tick late. These are the state keeper's write, the expected-write
  expiry, the transient status message, the language-server retries and
  handshake timeout, the analysis caps and the sessions directory check.
- After a sleep, the first frame's `Timestep` and ImGui's `DeltaTime` cover
  the whole sleep. That is right for a timer armed before the sleep, which
  should catch up. It is wrong for a timer armed by the input that ended the
  sleep, which would expire on the frame that armed it. Such a timer must not
  count its first frame. `SettlingFilter` skips it, and `Core::Animation` caps
  the step at 0.1 s. A new timer of this kind has to do the same.
- A timer that a result can start, and that finishes after the result's few
  frames, has to ask for its frame with `RequestIn`. If it does not, it fires
  on the idle tick, up to a second late.
- `Redraw` is process-wide state reached from worker threads, where ADR 0021
  says to avoid non-const globals. A waker handed to every producer would
  thread the window through layers that otherwise never see it.
- A background pass that keeps producing results keeps the loop drawing for as
  long as it runs, at three frames per wake. Examples are clangd indexing and
  the workspace analysis after a folder opens.

### Neutral

- Vsync stays on. It is what limits the loop while it is active.
- Relative commit ages ("5 minutes ago") and the sessions "live" dot refresh on
  the idle tick instead of every frame.

## Alternatives Considered

- **Keep polling and cap the frame rate.** A fixed 20 or 30 frames a second
  when idle is one line. It lost because it cuts the cost only by the ratio
  of the cap to the refresh rate. It would still draw 30 identical frames a
  second, and every interaction would feel slower until input raised the rate
  again.
- **An explicit deadline for every timer, and no idle tick.** It is precise,
  and a truly idle IDE would draw nothing at all. It lost because it needs
  about twenty timers across Core and Graphite to report their next deadline.
  A timer that is missed never fires, and that failure is silent. One frame a
  second in Debug is far below anything worth that risk.
- **Wake the loop from `JobQueue` after every job.** It covers every
  job-based producer from one place. It lost because several jobs publish
  results many times before they finish, such as search hits, transcript
  chunks and session summaries. Those still needed their own calls, and the
  hook would have made the Core job queue depend on the frame loop.

## Related

- [0516](0516-animate-the-ui-through-a-core-facade-over-imanim.md) — the
  facade asks for frames while a tween is under way.
- [0524](0524-hold-change-markers-in-a-model-of-their-own-apart-from-gitmodel.md) — its debounce and
  edit quiet ask for their frames.
- [0529](0529-run-system-dialogs-on-a-thread-of-their-own.md) — the dialog
  thread wakes the loop when it has an answer.
- [0509](0509-write-the-terminal-emulator-rather-than-adopt-one.md) — the terminal reader
  blocks when its queue is full, so without a wake a sleeping UI would stall
  the shell.
- [0021](0021-i-2-avoid-non-const-global-variables.md)
