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

		// git's last lines, under the sentence: the ones that say why, after its hints and advice.
		constexpr std::size_t kShownGitLines = 6;

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

		// The remote and the branch are named when they are known; git's output does not say which is meant.
		std::string RejectedText(const NetworkNames& names)
		{
			if (names.Remote.has_value() && names.Branch.has_value())
			{
				return *names.Remote + " has commits that " + *names.Branch + " lacks. Pull first, merge in a terminal, or replace them with Git: Force Push.";
			}

			return "The remote has commits that this branch lacks. Pull first, merge in a terminal, or replace them with Git: Force Push.";
		}

		std::string SignInText(const NetworkNames& names)
		{
			const std::string signIn = names.Remote.has_value() ? "Git could not sign in to " + *names.Remote : "Git could not sign in";

			return signIn + ". Run `git fetch` once in a terminal to enter credentials or accept the host key, or add your key to "
							"ssh-agent.";
		}

		// WARNING: No default, so a hint added later is a warning here, and warnings fail the build.
		std::string SentenceOf(Core::NetworkHint hint, const NetworkNames& names)
		{
			switch (hint)
			{
			case Core::NetworkHint::Rejected:
				return RejectedText(names);
			case Core::NetworkHint::SignIn:
				return SignInText(names);
			case Core::NetworkHint::None:
				break;
			}

			return {};
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
		if (operation == NetworkOperation::Pull && !failure.WouldOverwrite.empty())
		{
			return LocalChangesText(failure.WouldOverwrite);
		}

		std::string message = SentenceOf(failure.Hint, names);
		const std::size_t firstShown = failure.Lines.size() > kShownGitLines ? failure.Lines.size() - kShownGitLines : 0;

		for (std::size_t lineIndex = firstShown; lineIndex < failure.Lines.size(); ++lineIndex)
		{
			if (!message.empty())
			{
				message += '\n';
			}

			message += failure.Lines[lineIndex];
		}

		return message;
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
