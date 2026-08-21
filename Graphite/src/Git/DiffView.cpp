#include "Git/DiffView.h"

#include "Git/CommitLabels.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include <imgui.h>

#include <algorithm>
#include <optional>
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

		// Draws the runs RunsFor works out for the line — which of them there are, and that
		// there is always at least one, is that function's business and is tested there; this
		// only puts them on screen. Every run is positioned explicitly from `origin` rather
		// than with SameLine(): a run that draws nothing (an empty gap before two adjacent
		// tokens) would otherwise rewind the cursor and overwrite the row above, which is the
		// exact bug this file's history records.
		void DrawLine(const ImVec2& origin, std::string_view text,
					  const std::vector<Token>* tokens, ImU32 ordinaryColour,
					  const SyntaxPalette& syntax)
		{
			// Each run's x is measured from `origin` against the whole prefix up to it, not
			// accumulated from the previous run's width: CalcTextSize rounds its result up to a
			// whole pixel, and summing that rounding error run by run drifts the tail of a long,
			// densely tokenised line several pixels right of where the monospaced grid puts it —
			// the same trap already recorded against the editor's own caret, reached here by
			// accumulation instead of by caret math.
			const auto xAt = [&](std::size_t offset) {
				return origin.x + ImGui::CalcTextSize(text.data(), text.data() + offset).x;
			};

			const auto drawRun = [&](std::size_t from, std::size_t to, ImU32 colour) {
				ImGui::SetCursorPos(ImVec2(xAt(from), origin.y));
				ImGui::PushStyleColor(ImGuiCol_Text, colour);
				ImGui::TextUnformatted(text.data() + from, text.data() + to);
				ImGui::PopStyleColor();
			};

			for (const SyntaxRun& run : RunsFor(text, tokens))
			{
				drawRun(run.From, run.To,
						run.Kind.has_value() ? syntax.Of(*run.Kind) : ordinaryColour);
			}
		}
	}

	void DiffView::Render(const Core::FileDiff& file, const GitPalette& palette,
						  const SyntaxPalette& syntax, const std::shared_ptr<const FileTexts>& texts,
						  const FileTokens& tokens)
	{
		RenderToolbar(file);

		if (texts == nullptr)
		{
			// The owner's file changed to one whose texts are not ready (or not asked for), or
			// the previous file's entry was evicted. Either way the cache below is for a file
			// nobody is looking at any more; clearing it here, rather than only inside
			// RebuildLineCache on the next whole-file render, means a large file's texts are
			// not kept alive for the rest of however long it takes the reader to come back to
			// whole-file mode — or forever, if they never do.
			m_CachedTexts.reset();
			m_OldLines.clear();
			m_NewLines.clear();
		}

		// After the clear above, never before it: a binary file's owner hands us no texts, so
		// returning first would leave the previous file's cache in place for as long as the
		// reader stayed on binary files.
		if (file.IsBinary)
		{
			// libgit2 emits no hunks for a binary delta, so there is nothing to lay out.
			// Saying so is better than an empty pane that looks like a failure.
			ImGui::TextUnformatted("Binary file — no textual diff.");
			return;
		}

		// Whole-file mode needs the texts to know how many lines the file has and what an
		// Unchanged row says; while they are still in flight (or the owner has not asked for
		// them at all) this draws the hunks-only view instead, which needs no text and is what
		// the reader already had — so nothing on screen blinks once the texts land.
		if (m_ShowWholeFile && texts != nullptr)
		{
			RebuildLineCache(texts);

			const WholeFileRows rows = BuildWholeFileRows(file, m_OldLines.size(), m_NewLines.size());

			if (m_Mode == DiffViewMode::Unified)
			{
				RenderUnified(rows.Unified, file, palette, syntax, tokens);
			}
			else
			{
				RenderSideBySide(rows.SideBySide, file, palette, syntax, tokens);
			}

			return;
		}

		if (file.Hunks.empty())
		{
			ImGui::TextUnformatted("No changes in this file.");
			return;
		}

		if (m_Mode == DiffViewMode::Unified)
		{
			RenderUnified(BuildUnifiedRows(file), file, palette, syntax, tokens);
			return;
		}

		RenderSideBySide(BuildSideBySideRows(file), file, palette, syntax, tokens);
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

		ImGui::SameLine();

		if (ImGui::SmallButton(m_ShowWholeFile ? "Changes only" : "Whole file"))
		{
			m_ShowWholeFile = !m_ShowWholeFile;
		}

		ImGui::SameLine();

		if (ImGui::SmallButton(m_ColourSyntax ? "Plain text" : "Colour syntax"))
		{
			m_ColourSyntax = !m_ColourSyntax;
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

	void DiffView::RebuildLineCache(const std::shared_ptr<const FileTexts>& texts)
	{
		if (texts.get() == m_CachedTexts.get())
		{
			return;
		}

		m_CachedTexts = texts;
		m_OldLines = texts->HasOld ? SplitTextLines(texts->Old) : std::vector<std::string_view>{};
		m_NewLines = texts->HasNew ? SplitTextLines(texts->New) : std::vector<std::string_view>{};
	}

	void DiffView::RenderUnified(const std::vector<DiffRow>& rows, const Core::FileDiff& file,
								 const GitPalette& palette, const SyntaxPalette& syntax,
								 const FileTokens& tokens)
	{
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

				// One text column standing for both sides, so it resolves for New — which is
				// what this view drew before the two halves could differ.
				const ResolvedLine resolved =
					ResolveRow(file, row, DiffSide::New, m_OldLines, m_NewLines, tokens);

				if (resolved.Text.has_value())
				{
					DrawLine(ImVec2(rowPos.x + numberWidth * 2.0f, rowPos.y), *resolved.Text,
							 m_ColourSyntax ? resolved.Tokens : nullptr,
							 TextFor(row.Kind, palette), syntax);
				}
				else
				{
					ImGui::SetCursorPos(rowPos);
					ImGui::NewLine();
				}
			}
		}

		ImGui::PopFont();
		ImGui::EndChild();
	}

	void DiffView::RenderSideBySide(const std::vector<DiffPair>& pairs, const Core::FileDiff& file,
									const GitPalette& palette, const SyntaxPalette& syntax,
									const FileTokens& tokens)
	{
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

					const ResolvedLine resolved =
						ResolveRow(file, *row, leftSide ? DiffSide::Old : DiffSide::New,
								   m_OldLines, m_NewLines, tokens);

					if (resolved.Text.has_value())
					{
						DrawLine(ImVec2(rowPos.x + numberWidth, rowPos.y), *resolved.Text,
								 m_ColourSyntax ? resolved.Tokens : nullptr,
								 TextFor(row->Kind, palette), syntax);
					}
					else
					{
						ImGui::SetCursorPos(rowPos);
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
