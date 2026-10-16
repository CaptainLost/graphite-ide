#include "Language/Diagnostics/ProblemsPanel.h"

#include "Theme/Palettes.h"
#include "Editor/Navigation/SelectInDocument.h"

#include "Language/Diagnostics/ProblemIdentity.h"
#include "Language/Server/LspProtocol.h"
#include "UI/FittedText.h"
#include "UI/Lists/ListRow.h"
#include "UI/OpenGesture.h"

#include "Core/Base/Strings.h"
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

		constexpr float kStaleAlpha = 0.4f;

		static_assert(DiagnosticSeverity::Error < DiagnosticSeverity::Warning,
					  "IsDisplayedBefore puts an error before a warning at the same position by comparing severities");

		bool IsDisplayedBefore(const Diagnostic* left, const Diagnostic* right)
		{
			if (left->Line != right->Line)
			{
				return left->Line < right->Line;
			}

			if (left->Column.Value != right->Column.Value)
			{
				return left->Column.Value < right->Column.Value;
			}

			return left->Severity < right->Severity;
		}

		std::vector<const Diagnostic*> Sorted(const std::vector<Diagnostic>& items)
		{
			std::vector<const Diagnostic*> sorted;
			sorted.reserve(items.size());

			for (const Diagnostic& item : items)
			{
				sorted.push_back(&item);
			}

			std::sort(sorted.begin(), sorted.end(), IsDisplayedBefore);

			return sorted;
		}
	}

	ProblemsPanel::ProblemsPanel(const DiagnosticsModel& diagnostics, Core::DocumentSet& documents, Workspace& workspace,
								 const ActiveScheme& scheme)
		: Core::Panel("problems", "Problems", "tools", ICON_MDI_ALERT_CIRCLE_OUTLINE), m_Diagnostics(diagnostics), m_Documents(documents),
		  m_Workspace(workspace), m_Scheme(scheme)
	{
	}

	void ProblemsPanel::OnImGuiRender()
	{
		const auto isOpenDocument = [this](const std::filesystem::path& path) { return IsOpenDocument(path); };

		const DiagnosticsModel::Counts counts = m_Diagnostics.Total(isOpenDocument);

		if (counts.Errors == 0 && counts.Warnings == 0)
		{
			Core::EmptyState("No problems in open documents.");
			m_Selected.reset();
			return;
		}

		m_SelectionSeen = false;

		const EditorPalette palette = ReadEditorPalette(m_Scheme.Get());

		// WARNING: In a child of its own, which has no padding, so the rows' highlights reach the list's edges.
		if (ImGui::BeginChild("##problemList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened))
		{
			for (const auto& [path, entry] : m_Diagnostics.GetDocuments())
			{
				if (entry.Items.empty() || !isOpenDocument(path))
				{
					continue;
				}

				RenderFile(path, entry, palette);
			}
		}

		ImGui::EndChild();

		if (!m_SelectionSeen)
		{
			m_Selected.reset();
		}
	}

	void ProblemsPanel::RenderFile(const std::filesystem::path& path,
								   const DiagnosticsModel::Entry& entry,
								   const EditorPalette& palette)
	{
		const bool stale = m_Diagnostics.IsStale(path);

		if (stale)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, kStaleAlpha);
		}

		// WARNING: path.c_str() is const wchar_t* on Windows and would bind PushID's const void*
		// overload, hashing the pointer instead of the text.
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
		ImGui::TextDisabled("(%s, %s)", Core::Counted(errors, "error").c_str(), Core::Counted(warnings, "warning").c_str());

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

		const std::string label = std::to_string(diagnostic.Line + 1) + ":" +
								  std::to_string(diagnostic.Column.Value + 1) + "  " +
								  diagnostic.Message;

		const bool selected = m_Selected.has_value() && IsSameProblem(*m_Selected, path, diagnostic);

		if (selected)
		{
			m_SelectionSeen = true;
		}

		const ImVec2 rowStart = ImGui::GetCursorPos();
		const bool clicked = ListRowSelectable("##problem", selected,
											   ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap);

		if (!selected && ImGui::IsItemFocused())
		{
			m_Selected = IdentityOf(path, diagnostic);
			m_SelectionSeen = true;
		}

		if (clicked)
		{
			m_Selected = IdentityOf(path, diagnostic);
			m_SelectionSeen = true;

			OpenAndSelect(path, diagnostic, OpensPermanently());
		}

		// WARNING: Drawn over the row rather than as the Selectable's label, which would hide a message from a "##"
		// in it on, and leave the icon outside the row's highlight.
		ImGui::SetCursorPos(rowStart);
		ImGui::PushStyleColor(ImGuiCol_Text, colour);
		ImGui::TextUnformatted(icon);
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::TextUnformatted(FitToWidth(label, ListRowEndPosX() - ImGui::GetCursorPosX()).c_str());

		ImGui::PopID();
	}

	void ProblemsPanel::OpenAndSelect(const std::filesystem::path& path,
									  const Diagnostic& diagnostic, bool permanent)
	{
		const auto openFile = [this, permanent](const std::filesystem::path& file) {
			m_Workspace.RequestOpenFile(file, permanent);
		};

		SelectInDocument(m_Documents, path, diagnostic.Line, diagnostic.Column, diagnostic.EndLine,
						 diagnostic.EndColumn, openFile);
	}

	bool ProblemsPanel::IsOpenDocument(const std::filesystem::path& path) const
	{
		return m_Documents.Find(path).has_value();
	}
}
