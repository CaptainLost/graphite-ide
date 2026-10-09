# 0568. Colour a language through tree-sitter

Status: proposed
Date: 2026-10-10

## Context

C# files drew in plain text: the only colouring was the C++ line lexer ([0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md)), hand-written, and a second hand-written lexer per language is the cost [0485](0485-take-code-intelligence-from-external-language-servers.md) set out to avoid. C# needs nested constructs a line lexer handles badly — interpolated strings holding code, raw strings with a counted delimiter. Tree-sitter grammars exist for most languages, each with a highlight query. 0481 and 0485 rejected tree-sitter as a source of meaning (definitions, references), not of colour. A server is optional for C#, so colouring cannot wait for one.

## Decision

A language's colours come from a hand-written line lexer or from a tree-sitter grammar, named by `SyntaxKind` in its `LanguageDefinition`; C++ keeps its lexer, C# uses tree-sitter-c-sharp. The runtime and each grammar are git submodules built by premake as C static libraries with warnings off ([0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md)). The highlight query is our copy, compiled into the application and mapped onto `TokenKind` by capture name; overlapping captures resolve to the narrowest node, two captures of one node to the later pattern. A document keeps one tree in its `SyntaxCache`, re-parsed incrementally on each revision as one edit covering the middle that differs from the last finished text, within 4 ms per attempt and at most one attempt per 12 ms; a parse that runs out of time asks for a frame for when its pause ends, so it finishes without input; while it lags, a line keeps the old tree's tokens, clipped to the line. A snippet is coloured through `ColourLines`, which parses it as one text. A text above 4 MiB is not parsed. A server's semantic tokens recolour every kind a name can carry, so the server's word beats the grammar's guess.

## Rationale

A grammar and a query cost a submodule and a few dozen lines instead of a lexer, and colour nested constructs correctly because the parse is a real one. Incremental parsing keeps a keystroke in a ten-thousand-line file within a frame (2.3 ms measured against 26.7 ms for a full parse), and diffing the new text against the old finds the edit for every mutation path without a journal in the buffer.

## Consequences

### Positive

C# is coloured with no server. The next language costs a grammar submodule, a query and a table row. A grammar's guess at a name (a type, a method) shows before any server answers.

### Negative

Each grammar is tens of megabytes of generated C (29 MB for C#, a 5 MB object). A document's text is copied on every revision, and its tree costs several times the text in memory. Files above 4 MiB draw in plain text. The query is ours to keep in step with the grammar when the pin moves. While typing, an unclosed string or comment does not swallow the lines after it as a line lexer would, because tree-sitter recovers around the error.

### Neutral

C++ keeps its lexer; colouring C++ through tree-sitter is a separate decision.

## Alternatives Considered

- A hand-written C# lexer like the C++ one — loses on cost per language, and on interpolated and raw strings, which nest code inside literals.
- TextMate grammars — regex-driven, they need a regex engine (Oniguruma) as a further dependency, re-tokenise from a line's start state with no tree, and are slower on large files.
- Colours from the server's semantic tokens alone — loses because nothing would be coloured without a server, and a C# server is the user's choice to install.

## Related

- [0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md): how the runtime and the grammar are built.
- [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md): the line lexer C++ keeps.
- [0485](0485-take-code-intelligence-from-external-language-servers.md): meaning from servers, which this does not change.
- [0567](0567-keep-server-specific-behaviour-in-adapters.md): the server side of the same stage-by-stage work.
- The spec `2026-10-09-language-servers-design.md`, which is not tracked.