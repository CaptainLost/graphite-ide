#pragma once

#include "Core/Text/TextBuffer.h"
#include "Core/UI/Panel.h"
#include "Editor/EditorSettings.h"
#include "Lang/Api/LanguageService.h"
#include "Search/HitGroups.h"
#include "Search/HitList.h"
#include "Search/PreviewContent.h"
#include "Search/SearchQuery.h"
#include "Theme/ActiveScheme.h"

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

	// Answers the whole text of one line, however the caller can. Passed in rather than reached
	// for so AsHits stays a pure function and its tests need no files on disk.
	using LineTextSource =
		std::function<std::string(const std::filesystem::path&, std::size_t line)>;

	// Locations grouped by file, ascending by line within each — the shape HitGroups already
	// renders for search results.
	//
	// Deduplicates first: a server can report the same location twice, and a reference listed
	// twice would read as a miscount against the count the lens that opened this panel showed.
	// Sorted by file first, then line then column, so a `lineText` that reads a file only when
	// the path changes — the shape ReferencesPanel's own feeds it — reads each file at most
	// once. `symbol` supplies the width of the highlighted span: a reply says only where a
	// reference starts, not how long the name at that spot is.
	std::vector<SearchHit> AsHits(const std::vector<Location>& locations, const std::string& symbol,
								  const LineTextSource& lineText);

	// Lists a symbol's references: the panel Shift+F12 and a click on a lens row both fill,
	// through ShowWorking and then ShowResults once the reply arrives. Built from the same
	// machinery the search results panel draws from - HitGroups for grouping, the shared hit
	// list for the rows, SearchPreview for the pane - fed from a language server's reply instead
	// of a file-tree search.
	class ReferencesPanel final : public Core::Panel
	{
	public:
		ReferencesPanel(const Workspace& workspace, Core::DocumentSet& documents,
						const ActiveScheme& scheme, const EditorSettings& editor);

		void OnImGuiRender() override;

		// Called the instant a request goes out, before any answer can arrive: shows the symbol
		// and that a request for it is working, and drops whatever the previous request found,
		// since it answered a different question.
		void ShowWorking(const std::string& symbol);

		// The reply has arrived. `locations` becomes hits through AsHits and is grouped by file
		// exactly as a search's results are. An empty vector - a reply with nothing in it, or one
		// this document's language has no server for - reads as "no references found" rather
		// than as a panel stuck saying "working".
		void ShowResults(const std::string& symbol, const std::vector<Location>& locations);

		// Shows the panel and brings it to the front, the same way SearchPanel::Focus does for
		// its own window: a references request is a question just asked, not something to leave
		// buried behind another tab.
		void Focus();

	private:
		void RenderSummary();
		void RenderResults();
		void RenderPreview();

		void OpenAndSelect(const SearchHit& hit);

		// The concrete LineTextSource AsHits is fed from: an open document's buffer, so unsaved
		// edits agree with what the editor shows, or - failing that - the file on disk, read at
		// most once per distinct path because AsHits sorts its locations by file first and calls
		// this in that order.
		std::string LineTextFor(const std::filesystem::path& path, std::size_t line);

		const Workspace& m_Workspace;
		Core::DocumentSet& m_Documents;
		const ActiveScheme& m_Scheme;
		const EditorSettings& m_Editor;

		std::string m_Symbol;
		bool m_Working = false;

		HitGroups m_Groups;

		std::optional<HitSelection> m_Selected;
		PreviewContent m_Preview;

		// How tall the results are; the preview takes what is left. See SearchPanel's own field
		// of the same name for why it starts at zero and survives a preview being dismissed.
		float m_ResultsHeight = 0.0f;

		// LineTextFor's disk cache: the last path it read from disk and what that file held,
		// reused as long as the path does not change. Not a member of AsHits itself, which stays
		// a pure function - this is the state that makes reading "at most once per file" true in
		// practice, for the one caller that feeds it locations sorted by file. Unset when the
		// last path read could not be loaded, so a line asked for from an unreadable file comes
		// back empty rather than re-attempting the read for every location in it.
		std::filesystem::path m_DiskPath;
		std::optional<Core::TextBuffer> m_DiskBuffer;
	};
}
