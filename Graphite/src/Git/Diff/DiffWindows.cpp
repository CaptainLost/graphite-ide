#include "Git/Diff/DiffWindows.h"

#include "Core/Base/Base.h"

#include "Git/History/CommitLabels.h"

#include "UI/RememberedPlace.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/TabContextMenu.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {

	// A short, stable word per target, used in both the identity and the title. Stable
	// matters more than pretty: the identity is what imgui.ini keys a window's dock
	// position by, so changing these strings moves every open tab back to the default.
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

	// The other half of the staged/unstaged pair, for the two targets that have one.
	std::optional<Core::DiffTarget> OtherSide(Core::DiffTarget target)
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

	// Whether the side a tab shows holds any change for a file, read off the two halves git
	// tracks separately.
	bool SideHasChange(const Core::FileStatus& file, Core::DiffTarget target)
	{
		const Core::StatusFlags flags =
			target == Core::DiffTarget::IndexToHead ? file.Index : file.WorkTree;

		return flags != Core::StatusFlags::None;
	}

	// How a side is named in a sentence about it being empty. Not NameOfTarget: that word
	// is fixed by the identity it also spells, and "no working changes" is not English.
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
}

namespace Graphite {

	std::string DiffWindowIdentity(const OpenDiff& diff)
	{
		if (diff.Commit.has_value())
		{
			// Unchanged from before OpenDiff grew a Target and an optional Commit: this is
			// exactly what a commit's tab hashed to before, byte for byte. imgui.ini keys an
			// already-docked window by this string, so keeping it verbatim is what keeps that
			// window from jumping back to its default position the next time this runs.
			//
			// Commit first, path second, no separator: Core::Oid::ToHex() is always forty
			// characters, so a fixed-length prefix cannot be split two ways, and no separator
			// means no character a path might itself contain to collide on.
			//
			// One gap this does not close: ImGui restarts its window-id hash at every "###" in
			// a name, so two paths that both contain "###" and agree on everything after the
			// last one would still collide once this identity is embedded in DiffWindowName.
			// "#" is legal in a Windows filename. DocumentWindows carries the identical
			// exposure through its own window name (see WindowName in DocumentWindows.cpp), so
			// this is a property of the scheme both share rather than something particular to
			// diffs.
			return diff.Commit->ToHex() + Core::ToUtf8(diff.Path);
		}

		// A working-tree tab is new, so nothing depends on its identity's shape yet: no window
		// has ever been docked under one, and no imgui.ini entry exists to disturb. "worktree"
		// can never collide with the commit case above, whose identity always begins with a
		// forty-character hex id, and the target is included because the staged and unstaged
		// views of one file are two things a reader compares against each other, so they must
		// be two tabs.
		return std::string{"worktree##"} + NameOfTarget(diff.Target) + "##" + Core::ToUtf8(diff.Path);
	}

	std::string DiffWindowTitle(const OpenDiff& diff)
	{
		// The file's name and, for a commit's tab, the commit's short id; for a working-tree
		// tab, the word for what it compares instead — there is no commit id to show. Neither
		// carries the directory, which would push the identifying part off the end of a narrow
		// tab strip, and the same file's diff in two commits is told apart by the id, not by
		// the path.
		const std::string name = Core::ToUtf8(diff.Path.filename());

		if (!diff.Commit.has_value())
		{
			return name + " (" + NameOfTarget(diff.Target) + ")";
		}

		return name + " (" + diff.Commit->ShortHex() + ")";
	}

	std::string DiffWindowName(const OpenDiff& diff)
	{
		// Matches WindowName in DocumentWindows.cpp: the icon and title precede ###, the
		// identity follows it, so renaming the tab moves no window, dock position or
		// imgui.ini entry.
		return std::string{Core::IconForExtension(Core::ToUtf8(diff.Path.extension()))} +
			   "  " + DiffWindowTitle(diff) + "###" + DiffWindowIdentity(diff);
	}

