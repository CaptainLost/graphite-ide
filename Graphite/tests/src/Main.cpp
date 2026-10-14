#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Settings/UserDirectories.h"

#include "Core/Platform/Process.h"

#include <cstdio>
#include <filesystem>

// WARNING: Suites that exercise setters write graphite.json. Without the redirect they would
// overwrite the real %APPDATA%\Graphite settings of whoever runs the tests.
int main(int argc, char** argv)
{
	const Core::Result<std::filesystem::path> directory = Core::ExecutableDirectory();

	if (!directory)
	{
		std::fprintf(stderr, "Refusing to run: cannot locate the test executable to redirect user files beside it\n");
		return 1;
	}

	Graphite::UserDirectories::Redirect(directory.GetValue() / "user-data");

	return doctest::Context{argc, argv}.run();
}
