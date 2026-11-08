#include "corepch.h"

#include "Core/Git/Command/GitRunner.h"

#include "Core/Platform/Process.h"

#include <charconv>
#include <chrono>
#include <exception>
#include <system_error>
#include <thread>

namespace Core {

	namespace {
		using LineHandler = std::function<void(std::string_view)>;

		// How long a cancel can go unnoticed while git runs.
		constexpr std::chrono::milliseconds kCancelPollInterval{50};
		constexpr std::chrono::milliseconds kNoWait{0};
		constexpr std::size_t kReadChunkBytes = 4096;

		constexpr const char* kNoPromptName = "GIT_TERMINAL_PROMPT";
		constexpr const char* kNoPromptValue = "0";
		constexpr const char* kLocaleName = "LC_ALL";
		constexpr const char* kUntranslatedLocale = "C";
		constexpr const char* kSshCommandName = "GIT_SSH_COMMAND";
		constexpr const wchar_t* kSshCommandWideName = L"GIT_SSH_COMMAND";
		constexpr const char* kBatchModeSsh = "ssh -o BatchMode=yes";

		// git writes "<title>: <percent>% (<done>/<total>)", the percent padded to three places.
		constexpr std::string_view kPercentBeforeCount = "% (";
		constexpr std::size_t kMostPercentDigits = 3;
		constexpr int kWholePercent = 100;
		constexpr char kTitleEnd = ':';
		constexpr char kPadding = ' ';

		enum class RunEnd
		{
			Exited,
			Cancelled
		};

		// Hands on each line once it ends, so a line split across two reads arrives whole.
		class ProgressLines
		{
		public:
			explicit ProgressLines(const LineHandler& onLine) : m_OnLine(onLine) {}

			void Append(std::string_view bytes)
			{
				for (const char byte : bytes)
				{
					const bool endsLine = byte == '\r' || byte == '\n';

					if (endsLine)
					{
						HandOnPending();
					}
					else
					{
						m_Pending.push_back(byte);
					}
				}
			}

			void Finish()
			{
				HandOnPending();
			}

		private:
			// A "\r\n" ends one line, not a line and an empty one.
			void HandOnPending()
			{
				if (!m_Pending.empty() && m_OnLine)
				{
					m_OnLine(m_Pending);
				}

				m_Pending.clear();
			}

			const LineHandler& m_OnLine;
			std::string m_Pending;
		};

		void ReadOutputToEnd(ChildProcess& git, std::string& out)
		{
			std::array<char, kReadChunkBytes> buffer{};

			for (std::size_t bytesRead = git.Read(buffer); bytesRead != 0; bytesRead = git.Read(buffer))
			{
				out.append(buffer.data(), bytesRead);
			}
		}

		void ReadErrorsToEnd(ChildProcess& git, std::string& errors, const LineHandler& onProgressLine)
		{
			std::array<char, kReadChunkBytes> buffer{};
			ProgressLines lines{onProgressLine};

			for (std::size_t bytesRead = git.ReadErrors(buffer); bytesRead != 0; bytesRead = git.ReadErrors(buffer))
			{
				const std::string_view chunk{buffer.data(), bytesRead};

				errors.append(chunk);
				lines.Append(chunk);
			}

			lines.Finish();
		}

		// A git that ended as the cancel came counts as ended, so that a late cancel ends nothing it left behind.
		RunEnd WaitForExitOrCancel(const ChildProcess& git, const std::atomic<bool>& cancelled)
		{
			while (!git.WaitForExit(kCancelPollInterval))
			{
				if (cancelled.load())
				{
					const bool endedAsTheCancelCame = git.WaitForExit(kNoWait);

					return endedAsTheCancelCame ? RunEnd::Exited : RunEnd::Cancelled;
				}
			}

			return RunEnd::Exited;
		}

		void JoinIfStarted(std::thread& reader)
		{
			if (reader.joinable())
			{
				reader.join();
			}
		}

		Error CancelledError()
		{
			return Error{.Code = ErrorCode::Cancelled, .Message = "git was cancelled"};
		}

		bool IsDigit(char character)
		{
			return character >= '0' && character <= '9';
		}

