#pragma once

#include "Git/DiffView.h"
#include "Git/GitModel.h"
#include "Git/GitPalette.h"

#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// One diff open as a tab. Either a file as it stood in one commit against its parent,
	// or a file in the working tree against the index or HEAD.
	//
	// The commit is optional rather than a zero Oid standing in for "the working tree",
	// because a zero Oid is a value a real commit could in principle collide with and
	// because "no commit" is exactly what is meant. Target is part of the identity, not
	// only of the query: the staged and unstaged views of one file are two things a reader
	// compares against each other, so they are two tabs.
	struct OpenDiff
	{
		std::optional<Core::Oid> Commit;
		Core::DiffTarget Target = Core::DiffTarget::CommitToParent;
		std::filesystem::path Path;

		friend bool operator==(const OpenDiff&, const OpenDiff&) = default;
	};

	// What ImGui keys the window by. Stable across frames and unique per commit and file,
	// because a window's dock position and its imgui.ini entry hang off this string.
	std::string DiffWindowIdentity(const OpenDiff& diff);

	// What a reader sees on the tab: the file's name and the commit's short id, without
	// the directory — which would push the identifying part off a narrow tab strip.
	std::string DiffWindowTitle(const OpenDiff& diff);

	// The two joined the way ImGui wants them: everything before ### is shown, everything
	// after identifies. Changing the title therefore moves no window.
	std::string DiffWindowName(const OpenDiff& diff);

	// Diffs opened as tabs in the document area.
	//
	// A tab is not membership of Core::DocumentSet — that set holds editable files and is
	// keyed by path. A tab is an ImGui window docked into the centre node, which is what
	// DocumentWindows does for each open document and what this does for each open diff.
	// So a diff needs no entry in that set, no TextDocument, and nothing in Core.
	class DiffWindows
	{
	public:
		// Opening one already open brings it forward rather than adding a second.
		void Open(const OpenDiff& diff);

		bool IsOpen(const OpenDiff& diff) const;
		std::size_t GetCount() const { return m_Open.size(); }

		void CloseAll();

		// Closes whichever working-tree tab is open, if any — there can be at most one,
		// since GitModel keeps a single slot for the working-tree diff rather than a cache
		// per file (see RequestWorkingTreeDiff). A caller that opens a new working-tree tab
		// calls this immediately before Open() so the previous tab does not linger, asking
		// into the same slot the new tab now owns. Commit tabs are untouched.
		void CloseWorkingTree();

		// centreNode is where a window is placed the first time its identity appears —
		// after that ImGui remembers, including across a restart through imgui.ini.
		void Render(GitModel& model, const GitPalette& palette, const SyntaxPalette& syntax,
					unsigned int centreNode);

	private:
		struct Entry
		{
			OpenDiff Diff;
			DiffView View;

			// Cleared once the window has been docked and brought forward. Opening a diff
			// that already has a tab must raise that tab, not silently do nothing.
			bool Fresh = true;
			bool Focus = false;

			// The whole-file texts for this tab's file, the tokens lexed from them, and which
			// file they are for. Held here rather than inside DiffView, which must not reach
			// for the model: Render calls Update, which does the asking, and hands the texts
			// and tokens down to View.Render as plain data. It also carries the key a closed
			// tab forgets its own failed entry under, which is why the tab does not have to
			// track that path itself.
			FileTextsBinding Texts;
		};

		std::vector<Entry> m_Open;
	};
}
