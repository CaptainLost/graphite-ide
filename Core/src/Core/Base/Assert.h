#pragma once

namespace Core {

	// Reports a failed assertion everywhere it might be read, before the debugger breaks.
	//
	// Out of line, and not a macro body, because the only place a failure is legible in a
	// windowed build is the debugger's output window, and reaching that needs <Windows.h> —
	// which this header cannot pull in, being included by nearly everything.
	void ReportAssertionFailure(const char* condition, const char* message, const char* file,
								int line);
}

#ifdef CORE_PLATFORM_WINDOWS
#define CORE_DEBUGBREAK() __debugbreak()
#else
#define CORE_DEBUGBREAK() ((void)0)
#endif

#define CORE_INTERNAL_FAIL(condition, message)                                      \
	do                                                                              \
	{                                                                               \
		::Core::ReportAssertionFailure((condition), (message), __FILE__, __LINE__); \
		CORE_DEBUGBREAK();                                                          \
	} while (false)

#ifdef CORE_DEBUG
#define CORE_ASSERT(cond, message)              \
	do                                          \
	{                                           \
		if (!(cond))                            \
		{                                       \
			CORE_INTERNAL_FAIL(#cond, message); \
		}                                       \
	} while (false)

#define CORE_VERIFY(cond, message) CORE_ASSERT(cond, message)
#else
#define CORE_ASSERT(cond, message) ((void)0)

#define CORE_VERIFY(cond, message) \
	do                             \
	{                              \
		(void)(cond);              \
	} while (false)
#endif
