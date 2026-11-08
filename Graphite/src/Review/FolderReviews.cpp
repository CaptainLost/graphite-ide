#include "Review/FolderReviews.h"

#include "Git/GitModel.h"
#include "Git/Lines/LinesModel.h"
#include "Review/ReviewBase.h"
#include "Review/ReviewModel.h"

#include <optional>
#include <utility>

namespace Graphite {

	FolderReviews::FolderReviews(GitModel& git, LinesModel& lines, ReviewModel& model, ModalGate& modals)
		: m_Git(git),
		  m_Lines(lines),
		  m_Model(model),
		  m_Windows(modals)
	{
	}

	void FolderReviews::Update(Core::Timestep timestep)
	{
		m_ReadLines.Follow(m_Lines.GetRead());
		FollowBranches();

		m_Model.OnUpdate(timestep);
		m_Store.Update(timestep);
	}

	void FolderReviews::FollowBranches()
	{
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Git.GetBranches();

		// WARNING: GitModel replaces its snapshot only with a list it read whole, so a snapshot that lacks a branch means
		// the branch is gone, never that the read failed.
		if (branches == nullptr || branches == m_FollowedBranches)
		{
			return;
		}

		m_FollowedBranches = branches;

		for (const BranchRename& rename : m_Windows.FollowBranches(*branches, m_Model))
		{
			m_Store.GetSessions().Rename(rename.From, rename.To);
		}

		m_Store.GetSessions().Prune(*branches);
		m_Model.InvalidateRanges();
	}

	ReviewMenu FolderReviews::MenuOf(const Core::Branch& branch) const
	{
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Git.GetBranches();

		if (branches == nullptr)
		{
			return ReviewMenu{};
		}

		return ReviewMenuFor(branch, *branches, m_Store.GetSessions(), m_ReadLines.GetLines());
	}

	void FolderReviews::Apply(const ReviewMenuItem& item)
	{
		switch (item.Action)
		{
		case ReviewAction::Start:
			m_Store.GetSessions().Start(item.Branch, item.Base);
			Open(item.Branch);
			break;
		case ReviewAction::Open:
			Open(item.Branch);
			break;
		case ReviewAction::End:
			m_Windows.AskToEnd(item.Branch);
			break;
		}
	}

	bool FolderReviews::OpenDraft(const std::string& branch)
	{
		ReviewSessions& sessions = m_Store.GetSessions();

		if (sessions.Find(branch) != nullptr)
		{
			return true;
		}

		const Core::Ref<const std::vector<Core::Branch>> branches = m_Git.GetBranches();

		if (branches == nullptr)
		{
			return false;
		}

		const std::optional<std::string> base = ReviewBaseOf(branch, *branches, sessions, m_ReadLines.GetLines());

		if (!base.has_value())
		{
			return false;
		}

		sessions.OpenDraft(branch, *base);
		return true;
	}

	void FolderReviews::Open(const std::string& branch)
	{
		if (!OpenDraft(branch))
		{
			return;
		}

		m_Store.GetSessions().Keep(branch);
		m_Windows.Open(branch);
	}

	void FolderReviews::NoteRename(std::string from, std::string to)
	{
		m_Windows.NoteRename(std::move(from), std::move(to));
	}

	void FolderReviews::Render(const ReviewLook& look)
	{
		m_Windows.Render(ReviewRenderInput{.Model = m_Model,
										   .Sessions = m_Store.GetSessions(),
										   .Danger = look.Danger,
										   .Palette = look.Palette,
										   .Syntax = look.Syntax,
										   .Selection = look.Selection,
										   .DiffOptions = look.DiffOptions,
										   .CentreNode = look.CentreNode,
										   .CloseRequests = look.CloseRequests});
	}

	void FolderReviews::LetGo()
	{
		m_Windows.CloseAll(m_Model);
		m_Store.Close();
		m_FollowedBranches.reset();
	}

	void FolderReviews::Arrive(const std::filesystem::path& root, const std::vector<ReviewTabState>& restorable)
	{
		m_Store.Open(root);
		m_Windows.RestoreState(restorable, m_Store.GetSessions());
	}

	void FolderReviews::FollowMove(const std::filesystem::path& to)
	{
		m_Store.MoveTo(to);
	}

	std::vector<ReviewTabState> FolderReviews::Capture() const
	{
		return m_Windows.CaptureState();
	}

	void FolderReviews::Flush()
	{
		m_Store.Flush();
	}
}
