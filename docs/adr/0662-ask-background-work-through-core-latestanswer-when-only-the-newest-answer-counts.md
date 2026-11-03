# 0662. Ask background work through Core::LatestAnswer when only the newest answer counts

Status: proposed
Date: 2026-11-03

## Context

Several owners hand work to a worker and care only about the answer to the
question they asked last: an image tab decoding its file again after it
changed, the Explorer listing its folder again after every change, the
workspace reading its solution again. Each wrote the same plumbing by hand: a
generation counter, a "running" flag, a mailbox or a mutex-guarded struct held
through a shared pointer so a job could outlive its owner, a comparison that
dropped late answers, a second take right after submitting for submitters that
run the job in place, and in one of them a cancellation token. The copies
differed in small ways, and each owner tested its own.

Other background work has other shapes and is not this: hex blocks are many
reads of one version of a file, Explorer file operations are independent jobs
whose every result counts, a search streams its hits, and the git models land
answers through their own inbox and `GenerationSlot`.

## Decision

- Add `Core::LatestAnswer<T>`: `Ask(work)`, `TakeNewest()`, `IsInFlight()`,
  `Forget()`. Asking cancels the work asked before; only the answer to the
  newest question is taken; a forgotten question's answer is dropped when it
  lands. The work holds only the shared mailbox, so it may finish after its
  owner is gone, and posting asks for a frame.
- Use it for the image tab's load, the folder listing and the workspace's
  solution read. Each keeps its own logic for what to do with an answer.
- Leave hex reads, file operations, search and the git models as they are.
- It is a template, as `Mailbox` is: the answer type varies and nothing else
  does, which is the vocabulary-type case 0563 allows.

## Rationale

The staleness rules are the part that goes subtly wrong, and they now live and
are tested in one place, with every edge (a late answer, a forgotten one, work
run in place, work outliving its owner) covered once instead of three times.

## Consequences

### Positive

- An owner of newest-only work states what it asks and what it does with the
  answer; the counters, flags and lifetime rules are gone from it.
- A queued job whose question was superseded before it started no longer runs
  at all.

### Negative

- Moving a `LatestAnswer` needs a hand-written constructor, because a
  moved-from cancellation token holds nothing and the destructor cancels
  through it.
- A stale answer is still computed if its job had already started and does not
  ask its token, as the solution reader does not; it is only dropped.

### Neutral

- An owner still calls `TakeNewest` right after `Ask` when it wants an answer
  from work run in place, as before.

## Alternatives Considered

- **Make `GenerationSlot` the shared piece.** Lost: it lives in Git, keeps a
  shared value and an error the other owners do not want, and leaves the
  mailbox and the lifetime rule to each owner.
- **One mechanism for every background shape.** Lost: streaming hits, keyed
  blocks and independent operations would each need options that the
  newest-only owners never use.

## Related

- [0563](0563-write-concrete-code-and-keep-templates-for-vocabulary-types.md)
  allows a template for a vocabulary type.
- [0644](0644-read-solutions-off-the-ui-thread.md) moved the solution read to a
  worker; this record changes how its answer comes back.
