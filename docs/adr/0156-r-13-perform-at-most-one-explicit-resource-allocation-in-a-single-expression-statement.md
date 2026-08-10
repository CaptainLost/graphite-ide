# 0156. R.13: Perform at most one explicit resource allocation in a single expression statement

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

If two explicit resource allocations appear in one statement, resources can
leak, because the order of evaluation of many subexpressions — including
function arguments — is unspecified.

```cpp
void fun(shared_ptr<Widget> sp1, shared_ptr<Widget> sp2);
```

This can be called like so:

```cpp
// BAD: potential leak
fun(shared_ptr<Widget>(new Widget(a, b)), shared_ptr<Widget>(new Widget(c, d)));
```

The upstream argument is that a compiler may interleave the two argument
expressions: it can allocate memory for both objects first, then attempt both
`Widget` constructor calls, and if one constructor throws, the other object's
memory is never released.

The subtle problem has a simple solution — never perform more than one explicit
resource allocation in a single expression statement:

```cpp
shared_ptr<Widget> sp1(new Widget(a, b)); // Better, but messy
fun(sp1, new Widget(c, d));
```

The best solution is to avoid explicit allocation entirely and use factory
functions that return owning objects:

```cpp
fun(make_shared<Widget>(a, b), make_shared<Widget>(c, d)); // Best
```

Write your own factory wrapper if there is not one already.

One qualification matters here. Since C++17 the argument expressions of a
function call are indeterminately sequenced with respect to one another: each is
evaluated completely before the next begins, even though which goes first is
still unspecified. Under those rules the interleaving the example describes —
both allocations, then both constructors — cannot occur, and that particular
call no longer leaks. Whichever argument is evaluated first finishes as a
complete `shared_ptr` temporary, and unwinding out of the full-expression
destroys it.

What C++17 fixed is narrower than the rule. It helps only because each
allocation in the example is handed to an owner inside its own argument
expression. Two allocations that are still raw when the call is entered leak
exactly as they always did:

```cpp
void adopt(Widget* first, Widget* second);   // bad interface, but a common one

// BAD, and still bad in C++20: whichever argument is evaluated first leaves
// an unowned Widget, and evaluating the other one can throw
adopt(new Widget(a, b), new Widget(c, d));
```

Interleaving itself also survives wherever subexpressions remain merely
unsequenced rather than indeterminately sequenced — the operands of most
built-in binary operators, for instance — so a statement that allocates on both
sides of such an operator has the original hazard intact. Beyond that, the
`make_` form is shorter and mentions the type once.

## Decision

Write at most one explicit resource allocation per expression statement, and
prefer none.

- Build owning objects with `make_unique` and `make_shared` rather than passing
  the result of `new` to a smart pointer constructor.
- Where no factory exists, write one, rather than passing a raw `new` into a
  call.
- If an explicit allocation is unavoidable, give it a statement of its own and
  name the resulting owner, then pass the owner.
- Do not mix an explicit allocation with any other operation that can fail in
  the same expression.

## Rationale

The rule outlives the example that motivated it. C++17 removed the interleaving,
not the leak: what leaks is an allocation that is still unowned when some other
part of the same full-expression fails, and a statement with two explicit
allocations is a statement in which that has to be checked twice, for each of
them, in an order the language does not fix. Allowing at most one makes the
check trivial, and the fix that removes it altogether — `make_shared` — is also
the shorter, faster and more readable form, so there is nothing to trade off.

## Consequences

### Positive

- Ownership is established at the moment of acquisition in every argument,
  independent of evaluation order.
- The recommended fix produces shorter code that names the type once.
- The rule is easy to apply mechanically: count the allocations in the
  statement.

### Negative

- The upstream text argues the rule from an example that C++17 fixed, so anyone
  who checks the source finds an obsolete justification for a rule that is still
  sound on other grounds. This record has to supply those grounds itself, which
  makes it harder to defend in review than a rule whose source still holds.
- `make_shared` and `make_unique` cannot express a custom deleter or a custom
  allocator, so exactly the allocations that are most delicate must still be
  written explicitly.
- Splitting an expression into several statements introduces named temporaries
  that exist only to satisfy the rule, and the upstream fix itself is described
  as "messy".
- Enforcement depends on recognising which calls allocate a resource, which in
  general cannot be done.

### Neutral

- The rule is about expression statements; it says nothing about how many
  allocations a function may perform overall.
- The hazard needs some operation in the same full-expression to fail, so code
  that reports errors by return value rather than by throwing sees fewer
  instances of it — but not none, because the allocation itself can still throw.

## Enforcement

- Flag expressions containing multiple explicit resource allocations. The
  practical difficulty is recognising how many direct resource allocations there
  are.

## Related

- [0155](0155-r-12-immediately-give-the-result-of-an-explicit-resource-allocation-to-a-manager-object.md)
  — the companion rule about the moment of acquisition.
- [0161](0161-r-22-use-make-shared-to-make-shared-ptrs.md) and
  [0162](0162-r-23-use-make-unique-to-make-unique-ptrs.md) — the fix, stated as
  rules of their own.
- [0154](0154-r-11-avoid-calling-new-and-delete-explicitly.md) — no explicit
  allocation at all is better than one.
