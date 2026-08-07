/*
 * Hand-written replacement for the CMake-generated `deps/pcre/config.h`.
 *
 * The bundled PCRE copy libgit2 vendors under `deps/pcre` normally has this
 * header produced by `configure_file(config.h.in ...)` in
 * `deps/pcre/CMakeLists.txt`. This vendored build does not run libgit2's
 * CMake configuration, so this file plays that role by hand, using the
 * same fixed values that `deps/pcre/CMakeLists.txt` sets for a CMake build
 * (SUPPORT_PCRE8, PCRE_LINK_SIZE, etc.) plus the platform HAVE_* checks
 * appropriate for MSVC on Windows.
 *
 * Written for libgit2 v1.8.4 (bundled PCRE snapshot). Revisit when the
 * `vendor/libgit2` submodule is upgraded.
 */

/* Platform capability checks (deps/pcre/CMakeLists.txt: check_include_file /
 * check_function_exists). MSVC provides these; it has no dirent.h or
 * unistd.h. */
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_WINDOWS_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1

#define HAVE_MEMMOVE 1
#define HAVE_STRERROR 1
#define HAVE_STRTOLL 1
#define HAVE__STRTOI64 1

#define HAVE_LONG_LONG 1
#define HAVE_UNSIGNED_LONG_LONG 1

/* User-configurable options, fixed by deps/pcre/CMakeLists.txt regardless
 * of platform. */
#define SUPPORT_PCRE8 1
#define NO_RECURSE 1

#define NEWLINE 10
#define POSIX_MALLOC_THRESHOLD 10
#define LINK_SIZE 2
#define PARENS_NEST_LIMIT 250
#define MATCH_LIMIT 10000000
#define MATCH_LIMIT_RECURSION MATCH_LIMIT

#define MAX_NAME_SIZE 32
#define MAX_NAME_COUNT 10000
