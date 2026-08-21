#include "Git/DiffView.h"

#include "Git/CommitLabels.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include <imgui.h>

#include <algorithm>
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

		// Draws one line as a run per token plus the whitespace between them, or as a single
		// run when there is nothing to colour it with. Every run is positioned explicitly from
		// `origin` rather than with SameLine() — a run that draws nothing (an empty gap before
		// two adjacent tokens) would otherwise rewind the cursor and overwrite the row above,
		// which is the exact bug this file's history records.
		void DrawLine(const ImVec2& origin, std::string_view text,
					  const std::vector<Token>* tokens, ImU32 ordinaryColour,
					  const SyntaxPalette& syntax, bool colourOn)
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

			if (!colourOn || tokens == nullptr || tokens->empty())
			{
				drawRun(0, text.size(), ordinaryColour);
				return;
			}

			std::size_t cursor = 0;
			bool drewAnything = false;

			for (const Token& token : *tokens)
			{
				if (cursor >= text.size())
				{
					break;
				}

				// Clamped rather than trusted: the token came from lexing `text` in the first
				// place, so From/To should already fit, but a defensive clamp turns a mismatch
				// into a truncated colouring instead of a substr throwing mid-frame.
				const std::size_t from = std::min(token.From, text.size());
				const std::size_t to = std::min(token.To, text.size());

				if (from > cursor)
				{
					drawRun(cursor, from, ordinaryColour);
					drewAnything = true;
				}

				if (to > from)
				{
					drawRun(from, to, syntax.Of(token.Kind));
					drewAnything = true;
				}

				cursor = to;
			}

			if (cursor < text.size())
			{
				drawRun(cursor, text.size(), ordinaryColour);
				drewAnything = true;
			}

			// Unreachable today — CppLexer::Emit rejects an empty span, so an empty line always
			// yields an empty token vector, which the branch above already catches — but if a
			// future lexer ever emitted only zero-length tokens for a non-empty line, or ran
			// against an empty line without one, every branch above would draw nothing at all:
			// no ItemSize, so the cursor never advances and the next row draws on top of this
			// one. The same defect class the brief's SameLine() warning is about, reached by a
			// different route; guaranteeing at least one run closes it.
			if (!drewAnything)
			{
				drawRun(0, text.size(), ordinaryColour);
			}
		}
	}

	void DiffView::Render(const Core::FileDiff& file, const GitPalette& palette,
						  const SyntaxPalette& syntax, const std::shared_ptr<const FileTexts>& texts,
						  const FileTokens& tokens)
	{
		RenderToolbar(file);

		if (file.IsBinary)
		{
			// libgit2 emits no hunks for a binary delta, so there is nothing to lay out.
			// Saying so is better than an empty pane that looks like a failure.
			ImGui::TextUnformatted("Binary file — no textual diff.");
			return;
		}

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

	DiffView::ResolvedLine DiffView::Resolve(const Core::FileDiff& file, const DiffRow& row,
											 const FileTokens& tokens) const
	{
		if (row.Kind == DiffRowKind::Unchanged)
		{
			// Both sides carry identical content for an Unchanged row — it exists only where
			// the diff left a line alone — so New is preferred simply because it is always
			// present whenever an Unchanged row is: a file with no new side is entirely one
			// "added" hunk and produces no Unchanged rows for this branch to see.
			if (row.NewLine > 0 && static_cast<std::size_t>(row.NewLine) <= m_NewLines.size())
			{
				const std::size_t line = static_cast<std::size_t>(row.NewLine) - 1;
				const std::vector<Token>* lineTokens =
					line < tokens.New.size() ? &tokens.New[line] : nullptr;
				return ResolvedLine{m_NewLines[line], lineTokens};
			}

			if (row.OldLine > 0 && static_cast<std::size_t>(row.OldLine) <= m_OldLines.size())
			{
				const std::size_t line = static_cast<std::size_t>(row.OldLine) - 1;
				const std::vector<Token>* lineTokens =
					line < tokens.Old.size() ? &tokens.Old[line] : nullptr;
				return ResolvedLine{m_OldLines[line], lineTokens};
			}

			return ResolvedLine{};
		}

		const Core::DiffLine* line = LineOf(file, row);

		if (line == nullptr)
		{
			return ResolvedLine{};
		}

		const std::string_view text = Trimmed(*line);

		// A removed line exists only on the old side and an added line only on the new, so
		// this picks whichever side the diff itself says has the line; a context line has both
		// and prefers New for the same reason an Unchanged row does.
		const std::vector<Token>* lineTokens = nullptr;

		if (row.NewLine > 0 && static_cast<std::size_t>(row.NewLine) <= tokens.New.size())
		{
			lineTokens = &tokens.New[static_cast<std::size_t>(row.NewLine) - 1];
		}
		else if (row.OldLine > 0 && static_cast<std::size_t>(row.OldLine) <= tokens.Old.size())
		{
			lineTokens = &tokens.Old[static_cast<std::size_t>(row.OldLine) - 1];
		}

		return ResolvedLine{text, lineTokens};
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

				const ResolvedLine resolved = Resolve(file, row, tokens);

				if (resolved.Text.has_value())
				{
					DrawLine(ImVec2(rowPos.x + numberWidth * 2.0f, rowPos.y), *resolved.Text,
							 resolved.Tokens, TextFor(row.Kind, palette), syntax, m_ColourSyntax);
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

					const ResolvedLine resolved = Resolve(file, *row, tokens);

					if (resolved.Text.has_value())
					{
						DrawLine(ImVec2(rowPos.x + numberWidth, rowPos.y), *resolved.Text,
								 resolved.Tokens, TextFor(row->Kind, palette), syntax,
								 m_ColourSyntax);
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
