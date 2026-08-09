# 0014. P.8: Do not leak any resources

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

The rule is stated upstream as "Don't leak any resources"; the title here is
spelled without the apostrophe so that the filename stays clean.

## Context

An IDE is a long-running program, which is the case the rule singles out. The
resources that matter here are a libgit2 repository handle, a Win32 directory
handle plus its watcher thread, and open file streams.

## Decision

Adopt P.8 as written. Transcribed in full:

> **Reason.** Even a slow growth in resources will, over time, exhaust the
> availability of those resources. This is particularly important for
> long-running programs, but is an essential piece of responsible programming
> behavior.

**Example, bad.**

```cpp
void f(const char* name)
{
    FILE* input = fopen(name, "r");
    // ...
    if (something) return;   // bad: if something == true, a file handle is leaked
    // ...
    fclose(input);
}
```

Prefer RAII:

```cpp
void f(const char* name)
{
    ifstream input {name};
    // ...
    if (something) return;   // OK: no leak
    // ...
}
```

See also: the resource management section.

**Note.** A leak is colloquially "anything that isn't cleaned up." The more
important classification is "anything that can no longer be cleaned up." For
example, allocating an object on the heap and then losing the last pointer that
points to that allocation. This rule should not be taken as requiring that
allocations within long-lived objects must be returned during program shutdown.
For example, relying on system guaranteed cleanup such as file closing and
memory deallocation upon process shutdown can simplify code. However, relying on
abstractions that implicitly clean up can be as simple, and often safer.

**Note.** Enforcing the lifetime safety profile eliminates leaks. When combined
with resource safety provided by RAII, it eliminates the need for "garbage
collection" (by generating no garbage). Combine this with enforcement of the
type and bounds profiles and you get complete type- and resource-safety,
guaranteed by tools.

**Enforcement.**

- Look at pointers: Classify them into non-owners (the default) and owners.
  Where feasible, replace owners with standard-library resource handles (as in
  the example above). Alternatively, mark an owner as such using `owner` from
  the GSL.
- Look for naked `new` and `delete`
- Look for known resource allocating functions returning raw pointers (such as
  `fopen`, `malloc`, and `strdup`)

## Rationale

Adopted as written; the codebase already follows it. `Repository` owns its
libgit2 handle and closes it, `DirectoryWatcher`'s destructor stops and joins
its thread, and file access goes through `std::ifstream`/`std::ofstream` rather
than `fopen`. `NonCopyable` exists precisely to keep such handles from being
copied into a double release.

The GSL half of the first enforcement point does not apply: this project does
not vendor the GSL, so an owning raw pointer cannot be marked `owner<T*>`. The
substitute is not to have owning raw pointers.

The rule's first Note is worth keeping, because it licenses something this
program does: relying on process shutdown for the last cleanup rather than
unwinding everything by hand.

## Consequences

### Positive

- The rule matches existing practice, so it costs nothing to adopt and gives
  `NonCopyable` and the RAII wrappers a written reason.
- Names the right target: "anything that can no longer be cleaned up", not
  "anything still allocated at exit".

### Negative

- Win32 handles are not RAII by nature, so each one needs a wrapper written by
  hand — `DirectoryWatcher` carries that cost, and the next platform handle
  will too.
- With no `owner<T*>`, a raw pointer's ownership is documented in prose or not
  at all.

### Neutral

- The lifetime safety profile mentioned in the second Note is not enforced here;
  no tool in this build checks it.

## Deviations to fix

No leak is currently known. `Repository`, `DirectoryWatcher` and `JsonFile` were
checked while writing this record and each releases what it acquires. This row
exists to record that the check was made, not that it found something.

## Related

- [0013](0013-p-7-catch-run-time-errors-early.md) — the previous rule.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the next rule.
- [0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md) —
  libgit2, whose handle is the main thing being owned.
