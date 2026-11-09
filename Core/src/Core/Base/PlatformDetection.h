#pragma once

// premake defines CORE_PLATFORM_WINDOWS, and this header defines it too for a build that does not
// come from premake's project files: without it EntryPoint.h compiles to no main() at all and the
// link error that follows names nothing.

#ifdef _WIN32
#ifdef _WIN64
#ifndef CORE_PLATFORM_WINDOWS
#define CORE_PLATFORM_WINDOWS
#endif
#else
#error "x86 builds are not supported"
#endif
#elif defined(__APPLE__) || defined(__MACH__)
#error "macOS is not supported"
#elif defined(__ANDROID__)
#error "Android is not supported"
#elif defined(__linux__)
// Only the headless part of Core builds for Linux: the WSL helper and its tests (ADR 0693).
#ifdef __x86_64__
#ifndef CORE_PLATFORM_LINUX
#define CORE_PLATFORM_LINUX
#endif
#else
#error "Only x86-64 Linux is supported"
#endif
#else
#error "Unknown platform"
#endif
