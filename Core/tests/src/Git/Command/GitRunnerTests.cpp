#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Command/GitExecutable.h"
#include "Core/Git/Command/GitRunner.h"

#include "GitFixture.h"
#include "ProcessFixtures.h"
#include "ScopedEnvironmentVariable.h"
#include "TempDirectory.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {

	using RunResult = Core::Result<Core::GitOutput>;
	using LineHandler = std::function<void(std::string_view)>;

	constexpr char kSkippedWithoutGit[] = "Skipped: git is neither on PATH nor where Git for Windows installs it";

	constexpr bool kNoSshCommandConfigured = false;
	constexpr bool kSshCommandConfigured = true;
	constexpr std::uint32_t kExitedCleanly = 0;

	// Long enough for a slow machine, short enough that a run that stalls fails the suite instead of hanging it.
	constexpr std::chrono::seconds kRunDeadline{60};
	constexpr std::chrono::seconds kCancelDeadline{2};

	// A process is torn down a moment after its pipes close, so the directory it held gets a bounded time to come free.
	constexpr std::chrono::milliseconds kReleasePollInterval{20};
	constexpr int kReleasePollAttempts = 100;

	constexpr int kManyCommits = 200;
	constexpr int kFilesChangedInTurn = 7;
	constexpr std::size_t kWellPastAPipeBuffer = 16 * 1024;

	constexpr int kClonedCommits = 40;
	constexpr int kWholePercent = 100;

	// The process left behind sleeps a minute: a run that waited on it would take about that long.
	constexpr std::chrono::seconds kWellShortOfTheLeftBehindLifetime{30};
	constexpr std::chrono::seconds kLeftBehindDeadline{30};
	constexpr std::chrono::milliseconds kLongerThanATermination{1000};

	void IgnoreLine([[maybe_unused]] std::string_view line)
	{
	}

	std::vector<Core::EnvironmentVariable> GitEnvironmentWith(const std::string& name, const std::string& value)
	{
		std::vector<Core::EnvironmentVariable> environment = Core::GitEnvironment(kNoSshCommandConfigured);
		environment.push_back({.Name = name, .Value = value});
		return environment;
	}

	// Discovery stops below the ceiling, so a repository that happens to enclose the temporary directory is never found.
	std::vector<Core::EnvironmentVariable> GitEnvironmentBelowCeiling(const std::filesystem::path& ceiling)
	{
		return GitEnvironmentWith("GIT_CEILING_DIRECTORIES", Core::ToUtf8(ceiling));
	}

	// Runs git on a thread of its own and cancels it once the deadline passes, so that a run that stalls fails the test
	// with Cancelled rather than hanging the suite.
	RunResult RunGitWithin(std::chrono::seconds deadline, const std::filesystem::path& git, const Core::GitRun& run,
						   const LineHandler& onProgressLine)
	{
		std::atomic<bool> cancelled{false};
		std::future<RunResult> running = std::async(std::launch::async, Core::RunGit, std::cref(git), std::cref(run),
													std::cref(cancelled), std::cref(onProgressLine));

		const bool finishedInTime = running.wait_for(deadline) == std::future_status::ready;

		if (!finishedInTime)
		{
			cancelled.store(true);
		}

		return running.get();
	}

	// The shell that runs a git alias takes a backslash as an escape in places, and a forward slash nowhere.
	std::string WithForwardSlashes(std::string path)
	{
		for (char& character : path)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}

		return path;
	}

	std::optional<std::string> ValueOf(const std::vector<Core::EnvironmentVariable>& environment, const std::string& name)
	{
		for (const Core::EnvironmentVariable& variable : environment)
		{
			if (variable.Name == name)
			{
				return variable.Value;
			}
		}

		return std::nullopt;
	}

	std::vector<std::string_view> LinesOf(std::string_view text)
	{
		std::vector<std::string_view> lines;
		std::size_t lineStart = 0;

		while (lineStart < text.size())
		{
			const std::size_t lineEnd = text.find('\n', lineStart);
			const bool isLastLine = lineEnd == std::string_view::npos;
			const std::size_t lineLength = isLastLine ? text.size() - lineStart : lineEnd - lineStart;

			lines.push_back(text.substr(lineStart, lineLength));
			lineStart = isLastLine ? text.size() : lineEnd + 1;
		}

		return lines;
	}

	int CountLinesStartingWith(std::string_view text, std::string_view prefix)
	{
		int matchingLines = 0;

		for (const std::string_view line : LinesOf(text))
		{
			if (line.starts_with(prefix))
			{
				++matchingLines;
			}
		}

		return matchingLines;
	}

	std::string FirstLine(std::string_view text)
	{
		const std::vector<std::string_view> lines = LinesOf(text);

		return lines.empty() ? std::string{} : std::string{lines.front()};
	}

	std::vector<int> PercentsOfLinesStartingWith(const std::vector<std::string>& lines, std::string_view prefix)
	{
		std::vector<int> percents;

		for (const std::string& line : lines)
		{
			const std::optional<int> percent = Core::ProgressPercent(line);

			if (line.starts_with(prefix) && percent.has_value())
			{
				percents.push_back(*percent);
			}
		}

		return percents;
	}

	bool AnyLineHoldsALineEnd(const std::vector<std::string>& lines)
	{
		for (const std::string& line : lines)
		{
			if (line.find_first_of("\r\n") != std::string::npos)
			{
				return true;
			}
		}

		return false;
	}

	bool RisesOrHolds(const std::vector<int>& percents)
	{
		for (std::size_t percentIndex = 1; percentIndex < percents.size(); ++percentIndex)
		{
			if (percents[percentIndex] < percents[percentIndex - 1])
			{
				return false;
			}
		}

		return true;
	}

	bool TryRemoveDirectory(const std::filesystem::path& directory)
	{
		std::error_code removeError;
		const bool removed = std::filesystem::remove(directory, removeError);

		return removed && !removeError;
	}

	bool RemovedOnceReleased(const std::filesystem::path& directory)
	{
		for (int attempt = 0; attempt < kReleasePollAttempts; ++attempt)
		{
			if (TryRemoveDirectory(directory))
			{
				return true;
			}

			std::this_thread::sleep_for(kReleasePollInterval);
		}

		return false;
	}

	void CommitInTurn(GitFixture& fixture, int commitCount)
	{
		for (int commitIndex = 0; commitIndex < commitCount; ++commitIndex)
		{
			fixture.Write(std::format("file{}.txt", commitIndex % kFilesChangedInTurn), std::format("change {}\n", commitIndex));
			fixture.Commit(std::format("change {}", commitIndex));
		}
	}

	// Opens once git's trace says the git that waits on its input has started.
	class WaitingGitSignal
	{
	public:
		void OnLine(std::string_view line)
		{
			const bool waitingGitStarted = line.find("built-in: git hash-object") != std::string_view::npos;

			if (!waitingGitStarted)
			{
				return;
			}

			const std::lock_guard<std::mutex> lock(m_Mutex);
			m_Started = true;
			m_StartedChanged.notify_all();
		}

		bool WaitFor(std::chrono::seconds timeout)
		{
			std::unique_lock<std::mutex> lock(m_Mutex);

			return m_StartedChanged.wait_for(lock, timeout, [this] { return m_Started; });
		}

	private:
		std::mutex m_Mutex;
		std::condition_variable m_StartedChanged;
		bool m_Started = false;
	};

	// Keeps every line it is handed, and whether any came on the thread that started the run.
	class HandedLines
	{
	public:
		explicit HandedLines(std::thread::id runningThread) : m_RunningThread(runningThread) {}

		void OnLine(std::string_view line)
		{
			const std::lock_guard<std::mutex> lock(m_Mutex);
			m_Lines.emplace_back(line);

			if (std::this_thread::get_id() == m_RunningThread)
			{
				m_AnyOnRunningThread = true;
			}
		}

		std::vector<std::string> Lines() const
		{
			const std::lock_guard<std::mutex> lock(m_Mutex);
			return m_Lines;
		}

		bool AnyOnRunningThread() const
		{
			const std::lock_guard<std::mutex> lock(m_Mutex);
			return m_AnyOnRunningThread;
		}

	private:
		mutable std::mutex m_Mutex;
		std::thread::id m_RunningThread;
		std::vector<std::string> m_Lines;
		bool m_AnyOnRunningThread = false;
	};
}

