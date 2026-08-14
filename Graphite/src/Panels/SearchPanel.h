#pragma once

#include "Core/Text/DocumentSet.h"
#include "Core/UI/Panel.h"
#include "Search/SearchService.h"
#include "Search/SearchSettings.h"
#include "Workspace/Workspace.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// A window of its own, unlike SearchSection in the settings panel: this is where a query is
	// typed and run, not where its two persisted toggles are edited.
	class SearchPanel final : public Core::Panel
	{
	public:
		SearchPanel(SearchService& service, const SearchSettings& settings, Workspace& workspace,
					Core::DocumentSet& documents);

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
		// plain show/hide toggle PanelManager already registers for every panel (which never
		// touches focus): a command named "Find in Files" that only unhid a window and left the
		// user still clicking into the field would not be doing what its name says.
		void Focus();

	private:
		SearchStart BuildRequest() const;

		void RenderQuery();
		void RenderSummary();
		void RenderResults();

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

		// Accumulated across frames: Drain appends rather than replaces, and is cleared only
		// when a new search starts.
		std::vector<SearchHit> m_Hits;
		SearchProgress m_Progress;
	};
}
