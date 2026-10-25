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

	class SearchPanel final : public Core::Panel
	{
	public:
		SearchPanel(SearchService& service, const SearchSettings& settings, Workspace& workspace,
					Core::DocumentSet& documents, const ActiveScheme& scheme,
					const EditorSettings& editor, PreviewSemantics& semantics);

		// WARNING: Drains every frame regardless of visibility, because OnImGuiRender never reaches a
		// hidden panel and Progress.Running would stay true.
		void OnUpdate(Core::Timestep timestep) override;

		void OnImGuiRender() override;

		std::optional<std::string> GetStatusMessage() const;

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

		Core::DocumentSet& m_Documents;

		std::array<char, 256> m_QueryBuffer{};
		std::array<char, 128> m_MaskBuffer{};
		bool m_MatchCase = false;
		bool m_WholeWord = false;

		std::optional<std::string> m_SearchedNeedle;

		bool m_FocusQueryNextFrame = false;

		std::vector<SearchHit> m_Drained;

		HitGroups m_Groups;

		SearchProgress m_Progress;

		const ActiveScheme& m_Scheme;

		const EditorSettings& m_Editor;

		std::optional<HitSelection> m_Selected;

		PreviewContent m_Preview;

		float m_ResultsHeight = 0.0f;
	};
}
