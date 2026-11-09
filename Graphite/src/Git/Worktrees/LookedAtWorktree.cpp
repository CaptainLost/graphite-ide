#include "Git/Worktrees/LookedAtWorktree.h"

#include "Git/Worktrees/WorktreeChanges.h"
#include "Git/Worktrees/WorktreeListing.h"
#include "Git/Worktrees/WorktreesModel.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace Graphite {

	namespace {

		// Another place for the worktree's history: a detached HEAD is placed by its commit, a branch by its name, whose
		// moves the branch snapshot already brings.
		bool PlacedElsewhere(const Core::HeadState& listed, const Core::HeadState& read)
		{
			if (listed.Kind != read.Kind || listed.BranchName != read.BranchName)
			{
				return true;
			}

			return read.Kind == Core::HeadKind::Detached && listed.Commit != read.Commit;
		}
	}

	LookedAtWorktree::LookedAtWorktree(WorktreeChanges& changes, WorktreesModel& counts)
		: m_Changes(changes), m_Counts(counts)
	{
		m_Changes.OnWatchFired = [this](const std::filesystem::path& worktree) { m_Counts.RecountNow(worktree); };
	}

	LookedAtWorktree::~LookedAtWorktree()
	{
		m_Changes.OnWatchFired = nullptr;
	}

	void LookedAtWorktree::Update(Core::Timestep timestep)
	{
		m_Changes.OnUpdate(timestep);
		m_Counts.SetLookedAt(m_Changes.LookedAt());
		ListAgainWhenHeadMoved();
		ListAgainWhenGone();
	}

	void LookedAtWorktree::ListAgainWhenGone()
	{
		const std::optional<Core::Error>& error = m_Changes.Model().GetHeadError();
		const std::optional<std::filesystem::path>& followed = m_Changes.Worktree();
		const bool gone = error.has_value() && error->Code == Core::ErrorCode::NotFound && followed.has_value() &&
						  m_Counts.IndexOf(*followed).has_value();

		// Once as it goes: a list that still holds it is not asked again for every read that fails the same way.
		if (gone && !m_HeadGone)
		{
			m_Counts.ListAgain();
		}

		m_HeadGone = gone;
	}

	void LookedAtWorktree::ListAgainWhenHeadMoved()
	{
		const Core::Ref<const Core::HeadState> head = m_Changes.Model().GetHead();

		if (head == nullptr || head == m_HeadCompared)
		{
			return;
		}

		m_HeadCompared = head;

		// Only the watched worktree: its HEAD is read again whenever its watch fires.
		const std::optional<std::filesystem::path> watched = m_Changes.LookedAt();
		const std::vector<Core::WorktreeEntry>* const entries = m_Counts.Entries();

		if (!watched.has_value() || entries == nullptr || m_Changes.Worktree() != watched)
		{
			return;
		}

		const std::optional<std::size_t> index = m_Counts.IndexOf(*watched);

		if (!index.has_value() || (*entries)[*index].FolderMissing)
		{
			return;
		}

		if (PlacedElsewhere((*entries)[*index].Head, *head))
		{
			m_Counts.ListAgain();
		}
	}
}
