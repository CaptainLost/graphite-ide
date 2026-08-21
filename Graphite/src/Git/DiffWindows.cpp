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

namespace Graphite {

	std::string DiffWindowIdentity(const OpenDiff& diff)
	{
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
		return diff.Commit.ToHex() + Core::ToUtf8(diff.Path);
	}

	std::string DiffWindowTitle(const OpenDiff& diff)
	{
		// The file's name and the commit's short id, without the directory — which would
		// push the identifying part off the end of a narrow tab strip, and the same file's
		// diff in two commits is told apart by the id, not by the path.
		return Core::ToUtf8(diff.Path.filename()) + " (" + diff.Commit.ShortHex() + ")";
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
				const std::shared_ptr<const Core::Diff> diff = model.GetDiffFor(entry.Diff.Commit);

				if (!diff)
				{
					// A tab that vanished because a cache evicted something would be worse
					// than one that says the diff is no longer loaded; re-requesting it is
					// one call.
					ImGui::TextUnformatted("This diff is no longer loaded.");

					if (ImGui::SmallButton("Reload"))
					{
						model.RequestDiffFor(entry.Diff.Commit);
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

						// Every rule about which path keys the texts, when the tokens are still
						// valid and when a failed entry is forgiven lives in Update, shared
						// with the docked panel - the same steps written out twice were wrong
						// the same way in both copies.
						entry.Texts.Update(model, entry.Diff.Commit, file);

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
