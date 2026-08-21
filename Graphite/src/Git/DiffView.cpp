#include "Git/DiffView.h"

#include "Git/CommitLabels.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include <imgui.h>

#include <string>

namespace Graphite {

	namespace {

		constexpr float kLineNumberColumns = 5.0f;
		constexpr float kNumberPaddingX = 6.0f;

		ImU32 BandFor(DiffRowKind kind, const GitPalette& palette)
		{
			switch (kind)
			{
				case DiffRowKind::Added: return palette.DiffAddedLine;
				case DiffRowKind::Removed: return palette.DiffRemovedLine;
				case DiffRowKind::HunkHeader:
				case DiffRowKind::Context:
				case DiffRowKind::Unchanged: break;
			}

			return 0;
		}

		ImU32 TextFor(DiffRowKind kind, const GitPalette& palette)
		{
			switch (kind)
			{
				case DiffRowKind::Added: return palette.DiffAddedText;
				case DiffRowKind::Removed: return palette.DiffRemovedText;
				case DiffRowKind::HunkHeader:
				case DiffRowKind::Context:
				case DiffRowKind::Unchanged: break;
			}

			return ImGui::GetColorU32(ImGuiCol_Text);
		}

		// The text without its line terminator. Drawing the terminator puts a stray glyph
		// at the end of every line in fonts that have one.
		std::string_view Trimmed(const Core::DiffLine& line)
		{
			std::string_view text = line.Text;

			while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
			{
				text.remove_suffix(1);
			}

			return text;
		}

		void DrawNumber(float x, float y, float width, int number)
		{
			if (number <= 0)
			{
				return;
			}

			const std::string text = std::to_string(number);
			const float textWidth = ImGui::CalcTextSize(text.c_str()).x;

			ImGui::SetCursorPos(ImVec2(x + width - textWidth - kNumberPaddingX, y));
			ImGui::TextUnformatted(text.c_str());
		}
	}

	void DiffView::Render(const Core::FileDiff& file, const GitPalette& palette)
	{
		RenderToolbar(file);

		if (file.IsBinary)
		{
			// libgit2 emits no hunks for a binary delta, so there is nothing to lay out.
			// Saying so is better than an empty pane that looks like a failure.
			ImGui::TextUnformatted("Binary file — no textual diff.");
			return;
		}

		if (file.Hunks.empty())
		{
			ImGui::TextUnformatted("No changes in this file.");
			return;
		}

		if (m_Mode == DiffViewMode::Unified)
		{
			RenderUnified(file, palette);
			return;
		}

		RenderSideBySide(file, palette);
	}

	void DiffView::RenderToolbar(const Core::FileDiff& file)
	{
		const std::filesystem::path& path = ShownPath(file);
		ImGui::TextUnformatted(path.string().c_str());

		ImGui::SameLine();

		if (ImGui::SmallButton(m_Mode == DiffViewMode::SideBySide ? "Unified" : "Side by side"))
		{
			m_Mode = m_Mode == DiffViewMode::SideBySide ? DiffViewMode::Unified
														: DiffViewMode::SideBySide;
		}

		// Absent inside a tab: OnOpenAsTab is left unset there, so a diff already open as a
		// tab offers no way to open a second copy of itself.
		if (OnOpenAsTab)
		{
			ImGui::SameLine();

			if (ImGui::SmallButton("Open as tab"))
			{
				OnOpenAsTab();
			}
		}

		ImGui::Separator();
	}

	void DiffView::RenderUnified(const Core::FileDiff& file, const GitPalette& palette)
	{
		const std::vector<DiffRow> rows = BuildUnifiedRows(file);

		if (!ImGui::BeginChild("unified", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
							   ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::EndChild();
			return;
		}

		// The diff's own text, not the UI's — the same reasoning that puts the editor and
		// the search preview in the mono font applies to a line of a hunk.
		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont());

		const float numberWidth =
			ImGui::CalcTextSize("0").x * kLineNumberColumns + kNumberPaddingX * 2.0f;
		const float rowHeight = ImGui::GetTextLineHeightWithSpacing();

		// Only the visible rows are laid out. A diff of a generated file runs to tens of
		// thousands of lines and drawing them all costs the frame. The clipper is told the
		// row height explicitly rather than measuring it from a first item, because that
		// first item's own layout is exactly what a cursor-rewind bug would corrupt.
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(rows.size()), rowHeight);

