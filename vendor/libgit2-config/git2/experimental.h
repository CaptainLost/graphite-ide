/*
 * Hand-written replacement for the CMake-generated `include/git2/experimental.h`.
 *
 * libgit2 normally generates this from `src/libgit2/experimental.h.in` via
 * `configure_file()`. This vendored build does not run libgit2's CMake
 * configuration, so this file plays that role by hand.
 *
 * Written for libgit2 v1.8.4. No experimental features are enabled: the
 * task brief's scope is limited to linking and calling
 * git_libgit2_version(), so GIT_EXPERIMENTAL_SHA256 is left undefined.
 */
#ifndef INCLUDE_experimental_h__
#define INCLUDE_experimental_h__

#endif
