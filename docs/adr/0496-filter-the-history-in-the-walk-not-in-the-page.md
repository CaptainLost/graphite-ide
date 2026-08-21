# 0496. Filter the history in the walk, not in the page

Status: proposed
Date: 2026-08-21

## Context

The git view draws whatever page the model last fetched: five hundred commits
by default, more once the reader reaches the bottom and load-more asks for a
longer page. A reader looking for one commit — by a word from its message, by
its author, by the hash somebody pasted into a review — has no way to say so.
They scroll.

A filter can be applied at two places, and the two answer different questions.
In the view, over the page already in hand, it is instant and free, and it
finds only what has already been fetched: on a repository of four thousand
commits the reader sees the matches among the first five hundred and no
indication that the rest exist. In the walk, it is a query, and it finds the
matches wherever they are.

The walk is not free. `QueryLog` already bounds itself with `MaxWalk`, because
narrowing by file path diffs every commit passed on the way to a match. A text
filter does not diff, but it does mean the walk keeps going past commits it
drops, so the ceiling is what stands between a narrow filter and the whole
history.

## Decision

Filter in the walk. `LogRequest` carries a `CommitFilter`, and `QueryLog`
measures every commit it visits against it, keeping the ones that match.

Match one text every way a commit can be named: as a substring of the summary,
the body, and the author's name and email, and as a prefix of the id when the
text is hexadecimal throughout. One box, not four: a reader with a hash in the
clipboard and a reader who remembers half a summary both type into the same
place, and asking them to first declare which kind of thing they typed is
asking them to answer a question the filter can work out for itself.

Keep the refs column filtering in the view. Every branch is already in hand
there, so a query would answer a question nobody asked.

## Rationale

The commits worth finding are usually the ones not yet fetched — that is what
makes the search a search rather than a re-read of what is on screen. A filter
that cannot see past the current page would be at its least useful exactly when
the reader most needs it, on a long history, and would have to be explained
every time it came up short.

`MaxWalk` and `LogPage::Complete` already exist and already mean the right
thing: the walk stops looking and says that it stopped. A filtered page that
found three matches and gave up is exactly the case those two were written for.

## Consequences

### Positive

- A filter finds matches anywhere in the history the walk can reach, not only
  in the page on screen.
- Narrowing by path needs nothing new: `LogRequest::File` already walks one
  file's history and follows it through renames.
- `Complete` distinguishes "these are all the matches" from "the search stopped
  before the end of the history", so the view can say which one it is showing.

### Negative

- Every change of the filter is a query. Typing has to be waited out before one
  is sent, and the answer arrives a moment later, over a list that until then
  still shows the previous one. This is the real cost of the decision.
- A filter that matches nothing still costs a full walk up to `MaxWalk`, where
  a view-side filter would have cost nothing.
- What load-more asks for is twice the matches on screen, not twice the commits
  walked, so a narrow filter makes each additional page dig deeper than the last.

### Neutral

- The filter's text is matched against the id only when it is hexadecimal, so a
  word like "added" is read as a word first and as a hash only if it is found
  nowhere else — which, for a real hash prefix, is always.

## Alternatives Considered

- **Filter the fetched page in the view.** Instant, free, and no change to
  Core. Rejected because it answers a narrower question than the reader is
  asking: the match they are looking for is usually older than the page they
  have, and "load more until it appears" is the scrolling the filter was meant
  to replace.
- **A field per question — message, author, hash, path.** Unambiguous, and it
  would let the id be matched without the hexadecimal test. Rejected as four
  boxes to fill for a question that is nearly always one word, and because
  deciding which box a typed thing belongs in is work the filter can do.
- **Hand the walk to `git log --grep` and read its output.** Nothing new to
  write in Core. Rejected because the repository is read through libgit2
  everywhere else, and a second way in brings process spawning, output parsing
  and a dependency on the git binary being installed.

## Related

- [0491](0491-keep-git-data-in-core-and-git-freshness-in-graphite.md) — why the
  query lives in Core and the decision of when to ask lives in Graphite.
- [0492](0492-give-each-worker-thread-its-own-repository-handle.md) — what a
  filter query costs is a worker's walk, not the UI thread's frame.
