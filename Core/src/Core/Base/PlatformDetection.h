#pragma once

// The build already defines CORE_PLATFORM_WINDOWS from premake. This header exists to
// notice when it does not: without it EntryPoint.h compiles to no main() at all and
// Assert.h to no debug break, and the first sign of either is a link error that names
// nothing. Detect the platform here instead, so an unsupported one says so.

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