		while (clipper.Step())
		{
			for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
			{
				const DiffRow& row = rows[static_cast<std::size_t>(index)];

				// Each column is positioned explicitly from the row's own start rather than
				// with SameLine(), so a column that draws nothing (a blank line number) never
				// leaves a later column depending on it having existed.
				const ImVec2 rowPos = ImGui::GetCursorPos();
				const ImVec2 start = ImGui::GetCursorScreenPos();
				const float height = ImGui::GetTextLineHeight();

				if (const ImU32 band = BandFor(row.Kind, palette); band != 0)
				{
					ImGui::GetWindowDrawList()->AddRectFilled(
						start, ImVec2(start.x + ImGui::GetContentRegionAvail().x, start.y + height),
						band);
				}

				if (row.Kind == DiffRowKind::HunkHeader)
				{
					ImGui::SetCursorPos(rowPos);
					ImGui::TextUnformatted(file.Hunks[row.Hunk].Header.c_str());
					continue;
				}

				DrawNumber(rowPos.x, rowPos.y, numberWidth, row.OldLine);
				DrawNumber(rowPos.x + numberWidth, rowPos.y, numberWidth, row.NewLine);
				ImGui::SetCursorPos(ImVec2(rowPos.x + numberWidth * 2.0f, rowPos.y));

				if (const Core::DiffLine* line = LineOf(file, row); line != nullptr)
				{
					const std::string_view text = Trimmed(*line);
					ImGui::PushStyleColor(ImGuiCol_Text, TextFor(row.Kind, palette));
					ImGui::TextUnformatted(text.data(), text.data() + text.size());
					ImGui::PopStyleColor();
				}
				else
				{
					ImGui::NewLine();
				}
			}
		}

		ImGui::PopFont();
		ImGui::EndChild();
	}

	void DiffView::RenderSideBySide(const Core::FileDiff& file, const GitPalette& palette)
	{
		const std::vector<DiffPair> pairs = BuildSideBySideRows(file);

		const float half = ImGui::GetContentRegionAvail().x * 0.5f;

		// One child per side, both scrolled by the same clipper over the same row list, so
		// the two halves cannot drift apart the way two independently scrolled panes would.
		const auto renderHalf = [&](const char* id, bool leftSide) {
			if (!ImGui::BeginChild(id, ImVec2(half, 0.0f), ImGuiChildFlags_Borders,
								   ImGuiWindowFlags_HorizontalScrollbar))
			{
				ImGui::EndChild();
				return;
			}

			ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont());

			const float numberWidth =
				ImGui::CalcTextSize("0").x * kLineNumberColumns + kNumberPaddingX * 2.0f;
			const float rowHeight = ImGui::GetTextLineHeightWithSpacing();

			// Told the row height explicitly for the same reason as the unified view's
			// clipper: measuring it from a first item is exactly what a cursor-rewind bug
			// would corrupt.
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(pairs.size()), rowHeight);

			while (clipper.Step())
			{
				for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
				{
					const DiffPair& pair = pairs[static_cast<std::size_t>(index)];
					const std::optional<DiffRow>& row = leftSide ? pair.Left : pair.Right;

					if (!row.has_value())
					{
						// The other side changed and this one did not exist. The row still
						// occupies its height, or the two halves stop lining up.
						ImGui::NewLine();
						continue;
					}

					// Positioned explicitly from the row's own start rather than with
					// SameLine(), matching the unified view — a blank line number then costs
					// nothing instead of leaving the text column's position dependent on it.
					const ImVec2 rowPos = ImGui::GetCursorPos();
					const ImVec2 start = ImGui::GetCursorScreenPos();
					const float height = ImGui::GetTextLineHeight();

					if (const ImU32 band = BandFor(row->Kind, palette); band != 0)
					{
						ImGui::GetWindowDrawList()->AddRectFilled(
							start,
							ImVec2(start.x + ImGui::GetContentRegionAvail().x, start.y + height),
							band);
					}

					if (row->Kind == DiffRowKind::HunkHeader)
					{
						ImGui::SetCursorPos(rowPos);
						ImGui::TextUnformatted(file.Hunks[row->Hunk].Header.c_str());
						continue;
					}

					DrawNumber(rowPos.x, rowPos.y, numberWidth,
							   leftSide ? row->OldLine : row->NewLine);
					ImGui::SetCursorPos(ImVec2(rowPos.x + numberWidth, rowPos.y));

					if (const Core::DiffLine* line = LineOf(file, *row); line != nullptr)
					{
						const std::string_view text = Trimmed(*line);
						ImGui::PushStyleColor(ImGuiCol_Text, TextFor(row->Kind, palette));
						ImGui::TextUnformatted(text.data(), text.data() + text.size());
						ImGui::PopStyleColor();
					}
					else
					{
						ImGui::NewLine();
					}
				}
			}

			ImGui::PopFont();
			ImGui::EndChild();
		};

		renderHalf("old", true);
		ImGui::SameLine();
		renderHalf("new", false);
	}
}
