#pragma once

#include "Git/CommitLabels.h"
#include "Git/CommitListView.h"
#include "Git/DiffView.h"
#include "Git/DiffWindows.h"
#include "Git/GitModel.h"
#include "Theme/ActiveScheme.h"

#include "Core/Events/EventBus.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Ref.h"
#include "Core/UI/Panel.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>

namespace Graphite {

	// "main", "* main  2/3" for the checked-out branch with a measured distance from its
	// upstream. A pure function of the branch, pulled out of the draw loop so it can be
	// checked without a frame to draw into.
	std::string RefLabel(const Core::Branch& branch);

	// The whole history in one panel: refs down the left, and on the right the commit list
	// above the diff of whichever file is selected under an expanded commit.
	//
	// A panel rather than a document tab: this view is keyed by selection state (ref,
	// commit, file), not by a single path on disk, so it does not fit as one entry of
	// DocumentSet the way a file does — a diff of one file in one commit can, though, which
	// is what m_DiffWindows is for. Docked into the centre, it fills the same area a
	// document would.
	class GitView final : public Core::Panel
	{
	public:
		GitView(GitModel& model, const ActiveScheme& scheme, DiffWindows& diffWindows);

		void OnImGuiRender() override;

		// Resets the first-log latch on every open and close, so a folder opened after
		// another one leaves - which drops the model's log the same way a close does -
		// gets asked for again rather than leaving the history column loading forever. Also
		// resets the commit list's own expansion, selection and load-more state, since
		// GitModel::Clear() drops every cached diff on the same two events and a selection
		// or expansion naming a commit from the old repository would otherwise point at
		// nothing the new page ever re-requests.
		void SubscribeTo(Core::EventBus& events);

	private:
		void RenderRefs(const std::vector<Core::Branch>& branches);
		void RenderCommits(const GitPalette& palette);
		void RenderDiff(const GitPalette& palette);

		GitModel& m_Model;
		const ActiveScheme& m_Scheme;

		// Referenced rather than owned: GraphiteLayer owns the one DiffWindows and renders
		// it itself, beside m_DocumentWindows, so a tab opened from here keeps drawing after
		// this panel closes or loses focus.
		DiffWindows& m_DiffWindows;

		CommitListView m_Commits;
		DiffView m_Diff;

		std::optional<std::string> m_SelectedRef;

		// How tall the commit list is; the diff takes what is left. Persisted across frames by
		// DrawSplitResults's own contract — see SplitResults.h for why it starts at zero and
		// survives the diff pane being dismissed and brought back.
		float m_ListHeight = 0.0f;

		// Set once per workspace, the first frame the panel draws with no log snapshot yet
		// — an empty Ref means HEAD. Guards RequestLog so it fires once rather than every
		// frame the log is still in flight. Reset by SubscribeTo's handlers whenever the
		// workspace opens or closes, since GitModel::Clear() drops the log on both.
		bool m_LogRequested = false;

		// Which commits were expanded as of last frame, so a commit dropped from this set
		// this frame was just collapsed. Compared rather than driven by a callback out of
		// CommitListView, which must not know the model it would otherwise call into.
		std::unordered_set<Core::Oid> m_PreviouslyExpanded;

		// Which (commit, new path) the texts below were last fetched for. Compared against
		// the current selection each frame so a selection change forgets a previous file's
		// failed entry — mirroring m_PreviouslyExpanded/ForgetFailedDiffFor above, but for
		// GetTextsFor/ForgetFailedTextsFor, which are keyed by new path rather than by commit
		// alone. Unset until the first file is shown.
		std::optional<Core::Oid> m_TextsCommit;
		std::filesystem::path m_TextsNewPath;

		// The whole-file texts for whichever file is currently selected, and the tokens lexed
		// from them. Held here rather than inside DiffView because DiffView must not reach for
		// the model — RenderDiff is what asks GitModel::RequestTextsFor/GetTextsFor, rebuilds
		// the tokens only when GetTextsFor hands back a different object than m_Texts already
		// holds, and passes both down to DiffView::Render.
		std::shared_ptr<const FileTexts> m_Texts;
		FileTokens m_Tokens;
	};
}