TEST_CASE("Git: a run returns the exit code and both outputs")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	TempDirectory temporaryDirectory;
	const std::filesystem::path outsideAnyRepository = temporaryDirectory.MakeDirectory("outside");
	const std::vector<Core::EnvironmentVariable> environment = GitEnvironmentBelowCeiling(temporaryDirectory.GetPath());

	const Core::GitRun version{.Arguments = {"--version"}, .WorkingDirectory = outsideAnyRepository, .Environment = environment};
	const RunResult versionShown = RunGitWithin(kRunDeadline, *git, version, IgnoreLine);

	REQUIRE(versionShown);
	CHECK(versionShown.GetValue().ExitCode == kExitedCleanly);
	CHECK(versionShown.GetValue().Out.starts_with("git version "));
	CHECK(versionShown.GetValue().Errors.empty());

	const Core::GitRun bogus{.Arguments = {"rev-parse", "--bogus"}, .WorkingDirectory = outsideAnyRepository, .Environment = environment};
	const RunResult bogusRefused = RunGitWithin(kRunDeadline, *git, bogus, IgnoreLine);

	REQUIRE(bogusRefused);
	CHECK(bogusRefused.GetValue().ExitCode != kExitedCleanly);
	CHECK(bogusRefused.GetValue().Out.empty());
	CHECK(bogusRefused.GetValue().Errors.find("not a git repository") != std::string::npos);
}

