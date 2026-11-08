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

	// The git to run and the repository to run it in. SshCommandConfigured is IsSshCommandConfigured's answer for it.
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

	enum class NetworkResult
	{
		Done,
		Cancelled,
		Failed
	};

	// Failure is set only when Result is Failed.
	struct NetworkOutcome
	{
		NetworkResult Result = NetworkResult::Done;
		NetworkFailure Failure;
	};

	// Called with git's own percent, on a thread of the runner's, so it must be cheap and never throw. May be empty.
	using ProgressSink = std::function<void(int percent)>;

	// Each runs one git command to its end and is Done on exit code 0. A run cancelled before git exits is Cancelled, never
	// a failure to report. A git that cannot start is a failure whose Shown and Copied are the start error.
	NetworkOutcome FetchAll(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress);
	NetworkOutcome FetchRemote(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
							 const std::string& remote);
	NetworkOutcome FetchFullHistory(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress);

	// Never git pull: the user's pull.rebase and pull.ff would decide what it did. Refuses, changing nothing, when the
	// branch cannot be moved by a fast-forward or the move would overwrite local changes, whatever merge.autoStash says.
	NetworkOutcome FastForward(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
							 const std::string& upstreamFullName);

	// Never forces and never sends tags, whatever push.followTags says. An empty upstreamBranch pushes HEAD with
	// --set-upstream.
	NetworkOutcome PushTo(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
						const std::string& remote, const std::optional<std::string>& upstreamBranch);

	// Records which branch the remote calls its default, as refs/remotes/<remote>/HEAD.
	NetworkOutcome SetRemoteHeadAuto(const NetworkRun& run, const std::atomic<bool>& cancelled, const ProgressSink& progress,
								   const std::string& remote);

	// Whether git, run in `workingDirectory`, reads a core.sshCommand, asked of git itself: one set through an include that
	// libgit2 cannot follow, such as includeIf "hasconfig:remote.*.url:…", counts. Set even when empty. Any answer but
	// git's "not set" counts as set, a run cancelled or that cannot start included, so a batch-mode ssh of ours never
	// replaces one the user may have.
	bool IsSshCommandConfigured(const std::filesystem::path& git, const std::filesystem::path& workingDirectory,
								const std::atomic<bool>& cancelled);

	NetworkHint HintOf(std::string_view errors);

	// The hint sentence names no branch or remote, since git's output does not say which is meant.
	NetworkFailure DescribeFailure(const GitOutput& output);

	// The mtime of FETCH_HEAD in the common git dir (a linked worktree's own git dir has none); empty when never fetched.
	std::optional<std::chrono::sys_seconds> LastFetchTime(const std::filesystem::path& commonGitDir);
}