		std::string_view WithoutTrailingPadding(std::string_view text)
		{
			std::size_t length = text.size();

			while (length > 0 && text[length - 1] == kPadding)
			{
				--length;
			}

			return text.substr(0, length);
		}
	}

	Result<GitOutput> RunGit(const std::filesystem::path& git, const GitRun& run, const std::atomic<bool>& cancelled,
							 const std::function<void(std::string_view)>& onProgressLine)
	{
		if (cancelled.load())
		{
			return CancelledError();
		}

		// WARNING: The job is ended only on a cancel. What git leaves running when it ends on its own, such as a browser
		// the credential manager opened to sign in, must outlive the run, so closing the job ends nothing.
		const ChildProcessOptions gitOptions{.PipeErrors = true, .EndJobOnClose = false};

		const Result<Scope<ChildProcess>> started = ChildProcess::Start(git, run.Arguments, run.WorkingDirectory, run.Environment, gitOptions);

		if (!started)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot start git: {}", started.GetError().Message)};
		}

		ChildProcess& process = *started.GetValue();
		GitOutput output;

		// WARNING: A child that writes much to both pipes blocks on whichever is full, so both are read at once, each
		// on its own thread. A joinable thread destroyed unjoined ends this process, so one started before the other
		// failed to start is joined, once ending git has released its read.
		std::thread outputReader;
		std::thread errorReader;

		try
		{
			outputReader = std::thread(ReadOutputToEnd, std::ref(process), std::ref(output.Out));
			errorReader = std::thread(ReadErrorsToEnd, std::ref(process), std::ref(output.Errors), std::cref(onProgressLine));
		}
		catch (const std::exception&)
		{
			process.Terminate();
			process.StopReading();
			JoinIfStarted(outputReader);

			return Error{.Code = ErrorCode::IoFailure, .Message = "cannot start a thread to read git's output"};
		}

		const RunEnd end = WaitForExitOrCancel(process, cancelled);

		if (end == RunEnd::Cancelled)
		{
			process.Terminate();
		}

		// WARNING: A process git left running may hold the pipes open for as long as it runs. Everything git wrote is
		// in them once git has ended, so the readers take what is there and stop instead of waiting on that process.
		process.StopReading();
		outputReader.join();
		errorReader.join();

		if (end == RunEnd::Cancelled)
		{
			return CancelledError();
		}

		const std::optional<std::uint32_t> exitCode = process.GetExitCode();

		if (!exitCode.has_value())
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = "cannot read git's exit code"};
		}

		output.ExitCode = *exitCode;
		return output;
	}

	std::vector<EnvironmentVariable> GitEnvironment(bool sshCommandConfigured)
	{
		std::vector<EnvironmentVariable> environment{
			{.Name = kNoPromptName, .Value = kNoPromptValue},
			{.Name = kLocaleName, .Value = kUntranslatedLocale},
		};

		const bool sshCommandInherited = EnvironmentPath(kSshCommandWideName).has_value();

		if (!sshCommandConfigured && !sshCommandInherited)
		{
			environment.push_back({.Name = kSshCommandName, .Value = kBatchModeSsh});
		}

		return environment;
	}

	std::optional<int> ProgressPercent(std::string_view line)
	{
		const std::size_t percentSign = line.find(kPercentBeforeCount);

		if (percentSign == std::string_view::npos)
		{
			return std::nullopt;
		}

		std::size_t digitsStart = percentSign;

		while (digitsStart > 0 && IsDigit(line[digitsStart - 1]))
		{
			--digitsStart;
		}

		const std::size_t digitCount = percentSign - digitsStart;
		const bool hasPercentDigits = digitCount > 0 && digitCount <= kMostPercentDigits;
		const bool followsTitle = WithoutTrailingPadding(line.substr(0, digitsStart)).ends_with(kTitleEnd);

		if (!hasPercentDigits || !followsTitle)
		{
			return std::nullopt;
		}

		int percent = 0;
		const char* const digitsBegin = line.data() + digitsStart;
		const std::from_chars_result parsed = std::from_chars(digitsBegin, digitsBegin + digitCount, percent);

		if (parsed.ec != std::errc{} || percent > kWholePercent)
		{
			return std::nullopt;
		}

		return percent;
	}
}
