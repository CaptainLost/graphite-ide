/*
 * Hand-written replacement for the CMake-generated `git2_features.h`.
 *
 * libgit2 normally derives this header from
 * `src/util/git2_features.h.in` via `configure_file()` in its CMake build.
 * Since this project vendors libgit2 as a plain premake StaticLib instead of
 * running its CMake configuration, this file plays that role by hand.
 *
 * Written for libgit2 v1.8.4. It must be revisited whenever the vendored
 * `vendor/libgit2` submodule is upgraded to a different version, because the
 * set of available defines (and the logic that would have chosen them) can
 * change between releases.
 *
 * Every define below appears in `src/util/git2_features.h.in` at v1.8.4.
 * Nothing else may be added here: an unrecognised define is silently
 * ignored by the sources and will not surface as a build error.
 *
 * Configuration this file encodes: Windows x64, threads enabled, bundled
 * (builtin) regex, bundled (builtin) HTTP parser, no TLS backend and no
 * SSH/NTLM/GSSAPI. This is *not* a "no network" build: the http:// and
 * git:// smart transports are unconditionally referenced by
 * src/libgit2/transport.c's static transport table (only ssh:// is behind
 * an #ifdef), so they always compile in. Without a TLS backend, an actual
 * `https://` fetch fails at runtime by design — this integration
 * deliberately buys only local repository queries plus a linkable,
 * runnable library. See task-1-report.md for the full trail, including a
 * correction: `deps/llhttp` was initially (wrongly) treated as an
 * out-of-scope network dependency and is in fact libgit2's own bundled,
 * dependency-free HTTP parser (like deps/zlib and deps/pcre), not a
 * system package or TLS/network stack.
 */
#ifndef INCLUDE_features_h__
#define INCLUDE_features_h__

/* Threads are enabled (CMake option USE_THREADS defaults to ON;
 * see src/CMakeLists.txt). */
#define GIT_THREADS 1

/* Target is a 64-bit architecture (CMake sets this when
 * CMAKE_SIZEOF_VOID_P is 8; see src/CMakeLists.txt). */
#define GIT_ARCH_64 1

/* Bundled PCRE regex backend, as requested by the task brief. On the
 * CMake side this is REGEX_BACKEND=builtin in cmake/SelectRegex.cmake. */
#define GIT_REGEX_BUILTIN 1

/* MSVC's qsort_s() has the signature checked by the GIT_QSORT_MSC probe in
 * src/CMakeLists.txt; MSVC on Windows always provides it. */
#define GIT_QSORT_MSC 1

/* WSAPoll() is unconditionally selected on Windows: cmake/../src/CMakeLists.txt
 * has `if(WIN32) set(GIT_IO_WSAPOLL 1)`. */
#define GIT_IO_WSAPOLL 1

/* SHA1 and SHA256 both implemented via the native Windows CryptoAPI/CNG
 * backend (src/util/hash/win32.c), selected via cmake/SelectHashes.cmake's
 * "Win32" branch. This avoids pulling in the bundled sha1dc/rfc6234
 * implementations for a single vendored source file. */
#define GIT_SHA1_WIN32 1
#define GIT_SHA256_WIN32 1

/* Bundled (builtin) llhttp parser backs the smart HTTP transport, which
 * src/libgit2/transport.c references unconditionally. This is the CMake
 * default too (cmake/SelectHTTPParser.cmake's else() branch, taken when no
 * system http-parser/llhttp is requested) and needs nothing beyond the
 * three self-contained files in deps/llhttp (no system library). Do not
 * define GIT_HTTPPARSER_HTTPPARSER or GIT_HTTPPARSER_LLHTTP: both mean "use
 * a system-installed parser", which is not the case here. */
#define GIT_HTTPPARSER_BUILTIN 1

#endif
