#include <doctest/doctest.h>

#include "Core/Platform/ChildProcess.h"

#include "TempDirectory.h"

#include <array>
#include <chrono>
#include <string>
#include <thread>

#include <windows.h>

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

	// Clears the variable on every path out of a test, a failed REQUIRE included.
	class ScopedProcessVariable
	{
	public:
		ScopedProcessVariable(const wchar_t* name, const wchar_t* value) : m_Name(name)
		{
			m_Set = ::SetEnvironmentVariableW(m_Name, value) != FALSE;
		}

		~ScopedProcessVariable()
		{
			::SetEnvironmentVariableW(m_Name, nullptr);
		}

		ScopedProcessVariable(const ScopedProcessVariable&) = delete;
		ScopedProcessVariable& operator=(const ScopedProcessVariable&) = delete;

		bool WasSet() const { return m_Set; }

	private:
		const wchar_t* m_Name;
		bool m_Set = false;
	};

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

TEST_CASE("Terminating a child ends the processes it started so its output closes at once")
{
	// cmd waits on ping, and ping inherits the output pipe: ending cmd alone would leave the pipe
	// open until ping finished, half a minute later.
	StartedChild started = Core::ChildProcess::Start("cmd.exe", {"/c", "ping -n 30 127.0.0.1"},
													 std::filesystem::current_path());
	REQUIRE(started);
	Core::ChildProcess& child = *started.GetValue();

	std::array<char, kReadChunkBytes> buffer{};
	REQUIRE(child.Read(buffer) > 0);

	const std::chrono::steady_clock::time_point terminatedAt = std::chrono::steady_clock::now();
	child.Terminate();
	ReadAll(child);

	CHECK(std::chrono::steady_clock::now() - terminatedAt < std::chrono::seconds{10});
}

TEST_CASE("A variable given to a child reaches it")
{
	const std::array<Core::EnvironmentVariable, 1> environment{{{.Name = "CORE_TEST_CHILD_VARIABLE", .Value = "given"}}};

	StartedChild started = Core::ChildProcess::Start("cmd.exe", {"/c", "echo %CORE_TEST_CHILD_VARIABLE%"},
													 std::filesystem::current_path(), environment);
	REQUIRE(started);

	CHECK(ReadAll(*started.GetValue()).find("given") != std::string::npos);
}

TEST_CASE("A child given variables still inherits the rest and a given variable wins over an inherited one")
{
	const ScopedProcessVariable inherited(L"CORE_TEST_INHERITED_VARIABLE", L"inherited");
	const ScopedProcessVariable replaced(L"CORE_TEST_REPLACED_VARIABLE", L"parent");
	REQUIRE(inherited.WasSet());
	REQUIRE(replaced.WasSet());

	// "winner" sorts after "parent", so a block that kept both entries would put the inherited one first.
	const std::array<Core::EnvironmentVariable, 1> environment{{{.Name = "core_test_replaced_variable", .Value = "winner"}}};

	StartedChild started = Core::ChildProcess::Start("cmd.exe", {"/c", "echo %CORE_TEST_INHERITED_VARIABLE% %CORE_TEST_REPLACED_VARIABLE% & set CORE_TEST_REPLACED_VARIABLE"},
													 std::filesystem::current_path(), environment);
	REQUIRE(started);
	const std::string output = ReadAll(*started.GetValue());

	CHECK(output.find("inherited winner") != std::string::npos);
	CHECK(output.find("parent") == std::string::npos);
	CHECK(output.find("=winner") != std::string::npos);
}

TEST_CASE("A variable without a name or with an equals sign or a null in its name is refused before anything starts")
{
	using namespace std::string_literals;

	const std::array<Core::EnvironmentVariable, 4> refused{{{.Name = "", .Value = "value"},
															{.Name = "CORE_TEST=SPLIT", .Value = "value"},
															{.Name = "CORE_TEST\0NULL"s, .Value = "value"},
															{.Name = "CORE_TEST_NULL_VALUE", .Value = "cut\0short"s}}};

	for (const Core::EnvironmentVariable& variable : refused)
	{
		const std::array<Core::EnvironmentVariable, 1> environment{variable};

		StartedChild started = Core::ChildProcess::Start("cmd.exe", {"/c", "echo started"}, std::filesystem::current_path(), environment);

		REQUIRE_FALSE(started);
		CHECK(started.GetError().Code == Core::ErrorCode::InvalidArgument);
	}
}

TEST_CASE("A variable whose name starts with an equals sign is given as the drive entries are")
{
	const std::array<Core::EnvironmentVariable, 1> environment{{{.Name = "=Z:", .Value = "Z:\\"}}};

	StartedChild started = Core::ChildProcess::Start("cmd.exe", {"/c", "echo started"}, std::filesystem::current_path(), environment);
	REQUIRE(started);

	CHECK(ReadAll(*started.GetValue()).find("started") != std::string::npos);
}

TEST_CASE("A child runs as long as a process it started runs")
{
	// cmd starts ping in the background and exits at once; ping goes on for about two seconds.
	StartedChild started = Core::ChildProcess::Start("cmd.exe", {"/c", "start /b ping -n 3 127.0.0.1 >nul"},
													 std::filesystem::current_path());
	REQUIRE(started);
	Core::ChildProcess& child = *started.GetValue();

	std::this_thread::sleep_for(std::chrono::milliseconds{500});

	const bool running = child.IsRunning();
	child.Terminate();

	CHECK(running);
}
