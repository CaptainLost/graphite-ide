# 0257. CP.2: Avoid data races

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Unless you avoid data races, nothing is guaranteed to work and subtle errors
will persist.

In a nutshell: if two threads can access the same object concurrently without
synchronisation, and at least one of them is a writer performing a non-const
operation, you have a data race. For how to use synchronisation well enough to
eliminate them, the guidelines refer the reader to a good book on concurrency
rather than trying to teach it in a rule.

Many data races exist in production software at this moment. A very simple one:

```cpp
int get_id()
{
  static int id = 1;
  return id++;
}
```

The increment is a data race, and it can go wrong in more than one way. Thread A
loads the value of `id`, the operating system switches A out for a period during
which other threads create hundreds of IDs, and then A resumes and writes back
its own read plus one. Or threads A and B load `id` and increment it
simultaneously, and both get the same ID. Local static variables are a common
source of data races.

A race can be on the stack just as easily:

```cpp
void f(fstream& fs, regex pattern)
{
    array<double, max> buf;
    int sz = read_vec(fs, buf, max);            // read from fs into buf
    gsl::span<double> s {buf};
    // ...
    auto h1 = async([&] { sort(std::execution::par, s); });     // spawn a task to sort
    // ...
    auto h2 = async([&] { return find_all(buf, sz, pattern); });   // spawn a task to find matches
    // ...
}
```

There is a nasty race on the elements of `buf`, because `sort` both reads and
writes them. All data races are nasty, and not all of them are as easy to spot
as this one.

The consequences are not confined to reading a stale value:

```cpp
// code not controlled by a lock
unsigned val;
if (val < 5) {
    // ... other thread can change val here ...
    switch (val) {
    case 0: // ...
    case 1: // ...
    case 2: // ...
    case 3: // ...
    case 4: // ...
    }
}
```

A compiler that does not know `val` can change will most likely implement that
`switch` as a five-entry jump table. A `val` outside `[0..4]` then jumps to an
address that could be anywhere in the program, and execution proceeds there.
All bets are off once you have a data race, and it can be worse still: reading
the generated code may reveal where the stray jump goes for a given value, which
makes it a security risk.

## Decision

Treat a data race as a defect of the same class as a use-after-free, not as a
timing problem to be tuned away.

- Establish, for every object reachable from more than one thread, either that
  all accesses are reads, or that every access is synchronised.
- Do not use a mutable function-local `static` as a counter, cache or ID
  source; if such a thing must be shared, make the object itself atomic or put
  it behind a mutex.
- Do not capture a local by reference into a task that outlives, or runs
  concurrently with, the enclosing scope's own use of it.
- Do not reason about the values a racing variable can take. A race is undefined
  behaviour, so a value outside the range you validated is a legal outcome and
  the generated code may not contain the bounds check you wrote.
- Reduce the surface: avoid global data, avoid static variables, prefer concrete
  types on the stack without passing pointers around, and make more use of
  immutable data — literals, `constexpr` and `const`.

## Rationale

The `switch` example is the argument, and it is the one most often missed. The
intuitive model of a race — two threads interleave, so you get one of the values
they wrote — is wrong. The compiler optimised on the assumption that no race
exists, so a race does not produce a surprising value; it produces a jump to an
arbitrary address, from code that appears to have validated its input.

That is why this cannot be traded off against performance or convenience. A
program with a data race has no defined behaviour at all, so no reasoning about
the rest of it survives.

## Consequences

### Positive

- Puts data races in the same category as memory corruption, which is where the
  standard puts them.
- The listed mitigations — less global data, fewer statics, more stack, more
  `const` — are cheap and reduce the surface before any synchronisation is
  designed.
- Immutable shared data needs no locking at all, so the cheapest fix is often
  available.

### Negative

- The rule states the requirement without teaching the technique; the source
  itself defers to the literature, so following it demands knowledge this record
  cannot supply.
- Proving the absence of a race is not generally possible, so compliance is
  argued rather than demonstrated.
- The mitigations conflict with other pressures: copying instead of sharing
  costs memory and time, and moving state onto the stack can mean passing more
  arguments.
- Detection tools are expensive and partial, so in practice some races ship.

### Neutral

- Whether a given access needs synchronisation depends on reachability, not on
  how the object was declared; a stack object shared with a task is as exposed
  as a global.
- `const` here means that no thread performs a non-const operation, which is a
  statement about the whole program, not about one declaration.

## Enforcement

- Some enforcement is possible; do at least something. Commercial and
  open-source tools exist, but solutions have costs and blind spots. Static
  tools often have many false positives, run-time tools often have a significant
  cost, and using several tools catches more than using one.
- Independently of tooling, reduce exposure: avoid global data, avoid static
  variables, use more concrete types on the stack and pass fewer pointers
  around, and use more immutable data — literals, `constexpr` and `const`.

## Related

- [0256](0256-cp-1-assume-that-your-code-will-run-as-part-of-a-multi-threaded-program.md)
  — the assumption that makes this rule apply everywhere.
- [0021](0021-i-2-avoid-non-const-global-variables.md) — the same race argument,
  made against globals specifically.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — there can be no
  race on a constant, which is the cheapest way to satisfy this rule.
- [0261](0261-cp-9-whenever-feasible-use-tools-to-validate-your-concurrent-code.md)
  — the tooling that partially enforces it.
