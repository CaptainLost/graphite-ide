# 0269. CP.31: Pass small amounts of data between threads by value, rather than by reference or pointer

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

A small amount of data is cheaper to copy and access than to share using some
locking mechanism. Copying naturally gives unique ownership, which simplifies
the code, and eliminates the possibility of data races. Defining "small amount"
precisely is impossible.

```cpp
string modify1(string);
void modify2(string&);
void fct(string& s)
{
    auto res = async(modify1, s);
    async(modify2, s);
}
```

The call of `modify1` involves copying two string values; the call of `modify2`
does not. On the other hand, the implementation of `modify1` is exactly as we
would have written it for single-threaded code, whereas the implementation of
`modify2` will need some form of locking to avoid data races. If the string is
short — say ten characters — the call of `modify1` can be surprisingly fast, and
essentially all the cost is in the thread switch. If the string is long — say a
million characters — copying it twice is probably not a good idea.

This argument has nothing to do with `async` as such. It applies equally to
considerations about whether to use message passing or shared memory.

## Decision

Give a task its own copy of small data rather than a reference to yours.

- Take task parameters by value where the data is small, and let the task return
  its result by value too.
- Reserve passing by reference or pointer for data large enough that the copy is
  a real cost, and accept that doing so brings an obligation to synchronise.
- Count the whole cost when deciding: a copy that avoids a mutex is competing
  with the lock, the contention and the reasoning, not with nothing.
- Where data is passed by reference for size reasons and is not modified, pass
  it as `const` so the sharing is safe without locking.
- Apply the same reasoning to message passing: the choice between sending a
  value and sharing a buffer is this same trade.

## Rationale

The decisive part is not the copy's cost but what the copy buys: the function on
the other side is ordinary single-threaded code. `modify1` needs no lock, no
reasoning about interleaving and no review for races, while `modify2` needs all
three — and that difference is paid once in the implementation, not once per
call.

Against a thread switch, a small copy is close to free, which is why the
threshold sits so much higher than intuition suggests. Ten characters is not
worth sharing; a million is.

## Consequences

### Positive

- Removes the possibility of a data race on the data passed, rather than
  managing it.
- The task body is written and reviewed as ordinary sequential code.
- Unique ownership means no question about who may destroy the data or when.

### Negative

- The copy is a real cost, and for large data it dominates; the rule gives no
  threshold, so every case is a judgement.
- Copying is not always available: some types are not copyable, and copying a
  type with reference semantics duplicates the handle rather than the data,
  which leaves the sharing in place while looking as though it removed it.
- Passing by value forecloses returning results through the argument, so results
  must come back another way.
- The rule is not enforceable; the upstream rule has no enforcement clause.

### Neutral

- "Small" is deliberately undefined, and the source says so explicitly.
- The rule concerns data crossing a task boundary, not the general question of
  how to pass parameters, which the function-parameter rules cover.

## Related

- [0258](0258-cp-3-minimize-explicit-sharing-of-writable-data.md) — copying is
  the concrete way to share less.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — the other route
  to safe sharing, where the data is large but constant.
- [0448](0448-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md)
  — the sequential version of the same trade, with a different threshold.
- [0280](0280-cp-60-use-a-future-to-return-a-value-from-a-concurrent-task.md) —
  how the result comes back when the argument is a copy.