TEST_CASE("Git: a git that cannot be started is an input-output failure")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path missingGit = temporaryDirectory.GetPath() / "git.exe";

	const Core::GitRun version{.Arguments = {"--version"}, .WorkingDirectory = temporaryDirectory.GetPath(), .Environment = {}};
	const std::atomic<bool> notCancelled{false};

	const RunResult started = Core::RunGit(missingGit, version, notCancelled, IgnoreLine);

	REQUIRE_FALSE(started);
	CHECK(started.GetError().Code == Core::ErrorCode::IoFailure);
}

TEST_CASE("Git: a run cancelled before it starts never starts git")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path missingGit = temporaryDirectory.GetPath() / "git.exe";

	const Core::GitRun version{.Arguments = {"--version"}, .WorkingDirectory = temporaryDirectory.GetPath(), .Environment = {}};
	const std::atomic<bool> alreadyCancelled{true};

	const RunResult started = Core::RunGit(missingGit, version, alreadyCancelled, IgnoreLine);

	REQUIRE_FALSE(started);
	CHECK(started.GetError().Code == Core::ErrorCode::Cancelled);
}

TEST_CASE("Git: a run with lots of output on both pipes does not stall")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	GitFixture fixture;
	CommitInTurn(fixture, kManyCommits);

	// Packed, because GIT_TRACE_PACK_ACCESS reports each object read from a pack and none read loose.
	const Core::GitRun repack{.Arguments = {"repack", "-a", "-d", "-q"},
							  .WorkingDirectory = fixture.GetPath(),
							  .Environment = Core::GitEnvironment(kNoSshCommandConfigured)};
	const RunResult repacked = RunGitWithin(kRunDeadline, *git, repack, IgnoreLine);

	REQUIRE(repacked);
	REQUIRE(repacked.GetValue().ExitCode == kExitedCleanly);

	// The format and colour are named, so that a format or colour set in the user's own config cannot change the lines.
	const Core::GitRun log{.Arguments = {"log", "--stat", "--no-color", "--format=medium"},
						   .WorkingDirectory = fixture.GetPath(),
						   .Environment = GitEnvironmentWith("GIT_TRACE_PACK_ACCESS", "1")};
	const RunResult logged = RunGitWithin(kRunDeadline, *git, log, IgnoreLine);

	REQUIRE(logged);

	const Core::GitOutput& output = logged.GetValue();

	CHECK(output.ExitCode == kExitedCleanly);
	CHECK(output.Out.size() > kWellPastAPipeBuffer);
	CHECK(output.Errors.size() > kWellPastAPipeBuffer);
	CHECK(CountLinesStartingWith(output.Out, "commit ") == kManyCommits);
}

TEST_CASE("Git: cancelling a run ends git and every process it started")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	TempDirectory temporaryDirectory;
	const std::filesystem::path heldDirectory = temporaryDirectory.MakeDirectory("held");

	std::vector<Core::EnvironmentVariable> environment = GitEnvironmentBelowCeiling(temporaryDirectory.GetPath());
	environment.push_back({.Name = "GIT_TRACE", .Value = "1"});

	// The alias puts a shell and a second git between this test and the git that waits on its input, each holding the
	// directory as its current one: a cancel that ended only the git this test started would leave it held.
	const Core::GitRun waitOnInput{.Arguments = {"-c", "alias.wait-on-input=!git hash-object --stdin", "wait-on-input"},
								   .WorkingDirectory = heldDirectory,
								   .Environment = environment};

	WaitingGitSignal waitingGit;
	const LineHandler onProgressLine = [&waitingGit](std::string_view line) { waitingGit.OnLine(line); };

	std::atomic<bool> cancelled{false};
	std::future<RunResult> running = std::async(std::launch::async, Core::RunGit, std::cref(*git), std::cref(waitOnInput),
												std::cref(cancelled), std::cref(onProgressLine));

	const bool waitingGitStarted = waitingGit.WaitFor(kRunDeadline);
	const bool heldWhileRunning = !TryRemoveDirectory(heldDirectory);

	cancelled.store(true);

	const bool endedInTime = running.wait_for(kCancelDeadline) == std::future_status::ready;
	const RunResult ended = running.get();

	REQUIRE(waitingGitStarted);
	CHECK(heldWhileRunning);
	CHECK(endedInTime);
	REQUIRE_FALSE(ended);
	CHECK(ended.GetError().Code == Core::ErrorCode::Cancelled);
	CHECK(RemovedOnceReleased(heldDirectory));
}

