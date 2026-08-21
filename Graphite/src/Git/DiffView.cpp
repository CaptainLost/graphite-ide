#include "Git/DiffView.h"

#include "Git/CommitLabels.h"
#include "Git/DiffPanes.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include <IconsMaterialDesignIcons.h>

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

		// Last, and placed outright rather than after a SameLine, so it sits at the pane's
		// right edge however many buttons precede it. The reader's eye looks for a close in
		// the corner, not at the end of a row of toggles.
		if (OnClose)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float width =
				ImGui::CalcTextSize(ICON_MDI_CLOSE).x + style.FramePadding.x * 2.0f;

			ImGui::SameLine(ImGui::GetWindowWidth() - width - style.WindowPadding.x);

			if (ImGui::SmallButton(ICON_MDI_CLOSE))
			{
				OnClose();
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
		// The widest line is measured from the rows that have been on screen, for the reason
		// ImGui itself derives a window's content size from the items it saw: the widest line
		// among the rows nobody has scrolled to is not known without laying the whole file out,
		// which is the work the clipper exists to avoid.
		//
		// It is kept and only ever grown, rather than rebuilt each frame from what is visible,
		// because rebuilding shrinks the scroll range the moment the reader scrolls down past
		// the long line they came to read — and ImGui then clamps their position back to the
		// left. Keeping it means knowing when it stops applying, which is what the measurement
		// answers; see its own comment for why the path alone does not.
		if (const DiffMeasurement measurement = MeasurementOf(file, m_ShowWholeFile);
			measurement != m_WidestFor)
		{
			m_WidestFor = measurement;
			m_WidestSideLine = 0.0f;
		}

		// Last frame's width, because the content size has to be declared before the region
		// opens and the region's own width is not known until it has: it takes a border and,
		// once the rows stop fitting, a scrollbar off whatever it was given.
		const float innerWidth = m_SideInnerWidth > 0.0f
									 ? m_SideInnerWidth + ImGui::GetStyle().ChildBorderSize * 2.0f
									 : ImGui::GetContentRegionAvail().x;
		const float padding = ImGui::GetStyle().WindowPadding.x;

		ImGui::SetNextWindowContentSize(
			ImVec2(SideBySideContentWidth(innerWidth, m_WidestSideLine, padding), 0.0f));

		// One region for both halves, so there is one scroll position rather than two that have
		// to be kept equal; see SideBySidePanes for why that is worth the shared scrollbar.
		if (!ImGui::BeginChild("pairs", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
							   ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::EndChild();
			return;
		}

		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont());

		const float numberWidth =
			ImGui::CalcTextSize("0").x * kLineNumberColumns + kNumberPaddingX * 2.0f;
		const float rowHeight = ImGui::GetTextLineHeightWithSpacing();

		// The region's own clipping rectangle is what the reader can actually see — the border
		// and any scrollbar already taken off — so the split lands halfway across that rather
		// than halfway across what this was asked to fit into. It measures the visible area, so
		// a region the parent had clipped would be measured as the sliver that showed; nothing
		// lays this one out that way, and BeginChild sizing it to fit is what keeps that true.
		const ImVec2 innerMin = ImGui::GetWindowDrawList()->GetClipRectMin();
		const ImVec2 innerMax = ImGui::GetWindowDrawList()->GetClipRectMax();

		m_SideInnerWidth = innerMax.x - innerMin.x;

		// The panes are pinned to the region rather than to the content: the text inside them
		// slides as the reader scrolls across, while the split between them stays where it is.
		const SideBySidePanes panes = SplitIntoPanes(m_SideInnerWidth);

		float widest = 0.0f;

		// Draws one half of every row of a clipper step under a single clipping rectangle.
		// Per row it would be two rectangles a row, and ImGui starts a fresh draw command each
		// time one is pushed over anything already drawn — sixty rows of alternating halves
		// would cost a hundred and twenty commands where the whole region should cost two.
		const auto drawPane = [&](bool leftSide, int first, int last, const ImVec2& firstRowPos,
								  float firstRowScreenY) {
			const float paneX = leftSide ? 0.0f : panes.RightPaneX;
			const float paneScreenX = innerMin.x + paneX;

			// Clipped to its own half so a long line is reached by scrolling instead of
			// spilling across the split into the half it does not belong to.
			ImGui::PushClipRect(ImVec2(paneScreenX, innerMin.y),
								ImVec2(paneScreenX + panes.PaneWidth, innerMax.y), true);

			for (int index = first; index < last; ++index)
			{
				const std::optional<DiffRow>& row =
					leftSide ? pairs[static_cast<std::size_t>(index)].Left
							 : pairs[static_cast<std::size_t>(index)].Right;

				// The other side changed and this one did not exist. The row still occupies its
				// height — that is reserved once for the whole step, below, and not by whatever
				// either half drew — so the two halves cannot stop lining up.
				if (!row.has_value())
				{
					continue;
				}

				const float offsetY = static_cast<float>(index - first) * rowHeight;
				const ImVec2 rowPos = ImVec2(firstRowPos.x + paneX, firstRowPos.y + offsetY);
				const float rowScreenY = firstRowScreenY + offsetY;

				if (const ImU32 band = BandFor(row->Kind, palette); band != 0)
				{
					ImGui::GetWindowDrawList()->AddRectFilled(
						ImVec2(paneScreenX, rowScreenY),
						ImVec2(paneScreenX + panes.PaneWidth,
							   rowScreenY + ImGui::GetTextLineHeight()),
						band);
				}

				if (row->Kind == DiffRowKind::HunkHeader)
				{
					const std::string& header = file.Hunks[row->Hunk].Header;

					ImGui::SetCursorPos(rowPos);
					ImGui::TextUnformatted(header.c_str());

					// Measured like any other line: libgit2 puts the enclosing function's
					// signature in the header, which is routinely the widest thing here, and
					// leaving it out would put its tail beyond anything scrolling could reach.
					// It starts at the half's edge rather than past the line numbers.
					widest = std::max(widest, ImGui::CalcTextSize(header.c_str()).x);
					continue;
				}

				// Positioned explicitly from the row's own start rather than with SameLine(),
				// matching the unified view — a blank line number then costs nothing instead
				// of leaving the text column's position dependent on it.
				DrawNumber(rowPos.x, rowPos.y, numberWidth,
						   leftSide ? row->OldLine : row->NewLine);

				const ResolvedLine resolved =
					ResolveRow(file, *row, leftSide ? DiffSide::Old : DiffSide::New, m_OldLines,
							   m_NewLines, tokens);

				if (!resolved.Text.has_value())
				{
					continue;
				}

				DrawLine(ImVec2(rowPos.x + numberWidth, rowPos.y), *resolved.Text,
						 m_ColourSyntax ? resolved.Tokens : nullptr, TextFor(row->Kind, palette),
						 syntax);

				widest = std::max(widest, numberWidth +
											  ImGui::CalcTextSize(resolved.Text->data(),
																  resolved.Text->data() +
																	  resolved.Text->size())
												  .x);
			}

			ImGui::PopClipRect();
		};

		// Told the row height explicitly for the same reason as the unified view's clipper:
		// measuring it from a first item is exactly what a cursor-rewind bug would corrupt.
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(pairs.size()), rowHeight);

		while (clipper.Step())
		{
			const ImVec2 firstRowPos = ImGui::GetCursorPos();
			const float firstRowScreenY = ImGui::GetCursorScreenPos().y;
			const int rows = clipper.DisplayEnd - clipper.DisplayStart;

			drawPane(true, clipper.DisplayStart, clipper.DisplayEnd, firstRowPos, firstRowScreenY);
			drawPane(false, clipper.DisplayStart, clipper.DisplayEnd, firstRowPos, firstRowScreenY);

			// The step's rows reserved as one item rather than by moving the cursor: a bare
			// SetCursorPos past the last item is how a region gets extended behind ImGui's
			// back, and it asserts on the last row of the frame for exactly that. Submitting it
			// decides the height outright, so rows with nothing on either side are as tall as
			// rows with text on both. The spacing comes off because ItemSize adds it back.
			ImGui::SetCursorPos(firstRowPos);
			ImGui::Dummy(ImVec2(0.0f, static_cast<float>(rows) * rowHeight -
										  ImGui::GetStyle().ItemSpacing.y));
		}

		m_WidestSideLine = std::max(m_WidestSideLine, widest);

		// Drawn after the halves and outside their clipping rectangles, so it is not cut off by
		// either and does not slide when the reader scrolls across. A filled rectangle rather
		// than a line, which AddLine would centre on the split and so lay half of over the
		// right half's first pixel.
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(innerMin.x + panes.RightPaneX, innerMin.y),
												  ImVec2(innerMin.x + panes.RightPaneX + 1.0f,
														 innerMax.y),
												  ImGui::GetColorU32(ImGuiCol_Border));

		ImGui::PopFont();
		ImGui::EndChild();
	}
}
