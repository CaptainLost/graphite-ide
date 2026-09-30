#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Settings/UserDirectories.h"

#include "Core/Platform/Process.h"

#include <cstdio>

// Several suites exercise setters that write graphite.json. Without this they would write into
// the real %APPDATA%\Graphite and overwrite the settings of whoever runs the tests; redirected,
// they land in build output beside this executable, which is disposable.
int main(int argc, char** argv)
{
	const auto directory = Core::ExecutableDirectory();

	if (!directory)
	{
		std::fprintf(stderr, "Refusing to run: cannot locate the test executable to redirect user files beside it\n");
		return 1;
	}

	Graphite::UserDirectories::Redirect(directory.GetValue() / "user-data");

	return doctest::Context{argc, argv}.run();
}