	std::optional<Core::DiffTarget> TargetHolding(const Core::WorkingTreeStatus& status,
												  const std::filesystem::path& path,
												  Core::DiffTarget current)
	{
		const std::optional<Core::DiffTarget> other = OtherSide(current);

		if (!other.has_value())
		{
			return std::nullopt;
		}

		const auto entry = std::find_if(status.Entries.begin(), status.Entries.end(),
										[&path](const Core::FileStatus& file) {
											return file.Path == path;
										});

		// Gone from the status entirely - committed, discarded, or never changed. There is
		// no side to move to, so the tab keeps the one it has and reports it empty.
		if (entry == status.Entries.end())
		{
			return std::nullopt;
		}

		// A conflicted entry is not two ordinary halves - see ChangesRows.h - so its flags
		// are no basis for deciding which half a tab belongs to.
		if (entry->IsConflicted)
		{
			return std::nullopt;
		}

		// Staged one way and edited again since puts a file in both halves at once. Only a
		// side that has actually emptied sends its tab anywhere.
		if (SideHasChange(*entry, current))
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

		// The file's name and not its path, for the same reason the tab carries only the
		// name: the sentence is read next to a tab that already says which file this is.
		return Core::ToUtf8(diff.Path.filename()) + " has no " + WordForEmptySide(diff.Target) +
			   "changes.";
	}

	void DiffWindows::Open(const OpenDiff& diff)
	{
		for (Entry& entry : m_Open)
		{
			if (entry.Diff == diff)
			{
				// Already open: raise it rather than adding a second copy. Fresh stays
				// false, so Render does not re-dock a tab the user may have moved.
				entry.Focus = true;
				return;
			}
		}

		m_Open.push_back(Entry{diff, DiffView{}, true, true});
	}

	bool DiffWindows::IsOpen(const OpenDiff& diff) const
	{
		return std::any_of(m_Open.begin(), m_Open.end(),
						   [&diff](const Entry& entry) { return entry.Diff == diff; });
	}

	void DiffWindows::CloseAll()
	{
		m_Open.clear();
	}

