#include "Panels/ReferencesPanel.h"

#include "Panels/SearchPreview.h"
#include "UI/SplitResults.h"

#include "Lang/Server/LspClient.h"

#include "Core/ImGui/EmptyState.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Codec.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"

#include "Workspace/Workspace.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace Graphite {

	namespace {

		// The name as written at a use site, which is what a highlight there should cover. A
		// server names a symbol in full - clangd answers "Application::Get" - but a call site
		// spells only the last component, so measuring the highlight from the full name runs it
		// past the end of the name and into whatever follows: "Get" highlighted for sixteen
		// characters swallows "Get().GetWindow(", which is the same length by coincidence and
		// looks like a parser gone wrong.
		std::string_view UnqualifiedName(const std::string& symbol)
		{
			const std::size_t separator = symbol.rfind("::");

			return separator == std::string::npos
					   ? std::string_view{symbol}
					   : std::string_view{symbol}.substr(separator + 2);
		}

		// Where a hit's highlight starts and stops, in bytes of `text`.
		//
		// Ordinarily the server's own range, which points at the characters it matched and is
		// therefore right even where the name is spelled unusually - through a macro, across a
		// line continuation, behind an alias.
		//
		// It is overridden in one case, and only after checking: a range that does not cover the
		// name at all. clangd answers this way for a constructor reached through a qualified name
		// - for `: Core::Application(spec)` it names bytes 5 to 9, which is `Core`, the namespace
		// rather than the thing being referenced. Verified against clangd 22 rather than assumed.
		// Highlighting that draws the eye to the wrong word on the row, so where the name itself
		// can be found on the line at or after where the server pointed, that wins.
		std::pair<std::size_t, std::size_t> SpanIn(const std::string& text, std::size_t line,
												   Utf16Column column, std::size_t endLine,
												   Utf16Column endColumn, const std::string& symbol)
		{
			const std::size_t from = std::min(FromUtf16Column(text, column), text.size());
			const std::string_view name = UnqualifiedName(symbol);

			const bool spansOneLine = endLine == line && endColumn.Value > column.Value;
			const std::size_t to =
				spansOneLine ? std::min(FromUtf16Column(text, endColumn), text.size())
							 : std::min(from + name.size(), text.size());

			if (std::string_view{text}.substr(from, to - from) == name)
			{
				return {from, to};
			}

			// Searched forward from where the server pointed, never backward, and never off this
			// line: a name found earlier on the row belongs to some other expression, and the
			// server's position is the only thing tying this hit to a place at all.
			if (const std::size_t found = text.find(name, from); found != std::string::npos)
			{
				return {found, found + name.size()};
			}

			return {from, to};
		}

		// The same read the search itself does, deliberately: a preview that decoded a file
		// differently from the search that found the hit would disagree with the row above it.
		std::optional<std::string> ReadAll(const std::filesystem::path& path)
		{
			std::ifstream in(path, std::ios::binary);

			if (!in)
			{
				return std::nullopt;
			}

			return std::string(std::istreambuf_iterator<char>(in),
							   std::istreambuf_iterator<char>());
		}
	}

	std::vector<SearchHit> AsHits(const std::vector<Location>& locations, const std::string& symbol,
								  const LineTextSource& lineText)
	{
		std::vector<Location> sorted = locations;

		// File first, so a caller reading through disk reads each file only once - the whole
		// reason this is sorted before anything else happens to it. Line and column after that
		// so the panel's groups come out with their hits already in the order HitGroups expects.
		std::sort(sorted.begin(), sorted.end(),
				 [](const Location& a, const Location& b)
				 {
					 if (a.Path != b.Path)
					 {
						 return a.Path < b.Path;
					 }

					 if (a.Line != b.Line)
					 {
						 return a.Line < b.Line;
					 }

					 return a.Column.Value < b.Column.Value;
				 });

		// A server can report the same location twice; a reference listed twice would read as a
		// miscount against the number the lens that opened this panel showed. Sorted just above,
		// so equal locations are adjacent and std::unique finds them.
		sorted.erase(std::unique(sorted.begin(), sorted.end(),
								 [](const Location& a, const Location& b) {
									 return a.Path == b.Path && a.Line == b.Line &&
											a.Column.Value == b.Column.Value;
								 }),
					sorted.end());

		std::vector<SearchHit> hits;
		hits.reserve(sorted.size());

		for (const Location& location : sorted)
		{
			SearchHit hit;
			hit.File = location.Path;
			hit.Line = location.Line;

			if (std::optional<std::string> text = lineText(location.Path, location.Line))
			{
				hit.Text = std::move(*text);

				const auto [from, to] = SpanIn(hit.Text, location.Line, location.Column,
											   location.EndLine, location.EndColumn, symbol);

				hit.From = from;
				hit.To = to;
			}
			else
			{
				// The line is not available yet, and this function does not go looking for it.
				// The span is kept so whoever does read the line later can finish the job -
				// converting it needs the line's own bytes, and nothing else here is enough.
				hit.Unread = SearchHit::UnreadSpan{location.Column.Value, location.EndColumn.Value};
			}

			hits.push_back(std::move(hit));
		}

		return hits;
	}

	ReferencesPanel::ReferencesPanel(const Workspace& workspace, Core::DocumentSet& documents,
									 const ActiveScheme& scheme, const EditorSettings& editor,
									 PreviewSemantics& semantics)
		// Hidden until asked for, like the search panel: it opens with nothing to show, so it
		// costs no space until Shift+F12 or a lens click first fills it.
		: Core::Panel("references", "References", "navigate", ICON_MDI_LINK_VARIANT, false), m_Workspace(workspace),
		  m_Documents(documents), m_Scheme(scheme), m_Editor(editor), m_Preview(&semantics)
	{
	}

	void ReferencesPanel::OnImGuiRender()
	{
		// Nothing asked for yet: the whole panel says how to ask, rather than a summary line
		// over an empty list.
		if (!m_Asked)
		{
			Core::EmptyState("Shift+F12, or click a reference count, to list a symbol's references.");
			return;
		}

		RenderSummary();
		ImGui::Separator();

		const std::function<void()> drawResults = [this] { RenderResults(); };
		const std::function<void()> drawPreview = [this] { RenderPreview(); };

		DrawSplitResults(SplitResultsInput{.HasSelection = m_Selected.has_value(),
										   .ResultsHeight = m_ResultsHeight,
										   .DrawResults = drawResults,
										   .DrawPreview = drawPreview,
										   .ResultsChildId = "##referencesResults",
										   .PreviewChildId = "##referencesPreview",
										   .SplitterId = "##referencesSplit"});
	}

	void ReferencesPanel::ShowWorking(const std::string& symbol)
	{
		m_Symbol = symbol;
		m_Working = true;
		m_Asked = true;

		m_Groups.Clear();
		m_Groups.SetRoot(m_Workspace.GetRoot());

		m_Selected.reset();
		m_Preview.Clear();
	}

	void ReferencesPanel::ShowResults(const std::string& symbol, const std::vector<Location>& locations)
	{
		m_Symbol = symbol;
		m_Working = false;
		m_Asked = true;

		m_Groups.Clear();
		m_Groups.SetRoot(m_Workspace.GetRoot());

		m_Selected.reset();
		m_Preview.Clear();

		// A fresh reply starts the disk cache over: the file LineTextFor last read may not be
		// the first one this reply's locations name, and holding onto it costs nothing to avoid.
		m_DiskPath.clear();
		m_DiskBuffer.reset();

		// Open documents only. A reply about a common symbol names hundreds of files, and reading
		// them all here would be hundreds of probes, reads and decodes in the single frame the
		// reply arrives - on the UI thread, with the application stopped for the length of it.
		// The rest are read by ResolveGroup, one file at a time, as the user expands them.
		const LineTextSource lineText = [this](const std::filesystem::path& path, std::size_t line)
		{ return LineTextInOpenDocument(path, line); };

		for (SearchHit& hit : AsHits(locations, symbol, lineText))
		{
			m_Groups.Add(std::move(hit));
		}
	}

	void ReferencesPanel::Focus()
	{
		SetVisible(true);

		// The same name PanelManager gives the window, because that is what ImGui knows it by -
		// see SearchPanel::Focus for the same reasoning.
		const std::string window = GetWindowName();

		ImGui::SetWindowFocus(window.c_str());
	}

	void ReferencesPanel::RenderSummary()
	{
		std::string summary = "References to " + m_Symbol;

		if (m_Working)
		{
			summary += "  (working...)";
		}
		else
		{
			summary += ": " + std::to_string(m_Groups.GetCount()) + " hits";
		}

		ImGui::TextUnformatted(summary.c_str());
	}

	void ReferencesPanel::RenderResults()
	{
		const std::function<void(const SearchHit&)> onOpen = [this](const SearchHit& hit)
		{ OpenAndSelect(hit); };

		const std::function<void(HitGroup&)> onGroupOpened = [this](HitGroup& group)
		{ ResolveGroup(group); };

		DrawHitList(HitListInput{.Groups = m_Groups.Get(),
								 .Selected = m_Selected,
								 .OnOpen = onOpen,
								 .OnGroupOpened = &onGroupOpened});
	}

	// Only reached with a selection: OnImGuiRender's split leaves no preview pane without one.
	void ReferencesPanel::RenderPreview()
	{
		// Looked up by id rather than remembered as a pointer: groups are appended to as results
		// arrive, and the vector holding them reallocates.
		HitGroup* group = nullptr;

		for (HitGroup& candidate : m_Groups.Get())
		{
			if (candidate.Id == m_Selected->Group)
			{
				group = &candidate;
				break;
			}
		}

		// The results were cleared or regrouped under the selection. Dropping it here is what
		// takes the pane away again on the next frame.
		if (group == nullptr || m_Selected->Hit >= group->Hits.size())
		{
			m_Selected.reset();
			m_Preview.Clear();
			return;
		}

		// The row that was picked is drawn, so its group was expanded and is already resolved.
		// Asked again anyway, because the preview highlights every hit in the file rather than
		// only the selected one, and a row's byte range is what says where to draw that highlight.
		ResolveGroup(*group);

		const SearchHit& hit = group->Hits[m_Selected->Hit];

		// Before Show, not after: Show is what declares the file to the server, and the first
		// declaration of a newly previewed file has to carry the user's tab width rather than
		// Core::DefaultTabWidth — DrawSearchPreview's own SetTabWidth runs too late for that
		// first frame, so it would go out at the wrong width and be discarded next frame anyway.
		m_Preview.SetTabWidth(m_Editor.GetIndentStyle().Width);

		// Every frame, not only when the selection changes: the file may have been opened,
		// closed or edited since the last one, and Show is cheap when nothing moved.
		m_Preview.Show(hit.File, m_Documents);

		DrawSearchPreview(m_Preview,
						  SearchPreviewInput{.Hits = group->Hits,
											 .Current = m_Selected->Hit,
											 .Scheme = m_Scheme.Get(),
											 .TabWidth = m_Editor.GetIndentStyle().Width,
											 .FontSize = m_Editor.GetFontSize()});
	}

	void ReferencesPanel::OpenAndSelect(const SearchHit& hit)
	{
		// Opened permanently, not as a preview tab: like DefinitionNavigation::Jump, this is
		// somewhere the user meant to go, not a file they are skimming past.
		m_Documents.Open(hit.File, true);

		const auto index = m_Documents.Find(hit.File);

		if (!index)
		{
			return;
		}

		Core::OpenItem& item = m_Documents.At(*index);

		if (!item.Document)
		{
			return;
		}

		Core::TextBuffer& buffer = item.Document->GetBuffer();
		const std::size_t lineStart = buffer.LineStart(hit.Line);

		// The range comes from a reply that may be some frames old; the file may have changed
		// since. PlaceAt runs every offset through SnapToBoundary, which clamps to the buffer's
		// current length, so a stale range lands at the nearest valid position instead of
		// reading past the end of a document that has since shrunk.
		item.Caret = Core::PlaceAt(buffer, item.Caret, lineStart + hit.From, false, item.TabWidth);
		item.Caret = Core::PlaceAt(buffer, item.Caret, lineStart + hit.To, true, item.TabWidth);

		item.RowAimAt = Core::NoRowAim;
		item.PendingScroll = true;
	}

	std::optional<std::string> ReferencesPanel::LineTextInOpenDocument(
		const std::filesystem::path& path, std::size_t line) const
	{
		// An open document's buffer wins over the file on disk, exactly as PreviewContent
		// prefers it: unsaved edits are what the editor shows, and the row above this preview
		// has to agree with it.
		if (const auto index = m_Documents.Find(path))
		{
			const Core::OpenItem& item = m_Documents.At(*index);

			if (item.Document)
			{
				return item.Document->GetBuffer().LineText(line);
			}
		}

		return std::nullopt;
	}

	void ReferencesPanel::ResolveGroup(HitGroup& group)
	{
		for (SearchHit& hit : group.Hits)
		{
			// Already read - by ShowResults, because the file was open, or by an earlier call
			// here. This is what makes calling this on every frame a group stays expanded cost a
			// walk of its hits and nothing else.
			if (!hit.Unread)
			{
				continue;
			}

			hit.Text = LineTextFor(hit.File, hit.Line);

			// The same span rule ShowResults applies, so a row resolved here and a row resolved
			// there highlight identically - which they must, since which of the two ran depends
			// only on whether the file happened to be open.
			const auto [from, to] =
				SpanIn(hit.Text, hit.Line, Utf16Column(hit.Unread->Column), hit.Line,
					   Utf16Column(hit.Unread->EndColumn), m_Symbol);

			hit.From = from;
			hit.To = to;
			hit.Unread.reset();
		}
	}

	std::string ReferencesPanel::LineTextFor(const std::filesystem::path& path, std::size_t line)
	{
		if (const std::optional<std::string> open = LineTextInOpenDocument(path, line))
		{
			return *open;
		}

		// ResolveGroup asks about one file's lines at a time, so the path does not change until
		// every line wanted from it has been asked about - this is what makes the read below
		// happen once per file rather than once per hit.
		if (path != m_DiskPath)
		{
			m_DiskPath = path;
			m_DiskBuffer.reset();

			const Core::Result<Core::FileProbe> probe = Core::ProbeFile(path);

			// Binary and too-large are the only kinds PreviewContent's own disk load skips too -
			// a reference cannot sensibly have come from either, but the probe is what actually
			// knows, not this panel's assumptions about what a language server would have found.
			if (probe && probe.GetValue().Kind != Core::FileKind::Binary &&
				probe.GetValue().Kind != Core::FileKind::TooLarge)
			{
				if (const std::optional<std::string> bytes = ReadAll(path))
				{
					if (const Core::Result<std::string> text =
							Core::Decode(*bytes, probe.GetValue().FileEncoding))
					{
						m_DiskBuffer = Core::TextBuffer{text.GetValue()};
					}
				}
			}
		}

		if (m_DiskBuffer && line < m_DiskBuffer->GetLineCount())
		{
			return m_DiskBuffer->LineText(line);
		}

		return {};
	}
}
