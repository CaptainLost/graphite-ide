#pragma once

namespace Core {

	// Out of line rather than in the macro: reaching the debugger's output window needs <Windows.h>,
	// which a header included by nearly everything must not pull in.
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

// A release build does not evaluate CORE_ASSERT's condition at all; a condition whose call
// must happen in every build goes in CORE_VERIFY, which still evaluates it there.
#ifdef CORE_DEBUG
#define CORE_ASSERT(condition, message)              \
	do                                               \
	{                                                \
		if (!(condition))                            \
		{                                            \
			CORE_INTERNAL_FAIL(#condition, message); \
		}                                            \
	} while (false)

#define CORE_VERIFY(condition, message) CORE_ASSERT(condition, message)
#else
#define CORE_ASSERT(condition, message) ((void)0)

#define CORE_VERIFY(condition, message) \
	do                                  \
	{                                   \
		(void)(condition);              \
	} while (false)
#endif
