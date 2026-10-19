#include "Search/ReferencesPanel.h"

#include "Editor/Navigation/SelectInDocument.h"
#include "Search/SearchPreview.h"
#include "UI/SplitResults.h"

#include "Language/Server/LspProtocol.h"

#include "Core/ImGui/EmptyState.h"
#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"

#include "Workspace/Workspace.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace Graphite {

	namespace {

		constexpr bool OpenAsPermanentTab = true;

		struct HighlightSpan
		{
			std::size_t Start = 0;
			std::size_t End = 0;
		};

		std::string_view UnqualifiedName(const std::string& symbol)
		{
			const std::string_view scopeOperator = "::";
			const std::size_t separator = symbol.rfind(scopeOperator);

			if (separator == std::string::npos)
			{
				return std::string_view{symbol};
			}

			return std::string_view{symbol}.substr(separator + scopeOperator.size());
		}

		// WARNING: The server's range is overridden when it does not cover the name. clangd answers
		// a constructor reached through a qualified name with the namespace's bytes, not the
		// constructor's, so the name is searched for at or after where the server pointed.
		HighlightSpan SpanIn(const std::string& text, std::size_t line, Utf16Column column,
							 std::size_t endLine, Utf16Column endColumn, const std::string& symbol)
		{
			const std::size_t start = std::min(FromUtf16Column(text, column), text.size());
			const std::string_view name = UnqualifiedName(symbol);

			const bool spansOneLine = SpansOneLine(line, column, endLine, endColumn);
			const std::size_t serverEnd = std::min(FromUtf16Column(text, endColumn), text.size());
			const std::size_t guessedEnd = std::min(start + name.size(), text.size());
			const std::size_t end = spansOneLine ? serverEnd : guessedEnd;

			const std::string_view highlighted = std::string_view{text}.substr(start, end - start);

			if (highlighted == name)
			{
				return HighlightSpan{.Start = start, .End = end};
			}

			const std::size_t found = text.find(name, start);

			if (found != std::string::npos)
			{
				return HighlightSpan{.Start = found, .End = found + name.size()};
			}

			return HighlightSpan{.Start = start, .End = end};
		}

		bool LocationComesBefore(const Location& first, const Location& second)
		{
			if (first.Path != second.Path)
			{
				return first.Path < second.Path;
			}

			if (first.Line != second.Line)
			{
				return first.Line < second.Line;
			}

			return first.Column.Value < second.Column.Value;
		}

		bool SameLocation(const Location& first, const Location& second)
		{
			return first.Path == second.Path && first.Line == second.Line &&
				   first.Column.Value == second.Column.Value;
		}
	}

	std::vector<SearchHit> AsHits(const std::vector<Location>& locations, const std::string& symbol,
								  const LineTextSource& lineText)
	{
		std::vector<Location> sorted = locations;

		std::sort(sorted.begin(), sorted.end(), LocationComesBefore);
		sorted.erase(std::unique(sorted.begin(), sorted.end(), SameLocation), sorted.end());

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

				const HighlightSpan span = SpanIn(hit.Text, location.Line, location.Column,
												  location.EndLine, location.EndColumn, symbol);

				hit.From = span.Start;
				hit.To = span.End;
			}
			else
			{
				hit.Unread = SearchHit::UnreadSpan{location.Column.Value, location.EndColumn.Value};
			}

			hits.push_back(std::move(hit));
		}

		return hits;
	}

	ReferencesPanel::ReferencesPanel(Workspace& workspace, Core::DocumentSet& documents,
									 const ActiveScheme& scheme, const EditorSettings& editor,
									 PreviewSemantics& semantics)
		: Core::Panel("references", "References", "navigate", ICON_LC_LINK, StartsAs::Hidden),
		  m_Workspace(workspace), m_Documents(documents), m_Scheme(scheme), m_Editor(editor),
		  m_Preview(&semantics)
	{
	}

	void ReferencesPanel::OnImGuiRender()
	{
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

		m_DiskPath.clear();
		m_DiskBuffer.reset();

		const LineTextSource lineText = [this](const std::filesystem::path& path, std::size_t line) {
			return LineTextInOpenDocument(path, line);
		};

		for (SearchHit& hit : AsHits(locations, symbol, lineText))
		{
			m_Groups.Add(std::move(hit));
		}
	}

	void ReferencesPanel::Focus()
	{
		SetVisible(true);

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
		const std::function<void(const SearchHit&)> onOpen = [this](const SearchHit& hit) {
			OpenAndSelect(hit);
		};

		const std::function<void(HitGroup&)> onGroupOpened = [this](HitGroup& group) {
			ResolveGroup(group);
		};

		DrawHitList(HitListInput{.Groups = m_Groups.Get(),
								 .Selected = m_Selected,
								 .OnOpen = onOpen,
								 .OnGroupOpened = &onGroupOpened});
	}

	void ReferencesPanel::RenderPreview()
	{
		HitGroup* group = nullptr;

		for (HitGroup& candidate : m_Groups.Get())
		{
			if (candidate.Id == m_Selected->Group)
			{
				group = &candidate;
				break;
			}
		}

		if (group == nullptr || m_Selected->Hit >= group->Hits.size())
		{
			m_Selected.reset();
			m_Preview.Clear();
			return;
		}

		ResolveGroup(*group);

		const SearchHit& hit = group->Hits[m_Selected->Hit];

		// WARNING: SetTabWidth must run before Show, because Show declares the file to the server
		// and the first declaration has to carry the user's tab width.
		m_Preview.SetTabWidth(m_Editor.GetIndentStyle().Width);
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
		SelectInDocument(m_Documents, hit.File, hit.Line, hit.From, hit.To,
						 [this](const std::filesystem::path& file) {
							 m_Workspace.RequestOpenFile(file, OpenAsPermanentTab);
						 });
	}

	std::optional<std::string> ReferencesPanel::LineTextInOpenDocument(
		const std::filesystem::path& path, std::size_t line) const
	{
		const Core::TextBuffer* buffer = Core::BufferOfOpenDocument(m_Documents, path);

		if (buffer == nullptr)
		{
			return std::nullopt;
		}

		return buffer->LineText(line);
	}

	void ReferencesPanel::ResolveGroup(HitGroup& group)
	{
		for (SearchHit& hit : group.Hits)
		{
			if (!hit.Unread)
			{
				continue;
			}

			hit.Text = LineTextFor(hit.File, hit.Line);

			const HighlightSpan span =
				SpanIn(hit.Text, hit.Line, Utf16Column(hit.Unread->Column), hit.Line,
					   Utf16Column(hit.Unread->EndColumn), m_Symbol);

			hit.From = span.Start;
			hit.To = span.End;
			hit.Unread.reset();
		}
	}

	std::string ReferencesPanel::LineTextFor(const std::filesystem::path& path, std::size_t line)
	{
		if (const std::optional<std::string> openText = LineTextInOpenDocument(path, line))
		{
			return *openText;
		}

		if (path != m_DiskPath)
		{
			m_DiskPath = path;
			m_DiskBuffer.reset();

			Core::DiskText read = Core::ReadDiskText(path);

			if (read.Outcome == Core::DiskTextOutcome::Read)
			{
				m_DiskBuffer = Core::TextBuffer{std::move(read.Text)};
			}
		}

		if (m_DiskBuffer && line < m_DiskBuffer->GetLineCount())
		{
			return m_DiskBuffer->LineText(line);
		}

		return {};
	}
}
