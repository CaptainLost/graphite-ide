#include <doctest/doctest.h>

#include "Wsl/LauncherScript.h"

#include <string>

TEST_CASE("The graphite command runs Graphite on the folder it is given, as Windows names it")
{
	const std::string script = Graphite::LauncherScript("/mnt/d/Tools/Graphite/Graphite.exe");

	CHECK(script ==
		  "#!/bin/sh\n"
		  "# Opens a folder in Graphite. Written by Graphite; it rewrites this file when it moves.\n"
		  "target=$(realpath -- \"${1:-.}\") || exit 1\n"
		  "'/mnt/d/Tools/Graphite/Graphite.exe' \"$(wslpath -w \"$target\")\" >/dev/null 2>&1 &\n");
}

TEST_CASE("A quote in Graphite's path is escaped for the shell")
{
	const std::string script = Graphite::LauncherScript("/mnt/c/Users/O'Neil/Graphite.exe");

	CHECK(script.find("'/mnt/c/Users/O'\\''Neil/Graphite.exe'") != std::string::npos);
}
