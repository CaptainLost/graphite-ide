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
#include <cstdint>
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

	enum class ReferencesPhase : std::uint8_t
	{
		NotAsked,
		WaitingForServer,
		Finding,
		Found,
		Failed,
		Unavailable
	};

	struct ReferencesSummary
	{
		std::string Text;
		bool Waiting = false;
	};

	ReferencesSummary SummariseReferences(ReferencesPhase phase, const std::string& symbol, std::size_t hitCount,
										  const std::string& note);

	std::string ReferencesFailureText(const std::string& reason);

	std::string RefreshFailureText(const std::string& reason);

	bool AnswersRefreshOf(ReferencesPhase phase, bool refreshing, const std::string& shownSymbol,
						  const std::string& answeredSymbol);

	struct HitPlace
	{
		std::filesystem::path File;
		std::size_t Line = 0;
		std::size_t OrdinalOnLine = 0;
	};

	std::optional<HitPlace> PlaceOfSelectedHit(const std::vector<HitGroup>& groups, const HitSelection& selection);

	std::optional<HitSelection> SelectionAtPlace(const std::vector<HitGroup>& groups, const HitPlace& place);

	class ReferencesPanel final : public Core::Panel
	{
	public:
		ReferencesPanel(Workspace& workspace, Core::DocumentSet& documents,
						const ActiveScheme& scheme, const EditorSettings& editor,
						PreviewSemantics& semantics);

		void OnImGuiRender() override;

		void ShowWaiting(const std::string& symbol, const std::string& waitingFor);

		void ShowWorking(const std::string& symbol);

		void ShowRefreshing(const std::string& refreshingWhy);

		void ShowResults(const std::string& symbol, const std::vector<Location>& locations,
						 const std::string& incompleteWhy);

		void ShowFailure(const std::string& symbol, const std::string& reason);

		void ShowUnavailable(const std::string& symbol, const std::string& reason);

		bool TakeRetry();

		void Focus();

	private:
		void BeginShowing(ReferencesPhase phase, const std::string& symbol, std::string note);

		bool IsRefreshing(const std::string& symbol) const;

		void FillGroups(const std::string& symbol, const std::vector<Location>& locations);

		void KeepListAfterFailedRefresh(const std::string& reason);

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

		// WARNING: Not m_Symbol.empty() for "never asked": a search from an empty document asks about an empty word.
		ReferencesPhase m_Phase = ReferencesPhase::NotAsked;

		std::string m_Note;

		std::optional<std::string> m_Refreshing;
		std::optional<std::string> m_RefreshFailure;

		bool m_RetryWanted = false;

		HitGroups m_Groups;

		std::optional<HitSelection> m_Selected;

		PreviewContent m_Preview;

		float m_ResultsHeight = 0.0f;

		std::filesystem::path m_DiskPath;
		std::optional<Core::TextBuffer> m_DiskBuffer;
	};
}
