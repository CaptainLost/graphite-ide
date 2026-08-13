#pragma once

// premake already defines CORE_PLATFORM_WINDOWS. This header exists to notice when it does
// not: without it EntryPoint.h compiles to no main() at all, and the first sign is a link
// error that names nothing.

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
#error "Linux is not supported"
#else
#error "Unknown platform"
#endif
