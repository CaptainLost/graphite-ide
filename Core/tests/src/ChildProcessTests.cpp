#include <doctest/doctest.h>

#include "Core/Platform/ChildProcess.h"

#include "TempDirectory.h"

#include <string>
#include <thread>

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

TEST_CASE("ChildProcess quotes a program path that contains a space")
{
	// An unquoted command line lets CreateProcessW split the program path itself on the
	// space, which is exactly what a real "C:\Program Files\..." install path would trigger.
	TempDirectory temp;
	const std::filesystem::path directory = temp.MakeDir("has space");
	const std::filesystem::path copiedExe = directory / "cmd.exe";

	std::error_code copyError;
	std::filesystem::copy_file("C:\\Windows\\System32\\cmd.exe", copiedExe, copyError);
	REQUIRE_FALSE(copyError);

	auto process =
		Core::ChildProcess::Start(copiedExe, {"/c", "echo hello"}, std::filesystem::current_path());

	REQUIRE(process);
	CHECK(ReadAll(*process.GetValue()).find("hello") != std::string::npos);
}

TEST_CASE("ChildProcess terminating the child releases a thread blocked in Read")
{
	// cmd.exe with no arguments prints its banner and then waits on its input forever, so the
	// reader thread ends up blocked inside ReadFile with nothing coming. Only the child's death
	// closes the writing end and makes that call return, which is what a client's destructor
	// depends on to join its reader without waiting on a server that may never speak again.
	auto started = Core::ChildProcess::Start("cmd.exe", {}, std::filesystem::current_path());

	REQUIRE(started);

	Core::ChildProcess& process = *started.GetValue();

	std::thread reader([&process] { ReadAll(process); });

	process.Terminate();
	reader.join();

	CHECK_FALSE(process.IsRunning());
}

TEST_CASE("ChildProcess reports a program that does not exist")
{
	auto process = Core::ChildProcess::Start("no-such-program-8a3f.exe", {},
											 std::filesystem::current_path());

	CHECK_FALSE(process);
	CHECK(process.GetError().Code == Core::ErrorCode::NotFound);
}
