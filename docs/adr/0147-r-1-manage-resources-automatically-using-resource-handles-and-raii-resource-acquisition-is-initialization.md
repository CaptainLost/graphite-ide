# 0147. R.1: Manage resources automatically using resource handles and RAII (Resource Acquisition Is Initialization)

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

A resource is anything that must be acquired and then, explicitly or implicitly,
released: memory, file handles, sockets, locks. The reason it must be released
is usually that it is in short supply, so even a delayed release can do harm.
The aim of resource management is to leak nothing and to hold nothing longer
than needed. The entity responsible for releasing a resource is its owner.

Manual management means remembering the release on every path, exactly once:

```cpp
void send(X* x, string_view destination)
{
    auto port = open_port(destination);
    my_mutex.lock();
    // ...
    send(port, x);
    // ...
    my_mutex.unlock();
    close_port(port);
    delete x;
}
```

In this code you have to remember to unlock, `close_port` and `delete` on all
paths, and to do each exactly once. Further, if any of the code marked `...`
throws, then `x` is leaked and `my_mutex` remains locked.

C++'s language-enforced constructor/destructor symmetry mirrors the symmetry
inherent in acquire/release function pairs such as `fopen`/`fclose`,
`lock`/`unlock` and `new`/`delete`. Encapsulating each resource in an object
that acquires in its constructor and releases in its destructor makes the
pairing the compiler's job:

```cpp
void send(unique_ptr<X> x, string_view destination)  // x owns the X
{
    Port port{destination};            // port owns the PortHandle
    lock_guard<mutex> guard{my_mutex}; // guard owns the lock
    // ...
    send(port, x);
    // ...
} // automatically unlocks my_mutex and deletes the pointer in x
```

All cleanup is now automatic, performed once on all paths whether or not there
is an exception. As a bonus, the function advertises in its signature that it
takes over ownership of the pointer.

`Port` is a handy wrapper that encapsulates the resource:

```cpp
class Port {
    PortHandle port;
public:
    Port(string_view destination) : port{open_port(destination)} { }
    ~Port() { close_port(port); }
    operator PortHandle() { return port; }
    // port handles can't usually be cloned, so disable copying and assignment
    // if necessary
    Port(const Port&) = delete;
    Port& operator=(const Port&) = delete;
};
```

Where a resource is ill-behaved, in that it is not already represented as a
class with a destructor, wrap it in a class of your own, or use a scope-guard
type that runs an action at the end of the scope.

## Decision

Manage every resource through an object whose constructor acquires it and whose
destructor releases it.

- Acquire in the constructor, release in the destructor. Never pair an
  acquisition with a release statement that a `return`, a `break` or a thrown
  exception can skip.
- Prefer an existing handle — `std::ifstream`, `std::unique_ptr`,
  `std::lock_guard`, `std::scoped_lock` — to a hand-written one, and a
  hand-written handle to a bare acquire/release pair.
- When wrapping a handle that cannot be duplicated, delete the copy operations
  and decide explicitly whether moving is meaningful, as `Port` does.
- Wrap an ill-behaved C-style resource at the point it enters the codebase, so
  that the raw acquire/release pair appears in exactly one small class.
- Express ownership transfer in the signature by taking the handle by value, as
  `send(unique_ptr<X>, string_view)` does.

## Rationale

The pairing of acquire and release is not a rule the programmer applies; it is a
rule the language already enforces between a constructor and a destructor. RAII
is the trick of borrowing that enforcement for resources it was not originally
about. Once borrowed, the guarantee is total: destructors run on every exit
path, including exceptional ones, so no future edit that adds a branch can
reintroduce the leak. That is a stronger property than "we were careful", which
is all the manual version ever had.

## Consequences

### Positive

- Every exit path releases, including paths added years later by someone who
  never read this record.
- The lifetime of a resource becomes the lifetime of an object, which readers,
  debuggers and tools already track.
- Signatures start carrying ownership information, because the handle type is
  part of the type.

### Negative

- Each ill-behaved resource costs a small class, with copy and move semantics
  reasoned about, in place of two function calls.
- Destructors cannot report failure usefully. A resource whose release can fail
  — a buffered file where `close` flushes — has nowhere to send that error:
  throwing from a destructor during stack unwinding terminates the program, so
  such handles need an explicit `close`-style operation in addition to the
  destructor.
- Release is tied to scope exit. When that is the wrong moment, the workarounds
  — `release`, `reset`, deferred destruction, an extra scope — are fiddlier than
  the manual code would have been.
- A wrapper that is implicitly convertible to the raw handle, as `Port` is, lets
  the raw handle escape the wrapper and outlive it.

### Neutral

- Which handle type suits which resource is not decided here.
- This record says how a resource is managed, not where it should be acquired or
  how long it should live.

## Related

- [0014](0014-p-8-do-not-leak-any-resources.md) — the philosophy rule this whole
  section implements; RAII is named there as the mechanism.
- [0151](0151-r-5-prefer-scoped-objects-dont-heap-allocate-unnecessarily.md) — a
  scoped object is the simplest resource handle there is.
- [0154](0154-r-11-avoid-calling-new-and-delete-explicitly.md) — the naked
  `new`/`delete` pair this rule replaces.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — the wrapper class is the encapsulation that rule asks for.
