#include "Git/Diff/DiffView.h"

#include "Core/Base/Narrow.h"

#include "Core/Base/Base.h"

#include "Git/History/CommitLabels.h"
#include "Git/Diff/DiffPanes.h"
#include "Core/ImGui/IconButton.h"
#include "UI/FittedText.h"
#include "UI/LineNumberColumn.h"
#include "UI/SplitResults.h"
#include "UI/TypographicMarks.h"

#include "Core/ImGui/UiScale.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/ImGui/Theme.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/WordBounds.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr float kUnifiedNumberColumns = 2.0f;
		constexpr float kModeButtonGap = 2.0f;
		constexpr float kSplitLineWidth = 1.0f;

		constexpr float kNoWidth = 0.0f;
		constexpr float kAutoHeight = 0.0f;
		constexpr float kNoOffsetFromStart = 0.0f;
		constexpr float kScrollToStart = 0.0f;
		constexpr float kKeepScroll = -1.0f;
		constexpr float kFirstRowAtTop = 0.0f;
		constexpr std::size_t kPieceStart = 0;
		constexpr bool kNotEngaged = false;
		constexpr bool kIntersectWithCurrentClip = true;

		const ImVec2 kFillAvailable(0.0f, 0.0f);

		enum class HitTarget
		{
			NearestBoundary,
			CharacterUnder
		};

		struct SegmentRange
		{
			std::size_t First = 0;
			std::size_t End = 0;
		};

		void DrawWholeFileNote(const WholeFileNote& note, float roomKeptOnTheRight)
		{
			ImGui::PushID("##wholeFileNote");

			if (note.Loading)
			{
				Core::LoadingLine(note.Text.c_str());
			}
			else
			{
				bool cut = false;
				const float room = std::max(ImGui::GetContentRegionAvail().x - roomKeptOnTheRight, kNoWidth);
				const std::string fitted = FitToWidth(note.Text, room, &cut);
				ImGui::TextDisabled("%s", fitted.c_str());

				if (cut)
				{
					ImGui::SetItemTooltip("%s", note.Text.c_str());
				}
			}

			ImGui::PopID();
		}

		// WARNING: CalcTextSize rounds up, so a row never comes out wider than the width it fits.
		std::size_t CellsAcross(float width)
		{
			const float cell = ImGui::CalcTextSize("0").x;

			if (cell <= 0.0f || width <= cell)
			{
				return 1;
			}

			return static_cast<std::size_t>(width / cell);
		}

		std::size_t LastLineNumberOf(const Core::FileDiff& file, std::size_t oldLineCount, std::size_t newLineCount)
		{
			std::size_t last = std::max(oldLineCount, newLineCount);

			for (const Core::Hunk& hunk : file.Hunks)
			{
				last = std::max(last, static_cast<std::size_t>(std::max(0, hunk.OldStart + hunk.OldCount)));
				last = std::max(last, static_cast<std::size_t>(std::max(0, hunk.NewStart + hunk.NewCount)));
			}

			return last;
		}

		// WARNING: Measured with the mono font pushed, whose cells the numbers are drawn in.
		LineNumberColumn NumberColumnOf(const Core::FileDiff& file, std::size_t oldLineCount, std::size_t newLineCount)
		{
			return MeasureLineNumberColumn(LastLineNumberOf(file, oldLineCount, newLineCount), Core::MonospaceCellWidth());
		}

		float UnifiedTextOffset(float numberWidth)
		{
			return numberWidth * kUnifiedNumberColumns;
		}

		std::size_t WrapColumnsAcross(float width)
		{
			return CellsAcross(width - ImGui::GetStyle().WindowPadding.x);
		}

		std::size_t PaneColumns(bool wordWrap, float paneWidth, float padding, float numberWidth)
		{
			return wordWrap ? WrapColumnsAcross(paneWidth - padding - numberWidth)
							: Core::RowMap::kNoWrap;
		}

		SegmentRange VisibleSegments(float screenTop, std::size_t count, float rowHeight)
		{
			const ImDrawList& drawList = *ImGui::GetWindowDrawList();
			const float above = (drawList.GetClipRectMin().y - screenTop) / rowHeight;
			const float below = (drawList.GetClipRectMax().y - screenTop) / rowHeight;
			const float total = static_cast<float>(count);

			return {static_cast<std::size_t>(std::clamp(std::floor(above), 0.0f, total)),
					static_cast<std::size_t>(std::clamp(std::ceil(below), 0.0f, total))};
		}

		std::vector<std::size_t> BreaksOf(std::string_view text, std::size_t columns)
		{
			if (columns == Core::RowMap::kNoWrap)
			{
				return {};
			}

			return WrapBytes(text, columns);
		}

		// WARNING: ImGui leaves the item spacing out after the last item, so it comes off here. A
		// region whose scroll is restored declares this height up front, or ImGui clamps the
		// scroll to the height it measured for last frame's rows.
		float HeightOfLines(std::size_t count, float rowHeight)
		{
			return static_cast<float>(count) * rowHeight - ImGui::GetStyle().ItemSpacing.y;
		}

		// WARNING: Each clipper step must end on one Dummy covering its lines. A bare cursor move
		// past the last item extends the region behind ImGui's back and asserts on the frame's
		// last row.
		void ReserveStepLines(const ImVec2& stepPos, std::size_t lineCount, float rowHeight)
		{
			ImGui::SetCursorPos(stepPos);
			ImGui::Dummy(ImVec2(kNoWidth, HeightOfLines(lineCount, rowHeight)));
		}

		ImU32 BandColourFor(DiffRowKind kind, const GitPalette& palette)
		{
			switch (kind)
			{
			case DiffRowKind::Added:
				return palette.DiffAddedLine;
			case DiffRowKind::Removed:
				return palette.DiffRemovedLine;
			case DiffRowKind::HunkHeader:
			case DiffRowKind::HunkSignature:
				return ImGui::GetColorU32(Core::SurfaceColour());
			case DiffRowKind::HunkGap:
			case DiffRowKind::Context:
			case DiffRowKind::Unchanged:
				break;
			}

			return 0;
		}

		ImU32 TextColourFor(DiffRowKind kind, const GitPalette& palette)
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

		void DrawNumber(float x, float y, const LineNumberColumn& numbers, int number, ImU32 colour)
		{
			if (number <= 0)
			{
				return;
			}

			const std::string text = std::to_string(number);

			ImGui::SetCursorPos(ImVec2(numbers.NumberLeft(x, static_cast<std::size_t>(number)), y));
			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopStyleColor();
		}

		std::string HeaderTextFor(const Core::FileDiff& file, std::size_t hunkIndex, DiffRowKind kind,
								  std::optional<DiffSide> side)
		{
			const Core::Hunk& hunk = file.Hunks[hunkIndex];

			if (kind == DiffRowKind::HunkSignature)
			{
				return std::string(HunkScope(hunk.Header));
			}

			std::string text =
				"Hunk " + std::to_string(hunkIndex + 1) + " of " + std::to_string(file.Hunks.size());

			if (side.has_value())
			{
				const int lineCountOnSide = *side == DiffSide::Old ? hunk.OldCount : hunk.NewCount;

				if (lineCountOnSide <= 0)
				{
					return text + " | no lines";
				}
			}

			text += " | lines ";

			const bool showsOldRange = side != DiffSide::New;
			const bool showsBothRanges = !side.has_value();
			const bool showsNewRange = side != DiffSide::Old;

			if (showsOldRange)
			{
				text += LineRangeLabel(hunk.OldStart, hunk.OldCount);
			}

			if (showsBothRanges)
			{
				text += std::format(" {} ", kRightwardsArrow);
			}

			if (showsNewRange)
			{
				text += LineRangeLabel(hunk.NewStart, hunk.NewCount);
			}

			return text;
		}

		int SignatureSearchStart(int hunkStart, int hunkCount)
		{
			return hunkCount > 0 ? hunkStart : hunkStart + 1;
		}

		std::optional<DiffRow> SignatureRowFor(const Core::Hunk& hunk,
											   std::span<const std::string_view> oldLines,
											   std::span<const std::string_view> newLines)
		{
			const std::string_view scope = HunkScope(hunk.Header);

			DiffRow row;
			row.Kind = DiffRowKind::Unchanged;
			row.OldLine =
				SignatureLine(oldLines, SignatureSearchStart(hunk.OldStart, hunk.OldCount), scope);
			row.NewLine =
				SignatureLine(newLines, SignatureSearchStart(hunk.NewStart, hunk.NewCount), scope);

			if (row.OldLine == 0 && row.NewLine == 0)
			{
				return std::nullopt;
			}

			return row;
		}

		float DrawHeaderText(const ImVec2& origin, const std::string& text)
		{
			ImGui::SetCursorPos(origin);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopStyleColor();

			return ImGui::CalcTextSize(text.c_str()).x;
		}

		float DrawHeader(const ImVec2& origin, const std::string& text, DiffRowKind kind)
		{
			if (kind == DiffRowKind::HunkSignature)
			{
				return DrawHeaderText(origin, text);
			}

			Core::PushInterfaceFont();
			const float width = DrawHeaderText(origin, text);
			ImGui::PopFont();

			return width;
		}

		struct RunEndsBefore
		{
			std::size_t Offset = 0;

			bool operator()(const SyntaxRun& run) const { return run.To <= Offset; }
		};

		// WARNING: Measured from the segment's start against the whole prefix, never summed run by
		// run: CalcTextSize rounds up to a whole pixel, and summing that error drifts the tail of a
		// long, densely tokenised line off the monospaced grid.
		float RunStartX(const ImVec2& origin, std::string_view text, const WrapSegment& segment,
						std::size_t offset)
		{
			return origin.x + ImGui::CalcTextSize(text.data() + segment.From, text.data() + offset).x;
		}

		// WARNING: Every run is positioned from `origin` rather than with SameLine(). A run that
		// draws nothing would rewind the cursor and overwrite the row above.
		void DrawLine(const ImVec2& origin, std::string_view text, const std::vector<Token>* tokens,
					  ImU32 ordinaryColour, const SyntaxPalette& syntax,
					  std::span<const std::size_t> breaks, float rowHeight)
		{
			const std::vector<SyntaxRun> runs = RunsFor(text, tokens);
			const auto [firstSegment, endSegment] =
				VisibleSegments(ImGui::GetWindowPos().y - ImGui::GetScrollY() + origin.y,
								breaks.size() + 1, rowHeight);

			for (std::size_t segmentIndex = firstSegment; segmentIndex < endSegment; ++segmentIndex)
			{
				const WrapSegment segment = SegmentOf(text, breaks, segmentIndex);
				const float segmentY = origin.y + static_cast<float>(segmentIndex) * rowHeight;

				const std::vector<SyntaxRun>::const_iterator firstRun =
					std::partition_point(runs.begin(), runs.end(), RunEndsBefore{segment.From});

				for (std::vector<SyntaxRun>::const_iterator runIterator = firstRun;
					 runIterator != runs.end() && runIterator->From < segment.To; ++runIterator)
				{
					const SyntaxRun& run = *runIterator;
					const std::size_t from = std::max(run.From, segment.From);
					const std::size_t to = std::min(run.To, segment.To);

					if (from >= to)
					{
						continue;
					}

					ImGui::SetCursorPos(ImVec2(RunStartX(origin, text, segment, from), segmentY));
					ImGui::PushStyleColor(ImGuiCol_Text,
										  run.Kind.has_value() ? syntax.Of(*run.Kind) : ordinaryColour);
					ImGui::TextUnformatted(text.data() + from, text.data() + to);
					ImGui::PopStyleColor();
				}
			}
		}

		// WARNING: Pushes the mono font itself, because the selection gesture runs with the UI font
		// restored so that the context menu it opens draws like every other menu.
		float MonoWidthOf(std::string_view prefix)
		{
			Core::PushCodeFont();
			const float width = Core::TextWidth(prefix);
			ImGui::PopFont();

			return width;
		}

		void DrawSelected(std::string_view text, Core::ColumnSpan span,
						  std::span<const std::size_t> breaks, float textScreenX, float top,
						  float rowHeight, ImU32 colour)
		{
			const auto [firstSegment, endSegment] = VisibleSegments(top, breaks.size() + 1, rowHeight);

			for (std::size_t segmentIndex = firstSegment; segmentIndex < endSegment; ++segmentIndex)
			{
				const WrapSegment segment = SegmentOf(text, breaks, segmentIndex);
				const std::size_t from = std::clamp(span.From, segment.From, segment.To);
				const std::size_t to = std::clamp(span.To, segment.From, segment.To);

				if (from >= to)
				{
					continue;
				}

				const std::string_view piece = text.substr(segment.From, segment.To - segment.From);

				Core::DrawSelectionSpan(
					*ImGui::GetWindowDrawList(),
					textScreenX + Core::TextWidth(piece.substr(kPieceStart, from - segment.From)),
					textScreenX + Core::TextWidth(piece.substr(kPieceStart, to - segment.From)),
					top + static_cast<float>(segmentIndex) * rowHeight, ImGui::GetTextLineHeight(),
					colour);
			}
		}

		std::size_t LineUnder(float screenY, float startY, float rowHeight, std::size_t count)
		{
			const float yInRegion = screenY - ImGui::GetWindowPos().y + ImGui::GetScrollY() - startY;
			const float line = std::floor(yInRegion / rowHeight);

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

		std::size_t ByteOnSegment(std::string_view text, std::size_t columns, std::size_t segmentIndex,
								  float x, HitTarget target)
		{
			const std::vector<std::size_t> breaks = BreaksOf(text, columns);
			const WrapSegment bytes = SegmentOf(text, breaks, segmentIndex);
			const std::string_view piece = text.substr(bytes.From, bytes.To - bytes.From);
			const std::size_t byteInPiece = target == HitTarget::CharacterUnder
												? ByteUnderX(piece, x, MonoWidthOf)
												: ByteAtX(piece, x, MonoWidthOf);

			return bytes.From + byteInPiece;
		}

		class SelectableRows
		{
		public:
			virtual ~SelectableRows() = default;

			virtual std::size_t RowCount() const = 0;
			virtual std::optional<std::int64_t> LineAt(std::size_t rowIndex) const = 0;
			virtual std::string_view TextAt(std::size_t rowIndex) const = 0;
			virtual std::optional<std::size_t> RowOfLine(std::int64_t line) const = 0;
			virtual std::optional<Core::TextPoint> PointAt(ImVec2 screen, HitTarget target) = 0;
			virtual std::string SelectedText() const = 0;

			std::size_t LengthAt(std::size_t rowIndex) const { return TextAt(rowIndex).size(); }
		};

		std::optional<Core::TextPoint> NearestLine(std::size_t rowIndex, const SelectableRows& rows)
		{
			const std::size_t count = rows.RowCount();

			for (std::size_t distance = 1; distance <= rowIndex || rowIndex + distance < count;
				 ++distance)
			{
				if (distance <= rowIndex)
				{
					if (const std::optional<std::int64_t> line = rows.LineAt(rowIndex - distance))
					{
						return Core::TextPoint{*line, rows.LengthAt(rowIndex - distance)};
					}
				}

				if (rowIndex + distance < count)
				{
					if (const std::optional<std::int64_t> line = rows.LineAt(rowIndex + distance))
					{
						return Core::TextPoint{*line, 0};
					}
				}
			}

			return std::nullopt;
		}

		std::pair<Core::TextPoint, Core::TextPoint> EveryLine(const SelectableRows& rows)
		{
			const std::size_t count = rows.RowCount();

			for (std::size_t firstRow = 0; firstRow < count; ++firstRow)
			{
				if (const std::optional<std::int64_t> fromLine = rows.LineAt(firstRow))
				{
					for (std::size_t lastRow = count; lastRow-- > firstRow;)
					{
						if (const std::optional<std::int64_t> toLine = rows.LineAt(lastRow))
						{
							return {Core::TextPoint{*fromLine, 0},
									Core::TextPoint{*toLine, rows.LengthAt(lastRow)}};
						}
					}
				}
			}

			return {};
		}

		Core::SelectionSource SourceOf(SelectableRows& rows)
		{
			Core::SelectionSource source;
			source.PointAt = [&rows](ImVec2 screen) {
				return rows.PointAt(screen, HitTarget::NearestBoundary);
			};
			source.CharacterAt = [&rows](ImVec2 screen) {
				return rows.PointAt(screen, HitTarget::CharacterUnder);
			};
			source.WordAt = [&rows](Core::TextPoint at) {
				const std::optional<std::size_t> rowIndex = rows.RowOfLine(at.Line);

				return rowIndex.has_value()
						   ? Core::WordSpanAt(rows.TextAt(*rowIndex), at.Column, Core::WordKind::Code)
						   : Core::ColumnSpan{};
			};
			source.LineLength = [&rows](std::int64_t line) -> std::size_t {
				const std::optional<std::size_t> rowIndex = rows.RowOfLine(line);

				return rowIndex.has_value() ? rows.LengthAt(*rowIndex) : 0;
			};
			source.Everything = [&rows]() {
				return EveryLine(rows);
			};
			source.SelectedText = [&rows]() {
				return rows.SelectedText();
			};

			return source;
		}

		struct DiffTextSources
		{
			const Core::FileDiff& File;
			std::span<const std::string_view> OldLines;
			std::span<const std::string_view> NewLines;
			const FileTokens& Tokens;
		};

		struct RegionMetrics
		{
			const DiffRowLines& Lines;
			float StartY = 0.0f;
			float RowHeight = 0.0f;
			float TextScreenX = 0.0f;
			std::size_t Columns = 0;
			bool Pressing = false;
		};

		class UnifiedSelectableRows final : public SelectableRows
		{
		public:
			UnifiedSelectableRows(const std::vector<DiffRow>& rows, const DiffTextSources& sources,
								  const Core::TextSelection& selection, const RegionMetrics& metrics)
				: m_Rows(rows), m_Sources(sources), m_Selection(selection), m_Metrics(metrics)
			{
			}

			std::size_t RowCount() const override { return m_Rows.size(); }

			std::optional<std::int64_t> LineAt(std::size_t rowIndex) const override
			{
				return UnifiedLineOf(m_Rows, rowIndex);
			}

			std::string_view TextAt(std::size_t rowIndex) const override
			{
				return ResolveRow(m_Sources.File, m_Rows[rowIndex], DiffSide::New, m_Sources.OldLines,
								  m_Sources.NewLines, m_Sources.Tokens)
					.Text.value_or(std::string_view{});
			}

			std::optional<std::size_t> RowOfLine(std::int64_t line) const override
			{
				if (line < 0 || static_cast<std::size_t>(line) >= m_Rows.size())
				{
					return std::nullopt;
				}

				return static_cast<std::size_t>(line);
			}

			std::optional<Core::TextPoint> PointAt(ImVec2 screen, HitTarget target) override
			{
				if (m_Rows.empty())
				{
					return std::nullopt;
				}

				const std::size_t screenLine = LineUnder(screen.y, m_Metrics.StartY,
														 m_Metrics.RowHeight, m_Metrics.Lines.TotalLines());
				const std::size_t rowIndex = m_Metrics.Lines.RowOfLine(screenLine);

				if (const std::optional<std::int64_t> line = LineAt(rowIndex))
				{
					const std::string_view text = TextAt(rowIndex);
					const float x = screen.x - m_Metrics.TextScreenX;
					const std::size_t segmentIndex =
						screenLine - m_Metrics.Lines.FirstLineOf(rowIndex);

					return Core::TextPoint{
						*line, ByteOnSegment(text, m_Metrics.Columns, segmentIndex, x, target)};
				}

				if (m_Metrics.Pressing)
				{
					return std::nullopt;
				}

				return NearestLine(rowIndex, *this);
			}

			std::string SelectedText() const override
			{
				return SelectedUnifiedText(m_Selection, m_Rows, m_Sources.File, m_Sources.OldLines,
										   m_Sources.NewLines, m_Sources.Tokens);
			}

		private:
			const std::vector<DiffRow>& m_Rows;
			DiffTextSources m_Sources;
			const Core::TextSelection& m_Selection;
			RegionMetrics m_Metrics;
		};

		struct SideBySideGeometry
		{
			float InnerLeft = 0.0f;
			float RightPaneX = 0.0f;
			float PaneWidth = 0.0f;
			bool Extending = false;
		};

		class SideBySideSelectableRows final : public SelectableRows
		{
		public:
			SideBySideSelectableRows(const std::vector<DiffPair>& pairs, const DiffTextSources& sources,
									 const Core::TextSelection& selection, DiffSide& selectionSide,
									 const RegionMetrics& metrics, const SideBySideGeometry& geometry)
				: m_Pairs(pairs),
				  m_Sources(sources),
				  m_Selection(selection),
				  m_SelectionSide(selectionSide),
				  m_Metrics(metrics),
				  m_Geometry(geometry)
			{
			}

			std::size_t RowCount() const override { return m_Pairs.size(); }

			std::optional<std::int64_t> LineAt(std::size_t rowIndex) const override
			{
				return SideLineOf(m_Pairs[rowIndex], m_SelectionSide);
			}

			std::string_view TextAt(std::size_t rowIndex) const override
			{
				const DiffPair& pair = m_Pairs[rowIndex];
				const std::optional<DiffRow>& row =
					m_SelectionSide == DiffSide::Old ? pair.Left : pair.Right;

				if (!row.has_value())
				{
					return std::string_view{};
				}

				return ResolveRow(m_Sources.File, *row, m_SelectionSide, m_Sources.OldLines,
								  m_Sources.NewLines, m_Sources.Tokens)
					.Text.value_or(std::string_view{});
			}

			std::optional<std::size_t> RowOfLine(std::int64_t line) const override
			{
				for (std::size_t rowIndex = 0; rowIndex < m_Pairs.size(); ++rowIndex)
				{
					if (LineAt(rowIndex) == line)
					{
						return rowIndex;
					}
				}

				return std::nullopt;
			}

			std::optional<Core::TextPoint> PointAt(ImVec2 screen, HitTarget target) override
			{
				if (m_Pairs.empty())
				{
					return std::nullopt;
				}

				if (m_Metrics.Pressing && !m_Geometry.Extending)
				{
					m_SelectionSide = screen.x < m_Geometry.InnerLeft + m_Geometry.RightPaneX
										  ? DiffSide::Old
										  : DiffSide::New;
				}

				const float paneX = m_SelectionSide == DiffSide::Old ? 0.0f : m_Geometry.RightPaneX;
				const float paneLeft = m_Geometry.InnerLeft + paneX;
				const float x =
					std::clamp(screen.x, paneLeft, paneLeft + std::max(m_Geometry.PaneWidth, 0.0f)) -
					(m_Metrics.TextScreenX + paneX);

				const std::size_t screenLine = LineUnder(screen.y, m_Metrics.StartY,
														 m_Metrics.RowHeight, m_Metrics.Lines.TotalLines());
				const std::size_t rowIndex = m_Metrics.Lines.RowOfLine(screenLine);

				if (const std::optional<std::int64_t> line = LineAt(rowIndex))
				{
					const std::size_t segmentIndex =
						screenLine - m_Metrics.Lines.FirstLineOf(rowIndex);

					return Core::TextPoint{*line, ByteOnSegment(TextAt(rowIndex), m_Metrics.Columns,
																segmentIndex, x, target)};
				}

				if (m_Metrics.Pressing)
				{
					return std::nullopt;
				}

				return NearestLine(rowIndex, *this);
			}

			std::string SelectedText() const override
			{
				return SelectedSideText(m_Selection, m_SelectionSide, m_Pairs, m_Sources.File,
										m_Sources.OldLines, m_Sources.NewLines, m_Sources.Tokens);
			}

		private:
			const std::vector<DiffPair>& m_Pairs;
			DiffTextSources m_Sources;
			const Core::TextSelection& m_Selection;
			DiffSide& m_SelectionSide;
			RegionMetrics m_Metrics;
			SideBySideGeometry m_Geometry;
		};
	}

	struct DiffView::SideBySideContext
	{
		const std::vector<DiffPair>& Pairs;
		const Core::FileDiff& File;
		const GitPalette& Palette;
		const SyntaxPalette& Syntax;
		ImU32 Selection = 0;
		const FileTokens& Tokens;
		const DiffRowLines& Lines;
		SideBySidePanes Panes;
		ImVec2 InnerMin;
		ImVec2 InnerMax;
		LineNumberColumn Numbers;
		float RowHeight = 0.0f;
		std::size_t Columns = 0;
	};

	std::optional<WholeFileNote> WholeFileNoteFor(const DiffViewOptions& options, bool textsHeld, const TextsWait& wait)
	{
		const bool showsWholeFile = options.Scope == DiffScope::WholeFile;
		const bool textsWanted = showsWholeFile || options.ColourSyntax;

		if (textsHeld || !textsWanted)
		{
			return std::nullopt;
		}

		switch (wait.Progress)
		{
		case TextsProgress::Loading:
			return WholeFileNote{.Text = showsWholeFile ? "Loading the whole file" : "Colouring", .Loading = true};

		case TextsProgress::Failed:
			return WholeFileNote{.Text = wait.Reason.empty() ? std::string{"Whole file unavailable"}
															 : "Whole file unavailable: " + wait.Reason};

		case TextsProgress::Unknown:
			break;
		}

		return std::nullopt;
	}

	void DiffView::Render(const Core::FileDiff& file, const GitPalette& palette,
						  const SyntaxPalette& syntax, ImU32 selection,
						  const Core::Ref<const FileTexts>& wholeFileTexts, const FileTokens& tokens,
						  const DiffViewOptions& options)
	{
		m_Options = options;

		const TextsWait wait = std::exchange(m_TextsWait, TextsWait{});
		RenderToolbar(file, WholeFileNoteFor(m_Options, wholeFileTexts != nullptr, wait));

		const bool wordWrap = m_Options.WordWrap;

		if (wholeFileTexts != nullptr)
		{
			RebuildLineCache(wholeFileTexts);
		}
		else
		{
			ClearLineCache();
		}

		// WARNING: The cache is cleared before this return. A binary file's owner hands over no
		// texts, so returning first would keep the previous file's cache for as long as the reader
		// stays on binary files.
		if (file.IsBinary)
		{
			Core::EmptyState("Binary file — no textual diff.");
			return;
		}

		const bool wholeFile = m_Options.Scope == DiffScope::WholeFile && wholeFileTexts != nullptr;

		const Layout layout{m_Options.Mode, wholeFile ? DiffScope::WholeFile : DiffScope::Hunks, wordWrap};
		const bool layoutChanged = layout != m_DrawnLayout;
		m_RestoreKeptLine = layoutChanged && IsKeptLineOf(file);
		m_DrawnLayout = layout;

		const DiffSelectionContext context{
			.Path = ShownPath(file),
			.Mode = m_Options.Mode,
			.Scope = layout.Scope,
			.Side = m_Options.Mode == DiffViewMode::SideBySide ? m_SelectionSide : DiffSide::New,
			.ContentHash = ContentHashOf(file),
			.TextsHash = wholeFile ? m_TextsHash : 0};

		if (context != m_SelectionContext)
		{
			m_Selection.Clear();
			m_Gesture.Cancel();
			m_SelectionContext = context;
		}

		if (wholeFile)
		{
			const WholeFileRows rows = BuildWholeFileRows(file, m_OldLines.size(), m_NewLines.size());

			if (m_Options.Mode == DiffViewMode::Unified)
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
			const bool wholeFileOnItsWay =
				m_Options.Scope == DiffScope::WholeFile && wait.Progress == TextsProgress::Loading;

			if (!wholeFileOnItsWay)
			{
				Core::EmptyState("No changes in this file.");
			}

			return;
		}

		if (m_Options.Mode == DiffViewMode::Unified)
		{
			RenderUnified(BuildUnifiedRows(file), file, palette, syntax, selection, tokens, wordWrap);
			return;
		}

		RenderSideBySide(BuildSideBySideRows(file), file, palette, syntax, selection, tokens, wordWrap);
	}

	bool DiffView::IsKeptLineOf(const Core::FileDiff& file) const
	{
		return m_KeptLine.has_value() && m_KeptLine->Path == ShownPath(file);
	}

	// WARNING: A new snapshot can reuse the address, so the address alone does not identify the
	// file. Hashing reads every line, so it is done once per file, not once per frame.
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
		options.ScrollsHorizontally = !wordWrap;
		options.CopyKey = true;
		options.SelectAllKey = true;
		options.MenuId = "##diffMenu";

		const Core::SelectionGestureResult result = m_Gesture.Update(m_Selection, source, options);

		const bool copiedFromMenu = result.Copied && result.FromMenu;

		if (copiedFromMenu && OnCopied)
		{
			OnCopied(result.CopiedText);
		}
	}

	void DiffView::RenderToolbar(const Core::FileDiff& file, const std::optional<WholeFileNote>& note)
	{
		const std::filesystem::path& path = ShownPath(file);

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Core::ToUtf8(path).c_str());

		ImGui::SameLine();

		DiffViewOptions chosen = m_Options;

		if (Core::DrawIconButton("##sideBySide", ICON_LC_COLUMNS_2,
						   m_Options.Mode == DiffViewMode::SideBySide, "Side by side"))
		{
			chosen.Mode = DiffViewMode::SideBySide;
		}

		ImGui::SameLine(kNoOffsetFromStart, Core::Scaled(kModeButtonGap));

		if (Core::DrawIconButton("##unified", ICON_LC_ROWS_2,
						   m_Options.Mode == DiffViewMode::Unified, "Unified"))
		{
			chosen.Mode = DiffViewMode::Unified;
		}

		ImGui::SameLine();
		Core::DrawIconSeparator();
		ImGui::SameLine();

		if (Core::DrawIconButton("##hunks", ICON_LC_CHEVRONS_DOWN_UP,
						   m_Options.Scope == DiffScope::Hunks, "Hunks only"))
		{
			chosen.Scope = DiffScope::Hunks;
		}

		ImGui::SameLine(kNoOffsetFromStart, Core::Scaled(kModeButtonGap));

		if (Core::DrawIconButton("##wholeFile", ICON_LC_CHEVRONS_UP_DOWN,
						   m_Options.Scope == DiffScope::WholeFile, "Whole file"))
		{
			chosen.Scope = DiffScope::WholeFile;
		}

		ImGui::SameLine();
		Core::DrawIconSeparator();

		ImGui::SameLine();

		if (Core::DrawIconButton("##colourSyntax", ICON_LC_PALETTE, m_Options.ColourSyntax,
						   "Colour syntax"))
		{
			chosen.ColourSyntax = !m_Options.ColourSyntax;
		}

		ImGui::SameLine(kNoOffsetFromStart, Core::Scaled(kModeButtonGap));

		if (Core::DrawIconButton("##wordWrap", ICON_LC_WRAP_TEXT, m_Options.WordWrap, "Word wrap"))
		{
			chosen.WordWrap = !m_Options.WordWrap;
		}

		// WARNING: The owner's options come back only next frame, so the rows below are drawn with the choice now or
		// they show the old layout for a frame.
		if (chosen != m_Options)
		{
			m_Options = chosen;

			if (OnSetOptions)
			{
				OnSetOptions(chosen);
			}
		}

		// WARNING: NewPath and not ShownPath: the latter falls back to OldPath for a deleted file,
		// which has nothing left on disk to open.
		const bool canOpenFile = OnOpenFile && !file.NewPath.empty();

		if (canOpenFile || OnOpenAsTab)
		{
			ImGui::SameLine();
			Core::DrawIconSeparator();
		}

		if (canOpenFile)
		{
			ImGui::SameLine();

			if (Core::DrawIconButton("##openFile", ICON_LC_FILE_PEN, kNotEngaged, "Open file"))
			{
				OnOpenFile(file.NewPath);
			}
		}

		if (OnOpenAsTab)
		{
			ImGui::SameLine();

			if (Core::DrawIconButton("##openAsTab", ICON_LC_SQUARE_PLUS, kNotEngaged, "Open as tab"))
			{
				OnOpenAsTab();
			}
		}

		if (note.has_value())
		{
			ImGui::SameLine();
			DrawWholeFileNote(*note, OnClose ? PaneCloseRoom() : kNoWidth);
		}

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
		m_TextsHash = TextsContentHash(*texts);
	}

	void DiffView::ClearLineCache()
	{
		m_CachedTexts.reset();
		m_OldLines.clear();
		m_NewLines.clear();
		m_TextsHash = 0;
	}

	void DiffView::RenderUnified(const std::vector<DiffRow>& rows, const Core::FileDiff& file,
								 const GitPalette& palette, const SyntaxPalette& syntax,
								 ImU32 selection, const FileTokens& tokens, bool wordWrap)
	{
		Core::PushCodeFont();
		const float widthLastFrame = m_UnifiedInnerWidthLastFrame > 0.0f
										 ? m_UnifiedInnerWidthLastFrame
										 : ImGui::GetContentRegionAvail().x;
		const float numberWidthLastFrame = NumberColumnOf(file, m_OldLines.size(), m_NewLines.size()).Width();
		const std::size_t columnsLastFrame =
			wordWrap ? WrapColumnsAcross(widthLastFrame - UnifiedTextOffset(numberWidthLastFrame)) : Core::RowMap::kNoWrap;
		ImGui::PopFont();

		const DiffRowLines& linesLastFrame = RowLinesFor(std::span(rows), file, tokens, columnsLastFrame);
		const std::size_t totalLinesLastFrame = linesLastFrame.TotalLines();

		const std::optional<float> restoredScroll =
			TakeRestoredScroll(std::span(rows), linesLastFrame, kFirstRowAtTop);

		if (restoredScroll.has_value() || wordWrap)
		{
			ImGui::SetNextWindowScroll(ImVec2(wordWrap ? kScrollToStart : kKeepScroll,
											  restoredScroll.value_or(kKeepScroll)));
		}

		if (restoredScroll.has_value())
		{
			ImGui::SetNextWindowContentSize(
				ImVec2(kNoWidth, HeightOfLines(totalLinesLastFrame, m_RowHeightLastFrame)));
		}

		if (!ImGui::BeginChild("unified", kFillAvailable, ImGuiChildFlags_None,
							   wordWrap ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::EndChild();
			return;
		}

		Core::PushCodeFont();

		const LineNumberColumn numbers = NumberColumnOf(file, m_OldLines.size(), m_NewLines.size());
		const float numberWidth = numbers.Width();
		const float textOffset = UnifiedTextOffset(numberWidth);
		const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
		const float startX = ImGui::GetCursorPosX();
		const float startY = ImGui::GetCursorPosY();

		m_RowHeightLastFrame = rowHeight;

		m_UnifiedInnerWidthLastFrame = ImGui::GetWindowDrawList()->GetClipRectMax().x -
									   ImGui::GetWindowDrawList()->GetClipRectMin().x;

		const std::size_t columns =
			wordWrap ? WrapColumnsAcross(m_UnifiedInnerWidthLastFrame - textOffset)
					 : Core::RowMap::kNoWrap;
		const DiffRowLines& lines = RowLinesFor(std::span(rows), file, tokens, columns);
		const bool rewrapped =
			KeepLineAcrossRewrap(std::span(rows), lines, file, startY, columns, columnsLastFrame);

		ImGuiListClipper clipper;
		clipper.Begin(Core::Narrow<int>(lines.TotalLines()), rowHeight);

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

			for (std::size_t rowIndex = std::max(lines.RowOfLine(firstLine), drawnTo);
				 rowIndex < endRow; ++rowIndex)
			{
				const DiffRow& row = rows[rowIndex];

				const float rowOffsetY =
					(static_cast<float>(lines.FirstLineOf(rowIndex)) - static_cast<float>(firstLine)) *
					rowHeight;
				const ImVec2 rowPos(stepPos.x, stepPos.y + rowOffsetY);
				ImGui::SetCursorPos(rowPos);

				const ImVec2 rowScreenPos = ImGui::GetCursorScreenPos();
				const float bandHeight = HeightOfLines(lines.LinesOf(rowIndex), rowHeight);

				if (const ImU32 band = BandColourFor(row.Kind, palette); band != 0)
				{
					ImGui::GetWindowDrawList()->AddRectFilled(
						rowScreenPos,
						ImVec2(rowScreenPos.x + ImGui::GetContentRegionAvail().x,
							   rowScreenPos.y + bandHeight),
						band);
				}

				const DiffRow drawn = DrawnRow(file, row);

				if (drawn.Kind == DiffRowKind::HunkHeader || drawn.Kind == DiffRowKind::HunkSignature)
				{
					DrawHeader(ImVec2(rowPos.x + textOffset, rowPos.y),
							   HeaderTextFor(file, row.Hunk, row.Kind, std::nullopt), row.Kind);
					continue;
				}

				if (row.Kind == DiffRowKind::HunkGap)
				{
					continue;
				}

				DrawNumber(rowPos.x, rowPos.y, numbers, drawn.OldLine, palette.LineNumber);
				DrawNumber(rowPos.x + numberWidth, rowPos.y, numbers, drawn.NewLine, palette.LineNumber);

				const ResolvedLine resolved =
					ResolveRow(file, drawn, DiffSide::New, m_OldLines, m_NewLines, tokens);

				if (!resolved.Text.has_value())
				{
					continue;
				}

				const std::vector<std::size_t> breaks = BreaksOf(*resolved.Text, columns);

				if (const std::optional<std::int64_t> line = UnifiedLineOf(rows, rowIndex))
				{
					if (const std::optional<Core::ColumnSpan> span =
							m_Selection.SpanOn(*line, resolved.Text->size()))
					{
						DrawSelected(*resolved.Text, *span, breaks, rowScreenPos.x + textOffset,
									 rowScreenPos.y, rowHeight, selection);
					}
				}

				DrawLine(ImVec2(rowPos.x + textOffset, rowPos.y), *resolved.Text,
						 m_Options.ColourSyntax ? resolved.Tokens : nullptr,
						 TextColourFor(drawn.Kind, palette), syntax, breaks, rowHeight);
			}

			drawnTo = std::max(drawnTo, endRow);

			ReserveStepLines(stepPos, endLine - firstLine, rowHeight);
		}

		if (!rewrapped)
		{
			RememberKeptLine(std::span(rows), lines, file, startY);
		}

		ImGui::PopFont();

		SelectUnified(rows, lines, file, tokens, startX, startY, textOffset, rowHeight, columns);

		ImGui::EndChild();
	}

	void DiffView::SelectUnified(const std::vector<DiffRow>& rows, const DiffRowLines& lines,
								 const Core::FileDiff& file, const FileTokens& tokens, float startX,
								 float startY, float textOffset, float rowHeight, std::size_t columns)
	{
		const DiffTextSources sources{file, m_OldLines, m_NewLines, tokens};
		const RegionMetrics metrics{
			.Lines = lines,
			.StartY = startY,
			.RowHeight = rowHeight,
			.TextScreenX = ImGui::GetWindowPos().x - ImGui::GetScrollX() + startX + textOffset,
			.Columns = columns,
			.Pressing = ImGui::IsMouseClicked(ImGuiMouseButton_Left)};

		UnifiedSelectableRows selectableRows(rows, sources, m_Selection, metrics);
		const Core::SelectionSource source = SourceOf(selectableRows);

		UpdateSelection(source, columns != Core::RowMap::kNoWrap);
	}

	void DiffView::RenderSideBySide(const std::vector<DiffPair>& pairs, const Core::FileDiff& file,
									const GitPalette& palette, const SyntaxPalette& syntax,
									ImU32 selection, const FileTokens& tokens, bool wordWrap)
	{
		if (const DiffMeasurement measurement = MeasurementOf(file, m_Options.Scope);
			measurement != m_WidestLineMeasuredFor)
		{
			m_WidestLineMeasuredFor = measurement;
			m_WidestSideLine = 0.0f;
		}

		// WARNING: The content size is declared before the region opens, when its own width is not
		// known yet: it takes a border and, once the rows stop fitting, a scrollbar off whatever it
		// was given.
		const float innerWidth = m_SideInnerWidthLastFrame > 0.0f
									 ? m_SideInnerWidthLastFrame + ImGui::GetStyle().ChildBorderSize * 2.0f
									 : ImGui::GetContentRegionAvail().x;
		const float padding = ImGui::GetStyle().WindowPadding.x;

		Core::PushCodeFont();
		const SideBySidePanes panesLastFrame = SplitIntoPanes(
			m_SideInnerWidthLastFrame > 0.0f ? m_SideInnerWidthLastFrame : innerWidth);
		const std::size_t columnsLastFrame =
			PaneColumns(wordWrap, panesLastFrame.PaneWidth, padding,
						NumberColumnOf(file, m_OldLines.size(), m_NewLines.size()).Width());
		ImGui::PopFont();

		const DiffRowLines& linesLastFrame = RowLinesFor(std::span(pairs), file, tokens, columnsLastFrame);
		const std::size_t totalLinesLastFrame = linesLastFrame.TotalLines();

		const std::optional<float> restoredScroll = TakeRestoredScroll(
			std::span(pairs), linesLastFrame, ImGui::GetStyle().WindowPadding.y);

		if (restoredScroll.has_value() || wordWrap)
		{
			ImGui::SetNextWindowScroll(ImVec2(wordWrap ? kScrollToStart : kKeepScroll,
											  restoredScroll.value_or(kKeepScroll)));
		}

		ImGui::SetNextWindowContentSize(
			ImVec2(SideBySideContentWidth(innerWidth, wordWrap ? 0.0f : m_WidestSideLine, padding),
				   restoredScroll.has_value()
					   ? HeightOfLines(totalLinesLastFrame, m_RowHeightLastFrame)
					   : kAutoHeight));

		if (!ImGui::BeginChild("pairs", kFillAvailable, ImGuiChildFlags_Borders,
							   ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::EndChild();
			return;
		}

		Core::PushCodeFont();

		const LineNumberColumn numbers = NumberColumnOf(file, m_OldLines.size(), m_NewLines.size());
		const float numberWidth = numbers.Width();
		const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
		const float startX = ImGui::GetCursorPosX();
		const float startY = ImGui::GetCursorPosY();

		m_RowHeightLastFrame = rowHeight;

		// WARNING: The split is measured from the region's clipping rectangle, which is the visible
		// area with the border and scrollbar already taken off. A region its parent had clipped
		// would be measured as the sliver that showed; BeginChild sizing it to fit keeps that from
		// happening.
		const ImVec2 innerMin = ImGui::GetWindowDrawList()->GetClipRectMin();
		const ImVec2 innerMax = ImGui::GetWindowDrawList()->GetClipRectMax();

		m_SideInnerWidthLastFrame = innerMax.x - innerMin.x;

		const SideBySidePanes panes = SplitIntoPanes(m_SideInnerWidthLastFrame);

		const std::size_t columns = PaneColumns(wordWrap, panes.PaneWidth, padding, numberWidth);
		const DiffRowLines& lines = RowLinesFor(std::span(pairs), file, tokens, columns);
		const bool rewrapped =
			KeepLineAcrossRewrap(std::span(pairs), lines, file, startY, columns, columnsLastFrame);

		const SideBySideContext context{.Pairs = pairs,
										.File = file,
										.Palette = palette,
										.Syntax = syntax,
										.Selection = selection,
										.Tokens = tokens,
										.Lines = lines,
										.Panes = panes,
										.InnerMin = innerMin,
										.InnerMax = innerMax,
										.Numbers = numbers,
										.RowHeight = rowHeight,
										.Columns = columns};

		float widest = 0.0f;

		ImGuiListClipper clipper;
		clipper.Begin(Core::Narrow<int>(lines.TotalLines()), rowHeight);

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

			const std::size_t firstRow = std::max(lines.RowOfLine(firstLine), drawnTo);
			const std::size_t endRow = lines.RowOfLine(endLine - 1) + 1;

			const float widestOld =
				DrawPane(context, DiffSide::Old, firstRow, endRow, firstLine, stepPos, stepScreenY);
			const float widestNew =
				DrawPane(context, DiffSide::New, firstRow, endRow, firstLine, stepPos, stepScreenY);
			widest = std::max(std::max(widest, widestOld), widestNew);

			drawnTo = std::max(drawnTo, endRow);

			ReserveStepLines(stepPos, endLine - firstLine, rowHeight);
		}

		m_WidestSideLine = std::max(m_WidestSideLine, widest);

		if (!rewrapped)
		{
			RememberKeptLine(std::span(pairs), lines, file, startY);
		}

		// WARNING: A filled rectangle and not AddLine, which would centre on the split and lay half
		// of itself over the right pane's first pixel. Drawn outside both panes' clipping
		// rectangles, so neither cuts it off and it does not slide with the horizontal scroll.
		ImGui::GetWindowDrawList()->AddRectFilled(
			ImVec2(innerMin.x + panes.RightPaneX, innerMin.y),
			ImVec2(innerMin.x + panes.RightPaneX + kSplitLineWidth, innerMax.y),
			ImGui::GetColorU32(ImGuiCol_Border));

		ImGui::PopFont();

		SelectSideBySide(pairs, lines, file, tokens, startX, startY, numberWidth, rowHeight,
						 innerMin.x, panes.RightPaneX, panes.PaneWidth, columns);

		ImGui::EndChild();
	}

	// WARNING: One clipping rectangle covers all the rows of a step in a pane. Pushing one per row
	// makes ImGui start a fresh draw command each time, a hundred and twenty for sixty rows where
	// the whole region should cost two.
	float DiffView::DrawPane(const SideBySideContext& context, DiffSide side, std::size_t firstRow,
							 std::size_t endRow, std::size_t firstLine, const ImVec2& stepPos,
							 float stepScreenY) const
	{
		const bool isOldPane = side == DiffSide::Old;
		const float paneX = isOldPane ? 0.0f : context.Panes.RightPaneX;
		const float paneScreenX = context.InnerMin.x + paneX;
		const float numberWidth = context.Numbers.Width();
		const float rowHeight = context.RowHeight;
		float widest = 0.0f;

		ImGui::PushClipRect(ImVec2(paneScreenX, context.InnerMin.y),
							ImVec2(paneScreenX + context.Panes.PaneWidth, context.InnerMax.y),
							kIntersectWithCurrentClip);

		for (std::size_t rowIndex = firstRow; rowIndex < endRow; ++rowIndex)
		{
			const DiffPair& pair = context.Pairs[rowIndex];
			const std::optional<DiffRow>& row = isOldPane ? pair.Left : pair.Right;

			if (!row.has_value())
			{
				continue;
			}

			const float offsetY =
				(static_cast<float>(context.Lines.FirstLineOf(rowIndex)) - static_cast<float>(firstLine)) *
				rowHeight;
			const ImVec2 rowPos = ImVec2(stepPos.x + paneX, stepPos.y + offsetY);
			const float rowScreenY = stepScreenY + offsetY;

			if (const ImU32 band = BandColourFor(row->Kind, context.Palette); band != 0)
			{
				ImGui::GetWindowDrawList()->AddRectFilled(
					ImVec2(paneScreenX, rowScreenY),
					ImVec2(paneScreenX + context.Panes.PaneWidth,
						   rowScreenY + static_cast<float>(context.Lines.LinesOf(rowIndex)) * rowHeight -
							   ImGui::GetStyle().ItemSpacing.y),
					band);
			}

			const DiffRow drawn = DrawnRow(context.File, *row);

			if (drawn.Kind == DiffRowKind::HunkHeader || drawn.Kind == DiffRowKind::HunkSignature)
			{
				const float width = DrawHeader(ImVec2(rowPos.x + numberWidth, rowPos.y),
											   HeaderTextFor(context.File, row->Hunk, row->Kind, side), row->Kind);
				widest = std::max(widest, numberWidth + width);
				continue;
			}

			if (row->Kind == DiffRowKind::HunkGap)
			{
				continue;
			}

			DrawNumber(rowPos.x, rowPos.y, context.Numbers, isOldPane ? drawn.OldLine : drawn.NewLine,
					   context.Palette.LineNumber);

			const ResolvedLine resolved =
				ResolveRow(context.File, drawn, side, m_OldLines, m_NewLines, context.Tokens);

			if (!resolved.Text.has_value())
			{
				continue;
			}

			const std::vector<std::size_t> breaks = BreaksOf(*resolved.Text, context.Columns);

			if (side == m_SelectionSide)
			{
				if (const std::optional<std::int64_t> line = SideLineOf(pair, m_SelectionSide))
				{
					if (const std::optional<Core::ColumnSpan> span =
							m_Selection.SpanOn(*line, resolved.Text->size()))
					{
						DrawSelected(*resolved.Text, *span, breaks,
									 ImGui::GetWindowPos().x - ImGui::GetScrollX() + rowPos.x +
										 numberWidth,
									 rowScreenY, rowHeight, context.Selection);
					}
				}
			}

			DrawLine(ImVec2(rowPos.x + numberWidth, rowPos.y), *resolved.Text,
					 m_Options.ColourSyntax ? resolved.Tokens : nullptr,
					 TextColourFor(drawn.Kind, context.Palette), context.Syntax, breaks, rowHeight);

			widest = std::max(widest, numberWidth + Core::TextWidth(*resolved.Text));
		}

		ImGui::PopClipRect();

		return widest;
	}

	void DiffView::SelectSideBySide(const std::vector<DiffPair>& pairs, const DiffRowLines& lines,
									const Core::FileDiff& file, const FileTokens& tokens,
									float startX, float startY, float textOffset, float rowHeight,
									float innerLeft, float rightPaneX, float paneWidth,
									std::size_t columns)
	{
		const DiffTextSources sources{file, m_OldLines, m_NewLines, tokens};
		const RegionMetrics metrics{
			.Lines = lines,
			.StartY = startY,
			.RowHeight = rowHeight,
			.TextScreenX = ImGui::GetWindowPos().x - ImGui::GetScrollX() + startX + textOffset,
			.Columns = columns,
			.Pressing = ImGui::IsMouseClicked(ImGuiMouseButton_Left)};
		const SideBySideGeometry geometry{
			.InnerLeft = innerLeft,
			.RightPaneX = rightPaneX,
			.PaneWidth = paneWidth,
			.Extending = ImGui::GetIO().KeyShift && !m_Selection.IsEmpty()};

		SideBySideSelectableRows selectableRows(pairs, sources, m_Selection, m_SelectionSide, metrics,
												geometry);
		const Core::SelectionSource source = SourceOf(selectableRows);

		UpdateSelection(source, columns != Core::RowMap::kNoWrap);

		if (m_SelectionContext.has_value())
		{
			m_SelectionContext->Side = m_SelectionSide;
		}
	}

	DiffRow DiffView::DrawnRow(const Core::FileDiff& file, const DiffRow& row) const
	{
		if (row.Kind != DiffRowKind::HunkSignature)
		{
			return row;
		}

		return SignatureRowFor(file.Hunks[row.Hunk], m_OldLines, m_NewLines).value_or(row);
	}

	std::size_t DiffView::LinesOfRow(const Core::FileDiff& file, const DiffRow& row, DiffSide side,
									 const FileTokens& tokens, std::size_t columns) const
	{
		const DiffRow drawn = DrawnRow(file, row);

		if (drawn.Kind == DiffRowKind::HunkHeader || drawn.Kind == DiffRowKind::HunkSignature ||
			drawn.Kind == DiffRowKind::HunkGap)
		{
			return 1;
		}

		const ResolvedLine resolved = ResolveRow(file, drawn, side, m_OldLines, m_NewLines, tokens);

		return resolved.Text.has_value() ? BreaksOf(*resolved.Text, columns).size() + 1 : 1;
	}

	std::size_t DiffView::ScreenLinesOf(const DiffRow& row, const Core::FileDiff& file,
										const FileTokens& tokens, std::size_t columns) const
	{
		return LinesOfRow(file, row, DiffSide::New, tokens, columns);
	}

	std::size_t DiffView::ScreenLinesOf(const DiffPair& pair, const Core::FileDiff& file,
										const FileTokens& tokens, std::size_t columns) const
	{
		const std::size_t oldLines =
			pair.Left.has_value() ? LinesOfRow(file, *pair.Left, DiffSide::Old, tokens, columns) : 1;
		const std::size_t newLines =
			pair.Right.has_value() ? LinesOfRow(file, *pair.Right, DiffSide::New, tokens, columns) : 1;

		return std::max(oldLines, newLines);
	}

	template<typename Row>
	const DiffRowLines& DiffView::RowLinesFor(std::span<const Row> rows, const Core::FileDiff& file,
											  const FileTokens& tokens, std::size_t columns)
	{
		if (columns == Core::RowMap::kNoWrap)
		{
			m_RowLines = DiffRowLines::Uniform(rows.size());
			m_RowLinesKey.reset();
			return m_RowLines;
		}

		RowLinesKey key{.Path = ShownPath(file),
						.Mode = m_DrawnLayout.Mode,
						.Scope = m_DrawnLayout.Scope,
						.ContentHash = ContentHashOf(file),
						.TextsHash = m_TextsHash,
						.Rows = rows.size(),
						.Columns = columns};

		if (key == m_RowLinesKey)
		{
			return m_RowLines;
		}

		std::vector<std::size_t> screenLinesPerRow;
		screenLinesPerRow.reserve(rows.size());

		for (const Row& row : rows)
		{
			screenLinesPerRow.push_back(ScreenLinesOf(row, file, tokens, columns));
		}

		m_RowLines = DiffRowLines::FromCounts(screenLinesPerRow);
		m_RowLinesKey = std::move(key);

		return m_RowLines;
	}

	template<typename Row>
	std::optional<float> DiffView::TakeRestoredScroll(std::span<const Row> rows,
													  const DiffRowLines& lines, float startY)
	{
		const bool restoreRequested = m_RestoreKeptLine;
		m_RestoreKeptLine = false;

		if (!restoreRequested || !m_KeptLine.has_value() || rows.empty() ||
			m_RowHeightLastFrame <= 0.0f)
		{
			return std::nullopt;
		}

		return ScrollToKeptLine(rows, lines, startY);
	}

	// WARNING: A kept line the new rows do not have, when nothing of a hunk was on screen, is
	// stood in for by the hunk after it, put where the line was.
	template<typename Row>
	float DiffView::ScrollToKeptLine(std::span<const Row> rows, const DiffRowLines& lines,
									 float startY) const
	{
		const DiffRowAt keptRowAt = RowAt(rows, m_KeptLine->Anchor);

		return std::max(0.0f, startY + static_cast<float>(lines.FirstLineOf(keptRowAt.Row)) * m_RowHeightLastFrame -
								  m_KeptLine->OffsetBelowTop);
	}

	template<typename Row>
	bool DiffView::KeepLineAcrossRewrap(std::span<const Row> rows, const DiffRowLines& lines,
										const Core::FileDiff& file, float startY,
										std::size_t columns, std::size_t columnsLastFrame)
	{
		if (columns == columnsLastFrame || !IsKeptLineOf(file) || rows.empty() ||
			m_RowHeightLastFrame <= 0.0f)
		{
			return false;
		}

		// WARNING: This runs inside the region, too late for SetNextWindowScroll, so the reader sees
		// one frame at the old scroll over the new breaks.
		ImGui::SetScrollY(ScrollToKeptLine(rows, lines, startY));

		return true;
	}

	// WARNING: The rows on screen are worked out again here and not taken from the clipper, whose
	// range also counts the rows it lays out to measure.
	template<typename Row>
	void DiffView::RememberKeptLine(std::span<const Row> rows, const DiffRowLines& lines,
									const Core::FileDiff& file, float startY)
	{
		const float scrollY = ImGui::GetScrollY();

		if (m_RowHeightLastFrame <= 0.0f)
		{
			m_KeptLine.reset();
			return;
		}

		const float top = std::max(0.0f, scrollY - startY);
		const float bottom = std::max(0.0f, scrollY + ImGui::GetWindowHeight() - startY);
		const auto firstLine = static_cast<std::size_t>(top / m_RowHeightLastFrame);
		const auto endLine = static_cast<std::size_t>(std::ceil(bottom / m_RowHeightLastFrame));

		const std::size_t firstRow = std::min(lines.RowOfLine(firstLine), rows.size());
		const std::size_t endRow =
			endLine == 0 ? firstRow : std::min(lines.RowOfLine(endLine - 1) + 1, rows.size());

		const std::optional<std::size_t> keptIndex = RowToKeep(rows, firstRow, endRow);

		if (!keptIndex.has_value())
		{
			m_KeptLine.reset();
			return;
		}

		const float rowTop = startY + static_cast<float>(lines.FirstLineOf(*keptIndex)) * m_RowHeightLastFrame;
		const std::optional<DiffLineAnchor> anchor = AnchorOf(rows[*keptIndex]);
		m_KeptLine = KeptLine{ShownPath(file), *anchor, rowTop - scrollY};
	}
}
