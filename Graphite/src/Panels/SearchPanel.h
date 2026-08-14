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
					const Core::DocumentSet& documents);

		void OnImGuiRender() override;

		// Read by the status bar, which draws before panels do and so cannot call Drain itself
		// — that must happen exactly once a frame, here, at the top of OnImGuiRender.
		std::optional<std::string> GetStatusMessage() const;

	private:
		SearchStart BuildRequest() const;

		void RenderQuery();
		void RenderSummary();
		void RenderResults();

		void RunSearch();

		SearchService& m_Service;
		const SearchSettings& m_Settings;
		Workspace& m_Workspace;
		const Core::DocumentSet& m_Documents;

		std::array<char, 256> m_QueryBuffer{};
		std::array<char, 128> m_MaskBuffer{};
		bool m_MatchCase = false;
		bool m_WholeWord = false;

		// Accumulated across frames: Drain appends rather than replaces, and is cleared only
		// when a new search starts.
		std::vector<SearchHit> m_Hits;
		SearchProgress m_Progress;
	};
}
