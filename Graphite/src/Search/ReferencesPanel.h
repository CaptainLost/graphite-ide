#pragma once

#include "Core/Text/TextBuffer.h"
#include "Core/UI/Panel.h"
#include "Editor/EditorSettings.h"
#include "Language/Api/LanguageService.h"
#include "Search/HitGroups.h"
#include "Search/HitList.h"
#include "Search/PreviewContent.h"
#include "Search/SearchQuery.h"
#include "Theme/ActiveScheme.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Core {

	class DocumentSet;
}

namespace Graphite {

	class Workspace;
	class PreviewSemantics;

	// WARNING: Answering std::nullopt means "not now, ask again later"; AsHits then leaves the hit's text
	// unset and records the span in SearchHit::Unread.
	using LineTextSource =
		std::function<std::optional<std::string>(const std::filesystem::path&, std::size_t line)>;

	std::vector<SearchHit> AsHits(const std::vector<Location>& locations, const std::string& symbol,
								  const LineTextSource& lineText);

	class ReferencesPanel final : public Core::Panel
	{
	public:
		ReferencesPanel(Workspace& workspace, Core::DocumentSet& documents,
						const ActiveScheme& scheme, const EditorSettings& editor,
						PreviewSemantics& semantics);

		void OnImGuiRender() override;

		void ShowWorking(const std::string& symbol);

		void ShowResults(const std::string& symbol, const std::vector<Location>& locations);

		void Focus();

	private:
		void RenderSummary();
		void RenderResults();
		void RenderPreview();

		void OpenAndSelect(const SearchHit& hit);

		std::optional<std::string> LineTextInOpenDocument(const std::filesystem::path& path,
														  std::size_t line) const;

		void ResolveGroup(HitGroup& group);

		std::string LineTextFor(const std::filesystem::path& path, std::size_t line);

		Workspace& m_Workspace;
		Core::DocumentSet& m_Documents;
		const ActiveScheme& m_Scheme;
		const EditorSettings& m_Editor;

		std::string m_Symbol;
		bool m_Working = false;

		// WARNING: Not m_Symbol.empty(): a search from an empty document asks about an empty word.
		bool m_Asked = false;

		HitGroups m_Groups;

		std::optional<HitSelection> m_Selected;

		PreviewContent m_Preview;

		float m_ResultsHeight = 0.0f;

		std::filesystem::path m_DiskPath;
		std::optional<Core::TextBuffer> m_DiskBuffer;
	};
}
