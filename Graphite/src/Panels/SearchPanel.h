#pragma once

#include "Core/Text/DocumentSet.h"
#include "Core/UI/Panel.h"
#include "Editor/EditorSettings.h"
#include "Search/HitGroups.h"
#include "Search/HitList.h"
#include "Search/PreviewContent.h"
#include "Search/SearchService.h"
#include "Search/SearchSettings.h"
#include "Theme/ActiveScheme.h"
#include "Workspace/Workspace.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	class PreviewSemantics;

	// A window of its own, unlike SearchSection in the settings panel: this is where a query is
	// typed and run, not where its two persisted toggles are edited.
	class SearchPanel final : public Core::Panel
	{
	public:
		SearchPanel(SearchService& service, const SearchSettings& settings, Workspace& workspace,
					Core::DocumentSet& documents, const ActiveScheme& scheme,
					const EditorSettings& editor, PreviewSemantics& semantics);

		// Drains every frame regardless of visibility — PanelManager::OnUpdate runs for every
		// panel, PanelManager::OnImGuiRender does not (a hidden panel, or one behind another tab
		// of the same dock node, never reaches OnImGuiRender). Draining there instead left
		// Progress.Running stuck true, and the status bar reading a frozen file count, the whole
		// time this panel was not the one on screen.
		void OnUpdate(Core::Timestep ts) override;

		void OnImGuiRender() override;

		// Read by the status bar, which draws before panels do and so cannot call Drain itself.
		std::optional<std::string> GetStatusMessage() const;

		// Shows the panel and moves the keyboard into the query field. Kept distinct from the
		// command PanelManager already registers for every panel (which brings the window forward
		// but leaves the keyboard where it was): a command named "Find in Files" that only raised
		// a window and left the user still clicking into the field would not be doing what its
		// name says.
		void Focus();

	private:
		SearchStart BuildRequest() const;

		void RenderQuery();
		void RenderSummary();
		void RenderResults();
		void RenderPreview();

		void RunSearch();
		void OpenAndSelect(const SearchHit& hit);

		SearchService& m_Service;
		const SearchSettings& m_Settings;
		Workspace& m_Workspace;

		// Not const: a result selects the matched range in the document it opens, which means
		// writing its caret.
		Core::DocumentSet& m_Documents;

		std::array<char, 256> m_QueryBuffer{};
		std::array<char, 128> m_MaskBuffer{};
		bool m_MatchCase = false;
		bool m_WholeWord = false;

		// Set by Focus(), consumed by RenderQuery() on the next frame it draws: SetKeyboardFocusHere
		// only takes effect when called immediately before the widget it targets.
		bool m_FocusQueryNextFrame = false;

		// What Drain hands over each frame, emptied into m_Groups immediately. A member rather
		// than a local so a long search does not reallocate it sixty times a second.
		std::vector<SearchHit> m_Drained;

		// Accumulated across frames and grouped as hits arrive, because regrouping every hit on
		// every frame cost 5.3 ms per frame at 8000 hits — see the note on HitGroups.
		HitGroups m_Groups;

		SearchProgress m_Progress;

		const ActiveScheme& m_Scheme;

		// The preview draws in the editor's font at the editor's tab width, so a previewed file
		// reads exactly as it does in a tab.
		const EditorSettings& m_Editor;

		// Which hit the preview is showing. HitSelection rather than a struct of its own: this
		// panel and ReferencesPanel show the same shape of results, through DrawHitList, and a
		// selection means the same thing in both.
		std::optional<HitSelection> m_Selected;

		// Initialised in the constructor's list rather than in place, so it can be built with
		// the mediator that gives it a server's colours.
		PreviewContent m_Preview;

		// How tall the results are; the preview takes what is left. Zero until the first frame
		// that shows both, which is when there is a panel height to halve — asking for one in
		// the constructor would ask before there is a window to ask about.
		//
		// Kept across a preview being dismissed and brought back, so a split the user dragged
		// is theirs until they drag it again.
		float m_ResultsHeight = 0.0f;
	};
}
