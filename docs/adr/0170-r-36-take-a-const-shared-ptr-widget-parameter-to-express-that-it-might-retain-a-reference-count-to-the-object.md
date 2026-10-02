# 0170. R.36: Take a const shared_ptr<widget>& parameter to express that it might retain a reference count to the object

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

Taking a `shared_ptr` by reference to `const` makes explicit that the function
might retain a reference count to the object — that on some path it will copy
the pointer somewhere that outlives the call, and on others it will not.

The three forms read together:

```cpp
void share(shared_ptr<widget>);            // share -- "will" retain refcount
void reseat(shared_ptr<widget>&);          // "might" reseat ptr
void may_share(const shared_ptr<widget>&); // "might" retain refcount
```

The vocabulary is deliberately fine-grained. By value means the parameter
already holds a share of its own and the function is an owner. Non-`const`
reference means the caller's pointer may end up referring to something else.
Reference to
`const` means neither is guaranteed: the function can copy the pointer if it
decides to, and if it does not, no count was ever touched.

This is the weakest of the three, and the guidelines are visibly unfinished on
it: the upstream rule title and its stated reason both carry an unresolved
placeholder where the semantics should be. What survives, and is consistent with
the surrounding rules and their enforcement, is the reading given above. Where
the function always retains, the by-value form says so more strongly and
optimises better; where it never retains, it should be taking `widget&` and the
enforcement checks say so.

## Decision

Take `const std::shared_ptr<widget>&` only when the function might, but need
not, retain a share of the object.

- Use it where retention is conditional: a registration that only happens on
  some paths, a cache that stores the pointer if a lookup misses.
- Take `shared_ptr<widget>` by value where the function always retains, and move
  it into its destination.
- Take `widget&`, or `widget*` if absence is possible, where the function never
  retains. A `const shared_ptr<widget>&` that is never copied is a pessimised
  reference that also narrows the callers.
- Do not use this form as a general "cheap way to pass a `shared_ptr`"; it is a
  statement about lifetime, and callers will read it as one.

## Rationale

The three forms are worth distinguishing because the caller's obligations differ
under each. Under the by-value form the caller can drop its own pointer
immediately; under this one it cannot conclude anything, so it must keep its
pointer alive across the call and cannot know afterwards whether the object
outlives it. That is genuinely less information, which is why this form belongs
only where the retention really is conditional, and why choosing it out of habit
degrades every signature that uses it.

## Consequences

### Positive

- Conditional retention has a spelling, instead of being written as either of
  the two forms that overstate what the function does.
- No reference count is touched on the paths that do not retain.
- Completes the parameter vocabulary, so a reader can infer the lifetime
  contract from the parameter form alone.

### Negative

- The upstream rule is admittedly unfinished — its title and reason both end in
  an unresolved marker — so this is the least authoritative rule in the section,
  and the reading here is reconstructed from the examples and the enforcement
  rather than stated by the source.
- "Might retain" is the least useful thing a signature can say: the caller must
  plan for both outcomes and can verify neither.
- The form is a magnet for misuse, because it looks like the ordinary
  pass-by-`const`-reference idiom, and a function that never copies the pointer
  satisfies the syntax while meaning nothing.
- It still restricts callers to those holding the widget in a `shared_ptr`, with
  none of the guarantees the by-value form provides in exchange.

### Neutral

- The unresolved markers in the upstream text are not carried over; this record
  states the semantics the surrounding rules imply.
- Whether the widget should be shared-owned at all is decided elsewhere.

## Enforcement

- (Simple) Warn if a function takes a shared pointer parameter by lvalue
  reference and does not either assign to it or call `reset()` on it on at least
  one code path. Suggest taking a `T*` or `T&` instead.
- (Simple) Warn if a function takes a shared pointer by value or by reference to
  `const` and does not copy or move it to another shared pointer on at least one
  code path. Suggest taking a `T*` or `T&` instead.
- (Simple) Warn if a function takes a shared pointer by rvalue reference.
  Suggest taking it by value instead.

## Related

- [0168](0168-r-34-take-a-shared-ptr-widget-parameter-to-express-shared-ownership.md)
  — the form to use when retention is unconditional.
- [0169](0169-r-35-take-a-shared-ptr-widget-parameter-to-express-that-a-function-might-reseat-the-shared-pointer.md)
  — the third form, and the one this is most often confused with.
- [0164](0164-r-30-take-smart-pointers-as-parameters-only-to-explicitly-express-lifetime-semantics.md)
  — the rule that says most functions should take none of these.
- [0165](0165-r-31-if-you-have-non-std-smart-pointers-follow-the-basic-pattern-from-std.md)
  — the shared-pointer parameter guideline these forms belong to.
