#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Command/GitRunner.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	// The git to run and the repository to run it in. SshCommandConfigured is the repository's core.sshCommand.
	struct NetworkRun
	{
		std::filesystem::path Git;
		std::filesystem::path WorkingDirectory;
		bool SshCommandConfigured = false;
	};

	enum class NetworkHint
	{
		None,
		Rejected,
		SignIn
	};

	// Shown is the hint sentence, when there is one, then git's last six lines; Copied is the whole of git's stderr.
	struct NetworkFailure
	{
		std::string Shown;
		std::string Copied;
		NetworkHint Hint = NetworkHint::None;
	};

	// Called with git's own percent, on a thread of the runner's, so it must be cheap and never throw. May be empty.
	using ProgressSink = std::function<void(int percent)>;

	// Each runs one git command to its end and succeeds on exit code 0. A cancelled run is ErrorCode::Cancelled and never
	// a failure to report; any other failure is ErrorCode::IoFailure with NetworkError's message.
	Result<void> FetchAll(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress);
	Result<void> FetchRemote(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
							 const std::string& remote);
	Result<void> FetchFullHistory(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress);

	// Never git pull: the user's pull.rebase and pull.ff would decide what it did. Refuses, changing nothing, when the
	// branch cannot be moved by a fast-forward or the move would overwrite local changes.
	Result<void> FastForward(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
							 const std::string& upstreamFullName);

	// Never forces and never sends tags. An empty upstreamBranch pushes HEAD with --set-upstream.
	Result<void> PushTo(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
						const std::string& remote, const std::optional<std::string>& upstreamBranch);

	// Records which branch the remote calls its default, as refs/remotes/<remote>/HEAD.
	Result<void> SetRemoteHeadAuto(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
								   const std::string& remote);

	NetworkHint HintOf(std::string_view errors);

	// The hint sentence names no branch or remote, since git's output does not say which is meant.
	NetworkFailure DescribeFailure(const GitOutput& output);

	// DescribeFailure(output).Shown as an IoFailure. The copy text does not fit in an Error; a caller that needs it
	// calls DescribeFailure itself.
	Error NetworkError(const GitOutput& output);

	// The mtime of FETCH_HEAD in the common git dir (a linked worktree's own git dir has none); empty when never fetched.
	std::optional<std::chrono::sys_seconds> LastFetchTime(const std::filesystem::path& commonGitDir);
}
