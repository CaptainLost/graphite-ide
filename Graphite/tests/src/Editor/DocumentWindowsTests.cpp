#include <doctest/doctest.h>

#include "Editor/DocumentWindows.h"

#include <filesystem>

TEST_CASE("A save failure whose error names the file is shown as it is")
{
	const std::filesystem::path path("D:/Repos/IDE/src/a.cpp");

	CHECK(Graphite::SaveFailureMessage(path, "Cannot write 'D:/Repos/IDE/src/a.cpp': access denied") ==
		  "Cannot write 'D:/Repos/IDE/src/a.cpp': access denied");
}

TEST_CASE("A save failure whose error does not name the file is prefixed with its path")
{
	const std::filesystem::path path("D:/Repos/IDE/src/a.cpp");

	CHECK(Graphite::SaveFailureMessage(path, "cannot write 'D:/Temp/x1.tmp': disk full") ==
		  "D:/Repos/IDE/src/a.cpp: cannot write 'D:/Temp/x1.tmp': disk full");
	CHECK(Graphite::SaveFailureMessage(path, "access denied") == "D:/Repos/IDE/src/a.cpp: access denied");
}
