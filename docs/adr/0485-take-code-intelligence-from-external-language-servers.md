# 0485. Take code intelligence from external language servers

Status: proposed
Date: 2026-08-16

## Context

The editor colours code — [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md)
and [0484](0484-keep-cross-line-lexer-state-opaque.md) gave it a lexer per
line, table-driven per language — but it does not understand code. There is no
answer to "what does this name refer to", "where is it declared" or "what are
this function's overloads". The lexer cannot supply one: `LexLine` classifies
characters into `TokenKind`s from local, per-line state; it does not know that
two tokens spelled the same name are the same symbol, let alone what that
symbol is.

The plan on the table before this record was a name-based index: walk the
token stream, record every identifier that appears in a declaration position,
and answer "go to definition" by looking up the name. It needs nothing beyond
what the lexer already produces, and a first version looked like a small
addition to `Lang`.

It does not survive contact with C++. `auto x = f();` requires knowing what
`f` returns to know what `x` is, which requires resolving which overload of
`f` this call selects, which requires the types of the arguments, which can
themselves be `auto`. `using Alias = Other<T>;` requires substituting `T`
before `Alias` means anything. Two declarations named `f` are different
symbols only once overload resolution has run. None of this is an edge case a
name index could special-case around — it is what a C++ compiler's front end
is. A hand-rolled parser and type checker for a language whose grammar is
context-sensitive (a decade of `<` ambiguity, `typename`, most-vexing-parse),
whose preprocessor runs before any of it, and whose type system includes
templates, constexpr evaluation and deduction rules `auto` is itself defined
by, is not a smaller project than the one everyone else in the industry has
already built.

## Decision

Every semantic answer — go to definition, find references, hover, diagnostics,
completion — comes from an external language server speaking the Language
Server Protocol. This repository writes no symbol index, no declaration
scanner and no type deduction of its own. Support for a language is one row in
a table: file extensions, the command that launches its server, the root
markers that locate a workspace, and the protocol's language identifier.
C++ support is that row pointing at `clangd`; a second language is another
row, not another front end.

## Rationale

Deducing `auto` requires knowing an expression's type, which requires overload
resolution, which requires typed expressions, which requires a parser sitting
on top of a preprocessor. `auto` itself is specified in terms of template
argument deduction. There is no increment of "a bit more than a name index"
that reaches `auto` — the feature does not exist until the whole front end
does. The same is true, in smaller print, of `using` aliases, name lookup
across namespaces and overload resolution generally: each is a consequence of
machinery that either exists in full or answers nothing.

That machinery is also not this repository's to build twice. Visual Studio
licenses the EDG front end for IntelliSense rather than write one; JetBrains
wrote CLion's own C++ engine and later made clang, not their engine, the
default completion provider. Two IDE vendors with substantially more C++
tooling investment than this project both concluded that a hand-written front
end is disproportionate to what it buys. That is corroboration, not the
argument: the argument is the shape of the language above. A language server
already carries that cost, amortised over every editor that speaks the
protocol to it, and this project can consume the answer instead of rebuilding
the question.

## Consequences

### Positive

- Correctness for `auto`, `using`, overload resolution and templates arrives
  for free — it is `clangd`'s problem, not this repository's, and it is
  already solved to a standard no name index could reach.
- A further language costs one table row: extensions, a launch command, root
  markers, a protocol id. No parser, no grammar, no type system.
- The lexer's job stays exactly what [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md)
  and [0484](0484-keep-cross-line-lexer-state-opaque.md) already scoped it to
  — colouring — so this decision adds no pressure to grow it into something
  it was deliberately kept small.

### Negative

- The application now depends on a binary it does not compile and does not
  control the release cadence of. A `clangd` that crashes, hangs or is simply
  not installed is a failure mode this repository must handle, not one it can
  fix.
- Every semantic answer is asynchronous and can be stale: the server's view of
  a file lags the buffer between a keystroke and the next `didChange`
  notification, and a fast typist can see a hover or a diagnostic that
  describes text that already changed.
- A workspace with no compile database is not a workspace with no answers —
  it is a workspace with confident wrong ones, because `clangd` still parses
  with a guessed set of flags when it cannot find real ones. That is why "no
  compile database" has to be a state this project names and shows to the
  user, rather than a case it can ignore as "just don't answer".

### Neutral

- This record commits to the protocol, not to `clangd` specifically. Swapping
  the C++ server for another LSP-speaking implementation is a table-row edit;
  swapping the protocol for something else is not — nothing in this decision
  makes that swap any cheaper than it already was.

## Alternatives Considered

### A name-based index over the existing lexer

The option that started this record, and the only one on this list that needs
no compile flags at all — it can run before a build has ever succeeded.
Rejected because it cannot deduce `auto`, cannot resolve `using` aliases and
cannot disambiguate overloads; on real C++ it answers "go to definition" wrong
often enough to be worse than answering nothing, and every fix to a wrong
answer is a step toward reimplementing name lookup by hand.

### tree-sitter

A real parser, incremental, fast, and it would replace the hand-lexed
`CppLexer` with something more capable at the syntax level. Rejected because
it stops at syntax: it produces a tree, not resolved names, so cross-file "go
to definition" and "find references" would still need an index built on top —
and that index is exactly the name-based approach above, now sitting on a
better tree instead of a token stream. It does not remove the problem, it
relocates it one layer down.

### libclang

The actual C++ front end, vendored and linked in-process. Removes the process
boundary and the asynchrony that are the Negative consequences above.
Rejected: it is a large dependency to vendor and keep current, per
[0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md)'s
build-from-source policy it would mean building LLVM; it answers for exactly
one language, so a second language buys nothing from having paid this cost for
the first; and it still needs the same compile flags `clangd` needs, so it
does not remove the "no compile database" problem, only the process boundary
around it.

### A hand-written language server

Write the protocol server this project would otherwise launch as a
subprocess. Rejected because the protocol is the easy 5%: JSON-RPC over
stdio, a handful of request and response shapes. The hard 95% is the analysis
behind each response, which is the same front end this whole record exists to
avoid writing. Doing it in-process would also convert the clean process
boundary this decision keeps into pure cost — the isolation `clangd` gives
for free (a crash in the analyser does not take the editor down with it)
would have to be re-earned by hand for no corresponding benefit.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md) — an LSP client is IDE
  knowledge; see [0487](0487-correct-adr-0482s-premise-about-project-data.md)
  for how this record changes what 0482 argued about where that knowledge
  lives.
- [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md) and
  [0484](0484-keep-cross-line-lexer-state-opaque.md) — the lexer this record
  deliberately does not grow into an analyser.
- [0482](0482-keep-the-language-layer-in-graphite-rather-than-core.md) — the
  language layer this decision adds a client to.
