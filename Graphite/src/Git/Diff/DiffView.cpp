#include "Git/Diff/DiffView.h"

#include "Core/Base/Narrow.h"

#include "Core/Base/Base.h"

#include "Git/History/CommitLabels.h"
#include "Git/Diff/DiffPanes.h"
#include "UI/IconButton.h"
#include "UI/SplitResults.h"

#include "Core/ImGui/UiScale.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/WordBounds.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr float kLineNumberColumns = 5.0f;
		constexpr float kNumberPaddingX = 6.0f;
		constexpr float kModeButtonGap = 2.0f;

		// Where a line breaks when nothing wraps: never.
		constexpr std::size_t kNoWrap = static_cast<std::size_t>(-1);

		// How many cells of the font pushed fit across `width`; never none, so a pane dragged
		// narrower than one cell still breaks somewhere rather than nowhere. Measured with
		// CalcTextSize, which rounds up, so a row never comes out wider than the width it fits.
		std::size_t CellsAcross(float width)
		{
			const float cell = ImGui::CalcTextSize("0").x;

			if (cell <= 0.0f || width <= cell)
			{
				return 1;
			}

			return static_cast<std::size_t>(width / cell);
		}

		// Both line numbers' column in the font pushed.
		float NumberColumnWidth()
		{
			return ImGui::CalcTextSize("0").x * kLineNumberColumns + Core::Scaled(kNumberPaddingX) * 2.0f;
		}

		// How many cells a wrapped line has across a text column `width` wide: short of the
		// edge it runs to by a padding, the way text keeps clear of a window's edge.
		std::size_t WrapColumnsAcross(float width)
		{
			return CellsAcross(width - ImGui::GetStyle().WindowPadding.x);
		}

		// Which of a line's `count` screen lines, the first at `screenTop` and `rowHeight` apart,
		// the clipping rectangle in force shows, as [first, end). Only those are drawn: a
		// minified file's one line can wrap into thousands, of which a screenful is visible.
		std::pair<std::size_t, std::size_t> VisibleSegments(float screenTop, std::size_t count,
															float rowHeight)
		{
			const ImDrawList& drawList = *ImGui::GetWindowDrawList();
			const float above = (drawList.GetClipRectMin().y - screenTop) / rowHeight;
			const float below = (drawList.GetClipRectMax().y - screenTop) / rowHeight;
			const float total = static_cast<float>(count);

			return {static_cast<std::size_t>(std::clamp(std::floor(above), 0.0f, total)),
					static_cast<std::size_t>(std::clamp(std::ceil(below), 0.0f, total))};
		}

		// Where `text` breaks after `columns` cells; nowhere for kNoWrap.
		std::vector<std::size_t> BreaksOf(std::string_view text, std::size_t columns)
		{
			if (columns == kNoWrap)
			{
				return {};
			}

			return WrapBytes(text, columns);
		}

		// How tall a region's content is once the clipper has walked `count` rows: every row's
		// height, less the spacing ImGui leaves out after the last item. Declared up front only
		// on the frame the scroll is restored, which ImGui would otherwise clamp to the height
		// it measured for last frame's rows.
		float RowsHeight(std::size_t count, float rowHeight)
		{
			return static_cast<float>(count) * rowHeight - ImGui::GetStyle().ItemSpacing.y;
		}

		ImU32 BandFor(DiffRowKind kind, const GitPalette& palette)
		{
			switch (kind)
			{
			case DiffRowKind::Added:
				return palette.DiffAddedLine;
			case DiffRowKind::Removed:
				return palette.DiffRemovedLine;
			case DiffRowKind::HunkHeader:
			case DiffRowKind::HunkSignature:
				return ImGui::GetColorU32(ImGuiCol_FrameBg);
			case DiffRowKind::HunkGap:
			case DiffRowKind::Context:
			case DiffRowKind::Unchanged:
				break;
			}

			return 0;
		}

		ImU32 TextFor(DiffRowKind kind, const GitPalette& palette)
		{
			switch (kind)
			{
			case DiffRowKind::Added:
				return palette.DiffAddedText;
			case DiffRowKind::Removed:
				return palette.DiffRemovedText;
			case DiffRowKind::HunkHeader:
			case DiffRowKind::HunkSignature:
			case DiffRowKind::HunkGap:
			case DiffRowKind::Context:
			case DiffRowKind::Unchanged:
				break;
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

			ImGui::SetCursorPos(ImVec2(x + width - textWidth - Core::Scaled(kNumberPaddingX), y));
			ImGui::TextUnformatted(text.c_str());
		}

		// What a hunk's two opening rows say in place of git's "@@ -297,11 +319,18 @@ scope".
		// The header row names the hunk and where it stands among the file's others, then the
		// lines it covers; `side` picks one half for the side-by-side view, and the unified
		// view has both with an arrow from the old range to the new. The signature row under it
		// has the scope as git wrote it, for when the line it came from cannot be found.
		std::string HeaderTextFor(const Core::FileDiff& file, std::size_t index, DiffRowKind kind,
								  std::optional<DiffSide> side)
		{
			const Core::Hunk& hunk = file.Hunks[index];

			if (kind == DiffRowKind::HunkSignature)
			{
				return std::string(HunkScope(hunk.Header));
			}

			// Named, because a bare pair of numbers above the code says nothing about being one
			// changed stretch of the file, or about how many more there are to read.
			std::string text = "Hunk " + std::to_string(index + 1) + " of " + std::to_string(file.Hunks.size());

			// A half the hunk has no lines on says so in words: a bare dash reads as a range
			// that failed to show rather than as an added or deleted file's empty side.
			if (side.has_value() && (*side == DiffSide::Old ? hunk.OldCount : hunk.NewCount) <= 0)
			{
				return text + " | no lines";
			}

			text += " | lines ";

			if (side != DiffSide::New)
			{
				text += LineRangeLabel(hunk.OldStart, hunk.OldCount);
			}

			if (!side.has_value())
			{
				text += " → ";
			}

			if (side != DiffSide::Old)
			{
				text += LineRangeLabel(hunk.NewStart, hunk.NewCount);
			}

			return text;
		}

		// The line of the file a hunk's signature row names, as the Unchanged row that line
		// would be — so it draws, numbers and colours exactly as it does in the whole file.
		// Absent when neither side has it: no texts yet, or a scope git took from somewhere
		// the search does not reach, and the row then shows the scope as git wrote it.
		std::optional<DiffRow> SignatureRowFor(const Core::Hunk& hunk,
											   std::span<const std::string_view> oldLines,
											   std::span<const std::string_view> newLines)
		{
			const std::string_view scope = HunkScope(hunk.Header);

			// A side with no lines in the hunk names the line its change sits after, so the
			// search starts below that line rather than above it.
			DiffRow row;
			row.Kind = DiffRowKind::Unchanged;
			row.OldLine = SignatureLine(
				oldLines, hunk.OldCount > 0 ? hunk.OldStart : hunk.OldStart + 1, scope);
			row.NewLine = SignatureLine(
				newLines, hunk.NewCount > 0 ? hunk.NewStart : hunk.NewStart + 1, scope);

			if (row.OldLine == 0 && row.NewLine == 0)
			{
				return std::nullopt;
			}

			return row;
		}

		// Draws a header row's text from `origin`, dimmed as a whole: it describes the code below
		// rather than being any of it. Returns how wide it came out.
		float DrawHeader(const ImVec2& origin, const std::string& text)
		{
			ImGui::SetCursorPos(origin);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopStyleColor();

			return ImGui::CalcTextSize(text.c_str()).x;
		}

		// Draws the runs RunsFor works out for the line — which of them there are, and that
		// there is always at least one, is that function's business and is tested there; this
		// only puts them on screen. Every run is positioned explicitly from `origin` rather
		// than with SameLine(): a run that draws nothing (an empty gap before two adjacent
		// tokens) would otherwise rewind the cursor and overwrite the row above, which is the
		// exact bug this file's history records.
		//
		// A wrapped line is drawn one segment of `breaks` to a screen line, `rowHeight` apart,
		// each from `origin`'s x; a run crossing a break is cut in two there.
		void DrawLine(const ImVec2& origin, std::string_view text,
					  const std::vector<Token>* tokens, ImU32 ordinaryColour,
					  const SyntaxPalette& syntax, std::span<const std::size_t> breaks,
					  float rowHeight)
		{
			const std::vector<SyntaxRun> runs = RunsFor(text, tokens);
			const auto [first, end] =
				VisibleSegments(ImGui::GetWindowPos().y - ImGui::GetScrollY() + origin.y,
								breaks.size() + 1, rowHeight);

			for (std::size_t index = first; index < end; ++index)
			{
				const WrapSegment segment = SegmentOf(text, breaks, index);
				const float y = origin.y + static_cast<float>(index) * rowHeight;

				// Each run's x is measured from the segment's start against the whole prefix up
				// to it, not accumulated from the previous run's width: CalcTextSize rounds its
				// result up to a whole pixel, and summing that rounding error run by run drifts
				// the tail of a long, densely tokenised line several pixels right of where the
				// monospaced grid puts it — the same trap already recorded against the editor's
				// own caret, reached here by accumulation instead of by caret math.
				const auto xAt = [&](std::size_t offset) {
					return origin.x +
						   ImGui::CalcTextSize(text.data() + segment.From, text.data() + offset).x;
				};

				// The runs are in order, so the segment's are found by search rather than by
				// walking every run of the line for every segment of it.
				const auto firstRun = std::partition_point(
					runs.begin(), runs.end(), [&](const SyntaxRun& run) { return run.To <= segment.From; });

				for (auto run = firstRun; run != runs.end() && run->From < segment.To; ++run)
				{
					const std::size_t from = std::max(run->From, segment.From);
					const std::size_t to = std::min(run->To, segment.To);

					if (from >= to)
					{
						continue;
					}

					ImGui::SetCursorPos(ImVec2(xAt(from), y));
					ImGui::PushStyleColor(ImGuiCol_Text,
										  run->Kind.has_value() ? syntax.Of(*run->Kind) : ordinaryColour);
					ImGui::TextUnformatted(text.data() + from, text.data() + to);
					ImGui::PopStyleColor();
				}
			}
		}

		// The width a line's first bytes are drawn at, measured as DrawLine measures them: from
		// the line's start, never summed run by run.
		float WidthOf(std::string_view prefix)
		{
			if (prefix.empty())
			{
				return 0.0f;
			}

			return ImGui::CalcTextSize(prefix.data(), prefix.data() + prefix.size()).x;
		}

		// WidthOf in the mono font the rows are drawn in, for hit-testing from the selection
		// gesture: that runs with the UI font back, so the context menu it opens draws like
		// every other menu's.
		float MonoWidthOf(std::string_view prefix)
		{
			ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont());
			const float width = WidthOf(prefix);
			ImGui::PopFont();

			return width;
		}

		// Fills the part of a line `span` covers, from the x its text is drawn at, under the
		// text: called before DrawLine so the code stays readable on top of it. Laid out as
		// DrawLine lays the line out, one segment of `breaks` to a screen line.
		void DrawSelected(std::string_view text, Core::ColumnSpan span,
						  std::span<const std::size_t> breaks, float textScreenX, float top,
						  float rowHeight, ImU32 colour)
		{
			const auto [first, end] = VisibleSegments(top, breaks.size() + 1, rowHeight);

			for (std::size_t index = first; index < end; ++index)
			{
				const WrapSegment segment = SegmentOf(text, breaks, index);
				const std::size_t from = std::clamp(span.From, segment.From, segment.To);
				const std::size_t to = std::clamp(span.To, segment.From, segment.To);

				if (from >= to)
				{
					continue;
				}

				const std::string_view piece = text.substr(segment.From, segment.To - segment.From);

				Core::DrawSelectionSpan(*ImGui::GetWindowDrawList(),
										textScreenX + WidthOf(piece.substr(0, from - segment.From)),
										textScreenX + WidthOf(piece.substr(0, to - segment.From)),
										top + static_cast<float>(index) * rowHeight,
										ImGui::GetTextLineHeight(), colour);
			}
		}

		// The screen line of the `count` under `screenY` in the region open now, clamped to
		// them; they start at `startY` in the region's own coordinates and are `rowHeight` apart.
		std::size_t LineUnder(float screenY, float startY, float rowHeight, std::size_t count)
		{
			const float local = screenY - ImGui::GetWindowPos().y + ImGui::GetScrollY() - startY;
			const float line = std::floor(local / rowHeight);

			if (line <= 0.0f || count == 0)
			{
				return 0;
			}

			if (line >= static_cast<float>(count - 1))
			{
				return count - 1;
			}

			return static_cast<std::size_t>(line);
		}

		// The byte of `text` at `x` along screen line `segment` of it, as broken after
		// `columns` cells: the nearest boundary, or with `under` the start of the character
		// covering x. Past the line's last screen line, its end.
		std::size_t ByteOnSegment(std::string_view text, std::size_t columns, std::size_t segment,
								  float x, bool under)
		{
			const std::vector<std::size_t> breaks = BreaksOf(text, columns);
			const WrapSegment bytes = SegmentOf(text, breaks, segment);
			const std::string_view piece = text.substr(bytes.From, bytes.To - bytes.From);

			return bytes.From + (under ? ByteUnderX(piece, x, MonoWidthOf) : ByteAtX(piece, x, MonoWidthOf));
		}

		// Where a sweep over a row with no line lands: the end of the nearest line above or the
		// start of the nearest one below, the one above winning a tie — so dragging across a
		// hunk header takes in what precedes it rather than stalling on it.
		template<typename LineOf, typename LengthOf>
		std::optional<Core::TextPoint> NearestLine(std::size_t index, std::size_t count,
												   const LineOf& lineOf, const LengthOf& lengthOf)
		{
			for (std::size_t distance = 1; distance <= index || index + distance < count; ++distance)
			{
				if (distance <= index)
				{
					if (const std::optional<std::int64_t> line = lineOf(index - distance))
					{
						return Core::TextPoint{*line, lengthOf(index - distance)};
					}
				}

				if (index + distance < count)
				{
					if (const std::optional<std::int64_t> line = lineOf(index + distance))
					{
						return Core::TextPoint{*line, 0};
					}
				}
			}

			return std::nullopt;
		}

		// The first and the last row that carry a line, for Select All; empty without one.
		template<typename LineOf, typename LengthOf>
		std::pair<Core::TextPoint, Core::TextPoint> EveryLine(std::size_t count, const LineOf& lineOf,
															 const LengthOf& lengthOf)
		{
			// From each end inwards, so only the two lines found are resolved: the menu asks for
			// this every frame it is open.
			for (std::size_t first = 0; first < count; ++first)
			{
				if (const std::optional<std::int64_t> from = lineOf(first))
				{
					for (std::size_t last = count; last-- > first;)
					{
						if (const std::optional<std::int64_t> to = lineOf(last))
						{
							return {Core::TextPoint{*from, 0}, Core::TextPoint{*to, lengthOf(last)}};
						}
					}
				}
			}

			return {};
		}
	}

	void DiffView::Render(const Core::FileDiff& file, const GitPalette& palette,
						  const SyntaxPalette& syntax, ImU32 selection,
						  const Core::Ref<const FileTexts>& texts, const FileTokens& tokens,
						  bool wordWrap)
	{
		RenderToolbar(file, wordWrap);

		if (texts != nullptr)
		{
			// Whole-file mode draws every line from these, and the hunks-only one the line each
			// hunk's signature row names.
			RebuildLineCache(texts);
		}
		else
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
			m_TextsHash = 0;
		}

		// After the clear above, never before it: a binary file's owner hands us no texts, so
		// returning first would leave the previous file's cache in place for as long as the
		// reader stayed on binary files.
		if (file.IsBinary)
		{
			// libgit2 emits no hunks for a binary delta, so there is nothing to lay out.
			// Saying so is better than an empty pane that looks like a failure.
			Core::EmptyState("Binary file — no textual diff.");
			return;
		}

		// Whole-file mode needs the texts to know how many lines the file has and what an
		// Unchanged row says; while they are still in flight (or the owner has not asked for
		// them at all) this draws the hunks-only view instead, which needs no text and is what
		// the reader already had — so nothing on screen blinks once the texts land.
		const bool wholeFile = m_Scope == DiffScope::WholeFile && texts != nullptr;

		// A different layout is a different set of rows under the same scroll position, which
		// left alone would show whatever those rows happen to have there. Decided here, from
		// what is drawn rather than from the buttons, so that whole-file mode arriving a few
		// frames late with its texts still keeps the reader's line.
		const Layout layout{m_Mode, wholeFile ? DiffScope::WholeFile : DiffScope::Hunks, wordWrap};
		m_RestoreKeptLine = layout != m_Layout && m_KeptLine.has_value() && m_KeptLine->Path == ShownPath(file);
		m_Layout = layout;

		// A selection's lines are row ordinals or line numbers of one layout of one version of
		// one file; under anything else they would select whatever happens to sit there now.
		// The side is the selection's own, kept in step by SelectSideBySide, so this only sees
		// it change along with the layout.
		const DiffSelectionContext context{
			.Path = ShownPath(file),
			.Mode = m_Mode,
			.Scope = layout.Scope,
			.Side = m_Mode == DiffViewMode::SideBySide ? m_SelectionSide : DiffSide::New,
			.ContentHash = ContentHashOf(file),
			.TextsHash = wholeFile ? m_TextsHash : 0};

		if (context != m_SelectionFor)
		{
			m_Selection.Clear();
			m_Gesture.Cancel();
			m_SelectionFor = context;
		}

		if (wholeFile)
		{
			const WholeFileRows rows = BuildWholeFileRows(file, m_OldLines.size(), m_NewLines.size());

			if (m_Mode == DiffViewMode::Unified)
			{
				RenderUnified(rows.Unified, file, palette, syntax, selection, tokens, wordWrap);
			}
			else
			{
				RenderSideBySide(rows.SideBySide, file, palette, syntax, selection, tokens, wordWrap);
			}

			return;
		}

		if (file.Hunks.empty())
		{
			Core::EmptyState("No changes in this file.");
			return;
		}

		if (m_Mode == DiffViewMode::Unified)
		{
			RenderUnified(BuildUnifiedRows(file), file, palette, syntax, selection, tokens, wordWrap);
			return;
		}

		RenderSideBySide(BuildSideBySideRows(file), file, palette, syntax, selection, tokens, wordWrap);
	}

	std::size_t DiffView::ContentHashOf(const Core::FileDiff& file)
	{
		const DiffMeasurement shape = MeasurementOf(file, DiffScope::Hunks);

		if (&file != m_HashedFile || shape != m_HashedShape)
		{
			m_HashedFile = &file;
			m_HashedShape = shape;
			m_ContentHash = DiffContentHash(file);
		}

		return m_ContentHash;
	}

	void DiffView::UpdateSelection(const Core::SelectionSource& source, bool wordWrap)
	{
		Core::SelectionGestureOptions options;
		options.ScrollsVertically = true;
		// Side-by-side the halves share the region's one horizontal scroll (ADR 0495), so a
		// sweep past either edge moves both, as the scrollbar does. Wrapped, there is none.
		options.ScrollsHorizontally = !wordWrap;
		options.CopyKey = true;
		options.SelectAllKey = true;
		// The only context menu the region has, opened over it and nowhere else.
		options.MenuId = "##diffMenu";

		const Core::SelectionGestureResult result = m_Gesture.Update(m_Selection, source, options);

		// The gesture has already put the text on the clipboard; the owner only confirms it,
		// and only for the menu's Copy - Ctrl+C goes unremarked, as it does in the editor.
		if (result.Copied && result.FromMenu && OnCopied)
		{
			OnCopied(result.CopiedText);
		}
	}

	void DiffView::RenderToolbar(const Core::FileDiff& file, bool wordWrap)
	{
		const std::filesystem::path& path = ShownPath(file);

		// A frame tall, so the icon buttons after it share its centre line.
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Core::ToUtf8(path).c_str());

		// The two modes as a pair with the current one engaged, rather than one button naming
		// the mode it would switch to: an icon for "the other one" reads as the state it is in.
		ImGui::SameLine();

		if (DrawIconButton("##sideBySide", ICON_MDI_VIEW_SPLIT_VERTICAL,
						   m_Mode == DiffViewMode::SideBySide, "Side by side"))
		{
			m_Mode = DiffViewMode::SideBySide;
		}

		// Closer than ordinary spacing, so the two read as one control, but not touching: with
		// both filled — one engaged, the other hovered — flush fills run together into one blob.
		ImGui::SameLine(0.0f, Core::Scaled(kModeButtonGap));

		if (DrawIconButton("##unified", ICON_MDI_VIEW_AGENDA_OUTLINE,
						   m_Mode == DiffViewMode::Unified, "Unified"))
		{
			m_Mode = DiffViewMode::Unified;
		}

		// How the diff is laid out, then what it shows, then what can be done with the file.
		ImGui::SameLine();
		DrawIconSeparator();
		ImGui::SameLine();

		// The scopes as a pair like the modes, for the same reason; room for a third follows
		// the same pattern.
		if (DrawIconButton("##hunks", ICON_MDI_ARROW_COLLAPSE_VERTICAL,
						   m_Scope == DiffScope::Hunks, "Hunks only"))
		{
			m_Scope = DiffScope::Hunks;
		}

		ImGui::SameLine(0.0f, Core::Scaled(kModeButtonGap));

		if (DrawIconButton("##wholeFile", ICON_MDI_ARROW_EXPAND_VERTICAL,
						   m_Scope == DiffScope::WholeFile, "Whole file"))
		{
			m_Scope = DiffScope::WholeFile;
		}

		ImGui::SameLine();
		DrawIconSeparator();

		ImGui::SameLine();

		if (DrawIconButton("##colourSyntax", ICON_MDI_PALETTE_OUTLINE, m_ColourSyntax,
						   "Colour syntax"))
		{
			m_ColourSyntax = !m_ColourSyntax;
		}

		if (OnSetWordWrap)
		{
			ImGui::SameLine(0.0f, Core::Scaled(kModeButtonGap));

			if (DrawIconButton("##wordWrap", ICON_MDI_WRAP, wordWrap, "Word wrap"))
			{
				OnSetWordWrap(!wordWrap);
			}
		}

		// The file as it is on disk now, which a deleted file no longer has. NewPath and not
		// ShownPath: the latter falls back to OldPath for exactly that case.
		const bool canOpenFile = OnOpenFile && !file.NewPath.empty();

		// Only when something follows it: a divider with nothing after it divides nothing.
		if (canOpenFile || OnOpenAsTab)
		{
			ImGui::SameLine();
			DrawIconSeparator();
		}

		if (canOpenFile)
		{
			ImGui::SameLine();

			if (DrawIconButton("##openFile", ICON_MDI_FILE_EDIT_OUTLINE, false, "Open file"))
			{
				OnOpenFile(file.NewPath);
			}
		}

		// Absent inside a tab: OnOpenAsTab is left unset there, so a diff already open as a
		// tab offers no way to open a second copy of itself.
		if (OnOpenAsTab)
		{
			ImGui::SameLine();

			if (DrawIconButton("##openAsTab", ICON_MDI_TAB_PLUS, false, "Open as tab"))
			{
				OnOpenAsTab();
			}
		}

		// Last, so it sits at the pane's right edge however many buttons precede it — see
		// DrawPaneClose, which the commit details pane opens with for the same reason.
		if (OnClose && DrawPaneClose())
		{
			OnClose();
		}

		ImGui::Separator();
	}

	void DiffView::RebuildLineCache(const Core::Ref<const FileTexts>& texts)
	{
		if (texts.get() == m_CachedTexts.get())
		{
			return;
		}

		m_CachedTexts = texts;
		m_OldLines = texts->HasOld ? SplitTextLines(texts->Old) : std::vector<std::string_view>{};
		m_NewLines = texts->HasNew ? SplitTextLines(texts->New) : std::vector<std::string_view>{};

		// Here, keyed by the same pointer as the split, so it is read once per texts object;
		// it is the content that decides whether a selection survives, not the object.
		m_TextsHash = TextsContentHash(*texts);
	}

	void DiffView::RenderUnified(const std::vector<DiffRow>& rows, const Core::FileDiff& file,
								 const GitPalette& palette, const SyntaxPalette& syntax,
								 ImU32 selection, const FileTokens& tokens, bool wordWrap)
	{
		// Where a wrapped line breaks is known for certain only once the region has opened and
		// can be measured, and the scroll a switch of layout restores has to be set before
		// that. Last frame's width stands in for it here; the rows are laid out again inside
		// should the region have come out a different width since.
		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont());
		const float lastWidth =
			m_UnifiedInnerWidth > 0.0f ? m_UnifiedInnerWidth : ImGui::GetContentRegionAvail().x;
		const std::size_t lastColumns =
			wordWrap ? WrapColumnsAcross(lastWidth - NumberColumnWidth() * 2.0f) : kNoWrap;
		ImGui::PopFont();

		const DiffRowLines& lastLines = RowLinesFor(std::span(rows), file, tokens, lastColumns);
		const std::size_t lastTotal = lastLines.TotalLines();

		// No border, so ImGui gives the region no padding and its first row sits at its top.
		const std::optional<float> scroll = TakeRestoredScroll(std::span(rows), lastLines, 0.0f);

		// Wrapped, nothing reaches past the right edge, so a scroll left over from before is put
		// back to the start rather than left showing every line with its beginning cut off.
		if (scroll.has_value() || wordWrap)
		{
			ImGui::SetNextWindowScroll(ImVec2(wordWrap ? 0.0f : -1.0f, scroll.value_or(-1.0f)));
		}

		if (scroll.has_value())
		{
			ImGui::SetNextWindowContentSize(ImVec2(0.0f, RowsHeight(lastTotal, m_RowHeight)));
		}

		if (!ImGui::BeginChild("unified", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
							   wordWrap ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::EndChild();
			return;
		}

		// The diff's own text, not the UI's — the same reasoning that puts the editor and
		// the search preview in the mono font applies to a line of a hunk.
		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont());

		const float numberWidth = NumberColumnWidth();
		const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
		const float startX = ImGui::GetCursorPosX();
		const float startY = ImGui::GetCursorPosY();

		m_RowHeight = rowHeight;

		// What the reader can see across, any scrollbar already taken off.
		m_UnifiedInnerWidth = ImGui::GetWindowDrawList()->GetClipRectMax().x -
							  ImGui::GetWindowDrawList()->GetClipRectMin().x;

		const std::size_t columns =
			wordWrap ? WrapColumnsAcross(m_UnifiedInnerWidth - numberWidth * 2.0f) : kNoWrap;
		const DiffRowLines& lines = RowLinesFor(std::span(rows), file, tokens, columns);
		const bool rewrapped =
			KeepLineAcrossRewrap(std::span(rows), lines, file, startY, columns, lastColumns);

		// Only the visible screen lines are laid out. A diff of a generated file runs to tens
		// of thousands of lines and drawing them all costs the frame. The clipper counts screen
		// lines rather than rows, since a wrapped row takes several, and is told their height
		// explicitly rather than measuring it from a first item, because that first item's own
		// layout is exactly what a cursor-rewind bug would corrupt.
		ImGuiListClipper clipper;
		clipper.Begin(Core::Narrow<int>(lines.TotalLines()), rowHeight);

		// Every row before this one is drawn: one taller than a line can reach into the next
		// step, which must not draw it a second time over itself.
		std::size_t drawnTo = 0;

		while (clipper.Step())
		{
			const ImVec2 stepPos = ImGui::GetCursorPos();
			const auto firstLine = static_cast<std::size_t>(clipper.DisplayStart);
			const auto endLine = static_cast<std::size_t>(clipper.DisplayEnd);

			if (endLine <= firstLine)
			{
				continue;
			}

			const std::size_t endRow = lines.RowOfLine(endLine - 1) + 1;

			for (std::size_t index = std::max(lines.RowOfLine(firstLine), drawnTo); index < endRow;
				 ++index)
			{
				const DiffRow& row = rows[index];

				// Each column is positioned explicitly from the row's own start rather than
				// with SameLine(), so a column that draws nothing (a blank line number) never
				// leaves a later column depending on it having existed. A row that starts above
				// the step starts above it here too.
				const ImVec2 rowPos(stepPos.x, stepPos.y + (static_cast<float>(lines.FirstLineOf(index)) -
															static_cast<float>(firstLine)) *
															   rowHeight);
				ImGui::SetCursorPos(rowPos);

				const ImVec2 start = ImGui::GetCursorScreenPos();
				const float height = static_cast<float>(lines.LinesOf(index)) * rowHeight -
									 ImGui::GetStyle().ItemSpacing.y;

				if (const ImU32 band = BandFor(row.Kind, palette); band != 0)
				{
					ImGui::GetWindowDrawList()->AddRectFilled(
						start, ImVec2(start.x + ImGui::GetContentRegionAvail().x, start.y + height),
						band);
				}

				const DiffRow drawn = DrawnRow(file, row);

				if (drawn.Kind == DiffRowKind::HunkHeader || drawn.Kind == DiffRowKind::HunkSignature)
				{
					// In the text column, past both line numbers, so it lines up with the code
					// it introduces rather than hanging off the region's edge.
					DrawHeader(ImVec2(rowPos.x + numberWidth * 2.0f, rowPos.y),
							   HeaderTextFor(file, row.Hunk, row.Kind, std::nullopt));
					continue;
				}

				if (row.Kind == DiffRowKind::HunkGap)
				{
					continue;
				}

				DrawNumber(rowPos.x, rowPos.y, numberWidth, drawn.OldLine);
				DrawNumber(rowPos.x + numberWidth, rowPos.y, numberWidth, drawn.NewLine);

				// One text column standing for both sides, so it resolves for New — which is
				// what this view drew before the two halves could differ.
				const ResolvedLine resolved =
					ResolveRow(file, drawn, DiffSide::New, m_OldLines, m_NewLines, tokens);

				if (!resolved.Text.has_value())
				{
					continue;
				}

				const std::vector<std::size_t> breaks = BreaksOf(*resolved.Text, columns);

				if (const std::optional<std::int64_t> line = UnifiedLineOf(rows, index))
				{
					if (const std::optional<Core::ColumnSpan> span =
							m_Selection.SpanOn(*line, resolved.Text->size()))
					{
						DrawSelected(*resolved.Text, *span, breaks, start.x + numberWidth * 2.0f,
									 start.y, rowHeight, selection);
					}
				}

				DrawLine(ImVec2(rowPos.x + numberWidth * 2.0f, rowPos.y), *resolved.Text,
						 m_ColourSyntax ? resolved.Tokens : nullptr, TextFor(drawn.Kind, palette),
						 syntax, breaks, rowHeight);
			}

			drawnTo = std::max(drawnTo, endRow);

			// The step's lines reserved as one item, for the reason RenderSideBySide gives.
			ImGui::SetCursorPos(stepPos);
			ImGui::Dummy(ImVec2(0.0f, static_cast<float>(endLine - firstLine) * rowHeight -
										  ImGui::GetStyle().ItemSpacing.y));
		}

		if (!rewrapped)
		{
			RememberKeptLine(std::span(rows), lines, file, startY);
		}

		// The UI font back before the gesture, whose context menu is a menu like any other;
		// its hit-testing measures in the mono font by itself (MonoWidthOf).
		ImGui::PopFont();

		// After every row, as the gesture asks.
		SelectUnified(rows, lines, file, tokens, startX, startY, numberWidth * 2.0f, rowHeight,
					  columns);

		ImGui::EndChild();
	}

	void DiffView::SelectUnified(const std::vector<DiffRow>& rows, const DiffRowLines& lines,
								 const Core::FileDiff& file, const FileTokens& tokens, float startX,
								 float startY, float textOffset, float rowHeight, std::size_t columns)
	{
		// As drawn: one text column standing for both sides, resolved for New.
		const auto textOf = [&](std::size_t index) {
			return ResolveRow(file, rows[index], DiffSide::New, m_OldLines, m_NewLines, tokens)
				.Text.value_or(std::string_view{});
		};
		const auto lineOf = [&](std::size_t index) {
			return UnifiedLineOf(rows, index);
		};
		const auto lengthOf = [&](std::size_t index) {
			return textOf(index).size();
		};
		const auto indexOf = [&](std::int64_t line) -> std::optional<std::size_t> {
			if (line < 0 || static_cast<std::size_t>(line) >= rows.size())
			{
				return std::nullopt;
			}

			return static_cast<std::size_t>(line);
		};

		// A press asks where it landed; a sweep, where it has got to. Only a press can miss.
		const bool pressing = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		const float textScreenX = ImGui::GetWindowPos().x - ImGui::GetScrollX() + startX + textOffset;

		const auto pointAt = [&](ImVec2 screen, bool under) -> std::optional<Core::TextPoint> {
			if (rows.empty())
			{
				return std::nullopt;
			}

			const std::size_t screenLine = LineUnder(screen.y, startY, rowHeight, lines.TotalLines());
			const std::size_t index = lines.RowOfLine(screenLine);

			if (const std::optional<std::int64_t> line = lineOf(index))
			{
				const std::string_view text = textOf(index);
				const float x = screen.x - textScreenX;

				return Core::TextPoint{*line, ByteOnSegment(text, columns,
															screenLine - lines.FirstLineOf(index), x,
															under)};
			}

			// A header, a signature or a gap: no selection starts there, but one already being
			// swept carries on across it.
			if (pressing)
			{
				return std::nullopt;
			}

			return NearestLine(index, rows.size(), lineOf, lengthOf);
		};

		Core::SelectionSource source;
		source.PointAt = [&](ImVec2 screen) {
			return pointAt(screen, false);
		};
		source.CharacterAt = [&](ImVec2 screen) {
			return pointAt(screen, true);
		};
		source.WordAt = [&](Core::TextPoint at) {
			const std::optional<std::size_t> index = indexOf(at.Line);

			return index.has_value() ? Core::WordSpanAt(textOf(*index), at.Column, Core::WordKind::Code)
									 : Core::ColumnSpan{};
		};
		source.LineLength = [&](std::int64_t line) -> std::size_t {
			const std::optional<std::size_t> index = indexOf(line);

			return index.has_value() ? lengthOf(*index) : 0;
		};
		source.Everything = [&]() {
			return EveryLine(rows.size(), lineOf, lengthOf);
		};
		source.SelectedText = [&]() {
			return SelectedUnifiedText(m_Selection, rows, file, m_OldLines, m_NewLines, tokens);
		};

		UpdateSelection(source, columns != kNoWrap);
	}

	void DiffView::RenderSideBySide(const std::vector<DiffPair>& pairs, const Core::FileDiff& file,
									const GitPalette& palette, const SyntaxPalette& syntax,
									ImU32 selection, const FileTokens& tokens, bool wordWrap)
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
		if (const DiffMeasurement measurement = MeasurementOf(file, m_Scope);
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

		// Each half's text runs from past its line numbers, which start a padding in from the
		// half's own left edge, to its right edge.
		const auto columnsOf = [&](const SideBySidePanes& panes, float numberWidth) {
			return wordWrap ? WrapColumnsAcross(panes.PaneWidth - padding - numberWidth) : kNoWrap;
		};

		// Last frame's width stands in for where a wrapped line breaks, as in the unified view
		// and for the same reason: the scroll restored has to be set before the region opens.
		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont());
		const std::size_t lastColumns =
			columnsOf(SplitIntoPanes(m_SideInnerWidth > 0.0f ? m_SideInnerWidth : innerWidth),
					  NumberColumnWidth());
		ImGui::PopFont();

		const DiffRowLines& lastLines = RowLinesFor(std::span(pairs), file, tokens, lastColumns);
		const std::size_t lastTotal = lastLines.TotalLines();

		// Bordered, so ImGui pads the region and its first row sits that far below its top.
		const std::optional<float> scroll =
			TakeRestoredScroll(std::span(pairs), lastLines, ImGui::GetStyle().WindowPadding.y);

		// Wrapped, nothing reaches past either half's edge, so the shared horizontal scroll is
		// put back to the start rather than left where it cuts every line's beginning off.
		if (scroll.has_value() || wordWrap)
		{
			ImGui::SetNextWindowScroll(ImVec2(wordWrap ? 0.0f : -1.0f, scroll.value_or(-1.0f)));
		}

		ImGui::SetNextWindowContentSize(
			ImVec2(SideBySideContentWidth(innerWidth, wordWrap ? 0.0f : m_WidestSideLine, padding),
				   scroll.has_value() ? RowsHeight(lastTotal, m_RowHeight) : 0.0f));

		// One region for both halves, so there is one scroll position rather than two that have
		// to be kept equal; see SideBySidePanes for why that is worth the shared scrollbar.
		if (!ImGui::BeginChild("pairs", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
							   ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::EndChild();
			return;
		}

		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont());

		const float numberWidth = NumberColumnWidth();
		const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
		const float startX = ImGui::GetCursorPosX();
		const float startY = ImGui::GetCursorPosY();

		m_RowHeight = rowHeight;

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

		// Both halves break after the same number of cells, being the same width.
		const std::size_t columns = columnsOf(panes, numberWidth);
		const DiffRowLines& lines = RowLinesFor(std::span(pairs), file, tokens, columns);
		const bool rewrapped =
			KeepLineAcrossRewrap(std::span(pairs), lines, file, startY, columns, lastColumns);

		float widest = 0.0f;

		// Draws one half of the rows `first` up to `last` of a clipper step under a single
		// clipping rectangle. Per row it would be two rectangles a row, and ImGui starts a
		// fresh draw command each time one is pushed over anything already drawn — sixty rows
		// of alternating halves would cost a hundred and twenty commands where the whole region
		// should cost two. The step starts at screen line `firstLine`, at `stepPos`.
		const auto drawPane = [&](bool leftSide, std::size_t first, std::size_t last,
								  std::size_t firstLine, const ImVec2& stepPos, float stepScreenY) {
			const float paneX = leftSide ? 0.0f : panes.RightPaneX;
			const float paneScreenX = innerMin.x + paneX;

			// Clipped to its own half so a long line is reached by scrolling instead of
			// spilling across the split into the half it does not belong to.
			ImGui::PushClipRect(ImVec2(paneScreenX, innerMin.y),
								ImVec2(paneScreenX + panes.PaneWidth, innerMax.y), true);

			for (std::size_t index = first; index < last; ++index)
			{
				const std::optional<DiffRow>& row = leftSide ? pairs[index].Left : pairs[index].Right;

				// The other side changed and this one did not exist. The row still occupies its
				// height — that is reserved once for the whole step, below, and not by whatever
				// either half drew — so the two halves cannot stop lining up.
				if (!row.has_value())
				{
					continue;
				}

				// A row that starts above the step starts above it here too.
				const float offsetY =
					(static_cast<float>(lines.FirstLineOf(index)) - static_cast<float>(firstLine)) *
					rowHeight;
				const ImVec2 rowPos = ImVec2(stepPos.x + paneX, stepPos.y + offsetY);
				const float rowScreenY = stepScreenY + offsetY;

				// As tall as the pair, not as this half's own text: a line wrapped on the other
				// half leaves this one's change marked all the way down to where the next pair
				// starts, rather than a band with a gap under it.
				if (const ImU32 band = BandFor(row->Kind, palette); band != 0)
				{
					ImGui::GetWindowDrawList()->AddRectFilled(
						ImVec2(paneScreenX, rowScreenY),
						ImVec2(paneScreenX + panes.PaneWidth,
							   rowScreenY + static_cast<float>(lines.LinesOf(index)) * rowHeight -
								   ImGui::GetStyle().ItemSpacing.y),
						band);
				}

				const DiffRow drawn = DrawnRow(file, *row);

				if (drawn.Kind == DiffRowKind::HunkHeader || drawn.Kind == DiffRowKind::HunkSignature)
				{
					// Measured like any other line, so a long scope shown as git wrote it is
					// not left beyond anything scrolling could reach.
					const float width =
						DrawHeader(ImVec2(rowPos.x + numberWidth, rowPos.y),
								   HeaderTextFor(file, row->Hunk, row->Kind,
												 leftSide ? DiffSide::Old : DiffSide::New));
					widest = std::max(widest, numberWidth + width);
					continue;
				}

				// Blank on both halves: its height is the whole of what it says.
				if (row->Kind == DiffRowKind::HunkGap)
				{
					continue;
				}

				// Positioned explicitly from the row's own start rather than with SameLine(),
				// matching the unified view — a blank line number then costs nothing instead
				// of leaving the text column's position dependent on it.
				DrawNumber(rowPos.x, rowPos.y, numberWidth,
						   leftSide ? drawn.OldLine : drawn.NewLine);

				const ResolvedLine resolved =
					ResolveRow(file, drawn, leftSide ? DiffSide::Old : DiffSide::New, m_OldLines,
							   m_NewLines, tokens);

				if (!resolved.Text.has_value())
				{
					continue;
				}

				const std::vector<std::size_t> breaks = BreaksOf(*resolved.Text, columns);

				// Only on the half the selection belongs to; its lines are that side's.
				if (leftSide == (m_SelectionSide == DiffSide::Old))
				{
					if (const std::optional<std::int64_t> line = SideLineOf(pairs[index], m_SelectionSide))
					{
						if (const std::optional<Core::ColumnSpan> span =
								m_Selection.SpanOn(*line, resolved.Text->size()))
						{
							// The x DrawLine is given below, in screen coordinates: the region's
							// shared horizontal scroll is already in it.
							DrawSelected(*resolved.Text, *span, breaks,
										 ImGui::GetWindowPos().x - ImGui::GetScrollX() + rowPos.x +
											 numberWidth,
										 rowScreenY, rowHeight, selection);
						}
					}
				}

				DrawLine(ImVec2(rowPos.x + numberWidth, rowPos.y), *resolved.Text,
						 m_ColourSyntax ? resolved.Tokens : nullptr, TextFor(drawn.Kind, palette),
						 syntax, breaks, rowHeight);

				widest = std::max(widest, numberWidth +
											  ImGui::CalcTextSize(resolved.Text->data(),
																  resolved.Text->data() +
																	  resolved.Text->size())
												  .x);
			}

			ImGui::PopClipRect();
		};

		// Over screen lines, and told their height explicitly, for the same reasons as the
		// unified view's clipper.
		ImGuiListClipper clipper;
		clipper.Begin(Core::Narrow<int>(lines.TotalLines()), rowHeight);

		// Every pair before this one is drawn, as in the unified view.
		std::size_t drawnTo = 0;

		while (clipper.Step())
		{
			const ImVec2 stepPos = ImGui::GetCursorPos();
			const float stepScreenY = ImGui::GetCursorScreenPos().y;
			const auto firstLine = static_cast<std::size_t>(clipper.DisplayStart);
			const auto endLine = static_cast<std::size_t>(clipper.DisplayEnd);

			if (endLine <= firstLine)
			{
				continue;
			}

			const std::size_t first = std::max(lines.RowOfLine(firstLine), drawnTo);
			const std::size_t last = lines.RowOfLine(endLine - 1) + 1;

			drawPane(true, first, last, firstLine, stepPos, stepScreenY);
			drawPane(false, first, last, firstLine, stepPos, stepScreenY);

			drawnTo = std::max(drawnTo, last);

			// The step's lines reserved as one item rather than by moving the cursor: a bare
			// SetCursorPos past the last item is how a region gets extended behind ImGui's
			// back, and it asserts on the last row of the frame for exactly that. Submitting it
			// decides the height outright, so rows with nothing on either side are as tall as
			// rows with text on both. The spacing comes off because ItemSize adds it back.
			ImGui::SetCursorPos(stepPos);
			ImGui::Dummy(ImVec2(0.0f, static_cast<float>(endLine - firstLine) * rowHeight -
										  ImGui::GetStyle().ItemSpacing.y));
		}

		m_WidestSideLine = std::max(m_WidestSideLine, widest);

		if (!rewrapped)
		{
			RememberKeptLine(std::span(pairs), lines, file, startY);
		}

		// Drawn after the halves and outside their clipping rectangles, so it is not cut off by
		// either and does not slide when the reader scrolls across. A filled rectangle rather
		// than a line, which AddLine would centre on the split and so lay half of over the
		// right half's first pixel.
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(innerMin.x + panes.RightPaneX, innerMin.y),
												  ImVec2(innerMin.x + panes.RightPaneX + 1.0f,
														 innerMax.y),
												  ImGui::GetColorU32(ImGuiCol_Border));

		// The UI font back first, as in the unified view: the gesture's menu is a menu like
		// any other, and its hit-testing measures in the mono font by itself (MonoWidthOf).
		ImGui::PopFont();

		SelectSideBySide(pairs, lines, file, tokens, startX, startY, numberWidth, rowHeight,
						 innerMin.x, panes.RightPaneX, panes.PaneWidth, columns);

		ImGui::EndChild();
	}

	void DiffView::SelectSideBySide(const std::vector<DiffPair>& pairs, const DiffRowLines& lines,
									const Core::FileDiff& file, const FileTokens& tokens,
									float startX, float startY, float textOffset, float rowHeight,
									float innerLeft, float rightPaneX, float paneWidth,
									std::size_t columns)
	{
		// Read as m_SelectionSide is when each is called, not now: a press decides the side
		// inside PointAt, ahead of the word, the line or the drag that follows from it.
		const auto rowOf = [&](std::size_t index) -> const std::optional<DiffRow>& {
			return m_SelectionSide == DiffSide::Old ? pairs[index].Left : pairs[index].Right;
		};
		const auto textOf = [&](std::size_t index) {
			const std::optional<DiffRow>& row = rowOf(index);

			if (!row.has_value())
			{
				return std::string_view{};
			}

			return ResolveRow(file, *row, m_SelectionSide, m_OldLines, m_NewLines, tokens)
				.Text.value_or(std::string_view{});
		};
		const auto lineOf = [&](std::size_t index) {
			return SideLineOf(pairs[index], m_SelectionSide);
		};
		const auto lengthOf = [&](std::size_t index) {
			return textOf(index).size();
		};
		// Line numbers on one side are not row ordinals, so a line is looked for. Only a
		// double- or triple-click asks, never a frame by itself.
		const auto indexOf = [&](std::int64_t line) -> std::optional<std::size_t> {
			for (std::size_t index = 0; index < pairs.size(); ++index)
			{
				if (lineOf(index) == line)
				{
					return index;
				}
			}

			return std::nullopt;
		};

		const bool pressing = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

		// A Shift+click extends the selection there is, on the side it is on.
		const bool extending = ImGui::GetIO().KeyShift && !m_Selection.IsEmpty();

		// Where the left half's text starts on screen; the right half's is RightPaneX further.
		// The same x DrawLine is given, so the shared horizontal scroll is in it.
		const float textScreenX = ImGui::GetWindowPos().x - ImGui::GetScrollX() + startX + textOffset;

		const auto pointAt = [&](ImVec2 screen, bool under) -> std::optional<Core::TextPoint> {
			if (pairs.empty())
			{
				return std::nullopt;
			}

			// The half a press lands in is the selection's side until the next press: a sweep
			// over the other half keeps extending this one, by row.
			if (pressing && !extending)
			{
				m_SelectionSide = screen.x < innerLeft + rightPaneX ? DiffSide::Old : DiffSide::New;
			}

			const float paneX = m_SelectionSide == DiffSide::Old ? 0.0f : rightPaneX;
			const float paneLeft = innerLeft + paneX;
			const float x = std::clamp(screen.x, paneLeft, paneLeft + std::max(paneWidth, 0.0f)) -
							(textScreenX + paneX);

			const std::size_t screenLine = LineUnder(screen.y, startY, rowHeight, lines.TotalLines());
			const std::size_t index = lines.RowOfLine(screenLine);

			// Below a line that wrapped less than the other half's did, this half's line has
			// ended, and the point is its end.
			if (const std::optional<std::int64_t> line = lineOf(index))
			{
				return Core::TextPoint{*line, ByteOnSegment(textOf(index), columns,
															screenLine - lines.FirstLineOf(index), x,
															under)};
			}

			// A header, a gap, or a row this side does not have: no selection starts there,
			// and a sweep over it carries on from the nearest line this side does have.
			if (pressing)
			{
				return std::nullopt;
			}

			return NearestLine(index, pairs.size(), lineOf, lengthOf);
		};

		Core::SelectionSource source;
		source.PointAt = [&](ImVec2 screen) {
			return pointAt(screen, false);
		};
		source.CharacterAt = [&](ImVec2 screen) {
			return pointAt(screen, true);
		};
		source.WordAt = [&](Core::TextPoint at) {
			const std::optional<std::size_t> index = indexOf(at.Line);

			return index.has_value() ? Core::WordSpanAt(textOf(*index), at.Column, Core::WordKind::Code)
									 : Core::ColumnSpan{};
		};
		source.LineLength = [&](std::int64_t line) -> std::size_t {
			const std::optional<std::size_t> index = indexOf(line);

			return index.has_value() ? lengthOf(*index) : 0;
		};
		// The side last pressed in, New before any press.
		source.Everything = [&]() {
			return EveryLine(pairs.size(), lineOf, lengthOf);
		};
		source.SelectedText = [&]() {
			return SelectedSideText(m_Selection, m_SelectionSide, pairs, file, m_OldLines, m_NewLines,
									tokens);
		};

		UpdateSelection(source, columns != kNoWrap);

		// A press may have moved the selection to the other half. The selection made there is
		// this context's, not a stale one for Render to clear next frame.
		if (m_SelectionFor.has_value())
		{
			m_SelectionFor->Side = m_SelectionSide;
		}
	}

	DiffRow DiffView::DrawnRow(const Core::FileDiff& file, const DiffRow& row) const
	{
		if (row.Kind != DiffRowKind::HunkSignature)
		{
			return row;
		}

		// Once that line is found; until then the scope as git wrote it.
		return SignatureRowFor(file.Hunks[row.Hunk], m_OldLines, m_NewLines).value_or(row);
	}

	std::size_t DiffView::LinesOfRow(const Core::FileDiff& file, const DiffRow& row, DiffSide side,
									 const FileTokens& tokens, std::size_t columns) const
	{
		const DiffRow drawn = DrawnRow(file, row);

		// A header says what the code below it is and stays one line, cut off at the edge
		// rather than broken: there is nothing in it to read across a break.
		if (drawn.Kind == DiffRowKind::HunkHeader || drawn.Kind == DiffRowKind::HunkSignature ||
			drawn.Kind == DiffRowKind::HunkGap)
		{
			return 1;
		}

		const ResolvedLine resolved = ResolveRow(file, drawn, side, m_OldLines, m_NewLines, tokens);

		return resolved.Text.has_value() ? BreaksOf(*resolved.Text, columns).size() + 1 : 1;
	}

	template<typename Row>
	const DiffRowLines& DiffView::RowLinesFor(std::span<const Row> rows, const Core::FileDiff& file,
											  const FileTokens& tokens, std::size_t columns)
	{
		// Nothing to read while nothing wraps: one line a row, which needs no table.
		if (columns == kNoWrap)
		{
			m_RowLines = DiffRowLines::Uniform(rows.size());
			m_RowLinesFor.reset();
			return m_RowLines;
		}

		RowLinesKey key{.Path = ShownPath(file),
						.Mode = m_Layout.Mode,
						.Scope = m_Layout.Scope,
						.ContentHash = ContentHashOf(file),
						.TextsHash = m_TextsHash,
						.Rows = rows.size(),
						.Columns = columns};

		if (key == m_RowLinesFor)
		{
			return m_RowLines;
		}

		std::vector<std::size_t> counts;
		counts.reserve(rows.size());

		for (const Row& row : rows)
		{
			if constexpr (std::is_same_v<Row, DiffPair>)
			{
				// A pair is as tall as the taller of its halves, so the next pair starts level
				// on both.
				counts.push_back(std::max(
					row.Left.has_value() ? LinesOfRow(file, *row.Left, DiffSide::Old, tokens, columns) : 1,
					row.Right.has_value() ? LinesOfRow(file, *row.Right, DiffSide::New, tokens, columns)
										  : 1));
			}
			else
			{
				// One text column standing for both sides, resolved for New as it is drawn.
				counts.push_back(LinesOfRow(file, row, DiffSide::New, tokens, columns));
			}
		}

		m_RowLines = DiffRowLines::FromCounts(counts);
		m_RowLinesFor = std::move(key);

		return m_RowLines;
	}

	template<typename Row>
	std::optional<float> DiffView::TakeRestoredScroll(std::span<const Row> rows,
													  const DiffRowLines& lines, float startY)
	{
		const bool restore = std::exchange(m_RestoreKeptLine, false);

		if (!restore || !m_KeptLine.has_value() || rows.empty() || m_RowHeight <= 0.0f)
		{
			return std::nullopt;
		}

		return ScrollToKeptLine(rows, lines, startY);
	}

	template<typename Row>
	float DiffView::ScrollToKeptLine(std::span<const Row> rows, const DiffRowLines& lines,
									 float startY) const
	{
		// A line the new rows do not have — only when nothing of a hunk was on screen, leaving
		// the whole file — is stood in for by the hunk after it, put where the line was.
		const DiffRowAt at = RowAt(rows, m_KeptLine->Anchor);

		return std::max(0.0f, startY + static_cast<float>(lines.FirstLineOf(at.Row)) * m_RowHeight -
								  m_KeptLine->Offset);
	}

	template<typename Row>
	bool DiffView::KeepLineAcrossRewrap(std::span<const Row> rows, const DiffRowLines& lines,
										const Core::FileDiff& file, float startY,
										std::size_t columns, std::size_t lastColumns)
	{
		if (columns == lastColumns || !m_KeptLine.has_value() || m_KeptLine->Path != ShownPath(file) ||
			rows.empty() || m_RowHeight <= 0.0f)
		{
			return false;
		}

		// Too late for SetNextWindowScroll, so the reader sees one frame at the old scroll
		// over the new breaks — while a resize is being dragged, which redraws it anyway.
		ImGui::SetScrollY(ScrollToKeptLine(rows, lines, startY));

		return true;
	}

	template<typename Row>
	void DiffView::RememberKeptLine(std::span<const Row> rows, const DiffRowLines& lines,
									const Core::FileDiff& file, float startY)
	{
		const float scrollY = ImGui::GetScrollY();

		if (m_RowHeight <= 0.0f)
		{
			m_KeptLine.reset();
			return;
		}

		// The rows the region shows, from the one cut by its top edge to the one cut by its
		// bottom: the same span the clipper drew, worked out again rather than taken from it,
		// because the clipper's own also counts the rows it lays out to measure.
		const float top = std::max(0.0f, scrollY - startY);
		const float bottom = std::max(0.0f, scrollY + ImGui::GetWindowHeight() - startY);
		const auto firstLine = static_cast<std::size_t>(top / m_RowHeight);
		const auto endLine = static_cast<std::size_t>(std::ceil(bottom / m_RowHeight));

		const std::size_t first = std::min(lines.RowOfLine(firstLine), rows.size());
		const std::size_t end = endLine == 0 ? first : std::min(lines.RowOfLine(endLine - 1) + 1, rows.size());

		const std::optional<std::size_t> index = RowToKeep(rows, first, end);

		if (!index.has_value())
		{
			m_KeptLine.reset();
			return;
		}

		const float rowTop = startY + static_cast<float>(lines.FirstLineOf(*index)) * m_RowHeight;
		m_KeptLine = KeptLine{ShownPath(file), *AnchorOf(rows[*index]), rowTop - scrollY};
	}
}