TEST_CASE("Git: a run that succeeds leaves running what git started")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	TempDirectory temporaryDirectory;
	const std::filesystem::path workingDirectory = temporaryDirectory.MakeDirectory("work");
	const std::filesystem::path leftBehindIdFile = temporaryDirectory.GetPath() / "left-behind-id.txt";

	std::vector<Core::EnvironmentVariable> environment = GitEnvironmentBelowCeiling(temporaryDirectory.GetPath());
	environment.push_back({.Name = "CORE_TEST_POWERSHELL", .Value = WithForwardSlashes(Core::ToUtf8(WindowsPowerShell()))});
	environment.push_back({.Name = "CORE_TEST_PROCESS_ID_FILE", .Value = Core::ToUtf8(leftBehindIdFile)});

	// The shell puts PowerShell in the background and ends, so git ends at once, while PowerShell runs on for a minute
	// holding git's output pipes, much as a sign-in browser outlives the fetch that opened it.
	const Core::GitRun leaveBehind{.Arguments = {"-c",
												 "alias.leave-behind=!\"$CORE_TEST_POWERSHELL\" -NoProfile -NonInteractive -Command "
												 "'[IO.File]::WriteAllText($env:CORE_TEST_PROCESS_ID_FILE, [string]$PID + [Environment]::NewLine); "
												 "Start-Sleep -Seconds 60' &",
												 "leave-behind"},
								   .WorkingDirectory = workingDirectory,
								   .Environment = environment};

	const std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
	const RunResult ran = RunGitWithin(kRunDeadline, *git, leaveBehind, IgnoreLine);
	const std::chrono::steady_clock::duration ranFor = std::chrono::steady_clock::now() - startedAt;

	REQUIRE(ran);
	CHECK(ran.GetValue().ExitCode == kExitedCleanly);
	CHECK(ranFor < kWellShortOfTheLeftBehindLifetime);

	const std::optional<std::uint32_t> leftBehindId = WaitForProcessId(leftBehindIdFile, kLeftBehindDeadline);
	REQUIRE(leftBehindId.has_value());

	const LeftBehindProcess leftBehind{*leftBehindId};

	CHECK(leftBehind.IsRunning());
	CHECK_FALSE(leftBehind.EndsWithin(kLongerThanATermination));
}

TEST_CASE("Git: progress lines are handed over as they come")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	GitFixture source;
	CommitInTurn(source, kClonedCommits);

	TempDirectory destinationParent;
	const std::filesystem::path destination = destinationParent.GetPath() / "clone";

	// --no-local makes git send a pack, as over the network: a local clone copies the files and reports no progress.
	const Core::GitRun clone{.Arguments = {"clone", "--progress", "--no-local", Core::ToUtf8(source.GetPath()), Core::ToUtf8(destination)},
							 .WorkingDirectory = destinationParent.GetPath(),
							 .Environment = Core::GitEnvironment(kNoSshCommandConfigured)};

	HandedLines handed{std::this_thread::get_id()};
	const LineHandler onProgressLine = [&handed](std::string_view line) { handed.OnLine(line); };
	const std::atomic<bool> notCancelled{false};

	const RunResult cloned = Core::RunGit(*git, clone, notCancelled, onProgressLine);

	REQUIRE(cloned);
	CHECK(cloned.GetValue().ExitCode == kExitedCleanly);

	const std::vector<std::string> lines = handed.Lines();
	const std::vector<int> receivingPercents = PercentsOfLinesStartingWith(lines, "Receiving objects:");

	CHECK_FALSE(AnyLineHoldsALineEnd(lines));
	CHECK_FALSE(handed.AnyOnRunningThread());
	REQUIRE(receivingPercents.size() > 1);
	CHECK(RisesOrHolds(receivingPercents));
	CHECK(receivingPercents.back() == kWholePercent);
}

