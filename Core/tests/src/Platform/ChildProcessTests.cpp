#include <doctest/doctest.h>

#include "Core/Platform/ChildProcess.h"

#include "TempDirectory.h"

#include <chrono>
#include <string>
#include <thread>

namespace {

	using StartedChild = Core::Result<Core::Scope<Core::ChildProcess>>;

	constexpr std::size_t kReadChunkBytes = 256;
	constexpr int kExitPollAttempts = 100;
	constexpr std::chrono::milliseconds kExitPollInterval{20};

	std::string ReadAll(Core::ChildProcess& process)
	{
		std::string text;
		std::array<char, kReadChunkBytes> buffer{};

		for (std::size_t bytesRead = process.Read(buffer); bytesRead != 0; bytesRead = process.Read(buffer))
		{
			text.append(buffer.data(), bytesRead);
		}

		return text;
	}

	// The output pipe closes as the child is torn down, which can be a moment before its process
	// object is signalled, so the exit gets a bounded time to become visible.
	void WaitForExit(const Core::ChildProcess& process)
	{
		for (int attempt = 0; attempt < kExitPollAttempts && process.IsRunning(); ++attempt)
		{
			std::this_thread::sleep_for(kExitPollInterval);
		}
	}
}

TEST_CASE("ChildProcess reads what the child writes to its output until Read returns zero as the child exits")
{
	StartedChild started = Core::ChildProcess::Start("cmd.exe", {"/c", "echo hello"},
													 std::filesystem::current_path());

	REQUIRE(started);
	CHECK(ReadAll(*started.GetValue()).find("hello") != std::string::npos);
}

TEST_CASE("ChildProcess writes to the child's input")
{
	StartedChild started = Core::ChildProcess::Start("cmd.exe", {"/c", "findstr", "wanted"},
													 std::filesystem::current_path());

	REQUIRE(started);
	CHECK(started.GetValue()->Write("unwanted line\r\nwanted line\r\n"));
}

TEST_CASE("ChildProcess quotes a program path that contains a space so CreateProcessW does not split it as it would under Program Files")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path directory = temporaryDirectory.MakeDirectory("has space");
	const std::filesystem::path copiedProgram = directory / "cmd.exe";

	std::error_code copyError;
	std::filesystem::copy_file("C:\\Windows\\System32\\cmd.exe", copiedProgram, copyError);
	REQUIRE_FALSE(copyError);

	StartedChild started =
		Core::ChildProcess::Start(copiedProgram, {"/c", "echo hello"}, std::filesystem::current_path());

	REQUIRE(started);
	CHECK(ReadAll(*started.GetValue()).find("hello") != std::string::npos);
}

TEST_CASE("ChildProcess terminating the child releases a thread blocked in Read")
{
	const std::vector<std::string> noArgumentsSoItWaitsOnItsInputForever;
	StartedChild started = Core::ChildProcess::Start("cmd.exe", noArgumentsSoItWaitsOnItsInputForever, std::filesystem::current_path());

	REQUIRE(started);

	Core::ChildProcess& process = *started.GetValue();

	std::thread blockedReader([&process] { ReadAll(process); });

	process.Terminate();
	blockedReader.join();
	WaitForExit(process);

	CHECK_FALSE(process.IsRunning());
}

TEST_CASE("ChildProcess reports a program that does not exist")
{
	const std::vector<std::string> noArguments;
	StartedChild started = Core::ChildProcess::Start("no-such-program-8a3f.exe", noArguments,
													 std::filesystem::current_path());

	CHECK_FALSE(started);
	CHECK(started.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("ChildProcess reports a child that exited with code 259 which is also STILL_ACTIVE as not running")
{
	StartedChild started = Core::ChildProcess::Start("cmd.exe", {"/c", "exit 259"},
													 std::filesystem::current_path());

	REQUIRE(started);

	Core::ChildProcess& process = *started.GetValue();

	ReadAll(process);

	WaitForExit(process);

	CHECK_FALSE(process.IsRunning());
}
