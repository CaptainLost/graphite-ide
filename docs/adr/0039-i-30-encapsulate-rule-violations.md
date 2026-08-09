# 0039. I.30: Encapsulate rule violations

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The rule that governs every other record in this directory: it says what to do
when a rule cannot be followed. That is a live question here, because several
records — I.4 on `void*`, P.2 on Win32, I.10 on exceptions — describe places
where the guidelines are knowingly not met.

## Decision

Adopt I.30 as written. Transcribed in full:

> **Reason.** To keep code simple and safe. Sometimes, ugly, unsafe, or
> error-prone techniques are necessary for logical or performance reasons. If
> so, keep them local, rather than "infecting" interfaces so that larger groups
> of programmers have to be aware of the subtleties. Implementation complexity
> should, if at all possible, not leak through interfaces into user code.

**Example.** Consider a program that, depending on some form of input (e.g.,
arguments to `main`), should consume input from a file, from the command line,
or from standard input. We might write

```cpp
bool owned;
owner<istream*> inp;
switch (source) {
case std_in:        owned = false; inp = &cin;                       break;
case command_line:  owned = true;  inp = new istringstream{argv[2]}; break;
case file:          owned = true;  inp = new ifstream{argv[2]};      break;
}
istream& in = *inp;
```

This violated the rule against uninitialized variables, the rule against
ignoring ownership, and the rule against magic constants. In particular, someone
has to remember to somewhere write

```cpp
if (owned) delete inp;
```

We could handle this particular example by using `unique_ptr` with a special
deleter that does nothing for `cin`, but that's complicated for novices (who can
easily encounter this problem) and the example is an example of a more general
problem where a property that we would like to consider static (here, ownership)
needs infrequently be addressed at run time. The common, most frequent, and
safest examples can be handled statically, so we don't want to add cost and
complexity to those. But we must also cope with the uncommon, less-safe, and
necessarily more expensive cases. Such examples are discussed in [Str15].

So, we write a class

```cpp
class Istream { [[gsl::suppress("lifetime")]]
public:
    enum Opt { from_line = 1 };
    Istream() { }
    Istream(czstring p) : owned{true}, inp{new ifstream{p}} {}            // read from file
    Istream(czstring p, Opt) : owned{true}, inp{new istringstream{p}} {}  // read from command line
    ~Istream() { if (owned) delete inp; }
    operator istream&() { return *inp; }
private:
    bool owned = false;
    istream* inp = &cin;
};
```

Now, the dynamic nature of `istream` ownership has been encapsulated.
Presumably, a bit of checking for potential errors would be added in real code.

**Enforcement.**

- Hard, it is hard to decide what rule-breaking code is essential
- Flag rule suppression that enable rule-violations to cross interfaces

## Rationale

Adopted as written, and it is the rule that gives this whole directory its
shape. Its second enforcement point states the test that matters: a rule may be
broken, but the breakage must not cross an interface.

Measured against that test, this codebase's known violations sort into two
piles:

- **Encapsulated, and therefore acceptable.** Win32 in `Fs` and `Platform`
  behind `FileSystem`, `FileDialog`, `Shell` and `Process`; libgit2's C handles
  behind `Repository`; `DirectoryWatcher`'s overlapped I/O behind `Start`/`Stop`.
  Each is ugly inside and clean at the seam.
- **Leaking through an interface, and therefore not.** `Window::NativeHandle()`
  returning `void*` puts the platform type into the interface rather than behind
  it. `EventBus`'s `const void*` payload is a violation that its own template
  wrapper mostly reseals, which is the borderline case.

That sort is the useful output of this record, and it is a better test than "is
this ugly?" — plenty of necessary code is ugly.

## Consequences

### Positive

- Converts "we broke a rule here" from an apology into a checkable claim: is the
  breakage visible from outside?
- Gives every future exception a required shape — a type or a function whose
  interface is clean — rather than a comment saying why the rule did not apply.

### Negative

- Encapsulation is not free, and the rule can be used to justify a wrapper type
  for every awkward call, which is its own complexity. `Repository` and
  `DirectoryWatcher` are each a file of code that exists to hide another file's
  worth of code.
- "Crosses an interface" is clear for a type but fuzzy for a header full of free
  functions, and this codebase has both.

### Neutral

- The `[[gsl::suppress]]` attribute in the example has no equivalent here, since
  no analyser reads suppressions — see
  [0018](0018-p-12-use-supporting-tools-as-appropriate.md).

## Deviations to fix

| What | Where | Note |
|---|---|---|
| Platform type leaks through the interface rather than being hidden behind it | `Core/src/Core/Platform/Window.h:41` — `void* NativeHandle()` | Fails the rule's own test: the violation crosses the interface. An opaque handle type would fix it |

## Related

- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — the
  previous rule, and one sanctioned way to encapsulate.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — P.11, of which this is the interface-shaped statement.
- [0002](0002-keep-core-free-of-ide-knowledge.md) — the boundary this test is
  applied across.
