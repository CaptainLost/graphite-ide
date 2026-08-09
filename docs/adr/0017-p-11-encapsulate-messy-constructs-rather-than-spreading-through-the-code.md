# 0017. P.11: Encapsulate messy constructs, rather than spreading through the code

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

This is the rule Core exists to satisfy. `ReadDirectoryChangesW` with its
overlapped I/O, libgit2's C handles, `IFileDialog`'s COM dance and ImGui's
internal API are all messy constructs, and each is meant to live behind exactly
one interface.

## Decision

Adopt P.11 as written. Transcribed in full:

> **Reason.** Messy code is more likely to hide bugs and harder to write. A good
> interface is easier and safer to use. Messy, low-level code breeds more such
> code.

**Example.**

```cpp
int sz = 100;
int* p = (int*) malloc(sizeof(int) * sz);
int count = 0;
// ...
for (;;) {
    // ... read an int into x, exit loop if end of file is reached ...
    // ... check that x is valid ...
    if (count == sz)
        p = (int*) realloc(p, sizeof(int) * sz * 2);
    p[count++] = x;
    // ...
}
```

This is low-level, verbose, and error-prone. For example, we "forgot" to test
for memory exhaustion and assign new value to `sz`. Instead, we could use
`vector`:

```cpp
vector<int> v;
v.reserve(100);
// ...
for (int x; cin >> x; ) {
    // ... check that x is valid ...
    v.push_back(x);
}
```

**Note.** The standards library and the GSL are examples of this philosophy. For
example, instead of messing with the arrays, unions, cast, tricky lifetime
issues, `gsl::owner`, etc., that are needed to implement key abstractions, such
as `vector`, `span`, `lock_guard`, and `future`, we use the libraries designed
and implemented by people with more time and expertise than we usually have.
Similarly, we can and should design and implement more specialized libraries,
rather than leaving the users (often ourselves) with the challenge of repeatedly
getting low-level code well. This is a variant of the subset of superset
principle that underlies these guidelines.

**Enforcement.** Look for "messy code" such as complex pointer manipulation and
casting outside the implementation of abstractions.

## Rationale

Adopted as written, and it is the philosophical statement of
[0002](0002-keep-core-free-of-ide-knowledge.md): the Core/Graphite split is this
rule applied at the scale of a project rather than a class. Its enforcement note
gives the boundary a testable form — casts and pointer arithmetic are expected
*inside* an abstraction and suspicious outside one.

That test is what makes the ImGui exception in
[0002](0002-keep-core-free-of-ide-knowledge.md) defensible: panels calling ImGui
directly are not doing pointer manipulation, they are using a library at its
intended level. The Win32 handle juggling in `DirectoryWatcher` is the messy
construct, and it is encapsulated.

## Consequences

### Positive

- Turns "where does this code belong?" into a question with an answer: if it
  casts or manipulates handles, it belongs behind an interface.
- Validates the shape the project already has, at no cost.

### Negative

- Encapsulation has a price the rule does not mention: `DirectoryWatcher`'s
  interface hides real behaviour — a thread, a timeout, an arming handshake —
  and a caller who does not read it will misuse it in ways the interface cannot
  prevent.
- Every wrapper is code that must itself be correct, and it is the code least
  covered by tests, because it is the code that touches the operating system.

### Neutral

- The GSL is named in the Note as an example of this philosophy; this project
  does not use it, per
  [0019](0019-p-13-use-support-libraries-as-appropriate.md). The standard
  library serves the same role here.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| Win32 called from a rendering file, outside the platform abstraction | `Core/src/Core/Gui/ImGuiRenderer.cpp` | Same item as in [0008](0008-p-2-write-in-iso-standard-c.md); it is a P.11 violation as much as a P.2 one |
| `imgui_internal.h` used directly by application code | `Graphite/src/Editor/DocumentWindows.cpp`, `Graphite/src/Editor/TextEditorWidget.cpp`, `Graphite/src/Editor/StatusBar.cpp` | Permitted by [0002](0002-keep-core-free-of-ide-knowledge.md), but it is reaching past a library's intended interface, which is what this rule warns about |

## Related

- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — the previous rule.
- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — the next rule.
- [0002](0002-keep-core-free-of-ide-knowledge.md) — this rule at project scale.
