#include "Git/Network/NetworkText.h"

#include "Core/Base/Count.h"

#include <algorithm>
#include <format>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {

		constexpr const char* kEllipsis = "…";
		constexpr std::size_t kListedLocalChanges = 10;

		// Ends the line above each list of files git refuses to overwrite: tracked changes and untracked files alike.
		constexpr std::string_view kOverwrittenByMerge = "would be overwritten by merge:";

		// WARNING: No default, so an operation added later is a warning here, and warnings fail the build.
		const char* NameOf(NetworkOperation operation)
		{
			switch (operation)
			{
			case NetworkOperation::Fetch:
			case NetworkOperation::FullHistory:
				return "Fetch";
			case NetworkOperation::Pull:
				return "Pull";
			case NetworkOperation::Push:
				return "Push";
			}

			return "";
		}

		// WARNING: No default, so an operation added later is a warning here, and warnings fail the build.
		const char* RunningVerbOf(NetworkOperation operation)
		{
			switch (operation)
			{
			case NetworkOperation::Fetch:
				return "Fetching";
			case NetworkOperation::FullHistory:
				return "Fetching full history";
			case NetworkOperation::Pull:
				return "Pulling";
			case NetworkOperation::Push:
				return "Pushing";
			}

			return "";
		}

		std::string CountedText(const char* verb, const std::optional<std::size_t>& commits)
		{
			if (!commits.has_value())
			{
				return verb;
			}

			return std::string{verb} + ' ' + Core::FormatCount(*commits) + (*commits == 1 ? " commit" : " commits");
		}

		std::string RejectedText(const std::string& remote, const std::string& branch)
		{
			return remote + " has commits that " + branch + " lacks. Pull first, or merge in a terminal.";
		}

		std::string SignInText(const std::string& remote)
		{
			return "Git could not sign in to " + remote +
				   ". Run `git fetch` once in a terminal to enter credentials or accept the host key, or add your key to "
				   "ssh-agent.";
		}

		// WARNING: Core's shown text has its hint's sentence alone on the first line, before git's own lines.
		std::string WithFirstLineReplaced(const std::string& shown, const std::string& sentence)
		{
			const std::size_t firstLineEnd = shown.find('\n');

			if (firstLineEnd == std::string::npos)
			{
				return sentence;
			}

			return sentence + shown.substr(firstLineEnd);
		}

		// Split on '\n', each without the '\r' a Windows line end leaves.
		std::vector<std::string> LinesOf(std::string_view text)
		{
			std::vector<std::string> lines;
			std::size_t lineStart = 0;

			while (lineStart < text.size())
			{
				const std::size_t lineEnd = text.find('\n', lineStart);
				const std::size_t end = lineEnd == std::string_view::npos ? text.size() : lineEnd;
				std::string_view line = text.substr(lineStart, end - lineStart);

				if (line.ends_with('\r'))
				{
					line.remove_suffix(1);
				}

				lines.emplace_back(line);
				lineStart = end + 1;
			}

			return lines;
		}

		// git lists each file it would overwrite on a line of its own, indented by a tab, below the line saying so.
		std::vector<std::string> FilesMergeWouldOverwrite(std::string_view errors)
		{
			std::vector<std::string> files;
			bool inList = false;

			for (const std::string& line : LinesOf(errors))
			{
				if (line.find(kOverwrittenByMerge) != std::string::npos)
				{
					inList = true;
					continue;
				}

				const bool listsFile = inList && line.starts_with('\t');

				if (listsFile)
				{
					files.push_back(line.substr(1));
				}

				inList = listsFile;
			}

			return files;
		}

		std::string LocalChangesText(const std::vector<std::string>& files)
		{
			std::string text = "Commit or stash your changes to these files first:";
			const std::size_t listed = std::min(files.size(), kListedLocalChanges);

			for (std::size_t fileIndex = 0; fileIndex < listed; ++fileIndex)
			{
				text += '\n' + files[fileIndex];
			}

			if (files.size() > listed)
			{
				text += "\nand " + Core::FormatCount(files.size() - listed) + " more";
			}

			return text;
		}
	}

	std::string RunningText(const NetworkState& state)
	{
		if (!state.Running.has_value())
		{
			return {};
		}

		std::string text = std::string{RunningVerbOf(*state.Running)} + kEllipsis;

		if (state.Percent.has_value())
		{
			text += std::format(" {}%", *state.Percent);
		}

		return text;
	}

	std::string DoneText(const NetworkDone& done)
	{
		if (done.Cancelled)
		{
			return std::string{NameOf(done.Operation)} + " cancelled";
		}

		// WARNING: No default, so an operation added later is a warning here, and warnings fail the build.
		switch (done.Operation)
		{
		case NetworkOperation::Fetch:
		case NetworkOperation::FullHistory:
			return "Fetched";
		case NetworkOperation::Pull:
			return done.Commits == std::optional<std::size_t>{0} ? "Already up to date" : CountedText("Pulled", done.Commits);
		case NetworkOperation::Push:
			return done.Commits == std::optional<std::size_t>{0} ? "Nothing to push" : CountedText("Pushed", done.Commits);
		}

		return {};
	}

	std::string FailureTitle(NetworkOperation operation)
	{
		return std::string{NameOf(operation)} + " failed";
	}

	std::string DivergedText(const std::string& branch, const std::string& upstream, std::size_t ahead, std::size_t behind)
	{
		return std::format("{} and {} have diverged ({} to push, {} to pull). Pull can only fast-forward for now; merge or "
						   "rebase in a terminal.",
						   branch, upstream, Core::FormatCount(ahead), Core::FormatCount(behind));
	}

	std::string NoLongerCheckedOutText(const std::string& branch, const std::string& upstream)
	{
		return branch + " is no longer checked out, so Pull did not merge " + upstream + ".";
	}

	std::string FolderChangedText(const std::string& upstream)
	{
		return "The folder changed while Pull ran, so it did not merge " + upstream + ".";
	}

	std::string NoUpstreamText(const std::string& branch)
	{
		return branch + " has no upstream branch.";
	}

	std::string GoneUpstreamText(const std::string& branch, const std::string& upstream)
	{
		return branch + "'s upstream " + upstream + " no longer exists.";
	}

	std::string LocalUpstreamText(const std::string& branch, const std::string& upstream)
	{
		return branch + "'s upstream is the local branch " + upstream + ", so Push needs a remote chosen.";
	}

	std::string NoRemoteText()
	{
		return "This repository has no remote.";
	}

	std::string NoBranchText()
	{
		return "No branch is checked out.";
	}

	std::string PublishText(const std::string& branch, const std::string& remote, const std::optional<std::string>& upstream)
	{
		const std::string published = remote + "/" + branch;
		std::string text = "Push " + branch + " to " + remote + " as " + published;

		if (upstream.has_value() && *upstream != published)
		{
			text += "; it then tracks " + published + " instead of " + *upstream;
		}

		return text;
	}

	std::string FailureMessage(NetworkOperation operation, const Core::NetworkFailure& failure, const NetworkNames& names)
	{
		if (operation == NetworkOperation::Pull)
		{
			const std::vector<std::string> files = FilesMergeWouldOverwrite(failure.Copied);

			if (!files.empty())
			{
				return LocalChangesText(files);
			}
		}

		const bool namesRejection = failure.Hint == Core::NetworkHint::Rejected && names.Remote.has_value() && names.Branch.has_value();

		if (namesRejection)
		{
			return WithFirstLineReplaced(failure.Shown, RejectedText(*names.Remote, *names.Branch));
		}

		const bool namesSignIn = failure.Hint == Core::NetworkHint::SignIn && names.Remote.has_value();

		if (namesSignIn)
		{
			return WithFirstLineReplaced(failure.Shown, SignInText(*names.Remote));
		}

		return failure.Shown;
	}

	NetworkActivity DescribeNetworkActivity(const NetworkState& state, const std::optional<NetworkDone>& shown)
	{
		NetworkActivity activity;

		if (state.Running.has_value())
		{
			activity.Running = RunningText(state);
		}
		else if (shown.has_value())
		{
			activity.Done = DoneText(*shown);
		}

		return activity;
	}
}
