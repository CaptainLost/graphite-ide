#include "Git/Diff/DiffWindows.h"

#include "Core/Base/Base.h"

#include "Git/History/CommitLabels.h"
#include "UI/FileIcons.h"
#include "UI/FileTabLook.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContentWindowColours.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/TabContextMenu.h"

#include <imgui.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {

	// WARNING: These words are part of the window identity that imgui.ini keys a dock position
	// by, so changing one moves every open tab of that kind back to the default.
	const char* NameOfTarget(Core::DiffTarget target)
	{
		switch (target)
		{
		case Core::DiffTarget::WorkTreeToIndex:
			return "unstaged";
		case Core::DiffTarget::IndexToHead:
			return "staged";
		case Core::DiffTarget::WorkTreeToHead:
			return "working";
		case Core::DiffTarget::CommitToParent:
			return "commit";
		case Core::DiffTarget::StashToParent:
			return "stash";
		case Core::DiffTarget::RevisionToRevision:
			return "range";
		}

		return "diff";
	}

	std::optional<Core::DiffTarget> OtherSideOfStagedPair(Core::DiffTarget target)
	{
		if (target == Core::DiffTarget::WorkTreeToIndex)
		{
			return Core::DiffTarget::IndexToHead;
		}

		if (target == Core::DiffTarget::IndexToHead)
		{
			return Core::DiffTarget::WorkTreeToIndex;
		}

		return std::nullopt;
	}

	bool SideHasChange(const Core::FileStatus& file, Core::DiffTarget target)
	{
		const Core::StatusFlags flags =
			target == Core::DiffTarget::IndexToHead ? file.Index : file.WorkTree;

		return flags != Core::StatusFlags::None;
	}

	// WARNING: This is not NameOfTarget, whose words are fixed by the identity they also spell
	// and read wrongly in a sentence ("no working changes").
	const char* WordForEmptySide(Core::DiffTarget target)
	{
		switch (target)
		{
		case Core::DiffTarget::WorkTreeToIndex:
			return "unstaged ";
		case Core::DiffTarget::IndexToHead:
			return "staged ";
		default:
			return "";
		}
	}

	const Core::FileStatus* FindFileStatus(const Core::WorkingTreeStatus& status,
										   const std::filesystem::path& path)
	{
		for (const Core::FileStatus& file : status.Entries)
		{
			if (file.Path == path)
			{
				return &file;
			}
		}

		return nullptr;
	}

	bool IsWorkingTreeDiff(const Graphite::OpenDiff& diff)
	{
		return !diff.Commit.has_value();
	}
}

namespace Graphite {

	std::string DiffWindowIdentity(const OpenDiff& diff)
	{
		if (diff.Commit.has_value())
		{
			// WARNING: A commit tab's identity is the full hex id followed by the path, with no
			// separator, exactly as it always was: imgui.ini keys an already-docked window by it.
			// The id is always forty characters, so the concatenation cannot be split two ways.
			// Two paths that both contain "###" and agree after the last one still collide, as
			// they do in DocumentWindows, because ImGui restarts its id hash at every "###".
			return diff.Commit->ToHex() + Core::ToUtf8(diff.Path);
		}

		return std::string{"worktree##"} + NameOfTarget(diff.Target) + "##" +
			   Core::ToUtf8(diff.Path);
	}

	std::string DiffWindowTitle(const OpenDiff& diff)
	{
		const std::string fileName = Core::ToUtf8(diff.Path.filename());

		if (!diff.Commit.has_value())
		{
			return fileName + " (" + NameOfTarget(diff.Target) + ")";
		}

		return fileName + " (" + diff.Commit->ShortHex() + ")";
	}

	std::string DiffWindowName(const OpenDiff& diff)
	{
		return std::string{GlyphOf(KindOfFile(diff.Path))} + "  " +
			   DiffWindowTitle(diff) + "###" + DiffWindowIdentity(diff);
	}

