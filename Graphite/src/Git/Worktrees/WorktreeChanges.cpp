#include "Git/Worktrees/WorktreeChanges.h"

#include "Core/FileSystem/WslSharePath.h"
#include "Core/Git/Head.h"
#include "Core/Git/Status.h"

#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		bool SameWorktree(const std::optional<std::filesystem::path>& left, const std::optional<std::filesystem::path>& right)
		{
			if (!left.has_value() || !right.has_value())
			{
				return left.has_value() == right.has_value();
			}

			return Core::SamePath(*left, *right);
		}
	}

	WorktreeChanges::WorktreeChanges(GitPort& port, std::function<void(const std::filesystem::path&)> setPortRoot,
									 Core::JobSubmitter read, Core::JobSubmitter write, WorktreeWatchFactory watch)
		: m_SetPortRoot(std::move(setPortRoot)), m_StartWatch(std::move(watch)), m_Model(port, std::move(read), std::move(write))
	{
	}

	void WorktreeChanges::Follow(std::optional<std::filesystem::path> worktree, bool onScreen)
	{
		m_Wanted = std::move(worktree);
		m_OnScreen = onScreen;
		Settle();
	}

	void WorktreeChanges::OnUpdate(Core::Timestep timestep)
	{
		// Both are taken every frame, so neither holds a change back for the next.
		const bool folderChanged = m_FolderWatch != nullptr && m_FolderWatch->TakeChanged();
		const bool gitDirectoryChanged = m_GitDirectoryWatch != nullptr && m_GitDirectoryWatch->TakeChanged();

		if (folderChanged || gitDirectoryChanged)
		{
			m_Debounce.Mark();
		}

		if (m_Debounce.Advance(timestep))
		{
			m_Model.RefreshWorkingTree();

			if (OnWatchFired && m_Worktree.has_value())
			{
				OnWatchFired(*m_Worktree);
			}
		}

		m_Model.OnUpdate(timestep);

		// Before Settle, whose switch to another worktree clears the model's own; until it, the worktree followed is the
		// one the write ran in.
		while (std::optional<GitModel::WriteFailure> failure = m_Model.TakeWriteError())
		{
			m_WriteErrors.push_back(
				WorktreeWriteFailure{.Failure = std::move(*failure), .Worktree = m_Worktree.value_or(std::filesystem::path{})});
		}

		// Before Settle: a commit that lands is the followed worktree's, which no other is followed before it lands.
		if (m_Model.TakeCommitted().has_value())
		{
			ChangesShared& draft = Draft();
			draft.Message.clear();
			draft.Amend = false;
			m_RefMoved = true;
		}

		Settle();
	}

	std::optional<WorktreeWriteFailure> WorktreeChanges::TakeWriteError()
	{
		if (m_WriteErrors.empty())
		{
			return std::nullopt;
		}

		WorktreeWriteFailure taken = std::move(m_WriteErrors.front());
		m_WriteErrors.pop_front();
		return taken;
	}

	bool WorktreeChanges::TakeRefMoved()
	{
		return std::exchange(m_RefMoved, false);
	}

	std::optional<std::filesystem::path> WorktreeChanges::LookedAt() const
	{
		if (!IsWatching())
		{
			return std::nullopt;
		}

		return m_Worktree;
	}

	ChangesShared& WorktreeChanges::Draft()
	{
		if (!m_Worktree.has_value())
		{
			return m_NoDraft;
		}

		return m_Drafts[Core::PathKey(*m_Worktree)];
	}

	WriteExpectation WorktreeChanges::ExpectationNow(bool forCommit) const
	{
		WriteExpectation expectation;

		if (const Core::Ref<const Core::HeadState> head = m_Model.GetHead())
		{
			expectation.Kind = head->Kind;
			expectation.BranchName = head->BranchName;
			expectation.Head = head->Commit;
		}

		// WARNING: With no status read yet the user has seen nothing staged, so a commit expects nothing staged and is
		// refused should the worktree hold staged files after all.
		if (forCommit)
		{
			const Core::Ref<const Core::WorkingTreeStatus> status = m_Model.GetStatus();
			expectation.Staged = status ? StagedPaths(*status) : std::vector<std::filesystem::path>{};
		}

		if (m_Worktree.has_value())
		{
			expectation.Folder = Core::ToUtf8(Core::NormalizeDirectory(*m_Worktree).filename());
			expectation.Worktree = *m_Worktree;
		}

		return expectation;
	}

	void WorktreeChanges::Settle()
	{
		// WARNING: The port is shared by every job of the model, so re-rooting it under a write still queued or running would
		// run that write in the worktree followed next. The writes finish first; OnUpdate settles again once they have.
		if (!SameWorktree(m_Wanted, m_Worktree) && !m_Model.HasWritesPending())
		{
			Reroot();
		}

		Watch(m_OnScreen && m_Worktree.has_value());
	}

	void WorktreeChanges::Reroot()
	{
		// The watches name the worktree before; they go first, so the next ones start on the new root.
		Watch(false);

		m_Worktree = m_Wanted;

		const std::filesystem::path root = m_Worktree.value_or(std::filesystem::path{});
		m_Model.Clear();
		m_Model.SetRoot(root);
		m_SetPortRoot(root);
	}

	void WorktreeChanges::Watch(bool lookedAt)
	{
		if (lookedAt == m_LookedAt)
		{
			return;
		}

		m_LookedAt = lookedAt;
		m_FolderWatch.reset();
		m_GitDirectoryWatch.reset();
		m_Debounce.Reset();

		if (!lookedAt)
		{
			return;
		}

		m_FolderWatch = m_StartWatch(*m_Worktree);

		if (const std::optional<std::filesystem::path> gitDirectory = LinkedGitDirectoryOf(*m_Worktree))
		{
			m_GitDirectoryWatch = m_StartWatch(*gitDirectory);
		}

		// Whatever changed while nobody watched is read now, the watch failing to start or not.
		m_Model.RefreshWorkingTree();
	}

	std::string WorktreeCommitLabel(const Core::HeadState* head, std::string_view folder)
	{
		if (head == nullptr)
		{
			return "Commit";
		}

		if (head->Kind == Core::HeadKind::Detached)
		{
			return "Commit on detached HEAD";
		}

		return std::format("Commit to {} in {}", head->BranchName, folder);
	}

	std::string WorktreeMessageHint(const Core::HeadState* head, std::string_view folder)
	{
		if (head == nullptr)
		{
			return "Message";
		}

		const std::string target = head->Kind == Core::HeadKind::Detached ? std::string{"detached HEAD"} : head->BranchName;
		return std::format("Message for {} in {}", target, folder);
	}

	std::optional<std::filesystem::path> LinkedGitDirectoryOf(const std::filesystem::path& worktree)
	{
		const std::filesystem::path gitFile = worktree / ".git";
		std::error_code error;

		if (!std::filesystem::is_regular_file(gitFile, error))
		{
			return std::nullopt;
		}

		std::ifstream file{gitFile, std::ios::binary};
		std::string line;

		if (!std::getline(file, line))
		{
			return std::nullopt;
		}

		constexpr std::string_view kPrefix = "gitdir: ";

		if (!line.starts_with(kPrefix))
		{
			return std::nullopt;
		}

		std::string_view named = std::string_view{line}.substr(kPrefix.size());

		while (!named.empty() && (named.back() == '\r' || named.back() == ' ' || named.back() == '\t'))
		{
			named.remove_suffix(1);
		}

		return GitDirectoryNamedIn(worktree, named);
	}

	std::optional<std::filesystem::path> GitDirectoryNamedIn(const std::filesystem::path& worktree, std::string_view named)
	{
		if (named.empty())
		{
			return std::nullopt;
		}

		// A worktree inside WSL names its git directory as Linux sees it.
		if (const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(worktree); place && named.front() == '/')
		{
			return Core::ToWslSharePath(Core::WslSharePath{place->Distro, std::string{named}});
		}

		const std::filesystem::path gitDirectory = Core::FromUtf8(named);

		// git writes a relative path when the worktree was added with worktree.useRelativePaths.
		if (gitDirectory.is_relative())
		{
			return (worktree / gitDirectory).lexically_normal();
		}

		return gitDirectory.lexically_normal();
	}
}
