#include "Git/GitActivity.h"

#include "Core/Base/Count.h"

#include "Git/CheckoutQuestion.h"

#include <format>
#include <string>

namespace Graphite {

	namespace {

		constexpr const char* kEllipsis = "…";

		std::string PathsText(const GitModel::WriteSubject& subject)
		{
			const bool namesOnePath = subject.PathCount == 1 && !subject.OnePathName.empty();

			if (namesOnePath)
			{
				return subject.OnePathName;
			}

			return Core::FormatCount(subject.PathCount) + (subject.PathCount == 1 ? " file" : " files");
		}

		std::string StashText(const GitModel::WriteSubject& subject)
		{
			if (!subject.StashIndex.has_value())
			{
				return "the stash";
			}

			return std::format("stash@{{{}}}", *subject.StashIndex);
		}

		std::string CommittedText(const GitModel::WriteSubject& subject)
		{
			std::string text = "Committed";

			if (subject.Committed.has_value())
			{
				text += ' ' + subject.Committed->ShortHex();
			}

			if (!subject.CommitSummary.empty())
			{
				text += ' ' + subject.CommitSummary;
			}

			return text;
		}

		std::optional<std::string> AwaitingAnswerOf(const GitPanelCheckouts& checkouts)
		{
			if (checkouts.HeldForPanel.has_value())
			{
				return "Checkout of " + CheckoutTargetLabel(*checkouts.HeldForPanel);
			}

			if (checkouts.DeleteHeldForPanel.has_value())
			{
				return "Deleting branch " + *checkouts.DeleteHeldForPanel;
			}

			return std::nullopt;
		}

		std::optional<Core::CheckoutTarget> CheckoutStartedInPanel(const GitPanelCheckouts& checkouts)
		{
			if (checkouts.Asking.has_value())
			{
				return checkouts.Asking;
			}

			return checkouts.Planning;
		}
	}

	std::string WriteRunningText(const GitModel::WriteSubject& subject)
	{
		// WARNING: No default, so a kind added later is a warning here, and warnings fail the build.
		switch (subject.Kind)
		{
		case GitModel::WriteKind::Stage:
			return "Staging " + PathsText(subject) + kEllipsis;
		case GitModel::WriteKind::Unstage:
			return "Unstaging " + PathsText(subject) + kEllipsis;
		case GitModel::WriteKind::Discard:
			return "Discarding changes in " + PathsText(subject) + kEllipsis;
		case GitModel::WriteKind::Commit:
			return std::string{"Committing"} + kEllipsis;
		case GitModel::WriteKind::Init:
			return std::string{"Initialising the repository"} + kEllipsis;
		case GitModel::WriteKind::CreateBranch:
			return "Creating branch " + subject.Name + kEllipsis;
		case GitModel::WriteKind::RenameBranch:
			return "Renaming branch " + subject.Name + " to " + subject.NewName + kEllipsis;
		case GitModel::WriteKind::DeleteBranch:
			return "Deleting branch " + subject.Name + kEllipsis;
		case GitModel::WriteKind::StashSave:
			return std::string{"Stashing"} + kEllipsis;
		case GitModel::WriteKind::StashApply:
			return "Applying " + StashText(subject) + kEllipsis;
		case GitModel::WriteKind::StashPop:
			return "Popping " + StashText(subject) + kEllipsis;
		case GitModel::WriteKind::StashDrop:
			return "Dropping " + StashText(subject) + kEllipsis;
		case GitModel::WriteKind::Checkout:
		case GitModel::WriteKind::CheckoutPlan:
			return CheckingOutText(subject.Name);
		case GitModel::WriteKind::DeleteCheck:
			return std::string{"Counting commits not merged anywhere else"} + kEllipsis;
		}

		return {};
	}

