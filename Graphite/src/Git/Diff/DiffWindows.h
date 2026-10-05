#pragma once

#include "Git/Diff/DiffView.h"
#include "Git/GitModel.h"
#include "Theme/GitPalette.h"

#include "Persistence/PersistedState.h"

#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Status.h"
#include "Core/ImGui/TabCloseRequests.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
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

	// Which side of the working tree now holds this file's change, when it is not the side
	// the tab is showing. Nullopt means the tab stays where it is: because its own side
	// still holds a change, because no side does, or because the tab is not one of the two
	// sides that pair up at all.
	//
	// Read off the status rather than off an empty diff. The status is already on hand
	// every frame, so this costs no query; an empty diff, by contrast, is also what a tab
	// shows while the first query for a side is still in flight, and following on that
	// would send the tab to the other side and back again on every switch.
	std::optional<Core::DiffTarget> TargetHolding(const Core::WorkingTreeStatus& status,
												  const std::filesystem::path& path,
												  Core::DiffTarget current);

	// What a tab says when its file is not in the diff it asked for. For a commit that is a
	// genuine mismatch - the file was never part of it. For a side of the working tree it
	// is an ordinary, momentary emptiness, and saying which side and which file is what
	// tells the two apart.
	std::string EmptyDiffMessage(const OpenDiff& diff);

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

		// The open diffs, in tab order, for the next launch.
		std::vector<DiffTabState> CaptureState() const;

		// Reopens what CaptureState returned last time, after CloseAll has emptied the list for
		// the folder being opened. A restored tab goes back to the place imgui.ini remembers for
		// it rather than to the centre node, and none of them is brought forward: the tab the
		// reader was looking at is whichever one the dock node itself remembers as selected.
		// Only the first working-tree diff is reopened, for the reason CloseWorkingTree gives.
		void RestoreState(const std::vector<DiffTabState>& diffs);

		// centreNode is where a window is placed the first time its identity appears —
		// after that ImGui remembers, including across a restart through imgui.ini.
		// `selection` is the editor scheme's selection colour and `wordWrap` the setting for
		// diffs; see DiffView::Render for both.
		void Render(GitModel& model, const GitPalette& palette, const SyntaxPalette& syntax,
					ImU32 selection, bool wordWrap, unsigned int centreNode,
					Core::TabCloseRequests& closeRequests);

		// Confirms a copy out of any tab's diff, given the text already on the clipboard. Set
		// once by whoever owns the application's event bus; handed to every DiffView this draws.
		std::function<void(std::string_view)> OnCopied;

		// Sets the setting for diffs that Render's `wordWrap` comes from. Set once by whoever
		// owns the settings; handed to every DiffView this draws, as OnCopied is.
		std::function<void(bool)> OnSetWordWrap;

		// Opens a file in the editor, given its absolute path. Set once by whoever owns the
		// editor's tabs; every diff this draws offers it, and so does the docked history
		// pane, which reaches it through here for the same reason it opens tabs through here.
		std::function<void(const std::filesystem::path&)> OpenFile;

		// OpenFile bound to `model`'s working directory, which is what a diff's paths are
		// relative to; empty while OpenFile is. Reassigned each frame by whoever draws a
		// DiffView, since the working directory moves with the folder that is open. Holds this
		// and `model` by reference, so it is kept no longer than either: both outlive every
		// DiffView it is handed to.
		std::function<void(const std::filesystem::path&)> OpenFileIn(const GitModel& model) const;

	private:
		struct Entry;

		// Moves a working-tree tab to whichever side of the working tree now holds its
		// file's change, when its own side has emptied. See TargetHolding for when that is,
		// and for why it is decided off the status rather than off an empty diff.
		static void FollowFile(Entry& entry, const GitModel& model);

		struct Entry
		{
			OpenDiff Diff;
			DiffView View;

			// Cleared once the window has been docked and brought forward. Opening a diff
			// that already has a tab must raise that tab, not silently do nothing.
			bool Fresh = true;
			bool Focus = false;

			// Reopened by RestoreState and not yet drawn: its first frame leaves it wherever
			// imgui.ini remembers it, when it remembers anywhere it can still go.
			bool Restored = false;

			// A commit's diff reopened by RestoreState whose diff has not come back yet. The
			// commit may be gone - rebased away, or the branch deleted, since the last launch -
			// and a tab restored for it would only ever say its diff is no longer loaded, with
			// a Reload the model would ignore. So its first answer decides: a diff clears this,
			// a failure closes the tab. A diff the reader opens keeps its tab either way.
			bool AwaitingFirstAnswer = false;

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
