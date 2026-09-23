# 0518. Add ImPlot as a Core dependency

Status: proposed
Date: 2026-09-23

## Context

The session cost view needs charts — a stacked bar per turn in stage 1, and
cost over time, cache hit rate and tool statistics in the next stages. Nothing
in the repository draws a chart.

## Decision

Adopt [ImPlot](https://github.com/epezent/implot) as a submodule in
`vendor/implot`, compiled into the existing `ImGui` static library as ImAnim
is. `ImGuiRenderer` creates the ImPlot context straight after ImGui's and
destroys it straight before. Graphite calls ImPlot directly; there is no Core
facade over it.

The submodule is pinned to tag `v0.17`: ImPlot's current master has moved
`PlotBarGroups` and the other `PlotX` calls onto a property-pair `ImPlotSpec`
API, which fails to compile — a `static_assert` inside `implot.h` rejects the
classic `(labels, values, item_count, group_count, group_size, shift, flags)`
call. `v0.17` still carries the classic signature and compiles clean against
the vendored ImGui (commit `83f668625`, `1.93.0 WIP`, docking branch).

## Rationale

ImPlot is immediate-mode like everything else drawn here, MIT-licensed, and
depends on nothing but ImGui. Unlike ImAnim, whose facade exists so that every
hover shares one duration and one easing, a chart has no application-wide
policy to hold in one place: each chart's axes and series are its own, so a
facade would only rename ImPlot's API.

## Consequences

### Positive

- Every later chart (cost per project, cache effectiveness, tool statistics) is
  a few calls rather than hand-drawn geometry.

### Negative

- ImPlot tracks ImGui closely; bumping the ImGui submodule may require bumping
  ImPlot with it.
- Pinning to `v0.17` means a future move to ImPlot's newer `ImPlotSpec` API is
  a deliberate, separate migration, not a free `git submodule update`.

### Neutral

## Alternatives Considered

- **Draw charts with ImDrawList by hand.** Fine for one bar chart, not for the
  axes, legends, hover and zoom the later stages need.

## Related

- ADR 0516 (ImAnim, built the same way)