	std::string WriteDoneText(const GitModel::WriteSubject& subject)
	{
		// WARNING: No default, so a kind added later is a warning here, and warnings fail the build.
		switch (subject.Kind)
		{
		case GitModel::WriteKind::Stage:
			return "Staged " + PathsText(subject);
		case GitModel::WriteKind::Unstage:
			return "Unstaged " + PathsText(subject);
		case GitModel::WriteKind::Discard:
			return "Discarded changes in " + PathsText(subject);
		case GitModel::WriteKind::Commit:
			return CommittedText(subject);
		case GitModel::WriteKind::Init:
			return "Initialised the repository";
		case GitModel::WriteKind::CreateBranch:
			return "Created branch " + subject.Name;
		case GitModel::WriteKind::RenameBranch:
			return "Renamed branch " + subject.Name + " to " + subject.NewName;
		case GitModel::WriteKind::DeleteBranch:
			return "Deleted branch " + subject.Name;
		case GitModel::WriteKind::StashSave:
			return "Stashed changes";
		case GitModel::WriteKind::StashApply:
			return "Applied " + StashText(subject);
		case GitModel::WriteKind::StashPop:
			return "Popped " + StashText(subject);
		case GitModel::WriteKind::StashDrop:
			return "Dropped " + StashText(subject);
		case GitModel::WriteKind::Checkout:
			return "Checked out " + subject.Name;
		case GitModel::WriteKind::CheckoutPlan:
		case GitModel::WriteKind::DeleteCheck:
			break;
		}

		return {};
	}

	GitPanelWait DescribePanelWait(const GitPanelCheckouts& checkouts)
	{
		GitPanelWait wait;
		wait.AwaitingAnswer = AwaitingAnswerOf(checkouts);

		if (checkouts.Writing.has_value())
		{
			wait.CheckingOut = CheckoutTargetLabel(*checkouts.Writing);
			return wait;
		}

		const std::optional<Core::CheckoutTarget> started = CheckoutStartedInPanel(checkouts);
		const bool waitsForPanel = checkouts.HeldForPanel.has_value();

		if (started.has_value() && !waitsForPanel)
		{
			wait.CheckingOut = CheckoutTargetLabel(*started);
		}

		return wait;
	}

	std::optional<Core::CheckoutTarget> CheckoutMarkedOnRows(const GitPanelCheckouts& checkouts)
	{
		if (checkouts.Writing.has_value())
		{
			return checkouts.Writing;
		}

		if (const std::optional<Core::CheckoutTarget> started = CheckoutStartedInPanel(checkouts); started.has_value())
		{
			return started;
		}

		return checkouts.HeldForPanel;
	}

	bool IsCheckoutTargetOf(const Core::CheckoutTarget& target, const Core::Branch& branch)
	{
		switch (target.Kind)
		{
		case Core::CheckoutKind::LocalBranch:
			return branch.Kind == Core::RefKind::Local && branch.Name == target.Name;
		case Core::CheckoutKind::RemoteBranch:
			return branch.Kind == Core::RefKind::Remote && branch.Name == target.Name;
		case Core::CheckoutKind::Commit:
			break;
		}

		return false;
	}

	std::optional<GitActivity> DescribeGitActivity(const std::optional<GitModel::WriteReport>& report,
												   const std::optional<std::string>& awaitingAnswer)
	{
		if (awaitingAnswer.has_value())
		{
			return GitActivity{.Text = *awaitingAnswer + " is waiting for your answer in Git",
							   .Tooltip = "Click to answer it in the Git panel.",
							   .OpensGit = true};
		}

		if (!report.has_value())
		{
			return std::nullopt;
		}

		if (report->Progress == GitModel::WriteProgress::Done)
		{
			return GitActivity{.Text = WriteDoneText(report->Subject)};
		}

		const bool namedInPlaceOfHead = report->Subject.Kind == GitModel::WriteKind::Checkout;

		if (namedInPlaceOfHead)
		{
			return std::nullopt;
		}

		return GitActivity{.Text = WriteRunningText(report->Subject), .Busy = true};
	}
}
