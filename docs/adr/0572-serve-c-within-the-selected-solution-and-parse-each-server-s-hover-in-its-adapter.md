# 0572. Serve C# within the selected solution and parse each server's hover in its adapter

Status: proposed
Date: 2026-10-11

## Context

[0571](0571-choose-and-install-language-servers-from-settings-and-the-editor.md) lets C# be chosen and installed from Settings. A C# server running on a real folder then showed four faults:
- Every hover was read by clangd's parser. A C# server answers in markdown, with the declaration in a fenced block, so the tooltip showed the raw text: fences, escapes and `&nbsp;` included.
- A C# server roots at the nearest folder holding a solution. A sample solution nested below the selected one therefore started a second server, through the background pass or the warm-up, and loaded a second solution.
- The background pass waited on the head of a single queue, for up to 60 seconds. A Roslyn loading a large solution held C++ files back, and was then asked questions it could not answer yet.
- Roslyn without a solution was handed only the projects at the top of the folder.

## Decision

**Hovers.** Each server's adapter reads its hover where the reply arrives:
- clangd keeps its parser;
- every other server's reply is read as markdown: the first fenced block is the declaration, and the rest, unescaped and decoded, is the documentation.

The tooltip draws a declaration line by line, coloured by the language's grammar.

**The selected solution.** These rules hold for a language whose servers root at a solution:
- the background pass and the warm-up take that language's files only under the selected solution's folder;
- they skip every folder below it that holds another solution;
- with no solution selected, they start from the open folder, read each outermost folder that holds a solution, and skip every solution folder nested below another.

Opening a file in a skipped folder still starts its own server. Other languages are walked as before.

**Waiting.** The background pass keeps a queue per language. A language waits while its server starts or loads its projects, with no time limit.

A file whose server cannot run is counted done at once. A server cannot run when it is:
- not installed;
- not found;
- missing its runtime;
- restricted;
- turned off;
- failed with no restarts left.

**Projects without a solution.** Roslyn without a solution is handed every `.csproj` below its root. They are searched for on a worker before it starts. The search skips `node_modules`, `bin`, `obj` and every folder whose name starts with a dot. It also skips a folder that holds a solution or is another repository's root, as the background pass does. Above 500 projects, those under a `test` or `tests` folder are dropped first, then the rest past 500.

**The runtime.** A server that needs .NET starts with `DOTNET_ROOT` set to the installation whose runtime Graphite checked.

## Rationale

**Parsed where it arrives.** The adapter is what knows its server, so the editor holds one shape of hover whatever the server.

**The solution decides.** The selected solution is what the user said the workspace is. A sample solution below it is something they did not ask to load.

**No time limit.** A wait cut short asks a server that cannot answer yet, and every file asked then times out instead of being analysed. Holding only the language that waits costs the other languages nothing.

**A search on a worker.** A deep tree takes longer to list than a frame lasts.

## Consequences

### Positive

- A C# hover shows its declaration, coloured, and its documentation as text, whatever language Roslyn writes its headings in.
- One Roslyn runs per selected solution, however many sample solutions sit below it.
- C++ files are analysed while Roslyn loads.
- A folder of projects without a solution gets answers about each project's references.
- OmniSharp runs on a machine where .NET is found only through `dotnet` on `PATH`.

### Negative

- A file of a solution-rooted language outside the selected solution is not analysed in the background; it is analysed when it is opened.
- While Roslyn searches for projects, a question about an open C# file fails, and is asked again on the next hover or edit.
- A folder with more than 500 projects loads 500 of them.
- Projects added after Roslyn started are found only when it starts again for another reason.

### Neutral

- A C# tooltip has no heading line: the C# servers name no symbol kind.
- The warm-up runs again when the selected solution changes.
- With no solution selected, each outermost folder that holds a solution gets a server of its own.
- clangd's background indexing no longer holds the C++ queue: an indexing server answers about the file it is asked about.

## Alternatives Considered

- **Keep the 60-second limit, per language.** A large solution takes longer than that to load, and the pass would then ask questions that get no answers.
- **Skip a folder holding a nested solution for every language.** A C++ file there belongs to the same C++ server as the rest of the folder, and would go unanalysed for nothing.
- **One queue that steps past a waiting head.** Every frame would walk past every waiting file; a queue per language asks once per language.
- **Skip every folder that holds a solution when none is selected.** Solutions are looked for only at the top of the open folder, so a repository whose only solution sits in `src/` would get no background analysis at all.

## Related

- [0570](0570-run-a-language-server-from-graphite-s-install-once-its-runtime-is-found.md): running from Graphite's install once its runtime is found.
- [0571](0571-choose-and-install-language-servers-from-settings-and-the-editor.md): choosing and installing from Settings and the editor.
