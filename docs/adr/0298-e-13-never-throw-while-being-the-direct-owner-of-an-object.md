# 0298. E.13: Never throw while being the direct owner of an object

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

That would be a leak.

```cpp
void leak(int x)   // don't: might leak
{
    auto p = new int{7};
    if (x < 0) throw Get_me_out_of_here{};  // might leak *p
    // ...
    delete p;   // we might never get here
}
```

One way of avoiding such problems is to use resource handles consistently:

```cpp
void no_leak(int x)
{
    auto p = make_unique<int>(7);
    if (x < 0) throw Get_me_out_of_here{};  // will delete *p if necessary
    // ...
    // no need for delete p
}
```

Another solution, often better, is to use a local variable and eliminate the
explicit use of pointers altogether:

```cpp
void no_leak_simplified(int x)
{
    vector<int> v(7);
    // ...
}
```

If there is a local "thing" that requires cleanup but is not represented by an
object with a destructor, such cleanup must also be done before a throw.
Sometimes a `finally` object can make such unsystematic cleanup a bit more
manageable.

"Direct owner" is the operative phrase. The problem in `leak` is not that a
pointer exists but that the *function frame* is what owns the allocation:
nothing else knows about it, so unwinding past the frame loses it. As soon as
ownership belongs to an object — a `unique_ptr`, a `vector` — the frame is no
longer the owner, and unwinding runs a destructor that releases it.

The same reasoning applies to an early return, which is the same defect with a
quieter symptom: `delete p` is unreached either way. The rule names throwing
because a throw is the exit a function's own text does not show: a call to
anything that might throw adds one without a `return` appearing anywhere.

## Decision

Never throw while a resource is held directly by the function frame.

- Do not allocate with a bare `new` or `malloc` and hold the result in a raw
  local across anything that can throw — including calls that can throw.
- Give the resource to an owner immediately: a `unique_ptr` where pointer
  semantics are needed, and a local value such as a `vector` or `string` where
  they are not.
- Where a local requires cleanup that no destructor performs — a handle from a C
  API, a flag that must be reset — either wrap it in a handle type or attach a
  `finally` object at the point of acquisition.
- Do not attempt to fix this by releasing before each exit. That is the `f2`
  shape from [0294](0294-e-6-use-raii-to-prevent-leaks.md): correct today, and
  broken by the next exit added to the function.
- Apply the rule to early returns exactly as to throws. The leak is the same;
  only its visibility differs.

## Rationale

The distinguishing feature of this failure is that the dangerous code looks
finished. `leak` has a `delete`, in the right place, matching its `new`; nothing
is missing from the text. What is missing is a path, and paths are not visible
in the way statements are.

Making an object the owner converts the problem from one about control flow into
one about scope, and scope is a property the compiler already tracks exactly.
That is why the fix is total rather than partial: it does not matter how many
exits the function grows afterwards.

## Consequences

### Positive

- Adding an early exit to a function can no longer introduce a leak.
- Ownership becomes visible in the declaration of the local, rather than implied
  by the presence of a matching release further down.
- Exits from called functions — the invisible ones — are covered by the same
  mechanism as exits written locally.

### Negative

- C APIs hand out resources that no standard handle owns, so each one needs a
  custom deleter or a wrapper type before the rule can be satisfied, and that
  wrapper is code that exists only to satisfy the rule.
- Two-phase acquisition, where a resource is obtained and then registered, has a
  genuine window in which the frame is the direct owner, and closing it needs
  care rather than a rule.
- `finally` covers the remaining cases but is itself fragile: it is a local
  whose name nobody uses, so it is easy to move, shadow, or delete by accident.
- Nothing detects a violation. A raw `new` in a local is legal, common in older
  code, and only wrong in the presence of a path that may not exist yet.

### Neutral

- The rule is about direct ownership only. Holding a non-owning pointer or
  reference across an exit is a lifetime question, not a leak.
- Which handle is used is unconstrained; the rule requires only that one exists.

## Related

- [0294](0294-e-6-use-raii-to-prevent-leaks.md) — the general form of this rule,
  with the full set of examples.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the philosophy rule behind
  both.
- [0304](0304-e-19-use-a-final-action-object-to-express-cleanup-if-no-suitable-resource-handle-is-available.md)
  — what to do for cleanup no destructor performs.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the interface rule that keeps raw owning pointers from arriving in the first
  place.
