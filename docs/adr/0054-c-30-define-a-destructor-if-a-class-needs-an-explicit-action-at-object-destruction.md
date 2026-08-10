# 0054. C.30: Define a destructor if a class needs an explicit action at object destruction

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A destructor is implicitly invoked at the end of an object's lifetime. If the
default destructor is sufficient, use it. Only define a non-default destructor
if a class needs to execute code that is not already part of its members'
destructors.

"Does this class need a destructor?" is a surprisingly insightful design
question. For most classes the answer is no, either because the class holds no
resources or because destruction is handled by the rule of zero — its members
take care of themselves. If the answer is yes, much of the rest of the design
follows.

```cpp
template<typename A>
struct final_action {   // slightly simplified
    A act;
    final_action(A a) : act{a} {}
    ~final_action() { act(); }
};

template<typename A>
final_action<A> finally(A act)   // deduce action type
{
    return final_action<A>{act};
}

void test()
{
    auto act = finally([] { cout << "Exit test\n"; });  // establish exit action
    // ...
    if (something) return;   // act done here
    // ...
} // act done here
```

The whole purpose of `final_action` is to get a piece of code, usually a lambda,
executed upon destruction.

There are two general categories of class that need a user-defined destructor:
one with a resource that is not already represented as a class with a
destructor, such as a `vector` or a transaction class; and one that exists
primarily to execute an action upon destruction, such as a tracer or a
`final_action`.

```cpp
class Foo {   // bad; use the default destructor
public:
    // ...
    ~Foo() { s = ""; i = 0; vi.clear(); }  // clean up
private:
    string s;
    int i;
    vector<int> vi;
};
```

The default destructor does this better, more efficiently, and cannot get it
wrong. If the default destructor is needed but its generation has been
suppressed — for instance by defining a move constructor — use `=default`.

## Decision

Write a destructor when, and only when, destruction must do something the
members do not already do.

- Write one if the class holds a resource with no destructor of its own, or if
  the class exists to perform an action at the end of a scope.
- Do not write one to clear, empty or reset members that will be destroyed
  anyway; that work is redundant, slower, and easy to get wrong.
- Where the default behaviour is wanted but has been suppressed by another
  declaration, write `~X() = default` rather than an empty body.
- Prefer replacing the untidy resource with a handle type so that no destructor
  is needed at all, per
  [0051](0051-c-20-if-you-can-avoid-defining-default-operations-do.md).
- Once a destructor is declared, declare or `=delete` the copy and move
  operations too, per
  [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md).

## Rationale

A user-declared destructor is not a local addition; it changes the class's
lifecycle, suppressing implicit move generation and pulling the type out of the
rule of zero. Writing one for no reason therefore costs more than the lines it
occupies — it silently converts moves to copies and puts the class permanently
under the rule of five.

The `Foo` example makes the point that a destructor which merely tidies members
is not neutral but actively worse: it does work the members are about to do
themselves, and it is one edit away from being incomplete when a member is
added.

## Consequences

### Positive

- Classes that do not need a destructor do not have one, and keep implicitly
  generated moves.
- The presence of a destructor becomes a signal that the class owns something.
- Scope-exit actions have a stated, idiomatic home.

### Negative

- The judgement "needs an explicit action" is not mechanical, and the two
  categories the guideline gives do not settle every case: a class that must
  log, flush, unregister from an observer list, or assert a postcondition at
  destruction is arguably in neither.
- Declaring a destructor to satisfy this rule immediately imposes the four other
  declarations of the rule of five, so a one-line need becomes a five-line
  obligation.
- Work done in a destructor cannot report failure, per
  [0059](0059-c-36-a-destructor-must-not-fail.md), so putting a genuine action
  there means accepting that it must succeed or terminate.

### Neutral

- A destructor declared only to be `virtual` is required by
  [0058](0058-c-35-a-base-class-destructor-should-be-either-public-and-virtual-or-protected-and-non-virtual.md)
  and is not a violation of this record.
- The rule says when to write one, not what it should contain beyond releasing
  what the class holds.

## Enforcement

- Look for likely implicit resources, such as pointer and reference members.
- Look for classes with destructors even though all their data members already
  have destructors.

## Related

- [0051](0051-c-20-if-you-can-avoid-defining-default-operations-do.md) — the
  rule of zero, which this record is the exception to.
- [0055](0055-c-31-all-resources-acquired-by-a-class-must-be-released-by-the-classs-destructor.md)
  — what the destructor must then cover.
- [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md)
  — what declaring one obliges.
- [0057](0057-c-33-if-a-class-has-an-owning-pointer-member-define-a-destructor.md)
  — the specific case that most often triggers this record.
