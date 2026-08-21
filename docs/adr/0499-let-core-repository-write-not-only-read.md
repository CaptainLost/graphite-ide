# 0499. Let Core::Repository write, not only read

Status: proposed
Date: 2026-08-21

## Context

`Core::Repository` answers questions and changes nothing: log, diff, status,
branches, blobs. [ADR 0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md)
placed that read model in `Core` because what a repository contains is data,
and put the decision about *when* to re-ask in `Graphite`. Nothing in either
half can alter a repository, so the only way to stage or commit is a terminal
beside the application.

That gap is about to be closed by a panel that stages, unstages, discards,
deletes untracked files, commits and amends. The question this record settles
is where the act of changing a repository lives, and what shape the calls that
do it take.

## Decision

Performing an operation on a repository is a fact about git, not a product
decision, so it belongs in `Core::Repository` alongside the queries. A fifth
implementation file, `RepositoryWrite.cpp`, adds five methods, each
synchronous, uncached and returning `Result`, exactly as the `Query*` members
are — the class's contract does not change shape, only its verb count.

- `Stage` adds each path to the index as it stands on disk. **One method, not
  three.** libgit2 distinguishes adding an entry, updating one and removing
  one; the caller does not. It is looking at a row and wants the index to catch
  up with the disk. Staging a conflicted path this way is also what marks it
  resolved.
- `Unstage` puts each path back as HEAD has it and leaves the working tree
  alone. Under an unborn HEAD it removes the entry instead, which is the answer
  git gives.
- `DiscardWorkTree` overwrites the working tree from the index and does not
  touch untracked paths.
- `DeleteUntracked` deletes untracked files outright. **Two methods, not one** —
  deliberately the opposite choice to `Stage`.
- `Commit` writes the index as a commit on HEAD, or replaces HEAD keeping its
  parents when amending, and **returns the new `Oid`**. Author and committer
  come from git's own config resolution; a repository with no identity fails
  here rather than inventing one.

`CommitRequest` carries the message and the amend flag and nothing else: no
author override, no sign-off, no signing.

**Tests that touch commit authorship must shadow the machine's identity.**
`git_signature_default` resolves through git's whole config chain, so a
freshly-initialised repository is not identity-less — it inherits the
developer's global `user.name` and `user.email`. A test asserting that a
repository without an identity fails therefore passes or fails depending on
whose machine it runs on. The fixture gained `SetUnusableIdentity`, which
writes empty strings into the repository's *local* config to shadow whatever
the global config says.

## Rationale

The boundary of ADR 0491 is extended rather than broken: "what a repository is
and can be made to be" is Core; "when to do it, what to ask first, what to
refresh afterwards" is Graphite. Writes obey the same rule reads already do, so
there is one boundary to remember instead of two.

The two granularity choices point in opposite directions because the cases
underneath them differ. Splitting `Stage` would push "this file is deleted, so
a different call applies" into every caller — knowledge the caller has no
reason to hold. Merging discard and delete would produce one method whose
effect depends on something the caller did not check: one restores a state git
knows, the other destroys a file git has never seen. The rule is that a split
is worth it when the two halves have different *consequences*, not when they
have different implementations.

Failing on a missing identity is the same principle as returning `Result` at
all: an invented author is a wrong answer written permanently into history,
where an error is a question the user can answer once.

## Consequences

### Positive

- The panel, and anything else built on Core later, has one place to perform an
  operation, tested headless in `CoreTests` against a real temporary repository
  with no job queue and no application around it.
- Taking paths rather than hunks means hunk-level staging is a later *addition*
  to this API, not a rewrite of it.
- A commit's `Oid` gives the panel something to show and a test something to
  assert beyond the absence of an error.

### Negative

- `DiscardWorkTree` and `DeleteUntracked` are irreversible in a way no other
  Core call is: what they remove was never in git, so no later Core call can
  bring it back. Every caller now carries a duty to confirm first, and Core
  cannot enforce that.
- Five more methods on a class that was uniformly safe to call. A reader could
  previously assume any `Core::Repository` call left the repository alone; that
  assumption is gone, and only the method name distinguishes the two kinds.
- Authorship resolves through git's config chain, which means a test that does
  not shadow the global identity is testing the developer's machine. The
  fixture helper makes it avoidable, not impossible.

### Neutral

- `CommitRequest` is deliberately thin. Sign-off, signing and an author
  override are each easier to add against a shipped, tested write path than to
  design blind now, and none of them is needed to make the panel work.

## Alternatives Considered

### A separate `Core::RepositoryWriter`

Keeps the read class provably side-effect free, which is genuinely attractive:
a reader could keep the old assumption that any call on the type they hold
changes nothing. Rejected because the two would open the same handle against
the same repository and obey the same threading rule from
[ADR 0492](0492-give-each-worker-thread-its-own-repository-handle.md), so the
split would be one of naming, not of ownership — and every caller that both
reads and writes would then hold two objects describing one repository.

### Put the write calls in Graphite, beside the model that issues them

Tempting because every current caller is in Graphite, and it would keep Core
untouched. Rejected because it contradicts ADR 0491 on its own terms: staging a
file is a fact about git, true of any repository, and putting it in Graphite
would put it out of reach of `CoreTests`'s headless coverage and out of reach
of anything built on Core that is not this IDE.

### Take an author on `CommitRequest` and fall back to a default

Would make a commit always succeed, which is the outcome a user clicking
*Commit* wants. Rejected because the fallback has to invent something, and what
it invents is written permanently into the history of a repository shared with
other people. Failing costs one dialog; a wrong author costs a rewritten
history to correct.

## Related

- [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) — the
  boundary this record extends to writes.
- [0500](0500-serialise-git-writes-in-one-queue.md) — how Graphite calls these
  methods off the UI thread.
- `docs/superpowers/specs/2026-08-21-git-writes-design.md` — the design this
  record was extracted from.
