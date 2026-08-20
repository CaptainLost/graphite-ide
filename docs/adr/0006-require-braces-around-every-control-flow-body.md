# 0006. Require braces around every control-flow body

Status: accepted
Date: 2026-08-09

## Context

`.clang-format` already governs layout: `BasedOnStyle: Microsoft`,
`BreakBeforeBraces: Allman`, and — relevant here —
`AllowShortIfStatementsOnASingleLine: Never`, which forbids `if (x) return;`
written on one line. What it cannot express is a requirement that a body be
braced at all. That is a clang-tidy check
(`readability-braces-around-statements`), and this repository has no clang-tidy
configuration.

So the rule is currently unwritten and unenforced, and the form it leaves open
is the Allman one:

```cpp
if (!stream.is_open())
    return file;
```

A scan of `Core/src` and `Graphite/src` — matching each `if`, `else`, `for`,
`while` and `do` header, balancing its parentheses, and checking whether the
first non-blank line after it opens a brace — finds **120 such sites**. Their
distribution is the interesting part:

- **120 in Core, 0 in Graphite.**
- **0 in `Core/Text`**, the most recently written module.

The convention is therefore not new. It is what the recent code already does;
the older parts of Core predate it.

**Correction (2026-08-16).** "0 in Graphite" was wrong when it was written, and
the inventory at the end of this record — which lists Core files only — inherits
the same mistake. `Graphite/src/Editor/TextEditorWidget.cpp` alone holds dozens
of unbraced bodies, and it is not the worst offender: a rescan of `Graphite/src`
by the same method finds them spread across most of its files. Nothing about the
decision changes. The rule stands, existing sites are still fixed when the
surrounding code is touched rather than in a sweep, and everything written since
this record was accepted is braced. What changes is the claim that Graphite was
already conforming, which it never was. The Core inventory at the end is left as
it is: it is accurate about Core, and growing it into a second list of several
hundred Graphite sites would produce a table nobody keeps current. The honest
statement is that this rule is unenforced and both projects carry a backlog
against it.

## Decision

Wrap the body of every `if`, `else`, `else if`, `for`, `while` and `do` in
braces, including single-statement bodies, and including the final `else` of a
chain.

There is no carve-out for guard clauses. `if (!x) { return; }` is braced like
everything else, on Allman lines:

```cpp
if (!stream.is_open())
{
    return file;
}
```

One exception, narrow and named: a control-flow statement inside a
line-continued macro body, where every added line costs a trailing backslash.
`CORE_ASSERT` in `Core/src/Core/Base/Assert.h` is the only such site.

Existing sites are not fixed in a sweep. They are fixed when the surrounding
code is touched, and the inventory below is what tracks them.

## Rationale

The failure mode is not writing the code, it is editing it. Adding a second
statement to an unbraced body changes control flow silently, and the compiler
has nothing to say, because the result is still valid C++ that does something
else. The rule costs two lines at write time and removes a class of edit-time
bug entirely.

There is a second, smaller reason that matters in a repository worked on by an
agent: a diff that adds braces around an existing statement obscures the change
that actually mattered. Bracing everything up front means later diffs show only
the substance.

## Consequences

### Positive

- A body can gain a statement without gaining a bug.
- Diffs stay about their subject rather than about bracing.
- It matches what `Core/Text` and the newest Graphite code already do, so the
  rule is descriptive of recent practice rather than imposed on it. It does not
  match older Graphite, contrary to what the first version of this record said —
  see the correction under Context.

### Negative

- More vertical space, and guard clauses that read perfectly well in two lines
  now take four under Allman. In a file of early returns that is a real cost to
  density.
- Not machine-enforced. Until a clang-tidy configuration exists, this rule
  holds by review alone, which is the weakest kind of rule this directory can
  contain.
- The 120 Core sites inventoried below, and several hundred more in Graphite
  that are not inventoried, remain in the tree; the codebase does not match its
  own written rule and will not for some time.

### Neutral

- `.clang-format` is unchanged: it cannot express this and does not conflict
  with it.
- Machine enforcement is deliberately not decided here. Adding clang-tidy is a
  toolchain decision with its own costs and would be its own record.

## Deviations to fix

The 120 Core sites, by file. They are worked off opportunistically, not in a
sweep; this table is the checklist and should shrink over time. Graphite's own
sites are deliberately not enumerated here — see the correction under Context.

| File | Sites |
|---|---|
| `Core/src/Core/Fs/DirectoryWatcher.cpp` | 19 |
| `Core/src/Core/Platform/FileDialog.cpp` | 10 |
| `Core/src/Core/Git/Repository.cpp` | 10 |
| `Core/src/Core/Fs/FileOperations.cpp` | 10 |
| `Core/src/Core/Gui/Icons.cpp` | 9 |
| `Core/src/Core/Fs/FileTree.cpp` | 9 |
| `Core/src/Core/Fs/FileSystem.cpp` | 9 |
| `Core/src/Core/Sln/SlnParser.cpp` | 6 |
| `Core/src/Core/Sln/CsprojReader.cpp` | 5 |
| `Core/src/Core/Gui/DockSpaceHost.cpp` | 5 |
| `Core/src/Core/Platform/GlfwWindow.cpp` | 4 |
| `Core/src/Core/Base/JsonFile.cpp` | 4 |
| `Core/src/Core/Platform/Process.cpp` | 3 |
| `Core/src/Core/Base/Log.cpp` | 3 |
| `Core/src/Core/App/Application.cpp` | 3 |
| `Core/src/Core/Gui/ImGuiRenderer.cpp` | 2 |
| `Core/src/Core/Event/EventBus.h` | 2 |
| `Core/src/Core/Base/Uuid.cpp` | 2 |
| `Core/src/Core/Platform/Shell.cpp` | 1 |
| `Core/src/Core/Fs/ChangeCollector.cpp` | 1 |
| `Core/src/Core/Base/RingBufferSink.cpp` | 1 |
| `Core/src/Core/App/LayerStack.cpp` | 1 |
| `Core/src/Core/Base/Assert.h` | 1 (the macro exception above; not to be fixed) |

## Related

- [0001](0001-record-architecture-decisions.md)
- [0002](0002-keep-core-free-of-ide-knowledge.md) — every site above is in Core,
  which is the older half of the codebase.
