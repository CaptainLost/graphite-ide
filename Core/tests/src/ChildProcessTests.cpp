#include <doctest/doctest.h>

#include "Core/Platform/ChildProcess.h"

#include <string>

namespace {

	std::string ReadAll(Core::ChildProcess& process)
	{
		std::string text;
		std::array<char, 256> buffer{};

		for (std::size_t read = process.Read(buffer); read != 0; read = process.Read(buffer))
		{
			text.append(buffer.data(), read);
		}

		return text;
	}
}

TEST_CASE("ChildProcess reads what the child writes to its output")
{
	auto process = Core::ChildProcess::Start("cmd.exe", {"/c", "echo hello"},
											 std::filesystem::current_path());

	REQUIRE(process);
	CHECK(ReadAll(*process.GetValue()).find("hello") != std::string::npos);
}

TEST_CASE("ChildProcess writes to the child's input")
{
	auto process = Core::ChildProcess::Start("cmd.exe", {"/c", "findstr", "wanted"},
											 std::filesystem::current_path());

	REQUIRE(process);
	CHECK(process.GetValue()->Write("unwanted line\r\nwanted line\r\n"));
}

TEST_CASE("ChildProcess reports a program that does not exist")
{
	auto process = Core::ChildProcess::Start("no-such-program-8a3f.exe", {},
											 std::filesystem::current_path());

	CHECK_FALSE(process);
	CHECK(process.GetError().Code == Core::ErrorCode::NotFound);
}
