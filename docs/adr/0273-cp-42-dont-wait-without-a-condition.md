# 0273. CP.42: Don't wait without a condition

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

A wait without a condition can miss a wakeup, or wake up simply to find that
there is no work to do.

```cpp
std::condition_variable cv;
std::mutex mx;
void thread1()
{
    while (true) {
        // do some work ...
        std::unique_lock<std::mutex> lock(mx);
        cv.notify_one();    // wake other thread
    }
}
void thread2()
{
    while (true) {
        std::unique_lock<std::mutex> lock(mx);
        cv.wait(lock);    // might block forever
        // do work ...
    }
}
```

Here, if some other thread consumes thread1's notification, thread2 can wait
forever. The predicate form has no such failure:

```cpp
template<typename T>
class Sync_queue {
public:
    void put(const T& val);
    void put(T&& val);
    void get(T& val);
private:
    mutex mtx;
    condition_variable cond;    // this controls access
    list<T> q;
};
template<typename T>
void Sync_queue<T>::put(const T& val)
{
    lock_guard<mutex> lck(mtx);
    q.push_back(val);
    cond.notify_one();
}
template<typename T>
void Sync_queue<T>::get(T& val)
{
    unique_lock<mutex> lck(mtx);
    cond.wait(lck, [this] { return !q.empty(); });    // prevent spurious wakeup
    val = q.front();
    q.pop_front();
}
```

Now, if the queue is empty when a thread executing `get()` wakes up — because
another thread got to `get()` first, for instance — it will immediately go back
to sleep and keep waiting.

## Decision

Always wait on a predicate, never on a bare notification.

- Call `wait` with a predicate that names the state being waited for, rather
  than calling the one-argument overload.
- Write the predicate against the shared state itself — the queue is non-empty,
  the flag is set, the count has reached zero — not against whether a
  notification has been sent.
- Make the state change and the notification happen under the same mutex as the
  predicate reads, so a waiter cannot miss a change that has already happened.
- Do not assume that waking up means the condition holds; a wakeup is a hint,
  and the predicate is the answer.
- The same applies to a timed wait: check the predicate after the wait returns,
  rather than inferring from the return that the event occurred.

## Rationale

A notification is an event and the thing being waited for is a state, and the
two do not survive each other. The notification is lost if it is sent before the
waiter waits, or if another thread consumes it, whereas the state is still there
to be observed whenever the waiter looks. Testing the state removes both
failures at once.

That is also why the predicate must read the shared state rather than a flag
meaning "notified". A waiter that re-checks the queue is correct regardless of
how many notifications were sent, consumed or spuriously delivered, which is the
only assumption available.

## Consequences

### Positive

- Immune to lost wakeups, stolen wakeups and spurious ones, in one construct.
- The predicate documents in the source exactly what the thread is waiting for.
- The waiter is correct regardless of whether `notify_one` or `notify_all` is
  used, so the notifying side becomes less delicate.

### Negative

- The waiter now needs access to the shared state and its mutex, which couples
  it to the data structure and rules out waiting on an opaque event.
- A woken thread that finds the predicate false has paid for a context switch
  and done nothing, and the rule does not remove those wakeups, it only makes
  them harmless.
- Correctness still depends on the notifying side changing the state under the
  same mutex; the predicate form makes the waiter safe but does not enforce that
  discipline on the writer.
- A predicate that is expensive, or that has side effects, is evaluated an
  unspecified number of times.

### Neutral

- The predicate form is exactly equivalent to a `while` loop around a bare wait;
  it is the loop that matters, not the overload.
- The rule concerns condition variables and other waits; it says nothing about
  which synchronisation primitive a design should use.

## Enforcement

- Flag all waits without conditions.

## Related

- [0262](0262-cp-20-use-raii-never-plain-lock-unlock.md) — the `unique_lock`
  that a condition variable wait requires.
- [0272](0272-cp-41-minimize-thread-creation-and-destruction.md) — the worker
  queue in that record is this record's `Sync_queue`.
- [0276](0276-cp-50-define-a-mutex-together-with-the-data-it-guards-use-synchronized-value-t-where-possible.md)
  — `Sync_queue` also shows a mutex defined together with the data it guards.
- [0271](0271-cp-40-minimize-context-switching.md) — a wakeup with no work to do
  is a switch spent on nothing.