	std::optional<Core::DiffTarget> TargetHolding(const Core::WorkingTreeStatus& status,
												  const std::filesystem::path& path,
												  Core::DiffTarget current)
	{
		const std::optional<Core::DiffTarget> other = OtherSideOfStagedPair(current);

		if (!other.has_value())
		{
			return std::nullopt;
		}

		const Core::FileStatus* entry = FindFileStatus(status, path);
		const bool fileIsGoneFromStatus = entry == nullptr;

		if (fileIsGoneFromStatus)
		{
			return std::nullopt;
		}

		if (entry->IsConflicted)
		{
			return std::nullopt;
		}

		const bool currentSideStillHoldsChange = SideHasChange(*entry, current);

		if (currentSideStillHoldsChange)
		{
			return std::nullopt;
		}

		if (!SideHasChange(*entry, *other))
		{
			return std::nullopt;
		}

		return other;
	}

	std::string EmptyDiffMessage(const OpenDiff& diff)
	{
		if (diff.Commit.has_value())
		{
			return "That file is not in this diff.";
		}

		return Core::ToUtf8(diff.Path.filename()) + " has no " + WordForEmptySide(diff.Target) +
			   "changes.";
	}

	void DiffWindows::Open(const OpenDiff& diff)
	{
		for (Entry& entry : m_Open)
		{
			if (entry.Diff == diff)
			{
				entry.Window.Focus = true;
				return;
			}
		}

		m_Open.push_back(Entry{.Diff = diff, .Window = {.Fresh = true, .Focus = true}});
	}

	bool DiffWindows::IsOpen(const OpenDiff& diff) const
	{
		for (const Entry& entry : m_Open)
		{
			if (entry.Diff == diff)
			{
				return true;
			}
		}

		return false;
	}

	void DiffWindows::CloseAll()
	{
		m_Open.clear();
	}

	void DiffWindows::CloseWorkingTree()
	{
		std::erase_if(m_Open, [](const Entry& entry) { return IsWorkingTreeDiff(entry.Diff); });
	}

	std::vector<DiffTabState> DiffWindows::CaptureState() const
	{
		std::vector<DiffTabState> diffs;
		diffs.reserve(m_Open.size());

		for (const Entry& entry : m_Open)
		{
			diffs.push_back(DiffTabState{entry.Diff.Commit, entry.Diff.Target, entry.Diff.Path});
		}

		return diffs;
	}

	void DiffWindows::RestoreState(const std::vector<DiffTabState>& diffs)
	{
		bool workingTreeOpen = false;

		for (const Entry& entry : m_Open)
		{
			if (IsWorkingTreeDiff(entry.Diff))
			{
				workingTreeOpen = true;
				break;
			}
		}

		for (const DiffTabState& stored : diffs)
		{
			const OpenDiff diff{.Commit = stored.Commit, .Target = stored.Target, .Path = stored.Path};

			if (IsOpen(diff))
			{
				continue;
			}

			if (IsWorkingTreeDiff(diff))
			{
				if (workingTreeOpen)
				{
					continue;
				}

				workingTreeOpen = true;
			}

			Entry entry{diff, DiffView{}};
			entry.Window.Restored = true;
			entry.AwaitingFirstAnswer = diff.Commit.has_value();
			m_Open.push_back(std::move(entry));
		}
	}

	std::function<void(const std::filesystem::path&)> DiffWindows::OpenFileIn(
		const GitModel& model) const
	{
		if (!OpenFile)
		{
			return nullptr;
		}

		// WARNING: Paths are joined onto the working directory and not the workspace root:
		// Repository::Open searches upward for a .git, so only the former is what a diff's
		// paths are relative to.
		return [this, &model](const std::filesystem::path& relative) {
			OpenFile(model.GetWorkingDirectory() / relative);
		};
	}

	void DiffWindows::FollowFile(Entry& entry, const GitModel& model)
	{
		if (entry.Diff.Commit.has_value())
		{
			return;
		}

		const Core::Ref<const Core::WorkingTreeStatus> status = model.GetStatus();

		if (!status)
		{
			return;
		}

		const std::optional<Core::DiffTarget> target =
			TargetHolding(*status, entry.Diff.Path, entry.Diff.Target);

		if (!target.has_value())
		{
			return;
		}

		entry.Diff.Target = *target;

		// WARNING: A new target is a new identity ImGui has no dock position for, so the tab is
		// docked again and raised, or it would float loose and lose the reader's place.
		entry.Window.Fresh = true;
		entry.Window.Focus = true;
	}

