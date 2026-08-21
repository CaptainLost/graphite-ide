#pragma once

#include "Git/DiffView.h"
#include "Git/GitModel.h"
#include "Git/GitPalette.h"

#include "Core/Git/Oid.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	// One diff open as a tab: a file, as it stood in one commit against its parent.
	struct OpenDiff
	{
		Core::Oid Commit;
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

			// The whole-file texts for this tab's file, and the tokens lexed from them. Held
			// here rather than inside DiffView, which must not reach for the model: Render is
			// what asks GitModel::RequestTextsFor/GetTextsFor, rebuilds Tokens only when
			// GetTextsFor hands back a different object than Texts already holds, and passes
			// both down to View.Render.
			std::shared_ptr<const FileTexts> Texts;
			FileTokens Tokens;

			// The new path Texts was fetched against - not always Diff.Path, which falls back
			// to the old path for a deleted file. Kept so a closed tab can forget its own
			// failed entry with the same key it was requested under, mirroring GitView's
			// ForgetFailedTextsFor call for the same reason.
			std::filesystem::path TextsNewPath;
		};

		std::vector<Entry> m_Open;
	};
}
