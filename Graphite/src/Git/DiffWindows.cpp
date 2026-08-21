#include "Git/DiffWindows.h"

#include "Git/CommitLabels.h"

#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/Icons.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace {

	// A short, stable word per target, used in both the identity and the title. Stable
	// matters more than pretty: the identity is what imgui.ini keys a window's dock
	// position by, so changing these strings moves every open tab back to the default.
	const char* NameOfTarget(Core::DiffTarget target)
	{
		switch (target)
		{
		case Core::DiffTarget::WorkTreeToIndex: return "unstaged";
		case Core::DiffTarget::IndexToHead: return "staged";
		case Core::DiffTarget::WorkTreeToHead: return "working";
		case Core::DiffTarget::CommitToParent: return "commit";
		case Core::DiffTarget::RevisionToRevision: return "range";
		}

		return "diff";
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

	void DiffWindows::Render(GitModel& model, const GitPalette& palette, const SyntaxPalette& syntax,
							 unsigned int centreNode)
	{
		for (std::size_t index = 0; index < m_Open.size();)
		{
			Entry& entry = m_Open[index];
			const std::string name = DiffWindowName(entry.Diff);

			// Dock only on the frame this identity is new, exactly as DocumentWindows does:
			// docking every frame would fight a user who dragged the tab elsewhere.
			if (entry.Fresh)
			{
				entry.Fresh = false;

				if (centreNode != 0)
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

			// Begin() returning false still needs its End() - that is the Begin/End rule,
			// unlike BeginTable, which must not be ended when it returns false.
			if (ImGui::Begin(name.c_str(), &open))
			{
				std::shared_ptr<const Core::Diff> diff;

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

				if (!diff)
				{
					// A tab that vanished because a cache evicted something would be worse
					// than one that says the diff is no longer loaded; re-requesting it is
					// one call.
					ImGui::TextUnformatted("This diff is no longer loaded.");

					if (entry.Diff.Commit.has_value() && ImGui::SmallButton("Reload"))
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
						ImGui::TextUnformatted("That file is not in this diff.");
					}
					else
					{
						const Core::FileDiff& file = diff->Files[*fileIndex];

						// A working-tree tab has no commit to key texts by, and ReadFileTexts
						// reads both sides out of commits — it has no way to name "the file on
						// disk". So this tab's hunks, added and removed lines and change bands
						// draw normally, but without the whole-file text that gives a commit's
						// diff its syntax colouring; View.Render already accepts no texts while
						// they are unavailable.
						if (entry.Diff.Commit.has_value())
						{
							// Every rule about which path keys the texts, when the tokens are
							// still valid and when a failed entry is forgiven lives in Update,
							// shared with the docked panel - the same steps written out twice
							// were wrong the same way in both copies.
							entry.Texts.Update(model, *entry.Diff.Commit, file);
						}

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