	void DiffWindows::Render(GitModel& model, const GitPalette& palette, const SyntaxPalette& syntax,
							 ImU32 selection, const DiffViewOptions& options, unsigned int centreNode,
							 Core::TabCloseRequests& closeRequests)
	{
		for (std::size_t tabIndex = 0; tabIndex < m_Open.size();)
		{
			Entry& entry = m_Open[tabIndex];

			// WARNING: This is asked whether or not the window is in front, since a background
			// tab's Begin returns false and asks for nothing. Otherwise a restored tab for a
			// commit that is gone would never close.
			if (entry.AwaitingFirstAnswer)
			{
				const Core::Oid& commit = *entry.Diff.Commit;
				model.RequestDiffFor(commit);

				if (model.IsDiffFailed(commit))
				{
					APP_INFO("Not reopening the diff of '{}' in {}: that commit could not be read",
							 Core::ToUtf8(entry.Diff.Path), commit.ShortHex());

					model.ForgetFailedDiffFor(commit);
					m_Open.erase(m_Open.begin() + static_cast<std::ptrdiff_t>(tabIndex));
					continue;
				}

				if (model.GetDiffFor(commit) != nullptr)
				{
					entry.AwaitingFirstAnswer = false;
				}
			}

			// WARNING: Following the file comes before the name is taken, because the name is the
			// window: a tab that moves to the other side is a different window from this frame on.
			FollowFile(entry, model);

			const std::string windowName = DiffWindowName(entry.Diff);

			Core::PushContentWindowColours();

			const FileTab tab{.Path = entry.Diff.Path, .Title = DiffWindowTitle(entry.Diff)};
			const TabWindowFrame frame = BeginTabWindow(windowName, entry.Window, DockInto(centreNode), closeRequests,
														ImGuiWindowFlags_None, FileTabLook(tab, nullptr));

			Core::DrawTabContextMenu(closeRequests);

			if (frame.Visible)
			{
				Core::Ref<const Core::Diff> loadedDiff;

				if (entry.Diff.Commit.has_value())
				{
					model.RequestDiffFor(*entry.Diff.Commit);
					loadedDiff = model.GetDiffFor(*entry.Diff.Commit);
				}
				else
				{
					// WARNING: The working-tree diff is not cached by key like a commit's: it
					// changes on every keystroke, so the model holds exactly one and replaces it.
					model.RequestWorkingTreeDiff({.Target = entry.Diff.Target, .File = entry.Diff.Path});
					loadedDiff = model.GetWorkingTreeDiff();
				}

				if (!loadedDiff && !entry.Diff.Commit.has_value() && model.IsWorkingTreeDiffLoading())
				{
					Core::LoadingState("Loading the diff");
				}
				else if (!loadedDiff)
				{
					const char* reloadLabel = entry.Diff.Commit.has_value() ? "Reload" : nullptr;

					if (Core::EmptyState("This diff is no longer loaded.", reloadLabel))
					{
						model.RequestDiffFor(*entry.Diff.Commit);
					}
				}
				else
				{
					const std::optional<std::size_t> fileIndex =
						IndexOfFile(*loadedDiff, entry.Diff.Path);

					if (!fileIndex.has_value())
					{
						Core::EmptyState(EmptyDiffMessage(entry.Diff));
					}
					else
					{
						const Core::FileDiff& file = loadedDiff->Files[*fileIndex];

						if (entry.Diff.Commit.has_value())
						{
							entry.Texts.Update(model, *entry.Diff.Commit, file);
						}
						else
						{
							entry.Texts.UpdateFromWorkingTree(model, file);
						}

						entry.View.OnOpenFile = OpenFileIn(model);
						entry.View.OnCopied = OnCopied;
						entry.View.OnSetOptions = OnSetOptions;

						entry.View.Render(file, palette, syntax, selection, entry.Texts.Texts,
										  entry.Texts.Tokens, options);
					}
				}
			}

			ImGui::End();
			// WARNING: The pop waits for End: the diff view and every child begun inside the window
			// read ChildBg while it is open.
			Core::PopContentWindowColours();

			if (!frame.Open)
			{
				entry.Texts.ForgetFailure(model);

				m_Open.erase(m_Open.begin() + static_cast<std::ptrdiff_t>(tabIndex));
				continue;
			}

			++tabIndex;
		}
	}
}
