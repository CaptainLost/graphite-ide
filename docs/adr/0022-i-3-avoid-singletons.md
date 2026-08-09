# 0022. I.3: Avoid singletons

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

Two singletons exist in Core: `Application::Get()` in
`Core/src/Core/App/Application.h:39` and `Logger::Get()` in
`Core/src/Core/Base/Log.h:49`. Both are the classic static accessor returning a
reference, and both are reachable from any translation unit that includes the
header.

## Decision

Adopt I.3 as written. Transcribed in full:

> **Reason.** Singletons are basically complicated global objects in disguise.

**Example.**

```cpp
class Singleton {
    // ... lots of stuff to ensure that only one Singleton object is created,
    // that it is initialized properly, etc.
};
```

There are many variants of the singleton idea. That's part of the problem.

**Note.** If you don't want a global object to change, declare it `const` or
`constexpr`.

**Exception.** You can use the simplest "singleton" (so simple that it is often
not considered a singleton) to get initialization on first use, if any:

```cpp
X& myX()
{
    static X my_x {3};
    return my_x;
}
```

This is one of the most effective solutions to problems related to
initialization order. In a multi-threaded environment, the initialization of the
static object does not introduce a race condition (unless you carelessly access
a shared object from within its constructor).

Note that the initialization of a local static does not imply a race condition.
However, if the destruction of `X` involves an operation that needs to be
synchronized we must use a less simple solution. For example:

```cpp
X& myX()
{
    static auto p = new X {3};
    return *p;  // potential leak
}
```

Now someone must delete that object in some suitably thread-safe way. That's
error-prone, so we don't use that technique unless

- `myX` is in multi-threaded code,
- that `X` object needs to be destroyed (e.g., because it releases a resource),
  and
- `X`'s destructor's code needs to be synchronized.

If you, as many do, define a singleton as a class for which only one object is
created, functions like `myX` are not singletons, and this useful technique is
not an exception to the no-singleton rule.

**Enforcement.** Very hard in general.

- Look for classes with names that include `singleton`.
- Look for classes for which only a single object is created (by counting
  objects or by examining constructors).
- If a class `X` has a public static function that contains a function-local
  static of the class' type `X` and returns a pointer or reference to it, ban
  that.

## Rationale

Adopted as written — including its third enforcement point, which describes
`Logger::Get()` exactly and says to ban it.

The rule is adopted rather than waived because its complaint is real here. Both
singletons make their consumers untestable in isolation: a component that logs
cannot be handed a different logger, and anything reaching `Application::Get()`
cannot run without an application. That is the same argument
[0002](0002-keep-core-free-of-ide-knowledge.md) makes about the headless test
suite, and these two are where it leaks.

The rule's Exception does not cover them. `myX`-style initialization-on-first-use
is exempt only when it is not treated as *the* instance of a class; both of
these are.

## Consequences

### Positive

- Names two concrete obstacles to testing rather than leaving them as
  background discomfort.
- Points at the cheaper alternative from
  [0021](0021-i-2-avoid-non-const-global-variables.md): if the state must be
  global, a plain global object is the more honest form.

### Negative

- Removing `Logger::Get()` means the `CORE_INFO`/`CORE_WARN`/`CORE_ERROR` macros
  need a logger from somewhere, and threading one through every call site is a
  large change for a facility that genuinely is process-wide. This is the
  deviation least likely to be fixed, and saying otherwise would be dishonest.
- `Application::Get()` has fewer callers and is the realistic one to remove
  first.

### Neutral

- Neither singleton allocates with `new`, so the destruction hazard the rule
  spends most of its length on does not apply.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| `Application::Get()` — static accessor to the single application instance | `Core/src/Core/App/Application.h:39` | The realistic one to remove; few callers |
| `Logger::Get()` — static accessor behind the logging macros | `Core/src/Core/Base/Log.h:49` | Matches the rule's third enforcement point verbatim; removing it is a large change |

## Related

- [0021](0021-i-2-avoid-non-const-global-variables.md) — the previous rule,
  whose Exception argues a global object beats a singleton.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the next
  rule.
- [0002](0002-keep-core-free-of-ide-knowledge.md) — the testability these two
  undercut.
