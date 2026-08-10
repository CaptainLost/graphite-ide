# 0039. I.30: Encapsulate rule violations

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

The point is to keep code simple and safe. Sometimes ugly, unsafe or error-prone
techniques are necessary for logical or performance reasons. If so, keep them
local, rather than infecting interfaces so that larger groups of programmers
have to be aware of the subtleties. Implementation complexity should, if at all
possible, not leak through interfaces into user code.

Consider a program that, depending on some form of input, should consume input
from a file, from the command line, or from standard input:

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

This violates the rule against uninitialised variables, the rule against
ignoring ownership, and the rule against magic constants. In particular, someone
has to remember to write, somewhere:

```cpp
if (owned) delete inp;
```

This particular example could be handled with a `unique_ptr` and a special
deleter that does nothing for `cin`, but that is complicated for novices — who
can easily encounter this problem — and it is an instance of a more general one,
where a property one would like to consider static, here ownership, infrequently
needs to be addressed at run time. The common, most frequent and safest cases
can be handled statically, so cost and complexity should not be added to those;
but the uncommon, less safe and necessarily more expensive cases must also be
coped with. So a class is written:

```cpp
class Istream { [[gsl::suppress(lifetime)]]
public:
    enum Opt { from_line = 1 };
    Istream() { }
    Istream(zstring p) : owned{true}, inp{new ifstream{p}} {}
    Istream(zstring p, Opt) : owned{true}, inp{new istringstream{p}} {}
    ~Istream() { if (owned) delete inp; }
    operator istream&() { return *inp; }
private:
    bool owned = false;
    istream* inp = &cin;
};
```

Now the dynamic nature of `istream` ownership has been encapsulated.
Presumably, a bit of checking for potential errors would be added in real code.

## Decision

Encapsulate rule violations.

- Where a rule in this directory must be broken for a logical or performance
  reason, confine the violation inside one type or one translation unit, and
  give the rest of the code an interface that obeys the rules.
- A suppression — of a warning, of an analysis rule — stays inside the
  encapsulation. It never appears in a header that callers include, and the
  behaviour it permits never becomes part of a public contract.
- State at the site why the violation is necessary, so a later reader can judge
  whether it still is.
- Add the checking that the encapsulated code needs but the raw technique
  omitted; the wrapper is the place where errors can still be caught.

## Rationale

This is the escape hatch that makes the rest of the section liveable. Without
it, a codebase that has one genuinely necessary unsafe technique has two bad
options: break the rule everywhere it is needed, or contort the design to avoid
it. Encapsulation gives a third: break it once, in a place chosen for the
purpose, and pay for the review and the tests there.

The scale argument is what makes the rule about interfaces rather than about
style. A violation inside a class is a cost borne by whoever maintains that
class. The same violation in an interface is a cost borne by everyone who calls
it, most of whom will not know it is there.

## Consequences

### Positive

- Unsafe techniques exist in known, few, reviewable places.
- Callers are not required to know the subtleties, because the interface does
  not expose them.
- The wrapper is a natural home for the error checking the raw technique lacked.

### Negative

- The wrapper is real code with a real interface, and a badly designed one can
  spread the problem more effectively than the inline violation would have.
- A suppression inside an encapsulation still turns the analyser off for that
  region, so the mistakes it would have caught are on the reviewer.
- It is genuinely hard to judge which rule-breaking is essential, which makes
  this the easiest record in the directory to invoke as an excuse.

### Neutral

- The rule does not say which violations are acceptable; it says where they may
  live.
- The `Istream` example is deliberately not a general solution; it is an
  illustration of a category.

## Enforcement

- Hard; it is hard to decide what rule-breaking code is essential.
- Flag rule suppressions that enable rule violations to cross interfaces.

## Related

- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — the philosophy rule this is the interface-level form of.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  `owner` and the dynamic-ownership problem the example turns on.
- [0008](0008-p-2-write-in-iso-standard-c.md) — localising a platform extension
  is the same move for a different kind of violation.
