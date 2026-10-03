#include "Panels/ProblemsPanel.h"

#include "Editor/Palettes.h"
#include "Editor/SelectInDocument.h"

#include "Language/Server/LspProtocol.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/Icons.h"
#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace Graphite {

	namespace {

		// A stale group's rows are dimmed rather than hidden — see DiagnosticsModel::IsStale's own
		// comment: staleness is never a reason to drop anything, only to mark it. The same alpha
		// TextEditorWidget's squiggle uses for the same state, so a document reads as equally
		// provisional in both places.
		constexpr float kStaleAlpha = 0.4f;

		// Sorted for display: line, then column, then severity, so an error and a warning that
		// share a position put the error first — the more urgent thing wins the tie.
		std::vector<const Diagnostic*> Sorted(const std::vector<Diagnostic>& items)
		{
			std::vector<const Diagnostic*> sorted;
			sorted.reserve(items.size());

			for (const Diagnostic& item : items)
			{
				sorted.push_back(&item);
			}

			std::sort(sorted.begin(), sorted.end(),
					  [](const Diagnostic* a, const Diagnostic* b) {
						  if (a->Line != b->Line)
						  {
							  return a->Line < b->Line;
						  }

						  if (a->Column.Value != b->Column.Value)
						  {
							  return a->Column.Value < b->Column.Value;
						  }

						  // DiagnosticSeverity::Error is 0 and Warning is 1, so the natural order
						  // already puts an error before a warning at the same position.
						  return a->Severity < b->Severity;
					  });

			return sorted;
		}
	}

	ProblemsPanel::ProblemsPanel(const DiagnosticsModel& diagnostics, Core::DocumentSet& documents,
								 const ActiveScheme& scheme)
		: Core::Panel("problems", "Problems", "tools", ICON_MDI_ALERT_CIRCLE_OUTLINE), m_Diagnostics(diagnostics), m_Documents(documents),
		  m_Scheme(scheme)
	{
	}

	void ProblemsPanel::OnImGuiRender()
	{
		// The filter that makes this a list of open documents rather than of everything the model
		// has ever heard about: a preview pane or a warm-up probe leaves an entry here with no tab
		// behind it, and Find is the only thing that tells the two apart. The status bar's own
		// count is built from this exact predicate too - see DiagnosticsModel::Total's comment -
		// so a reader can always reconcile the number there with the rows drawn here.
		const auto isOpenDocument = [this](const std::filesystem::path& path) { return m_Documents.Find(path).has_value(); };

		const DiagnosticsModel::Counts counts = m_Diagnostics.Total(isOpenDocument);

		if (counts.Errors == 0 && counts.Warnings == 0)
		{
			// Not a bare window: an empty problems list is a state worth naming, the same way an
			// empty search result is.
			Core::EmptyState("No problems in open documents.");
			return;
		}

		// Read once a frame, like every other panel that draws from a scheme, rather than once
		// per row: every row this frame draws from the same colours.
		const EditorPalette palette = ReadEditorPalette(m_Scheme.Get());

		for (const auto& [path, entry] : m_Diagnostics.GetDocuments())
		{
			if (entry.Items.empty() || !isOpenDocument(path))
			{
				continue;
			}

			RenderFile(path, entry, palette);
		}
	}

	void ProblemsPanel::RenderFile(const std::filesystem::path& path,
								   const DiagnosticsModel::Entry& entry,
								   const EditorPalette& palette)
	{
		const bool stale = m_Diagnostics.IsStale(path);

		// Dims the whole group — header and rows alike — rather than recolouring each row's text:
		// staleness is a property of the document, and a reader should see it as one glance rather
		// than piece it together row by row.
		if (stale)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, kStaleAlpha);
		}

		// path.c_str() is const wchar_t* on Windows, which binds PushID's const void* overload
		// and hashes the pointer rather than the text - unique and stable by accident, and a
		// different overload entirely on a POSIX build. Push the path's actual text instead.
		ImGui::PushID(Core::ToUtf8(path).c_str());

		std::size_t errors = 0;
		std::size_t warnings = 0;

		for (const Diagnostic& item : entry.Items)
		{
			if (item.Severity == DiagnosticSeverity::Error)
			{
				++errors;
			}
			else
			{
				++warnings;
			}
		}

		std::string header =
			std::string(Core::IconForExtension(Core::ToUtf8(path.extension()))) + "  " +
			Core::ToUtf8(path.filename());

		ImGui::TextUnformatted(header.c_str());

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", Core::ToUtf8(path).c_str());
		}

		ImGui::SameLine();
		ImGui::TextDisabled("(%zu error%s, %zu warning%s)", errors, errors == 1 ? "" : "s",
							warnings, warnings == 1 ? "" : "s");

		ImGui::Indent();

		int rowId = 0;

		for (const Diagnostic* diagnostic : Sorted(entry.Items))
		{
			RenderRow(path, *diagnostic, rowId++, palette);
		}

		ImGui::Unindent();

		ImGui::PopID();

		if (stale)
		{
			ImGui::PopStyleVar();
		}
	}

	void ProblemsPanel::RenderRow(const std::filesystem::path& path, const Diagnostic& diagnostic,
								  int rowId, const EditorPalette& palette)
	{
		const bool isError = diagnostic.Severity == DiagnosticSeverity::Error;

		const char* icon = isError ? ICON_MDI_CLOSE_CIRCLE : ICON_MDI_ALERT;
		const ImU32 colour = isError ? palette.DiagnosticError : palette.DiagnosticWarning;

		ImGui::PushID(rowId);

		ImGui::PushStyleColor(ImGuiCol_Text, colour);
		ImGui::TextUnformatted(icon);
		ImGui::PopStyleColor();

		ImGui::SameLine();

		const std::string label = std::to_string(diagnostic.Line + 1) + ":" +
								  std::to_string(diagnostic.Column.Value + 1) + "  " +
								  diagnostic.Message;

		if (ImGui::Selectable(label.c_str()))
		{
			OpenAndSelect(path, diagnostic);
		}

		ImGui::PopID();
	}

	void ProblemsPanel::OpenAndSelect(const std::filesystem::path& path,
									  const Diagnostic& diagnostic)
	{
		// Only reached for a document Find already answered for, so this opens nothing and only
		// activates its tab: there is no size question to ask and no Workspace to ask it
		// through. Permanent, not a preview tab - a problem clicked from this list is somewhere
		// the user meant to go, not a file they are skimming past.
		SelectInDocument(m_Documents, path, diagnostic.Line, diagnostic.Column, diagnostic.EndLine,
						 diagnostic.EndColumn,
						 [this](const std::filesystem::path& file) { m_Documents.Open(file, true); });
	}
}
