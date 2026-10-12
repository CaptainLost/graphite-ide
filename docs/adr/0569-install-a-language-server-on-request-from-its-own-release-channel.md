# 0569. Install a language server on request from its own release channel

Status: proposed
Date: 2026-10-10

## Context

[0488](0488-ship-clangd-with-the-release.md) ships clangd beside the executable, copied from `vendor/bin/clangd/` when someone has put a binary there. The copy is the bare `clangd.exe`, without the `lib/clang` headers it needs. The approach does not stretch to C#: its three servers come from NuGet and GitHub, and two of them need .NET. The language-server work ([0567](0567-keep-server-specific-behaviour-in-adapters.md), [0568](0568-colour-a-language-through-tree-sitter.md)) moves to a server chosen per language and installed when the user asks. That needs four things: a downloader, a way to know an install is whole, a record of what is installed, and a place on disk that two Graphite windows can share.

## Decision

Core gains generic pieces. Each has a Windows backend and returns `Unsupported` elsewhere ([0002](0002-keep-core-free-of-ide-knowledge.md)):
- `HttpClient` over WinHTTP: HTTPS only, the system proxy, redirects followed. A fetched answer is decompressed when it comes compressed; a download never is, so its bytes are hashed exactly as served;
- SHA-256 and SHA-512 through BCrypt;
- zip extraction through miniz, a submodule ([0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md)). It refuses the whole archive when any entry could land outside the destination;
- an exclusive lock file that is deleted when released.

Graphite's `ServerInstaller` installs one server per job and reports through `Drain`. It finds the latest version in one of two ways:
- from GitHub's `releases/latest`, taking the asset named by the platform's template and its `digest`;
- from NuGet's SemVer 2.0 registration index, taking the highest listed version (prereleases only when the server's row allows them) and the `packageHash` from its catalogue entry.

It then:
1. downloads to `servers/<id>/.download/`;
2. verifies the checksum when the source publishes one, and installs without one, saying so, when it does not;
3. extracts to `<version>.tmp/` and renames that to `<version>/`;
4. writes `installed.json` atomically, with the version, source, checksum, date and command.

For GitHub the command comes from the row's executable template; for NuGet, from the package's `DotnetToolSettings.xml`. `servers/<id>/install.lock` is held for the whole of an install or an uninstall; a check for updates only reads. The start-up sweep removes every directory the record does not name, and skips a server whose lock it cannot take. While the record cannot be read, the sweep keeps every version. `installed.json` carries a format number, and a record written by a newer format is left alone together with every version beside it. Nothing installs by itself: the user asks for it. Starting a server stays gated by trust ([0532](0532-open-an-unknown-folder-in-restricted-mode-until-the-user-trusts-it.md)).

A version directory is removed whole or not at all, whether by the sweep, by an uninstall or by an install replacing a leftover: it is first renamed to a temporary name, and only then deleted. A rename fails while any file inside is open, so a running server's version is protected once a server's window holds its command file open. Whatever a deletion leaves under the temporary name, the next sweep removes.

## Rationale

**The HTTP stack.** The platform's own stack brings the proxy settings, certificate store and TLS updates the user already has, at no cost in dependencies.

**Staging and the record.** Extracting beside the target and then renaming means an interrupted install never leaves half a server where a whole one is expected. Whether a server is installed rests on one file written atomically, not on what happens to be in a directory.

**The lock.** It is what lets a window that starts up sweep safely while another window installs.

**Sizes.** The downloads are 29.7 MB for clangd 23.1.0, 21.6 MB for csharp-ls 0.28.0 and 72 MB for Roslyn 5.12.0-1.26475.2. Each is fetched once per version, not with every Graphite release.

## Consequences

### Positive

- Servers update without a Graphite release.
- clangd arrives with its headers.
- C# servers install the same way as C++'s.
- Every source used today publishes a checksum, and each is verified.

### Negative

- A first install needs the network.
- GitHub's unauthenticated API allows 60 calls an hour from one address.
- A source that publishes no checksum is trusted on HTTPS alone.
- A replaced version's directory stays until a later start, which removes it once nothing holds its files.
- `.tar.gz` packages are not extracted until a platform needs them.
- Cancelling during the version lookup takes effect when the lookup ends; during a download, within one request stage of up to 30 seconds.

### Neutral

- 0488 stays in force until the bundled clangd is removed, which is the change that supersedes it.

## Alternatives Considered

- **libcurl with OpenSSL.** Another large dependency to keep patched, with proxy and certificate behaviour that differs from the system's.
- **`dotnet tool install` for the NuGet servers.** It needs the .NET SDK and not only the runtime. It writes to the user's global or local tool manifest. It would still need a separate path for a self-contained server such as Roslyn.
- **A package manager (winget, npm).** Not on every machine, each covers only some of the servers, and their versions trail the servers' own releases.
- **NuGet's flat container version list.** It lists unlisted versions too, so a pulled release could be installed.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md): why the HTTP, hash, zip and lock pieces are generic.
- [0003](0003-vendor-dependencies-as-git-submodules-built-by-premake.md): how miniz is built.
- [0488](0488-ship-clangd-with-the-release.md): superseded by [0577](0577-install-clangd-on-request-like-every-other-language-server.md).
- [0532](0532-open-an-unknown-folder-in-restricted-mode-until-the-user-trusts-it.md): trust still gates starting a server.
- [0567](0567-keep-server-specific-behaviour-in-adapters.md): the server rows this installs from.
- The spec `2026-10-09-language-servers-design.md`, which is not tracked.
