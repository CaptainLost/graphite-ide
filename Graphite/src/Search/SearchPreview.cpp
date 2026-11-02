#include "Search/SearchPreview.h"

#include "Editor/Find/MatchColumns.h"
#include "Theme/Palettes.h"
#include "Editor/Overlays/SemanticOverlay.h"
#include "Search/PreviewContent.h"
#include "Search/PreviewWindow.h"
#include "UI/LineNumberColumn.h"

#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/Text/TextBuffer.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {

		constexpr const char* kOpenAction = "Open";

		// WARNING: A run is placed by the columns before it, not by its byte offset: a character of more
		// than one byte takes one cell, and the match highlights are placed by columns.
		struct TextRunPainter
		{
			ImDrawList* DrawList;
			const std::string& Text;
			float TextLeft;
			float TextTop;
			float CellWidth;
			std::size_t TabWidth;
			std::size_t PaintedUpToByte = 0;
			std::size_t Column = 0;

			void PaintUpTo(std::size_t toByte, ImU32 colour)
			{
				const std::size_t end = std::min(toByte, Text.size());

				if (end <= PaintedUpToByte)
				{
					return;
				}

				const std::string_view run(Text.data() + PaintedUpToByte, end - PaintedUpToByte);

				DrawList->AddText(ImVec2(TextLeft + static_cast<float>(Column) * CellWidth, TextTop), colour,
								  run.data(), run.data() + run.size());

				Column += Core::ColumnCount(run, TabWidth);
				PaintedUpToByte = end;
			}
		};

		void DrawMessage(const char* message)
		{
			Core::EmptyState(message);
		}

		LineMatchInput MatchesOnLine(const SearchPreviewInput& input, std::size_t lineStart,
									 std::size_t line, std::string_view text,
									 std::vector<Core::FindMatch>& matchesOnLine,
									 std::size_t& currentMatchOnLine)
		{
			matchesOnLine.clear();
			currentMatchOnLine = 0;

			for (std::size_t index = 0; index < input.Hits.size(); ++index)
			{
				const SearchHit& hit = input.Hits[index];

				if (hit.Line != line)
				{
					continue;
				}

				if (index == input.Current)
				{
					currentMatchOnLine = matchesOnLine.size();
				}

				matchesOnLine.push_back(
					Core::FindMatch{.From = lineStart + hit.From, .To = lineStart + hit.To});
			}

			return LineMatchInput{.Matches = matchesOnLine,
								  .LineText = text,
								  .LineStart = lineStart,
								  .CurrentMatch = currentMatchOnLine,
								  .TabWidth = input.TabWidth};
		}
	}

	PreviewRequest DrawSearchPreview(PreviewContent& content, const SearchPreviewInput& input)
	{
		switch (content.GetState())
		{
		case PreviewState::Empty:
			DrawMessage("Select a result to preview it.");
			return PreviewRequest::None;

		case PreviewState::Unreadable:
		case PreviewState::Skipped:
			DrawMessage(content.GetDetail().c_str());
			return PreviewRequest::None;

		case PreviewState::TooLarge:
			return Core::EmptyState(content.GetDetail(), kOpenAction) ? PreviewRequest::OpenFile : PreviewRequest::None;

		case PreviewState::Ready:
			break;
		}

		const Core::TextBuffer* buffer = content.GetBuffer();

		if (buffer == nullptr || input.Current >= input.Hits.size())
		{
			DrawMessage("Select a result to preview it.");
			return PreviewRequest::None;
		}

		content.SetTabWidth(input.TabWidth);

		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont(), input.FontSize);

		// WARNING: Each line's Dummy is an ordinary item, so without zero ItemSpacing ImGui
		// stripes the panel background between the lines.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

		const EditorPalette palette = ReadEditorPalette(input.Scheme);
		const SyntaxPalette syntax = ReadSyntaxPalette(input.Scheme);

		const std::size_t hitLine = input.Hits[input.Current].Line;
		const PreviewWindow window = WindowAround(hitLine, buffer->GetLineCount(), kPreviewSpan);

		// WARNING: CalcTextSize rounds the advance up, which opens a gap at each token boundary
		// that widens along the line, so the cell width comes from the baked font.
		const float cellWidth = Core::MonospaceCellWidth();
		const float lineHeight = ImGui::GetTextLineHeight();
		const LineNumberColumn numbers = MeasureLineNumberColumn(buffer->GetLineCount(), cellWidth);
		const float gutter = numbers.Width();

		if (content.TakeCentring(hitLine))
		{
			ImGui::SetScrollY(ScrollCentring(
				ScrollCentringInput{.ContentTop = ImGui::GetCursorPosY(),
									.RowsAbove = hitLine - window.First,
									.LineHeight = lineHeight,
									.ViewHeight = ImGui::GetWindowHeight()}));
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		std::vector<Core::FindMatch> matchesOnLine;
		std::string expanded;

		for (std::size_t line = window.First; line <= window.Last; ++line)
		{
			const ImVec2 lineOrigin = ImGui::GetCursorScreenPos();
			const float textLeft = lineOrigin.x + gutter;

			const std::string raw = buffer->LineText(line);
			expanded = Core::ExpandTabs(raw, input.TabWidth);

			const float lineWidth = gutter + static_cast<float>(Core::ColumnCount(expanded, input.TabWidth)) * cellWidth;
			const float backgroundWidth = std::max(lineWidth, ImGui::GetContentRegionAvail().x);

			drawList->AddRectFilled(
				lineOrigin,
				ImVec2(lineOrigin.x + backgroundWidth, lineOrigin.y + lineHeight),
				palette.Background);

			drawList->AddRectFilled(lineOrigin, ImVec2(textLeft, lineOrigin.y + lineHeight),
									palette.GutterFill);

			std::size_t currentMatchOnLine = 0;
			const LineMatches matches = MatchColumnsOnLine(MatchesOnLine(
				input, buffer->LineStart(line), line, raw, matchesOnLine, currentMatchOnLine));

			for (std::size_t index = 0; index < matches.Columns.size(); ++index)
			{
				const MatchColumns& columns = matches.Columns[index];

				const float left = textLeft + static_cast<float>(columns.FromColumn) * cellWidth;
				const float right = textLeft + static_cast<float>(columns.ToColumn) * cellWidth;

				drawList->AddRectFilled(ImVec2(left, lineOrigin.y),
										ImVec2(right, lineOrigin.y + lineHeight),
										index == matches.Current ? palette.FindCurrentMatch
																 : palette.FindMatch);
			}

			const std::string lineNumber = std::to_string(line + 1);
			const float lineNumberLeft = numbers.NumberLeft(lineOrigin.x, line + 1);

			drawList->AddText(ImVec2(lineNumberLeft, lineOrigin.y),
							  line == hitLine ? palette.CurrentNumber : palette.Number,
							  lineNumber.c_str());

			const std::vector<Token>& tokens = content.TokensOf(line);
			const std::vector<SemanticSpan>& spans = content.SpansOn(line);

			TextRunPainter painter{.DrawList = drawList,
								   .Text = expanded,
								   .TextLeft = textLeft,
								   .TextTop = lineOrigin.y,
								   .CellWidth = cellWidth,
								   .TabWidth = input.TabWidth};

			for (const Token& token : tokens)
			{
				painter.PaintUpTo(token.From, palette.Text);
				painter.PaintUpTo(token.To, syntax.Of(ColourOf(token, spans)));
			}

			painter.PaintUpTo(expanded.size(), palette.Text);

			ImGui::Dummy(ImVec2(lineWidth, lineHeight));
		}

		ImGui::PopStyleVar();
		ImGui::PopFont();

		return PreviewRequest::None;
	}
}
