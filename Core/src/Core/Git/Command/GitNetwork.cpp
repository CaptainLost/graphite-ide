#include "corepch.h"

#include "Core/Git/Command/GitNetwork.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Core {

	namespace {

		constexpr std::size_t kShownStderrLines = 6;

		constexpr char kRejectedSentence[] = "The remote has commits that this branch lacks. Pull first, or merge in a terminal.";
		constexpr char kSignInSentence[] =
			"Git could not sign in. Run `git fetch` once in a terminal to enter credentials or accept the host key, or add "
			"your key to ssh-agent.";

		bool Contains(std::string_view text, std::string_view part)
		{
			return text.find(part) != std::string_view::npos;
		}

		// Split on '\r' as well, because git redraws its progress in place; the progress lines and blank ones say nothing
		// about why a command failed.
		std::vector<std::string> MessageLines(std::string_view errors)
		{
			std::vector<std::string> lines;
			std::size_t lineStart = 0;

			while (lineStart <= errors.size())
			{
				const std::size_t lineEnd = errors.find_first_of("\r\n", lineStart);
				const std::size_t end = lineEnd == std::string_view::npos ? errors.size() : lineEnd;
				const std::string_view line = errors.substr(lineStart, end - lineStart);

				if (!line.empty() && !ProgressPercent(line).has_value())
				{
					lines.emplace_back(line);
				}

				lineStart = end + 1;
			}

			return lines;
		}

		const char* SentenceOf(NetworkHint hint)
		{
			switch (hint)
			{
			case NetworkHint::Rejected:
				return kRejectedSentence;
			case NetworkHint::SignIn:
				return kSignInSentence;
			case NetworkHint::None:
				break;
			}

			return "";
		}

		// Reads each progress line on the runner's thread and hands its percent on.
		class PercentRelay
		{
		public:
			explicit PercentRelay(const ProgressSink& progress) : m_Progress(progress) {}

			void OnLine(std::string_view line) const
			{
				if (!m_Progress)
				{
					return;
				}

				if (const std::optional<int> percent = ProgressPercent(line); percent.has_value())
				{
					m_Progress(*percent);
				}
			}

		private:
			const ProgressSink& m_Progress;
		};

		NetworkOutcome RunToSuccess(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
								  std::vector<std::string> arguments)
		{
			const GitRun gitRun{.Arguments = std::move(arguments),
								.WorkingDirectory = run.WorkingDirectory,
								.Environment = GitEnvironment(run.SshCommandConfigured)};

			const PercentRelay relay{progress};
			const std::function<void(std::string_view)> onLine = [&relay](std::string_view line) { relay.OnLine(line); };

			Result<GitOutput> finished = RunGit(run.Git, gitRun, cancelled, onLine);

			if (!finished)
			{
				if (finished.GetError().Code == ErrorCode::Cancelled)
				{
					return NetworkOutcome{.Result = NetworkResult::Cancelled};
				}

				const std::string& startError = finished.GetError().Message;
				return NetworkOutcome{.Result = NetworkResult::Failed,
									  .Failure = {.Shown = startError, .Copied = startError, .Hint = NetworkHint::None}};
			}

			const GitOutput& output = finished.GetValue();

			if (output.ExitCode != 0)
			{
				return NetworkOutcome{.Result = NetworkResult::Failed, .Failure = DescribeFailure(output)};
			}

			return NetworkOutcome{};
		}
	}

	NetworkOutcome FetchAll(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress)
	{
		return RunToSuccess(run, cancelled, progress, {"fetch", "--all", "--prune", "--progress"});
	}

	NetworkOutcome FetchRemote(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
							 const std::string& remote)
	{
		return RunToSuccess(run, cancelled, progress, {"fetch", "--progress", "--", remote});
	}

	NetworkOutcome FetchFullHistory(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress)
	{
		return RunToSuccess(run, cancelled, progress, {"fetch", "--unshallow", "--progress"});
	}

	NetworkOutcome FastForward(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
							 const std::string& upstreamFullName)
	{
		// WARNING: --no-autostash, so a user's merge.autoStash never stashes local changes and lays them back over the merge
		// as a conflict that the window cannot show: the merge refuses instead, changing nothing.
		return RunToSuccess(run, cancelled, progress, {"merge", "--ff-only", "--no-autostash", upstreamFullName});
	}

	NetworkOutcome PushTo(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
						const std::string& remote, const std::optional<std::string>& upstreamBranch)
	{
		// WARNING: --no-follow-tags, so the user's push.followTags never sends the tags on the commits pushed.
		if (upstreamBranch.has_value())
		{
			return RunToSuccess(run, cancelled, progress,
								{"push", "--progress", "--no-follow-tags", "--", remote, "HEAD:" + *upstreamBranch});
		}

		return RunToSuccess(run, cancelled, progress, {"push", "--progress", "--no-follow-tags", "--set-upstream", "--", remote, "HEAD"});
	}

	NetworkOutcome SetRemoteHeadAuto(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
								   const std::string& remote)
	{
		return RunToSuccess(run, cancelled, progress, {"remote", "set-head", "--auto", "--", remote});
	}

	bool IsSshCommandConfigured(const std::filesystem::path& git, const std::filesystem::path& workingDirectory,
								const std::atomic<bool>& cancelled)
	{
		// git config --get exits 1 for a key no config sets.
		constexpr std::uint32_t kNotSet = 1;

		// This run reaches no remote, so whether its own environment would put ssh in batch mode does not matter.
		constexpr bool kSshNotUsed = true;

		const GitRun query{.Arguments = {"config", "--get", "core.sshCommand"},
						   .WorkingDirectory = workingDirectory,
						   .Environment = GitEnvironment(kSshNotUsed)};
		const std::function<void(std::string_view)> noProgress;
		const Result<GitOutput> answered = RunGit(git, query, cancelled, noProgress);

		return !answered.HasValue() || answered.GetValue().ExitCode != kNotSet;
	}

	NetworkHint HintOf(std::string_view errors)
	{
		const bool rejected = Contains(errors, "rejected") && (Contains(errors, "non-fast-forward") || Contains(errors, "fetch first"));

		if (rejected)
		{
			return NetworkHint::Rejected;
		}

		const bool signIn = Contains(errors, "Authentication failed") || Contains(errors, "Permission denied (publickey)") ||
							Contains(errors, "Host key verification failed");

		return signIn ? NetworkHint::SignIn : NetworkHint::None;
	}

	NetworkFailure DescribeFailure(const GitOutput& output)
	{
		NetworkFailure failure;
		failure.Hint = HintOf(output.Errors);
		failure.Copied = output.Errors;
		failure.Shown = SentenceOf(failure.Hint);

		const std::vector<std::string> lines = MessageLines(output.Errors);
		const std::size_t firstShown = lines.size() > kShownStderrLines ? lines.size() - kShownStderrLines : 0;

		for (std::size_t lineIndex = firstShown; lineIndex < lines.size(); ++lineIndex)
		{
			if (!failure.Shown.empty())
			{
				failure.Shown += '\n';
			}

			failure.Shown += lines[lineIndex];
		}

		return failure;
	}

	std::optional<std::chrono::sys_seconds> LastFetchTime(const std::filesystem::path& commonGitDir)
	{
		std::error_code timeError;
		const std::filesystem::file_time_type written = std::filesystem::last_write_time(commonGitDir / "FETCH_HEAD", timeError);

		if (timeError)
		{
			return std::nullopt;
		}

		return std::chrono::floor<std::chrono::seconds>(std::chrono::clock_cast<std::chrono::system_clock>(written));
	}
}