TEST_CASE("Git: the environment never prompts and keeps a configured ssh command")
{
	const ScopedEnvironmentVariable noInheritedSshCommand(L"GIT_SSH_COMMAND", std::nullopt);
	REQUIRE(noInheritedSshCommand.WasSet());

	const std::vector<Core::EnvironmentVariable> unconfigured = Core::GitEnvironment(kNoSshCommandConfigured);

	CHECK(ValueOf(unconfigured, "GIT_TERMINAL_PROMPT") == "0");
	CHECK(ValueOf(unconfigured, "LC_ALL") == "C");
	CHECK(ValueOf(unconfigured, "GIT_SSH_COMMAND") == "ssh -o BatchMode=yes");

	const std::vector<Core::EnvironmentVariable> configured = Core::GitEnvironment(kSshCommandConfigured);

	CHECK(ValueOf(configured, "GIT_TERMINAL_PROMPT") == "0");
	CHECK(ValueOf(configured, "LC_ALL") == "C");
	CHECK_FALSE(ValueOf(configured, "GIT_SSH_COMMAND").has_value());

	const ScopedEnvironmentVariable inheritedSshCommand(L"GIT_SSH_COMMAND", L"ssh -i chosen-key");
	REQUIRE(inheritedSshCommand.WasSet());

	const std::vector<Core::EnvironmentVariable> inherited = Core::GitEnvironment(kNoSshCommandConfigured);

	CHECK(ValueOf(inherited, "GIT_TERMINAL_PROMPT") == "0");
	CHECK_FALSE(ValueOf(inherited, "GIT_SSH_COMMAND").has_value());
}

TEST_CASE("Git: progress percent reads git's counting lines")
{
	CHECK(Core::ProgressPercent("Receiving objects:  45% (9/20)") == 45);
	CHECK(Core::ProgressPercent("Receiving objects:   0% (1/900)") == 0);
	CHECK(Core::ProgressPercent("remote: Counting objects: 100% (5/5), done.") == 100);
	CHECK(Core::ProgressPercent("Writing objects: 100% (3/3), 250 bytes | 250.00 KiB/s, done.") == 100);
	CHECK(Core::ProgressPercent("Resolving deltas:  50% (1/2)") == 50);

	CHECK_FALSE(Core::ProgressPercent("remote: Enumerating objects: 5, done.").has_value());
	CHECK_FALSE(Core::ProgressPercent("Cloning into 'clone'...").has_value());
	CHECK_FALSE(Core::ProgressPercent("fatal: 50% of what was asked").has_value());
	CHECK_FALSE(Core::ProgressPercent("Receiving objects: 145% (1/1)").has_value());
	CHECK_FALSE(Core::ProgressPercent("Receiving objects: % (1/1)").has_value());
	CHECK_FALSE(Core::ProgressPercent("").has_value());
}

TEST_CASE("Git: a path with spaces and non-ASCII characters is passed and read back intact")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	TempDirectory temporaryDirectory;
	const std::string repositoryName = "repository with spaces żółw 日本";
	const std::filesystem::path repository = temporaryDirectory.GetPath() / Core::FromUtf8(repositoryName);
	const std::vector<Core::EnvironmentVariable> environment = Core::GitEnvironment(kNoSshCommandConfigured);

	const Core::GitRun init{.Arguments = {"init", "-q", Core::ToUtf8(repository)}, .WorkingDirectory = temporaryDirectory.GetPath(), .Environment = environment};
	const RunResult initialised = RunGitWithin(kRunDeadline, *git, init, IgnoreLine);

	REQUIRE(initialised);
	REQUIRE(initialised.GetValue().ExitCode == kExitedCleanly);
	CHECK(std::filesystem::is_directory(repository / ".git"));

	const Core::GitRun showTopLevel{.Arguments = {"rev-parse", "--show-toplevel"}, .WorkingDirectory = repository, .Environment = environment};
	const RunResult shown = RunGitWithin(kRunDeadline, *git, showTopLevel, IgnoreLine);

	REQUIRE(shown);
	REQUIRE(shown.GetValue().ExitCode == kExitedCleanly);

	const std::string shownPath = FirstLine(shown.GetValue().Out);
	std::error_code compareError;

	CHECK(shownPath.ends_with("/" + repositoryName));
	CHECK(std::filesystem::equivalent(Core::FromUtf8(shownPath), repository, compareError));
	CHECK_FALSE(compareError);
}
