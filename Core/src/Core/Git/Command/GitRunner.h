#pragma once

#include "Core/Base/Result.h"
#include "Core/Platform/ChildProcess.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	struct GitRun
	{
		std::vector<std::string> Arguments;
		std::filesystem::path WorkingDirectory;
		std::vector<EnvironmentVariable> Environment;
	};

	// Out and Errors hold git's bytes as it wrote them, which are UTF-8.
	struct GitOutput
	{
		std::uint32_t ExitCode = 0;
		std::string Out;
		std::string Errors;
	};

	// Runs git to its end, whatever its exit code. Each stderr line goes to onProgressLine as it arrives, split on '\r'
	// as well as '\n' because git redraws its progress in place. A run that is cancelled ends git and every process it
	// started, and returns ErrorCode::Cancelled; a git that cannot start is ErrorCode::IoFailure.
	// WARNING: onProgressLine is called on a thread of the runner's while git runs, so it must be cheap, safe to call
	// from there, and never throw. git's stdin stays open and is never written, so a command that reads it waits until
	// it is cancelled.
	// WARNING: A cancel ends every process git started, a browser the credential manager opened during that run
	// included. A run that ends on its own ends none of them, and what they write once git has ended is not read.
	Result<GitOutput> RunGit(const std::filesystem::path& git, const GitRun& run, const std::atomic<bool>& cancelled,
							 const std::function<void(std::string_view)>& onProgressLine);

	// Never lets git prompt, keeps its messages in English, and puts ssh in batch mode unless an ssh command is
	// configured. sshCommandConfigured is the repository's core.sshCommand; a GIT_SSH_COMMAND in this process's
	// environment is honoured here as well, since the child inherits it.
	std::vector<EnvironmentVariable> GitEnvironment(bool sshCommandConfigured);

	// The percent of one of git's progress lines, such as "Receiving objects:  45% (9/20)" or
	// "remote: Counting objects: 100% (5/5), done."; empty for every other line.
	std::optional<int> ProgressPercent(std::string_view line);
}
