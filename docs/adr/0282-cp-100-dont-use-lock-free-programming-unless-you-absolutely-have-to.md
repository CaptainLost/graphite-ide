# 0282. CP.100: Don't use lock-free programming unless you absolutely have to

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Synchronisation using mutexes and condition variables can be relatively
expensive, and it can lead to deadlock. For performance, and to eliminate the
possibility of deadlock, we sometimes have to use the tricky low-level
"lock-free" facilities that rely on briefly gaining exclusive ("atomic") access
to memory. Lock-free programming is nevertheless error-prone and requires
expert-level knowledge of language features, machine architecture, and data
structures.

```cpp
// bad
extern atomic<Link*> head;             // the shared head of a linked list

Link* nh = new Link(data, nullptr);    // make a link ready for insertion
Link* h = head.load();                 // read the shared head of the list
do {
    if (h->data <= data) break;        // if so, insert elsewhere
    nh->next = h;                      // next element is the previous head
} while (!head.compare_exchange_weak(h, nh));    // write nh to head or to h
```

Spot the bug. It would be really hard to find through testing. The subject to
read up on is the ABA problem.

There is an exception. Atomic variables can be used simply and safely, as long
as you are using the sequentially consistent memory model
(`memory_order_seq_cst`), which is the default.

Note also that the higher-level concurrency mechanisms — threads and mutexes —
are themselves implemented using lock-free programming. The alternative the
guidelines offer is to use lock-free data structures implemented by others as
part of some library.

## Decision

Reach for a mutex first, and treat lock-free code as a last resort.

- Express shared state with mutexes, condition variables, and the higher-level
  facilities built on them.
- Where an atomic variable is genuinely the right tool, use it with the default
  sequentially consistent ordering and leave the ordering argument alone.
- Do not hand-craft a lock-free container or a compare-and-swap loop over your
  own linked structure; take a lock-free data structure from a library that
  someone qualified maintains.
- Where lock-free code is unavoidable, confine it to one place behind an
  ordinary interface, so that the expertise it demands is needed in one file
  rather than everywhere it is used.

## Rationale

The bad example is the whole argument. It reads correctly, it compiles, it will
pass the tests you would think to write, and it is wrong — the ABA problem means
a compare-and-swap can succeed against a pointer value that is no longer the
node it was. Code whose defects are invisible to review and to testing is code
whose cost is not paid when it is written but at some unpredictable later point,
which is precisely the trade the phrase "unless you absolutely have to" is
asking about.

## Consequences

### Positive

- Concurrency bugs stay within the class that mutexes produce, which are at
  least reproducible and reasoned about with familiar tools.
- Restricting atomics to `memory_order_seq_cst` keeps the one lock-free facility
  in normal use inside its documented-simple case.
- Buying the data structure rather than writing it puts the hard part in the
  hands of people who specialise in it.
- Deadlock, the failure mode the rule accepts, is at least detectable at run
  time.

### Negative

- The rule accepts a real cost by its own admission: mutexes can be relatively
  expensive and they can deadlock, and this rule tells you to take that.
- "Absolutely have to" is not a checkable threshold. Two reviewers can disagree
  about whether a case qualifies, and neither has an argument the other must
  accept.
- Taking a lock-free data structure from a library relocates the expertise
  rather than removing it; you still have to judge whether that library's
  guarantees match your usage, and taking on the dependency is itself a cost.
- The exception is narrower than it looks: the default ordering rules out a data
  race on the variable itself, but nothing stops two individually correct
  `seq_cst` operations from composing into a logical race, because the pair of
  them is not atomic.

### Neutral

- The rule is about writing lock-free code, not about depending on it. Every
  mutex you use is implemented with it.
- Nothing here forbids atomics; it forbids inventing lock-free algorithms out of
  them.

## Related

- [0283](0283-cp-101-distrust-your-hardware-compiler-combination.md) — what to
  do about the platform if you write lock-free code anyway.
- [0284](0284-cp-102-carefully-study-the-literature.md) — the expertise this
  rule says you need before shipping such code.
- [0285](0285-cp-110-do-not-write-your-own-double-checked-locking-for-initialization.md)
  — the most common attempt at hand-rolled lock-free code, ruled out by name.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the alternative
  the rule offers: someone else's lock-free data structure.
