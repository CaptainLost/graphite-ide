#include <doctest/doctest.h>

#include "Core/Platform/Shell.h"

TEST_CASE("Only an https address is handed to the browser")
{
	const Core::Result<void> file = Core::Shell::OpenInBrowser("file:///C:/Windows/System32/calc.exe");
	const Core::Result<void> program = Core::Shell::OpenInBrowser("C:\\Windows\\notepad.exe");
	const Core::Result<void> plain = Core::Shell::OpenInBrowser("http://example.com/");

	REQUIRE_FALSE(file);
	REQUIRE_FALSE(program);
	REQUIRE_FALSE(plain);
	CHECK(file.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK(program.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK(plain.GetError().Code == Core::ErrorCode::InvalidArgument);
}
