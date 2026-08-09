# 0026. I.7: State postconditions

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The rule's strongest example — a mutex left locked because the postcondition was
never stated — has a direct analogue here: `DirectoryWatcher::Start` must leave
the watcher armed, and its destructor must leave the thread joined. Both are
stated in comments today.

## Decision

Adopt I.7 as written. Transcribed in full:

> **Reason.** To detect misunderstandings about the result and possibly catch
> erroneous implementations.

**Example, bad.** Consider:

```cpp
int area(int height, int width) { return height * width; }  // bad
```

Here, we (incautiously) left out the precondition specification, so it is not
explicit that `height` and `width` must be positive. We also left out the
postcondition specification, so it is not obvious that the algorithm
(`height * width`) is wrong for areas larger than the largest integer. Overflow
can happen. Consider using:

```cpp
int area(int height, int width)
{
    auto res = height * width;
    Ensures(res > 0);
    return res;
}
```

**Example, bad.** Consider a famous security bug:

```cpp
void f()    // problematic
{
    char buffer[MAX];
    // ...
    memset(buffer, 0, sizeof(buffer));
}
```

There was no postcondition stating that the buffer should be cleared and the
optimizer eliminated the apparently redundant `memset()` call:

```cpp
void f()    // better
{
    char buffer[MAX];
    // ...
    memset(buffer, 0, sizeof(buffer));
    Ensures(buffer[0] == 0);
}
```

**Note.** Postconditions are often informally stated in a comment that states the
purpose of a function; `Ensures()` can be used to make this more systematic,
visible, and checkable.

**Note.** Postconditions are especially important when they relate to something
that is not directly reflected in a returned result, such as a state of a data
structure used.

**Example.** Consider a function that manipulates a `Record`, using a mutex to
avoid race conditions:

```cpp
mutex m;

void manipulate(Record& r)    // don't
{
    m.lock();
    // ... no m.unlock() ...
}
```

Here, we "forgot" to state that the mutex should be released, so we don't know
if the failure to ensure release of the mutex was a bug or a feature. Stating
the postcondition would have made it clear:

```cpp
void manipulate(Record& r)    // postcondition: m is unlocked upon exit
{
    m.lock();
    // ... no m.unlock() ...
}
```

The bug is now obvious (but only to a human reading comments).

Better still, use RAII to ensure that the postcondition ("the lock must be
released") is enforced in code:

```cpp
void manipulate(Record& r)    // best
{
    lock_guard<mutex> _ {m};
    // ...
}
```

**Note.** Ideally, postconditions are stated in the interface/declaration so that
users can easily see them. Only postconditions related to the users can be
stated in the interface. Postconditions related only to internal state belong in
the definition/implementation.

**Enforcement.** (Not enforceable) This is a philosophical guideline that is
infeasible to check directly in the general case. Domain specific checkers (like
lock-holding checkers) exist for many toolchains.

## Rationale

Adopted as written. Its most useful move for this codebase is the escalation in
the mutex example: stating the postcondition is better than leaving it implicit,
but making it structurally impossible to violate is better still. That is the
argument for RAII, and it is the reason `DirectoryWatcher`'s thread lifetime
lives in a destructor rather than in a `Stop()` the caller must remember.

The second Note is the one that applies most here: this codebase's important
postconditions are about state, not return values — a watcher armed, a document
registered, a `.slnx` left syntactically whole after a splice — and those are
exactly the ones a return type cannot express.

## Consequences

### Positive

- Gives a written reason to prefer a destructor over a documented cleanup call,
  which is a recurring choice in `Fs` and `Git`.

### Negative

- Postconditions here live in comments, which the rule itself calls out as
  visible "only to a human reading comments". Without `Ensures()` there is no
  stronger form available.
- The rule cannot be checked, so a postcondition that quietly stops holding
  produces no signal at all.

### Neutral

- The overflow example has little purchase here; this codebase does little
  arithmetic on user-supplied sizes.

## Related

- [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md) — the previous
  rule.
- [0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md) — the next
  rule, on the form.
- [0014](0014-p-8-do-not-leak-any-resources.md) — RAII, which the rule prefers to
  a stated postcondition.
