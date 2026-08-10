# 0281. CP.61: Use async() to spawn concurrent tasks

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

The same argument that rules out raw owning pointers rules out raw threads and
raw promises: prefer a factory function such as `std::async`, which handles
spawning or reusing a thread without exposing a raw thread to your own code.

```cpp
int read_value(const std::string& filename)
{
    std::ifstream in(filename);
    in.exceptions(std::ifstream::failbit);
    int value;
    in >> value;
    return value;
}

void async_example()
{
    try {
        std::future<int> f1 = std::async(read_value, "v1.txt");
        std::future<int> f2 = std::async(read_value, "v2.txt");
        std::cout << f1.get() + f2.get() << '\n';
    } catch (const std::ios_base::failure& fail) {
        // handle exception here
    }
}
```

`std::async` is not perfect. It does not use a thread pool, which means it might
fail through resource exhaustion rather than queuing tasks up to be executed
later. Even where `std::async` itself cannot be used, however, the answer is to
write your own future-returning factory function, not to fall back on raw
promises.

The bad example shows two different ways to succeed at using `std::future` while
failing to avoid raw `std::thread` management:

```cpp
void async_example()   // bad
{
    std::promise<int> p1;
    std::future<int> f1 = p1.get_future();
    std::thread t1([p1 = std::move(p1)]() mutable {
        p1.set_value(read_value("v1.txt"));
    });
    t1.detach(); // evil

    std::packaged_task<int()> pt2(read_value, "v2.txt");
    std::future<int> f2 = pt2.get_future();
    std::thread(std::move(pt2)).detach();

    std::cout << f1.get() + f2.get() << '\n';
}
```

The good example follows the general pattern set by `std::async` in a context
where `std::async` itself was unacceptable for production use:

```cpp
void async_example(WorkQueue& wq)   // good
{
    std::future<int> f1 = wq.enqueue([]() {
        return read_value("v1.txt");
    });
    std::future<int> f2 = wq.enqueue([]() {
        return read_value("v2.txt");
    });
    std::cout << f1.get() + f2.get() << '\n';
}
```

Any threads spawned to execute the code of `read_value` are hidden behind the
call to `WorkQueue::enqueue`. The user code deals only with `future` objects,
never with raw `thread`, `promise`, or `packaged_task` objects.

## Decision

Spawn concurrent work through a future-returning factory, and never by
constructing a thread at the call site.

- Call `std::async` to start a task, and take the result through the
  `std::future` it returns.
- Where `std::async` is unsuitable, write a factory of your own — an `enqueue`
  on a work queue — that also returns a `std::future`.
- Keep `std::thread`, `std::promise` and `std::packaged_task` inside that
  factory. Calling code sees futures only.
- Do not detach a thread to make a promise's lifetime work out; a detached
  thread outlives every handle you have to it.
- Retrieve every future you are handed. A future whose result is never asked for
  discards whatever the task failed with.

## Rationale

A future is the return channel a function already has, kept intact across a
thread boundary — one object carries both the value and the failure, and the
caller joins by asking for the result rather than by remembering to. The bad
example is the point: it uses `std::future` correctly and is still wrong,
because the thread it created has been detached and nothing now owns the work.
Once every task is spawned through one factory, the scheduling policy is a
property of that factory and can be replaced without touching a call site.

## Consequences

### Positive

- No thread object to join, detach, or forget; the handle the caller holds is a
  future, not a thread whose lifetime it has to manage.
- Value return and error return come back along the same path, so a failing task
  cannot be silently dropped.
- The execution strategy — a new thread, a pooled thread, a deferred call — is
  concentrated in one place and is replaceable.
- Call sites read like ordinary function calls with the wait made explicit.

### Negative

- `std::async` is not required to use a thread pool, so it may fail through
  resource exhaustion rather than queuing the work for later; the rule's default
  mechanism has a failure mode the source itself names.
- Writing the replacement factory means building and owning a work queue before
  the first concurrent task can be written, which is a large cost for a program
  that needs two.
- `future::get` both blocks and rethrows, so every failure arrives at the join
  point rather than where it happened, on a thread that no longer has the
  context in which it arose — and a future that is dropped without being
  retrieved swallows the failure entirely.
- Hiding threads behind a factory also hides how many were asked for; nothing at
  the call site shows the degree of parallelism being requested.

### Neutral

- The rule constrains how work is started and how its result comes back. It says
  nothing about how concurrent tasks synchronise with each other.
- `std::async`'s launch policy is a property of the factory under this rule, so
  callers should not depend on whether a task ran on another thread at all.
- The two factories are not interchangeable in one respect: destroying the last
  future from `std::async` blocks until the task finishes, whereas a future from
  a hand-written `promise` or `packaged_task` factory does not. A replacement
  factory has to decide what dropping the future means.

## Related

- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the ownership argument this rule borrows and applies to threads.
- [0014](0014-p-8-do-not-leak-any-resources.md) — a detached thread is a leaked
  resource with a handle you never held.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — the factory is exactly this: the messy part behind one call.
