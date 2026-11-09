#include <doctest/doctest.h>

#include "Core/Base/Assert.h"
#include "Core/Base/PlatformDetection.h"

#if defined(CORE_PLATFORM_LINUX)
#include <csignal>
#include <cstdio>

#include <sys/wait.h>
#include <unistd.h>
#endif

// On Windows a failed assert breaks into the debugger, which ends a process nobody debugs; the
// Linux build has to stop the process the same way rather than run on past a broken invariant.
// Fork gives the assert a process of its own to end.
#if defined(CORE_PLATFORM_LINUX) && defined(CORE_DEBUG)
TEST_CASE("On Linux, a failed assert stops the process with a signal")
{
	const pid_t child = ::fork();
	REQUIRE(child >= 0);

	if (child == 0)
	{
		// The child inherits doctest's crash handlers, which would report the trap as this test
		// crashing; the default action is what a process nobody watches gets.
		std::signal(SIGILL, SIG_DFL);
		std::signal(SIGTRAP, SIG_DFL);

		// Keeps the child's report out of the test run's output.
		static_cast<void>(std::freopen("/dev/null", "w", stdout));
		static_cast<void>(std::freopen("/dev/null", "w", stderr));
		CORE_ASSERT(false, "this assert fails on purpose");
		::_exit(0);
	}

	int status = 0;
	REQUIRE(::waitpid(child, &status, 0) == child);
	CHECK(WIFSIGNALED(status));
}
#endif