	void DiffWindows::CloseWorkingTree()
	{
		std::erase_if(m_Open, [](const Entry& entry) { return !entry.Diff.Commit.has_value(); });
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
		bool workingTreeOpen = std::any_of(m_Open.begin(), m_Open.end(),
										   [](const Entry& entry) { return !entry.Diff.Commit.has_value(); });

		for (const DiffTabState& stored : diffs)
		{
			const OpenDiff diff{.Commit = stored.Commit, .Target = stored.Target, .Path = stored.Path};

			if (IsOpen(diff))
			{
				continue;
			}

			// GitModel keeps one slot for the working-tree diff, so a second such tab would take
			// turns with the first asking into it.
			if (!diff.Commit.has_value())
			{
				if (workingTreeOpen)
				{
					continue;
				}

				workingTreeOpen = true;
			}

			Entry entry{diff, DiffView{}};
			entry.Restored = true;
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

		// Joined onto the working directory and not the workspace root: Repository::Open
		// searches upward for a .git, so only the former is what a diff's paths are relative to.
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

		// A new target is a new identity, so ImGui has never seen this window and has no
		// dock position for it. Docking it again is what keeps a followed tab in the centre
		// node instead of floating loose, and raising it is what keeps the reader looking at
		// the thing they were already looking at.
		entry.Fresh = true;
		entry.Focus = true;
	}

	void DiffWindows::Render(GitModel& model, const GitPalette& palette, const SyntaxPalette& syntax,
							 unsigned int centreNode, Core::TabCloseRequests& closeRequests)
	{
		for (std::size_t index = 0; index < m_Open.size();)
		{
			Entry& entry = m_Open[index];

			// Asked for whether or not the window is in front - a background tab's Begin returns
			// false and asks for nothing - so a restored tab for a commit that is gone closes
			// rather than waiting to be looked at.
			if (entry.AwaitingFirstAnswer)
			{
				const Core::Oid& commit = *entry.Diff.Commit;
				model.RequestDiffFor(commit);

				if (model.IsDiffFailed(commit))
				{
					APP_INFO("Not reopening the diff of '{}' in {}: that commit could not be read",
							 Core::ToUtf8(entry.Diff.Path), commit.ShortHex());

					// Forgotten so the failure does not stay behind, suppressing a later ask for
					// the same commit from the history list.
					model.ForgetFailedDiffFor(commit);
					m_Open.erase(m_Open.begin() + static_cast<std::ptrdiff_t>(index));
					continue;
				}

				if (model.GetDiffFor(commit) != nullptr)
				{
					entry.AwaitingFirstAnswer = false;
				}
			}

			// Before the name is taken, because the name is the window: a tab that follows
			// its file to the other side is, to ImGui, a different window from this frame
			// on, and everything below has to be talking about the new one.
			FollowFile(entry, model);

			const std::string name = DiffWindowName(entry.Diff);

			// Dock only on the frame this identity is new, exactly as DocumentWindows does:
			// docking every frame would fight a user who dragged the tab elsewhere.
			//
			// A tab reopened from the last launch goes back to where it was instead, which
			// imgui.ini already carries - unless that place is gone, in which case it is new
			// as far as anyone can tell and goes where a new one would.
			bool restored = false;

			if (entry.Fresh)
			{
				entry.Fresh = false;

				restored = std::exchange(entry.Restored, false);

				if (centreNode != 0 && (!restored || !HasRememberedPlace(name)))
				{
					ImGui::DockBuilderDockWindow(name.c_str(), centreNode);
				}
			}

			// The same problem Core::OpenItem::PendingFocus solves for documents: activating
			// a window moves nothing on screen by itself, so opening a diff that already had
			// a tab would otherwise do nothing visible. Taken and cleared in one step, so a
			// focus asked for once is given once.
			if (std::exchange(entry.Focus, false))
			{
				ImGui::SetNextWindowFocus();
			}

			bool open = true;

			// Restored windows must not take focus on the frame they appear: ImGui focuses every
			// window then, focusing a docked window selects its tab, and so the last one drawn
			// would take the front of its node from whatever imgui.ini remembers there.
			const ImGuiWindowFlags windowFlags =
				restored ? ImGuiWindowFlags_NoFocusOnAppearing : ImGuiWindowFlags_None;

			// Begin() returning false still needs its End() - that is the Begin/End rule,
			// unlike BeginTable, which must not be ended when it returns false.
			const bool visible = ImGui::Begin(name.c_str(), &open, windowFlags);

			// A tab's menu asked for this one last frame: the same as its close button.
			if (closeRequests.Take(name))
			{
				open = false;
			}

			Core::DrawTabContextMenu(closeRequests);

			if (visible)
			{
				Core::Ref<const Core::Diff> diff;

				if (entry.Diff.Commit.has_value())
				{
					model.RequestDiffFor(*entry.Diff.Commit);
					diff = model.GetDiffFor(*entry.Diff.Commit);
				}
				else
				{
					// Not cached by key the way a commit's diff is: this one changes on every
					// keystroke, so the model holds exactly one and replaces it.
					model.RequestWorkingTreeDiff({.Target = entry.Diff.Target, .File = entry.Diff.Path});
					diff = model.GetWorkingTreeDiff();
				}

				if (!diff && !entry.Diff.Commit.has_value() && model.IsWorkingTreeDiffLoading())
				{
					// First opened, or following its file to the other side: the answer is on its
					// way, which is not the same as a diff that was lost.
					Core::EmptyState("Loading…");
				}
				else if (!diff)
				{
					// A tab that vanished because a cache evicted something would be worse
					// than one that says the diff is no longer loaded; re-requesting it is
					// one call.
					const char* reload = entry.Diff.Commit.has_value() ? "Reload" : nullptr;

					if (Core::EmptyState("This diff is no longer loaded.", reload))
					{
						model.RequestDiffFor(*entry.Diff.Commit);
					}
				}
				else
				{
					const std::optional<std::size_t> fileIndex =
						IndexOfFile(*diff, entry.Diff.Path);

					if (!fileIndex.has_value())
					{
						Core::EmptyState(EmptyDiffMessage(entry.Diff));
					}
					else
					{
						const Core::FileDiff& file = diff->Files[*fileIndex];

						if (entry.Diff.Commit.has_value())
						{
							// Every rule about which path keys the texts, when the tokens are
							// still valid and when a failed entry is forgiven lives in Update,
							// shared with the docked panel - the same steps written out twice
							// were wrong the same way in both copies.
							entry.Texts.Update(model, *entry.Diff.Commit, file);
						}
						else
						{
							// The working tree's own text, which the model fetched on the same job
							// as the diff above - so the colouring and the whole-file view describe
							// the same moment of a file the reader may still be typing in.
							entry.Texts.UpdateFromWorkingTree(model, file);
						}

						entry.View.OnOpenFile = OpenFileIn(model);

						entry.View.Render(file, palette, syntax, entry.Texts.Texts,
										  entry.Texts.Tokens);
					}
				}
			}

			ImGui::End();

			if (!open)
			{
				// A closed tab forgets its own failed texts entry, mirroring the model's
				// ForgetFailedDiffFor on a collapsed commit row - ForgetFailure is a no-op for
				// anything but a failed entry, so this costs nothing when the tab closed clean.
				entry.Texts.ForgetFailure(model);

				m_Open.erase(m_Open.begin() + static_cast<std::ptrdiff_t>(index));
				continue;
			}

			++index;
		}
	}
}
