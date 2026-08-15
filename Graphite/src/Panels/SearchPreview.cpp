#include "Panels/SearchPreview.h"

#include "Editor/MatchColumns.h"
#include "Editor/Palettes.h"
#include "Search/PreviewContent.h"
#include "Search/PreviewWindow.h"

#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/Text/TextBuffer.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		// The gutter is line numbers and nothing else — no breakpoints, no git marks, unlike
		// the editor's — so it is as wide as the widest number it will draw and a space after
		// it.
		float GutterWidth(std::size_t lastLine, float cellWidth)
		{
			std::size_t digits = 1;

			for (std::size_t value = lastLine + 1; value >= 10; value /= 10)
			{
				++digits;
			}

			return static_cast<float>(digits + 1) * cellWidth;
		}

		void DrawMessage(const char* message)
		{
			ImGui::TextDisabled("%s", message);
		}

		// Every hit on `line` as document byte ranges, ascending, plus which of them is the
		// selected one. Ascending is load-bearing: MatchColumnsOnLine walks them in order, and
		// hits within a group already arrive that way.
		LineMatchInput MatchesOnLine(const SearchPreviewInput& input, std::size_t lineStart,
									 std::size_t line, std::string_view text,
									 std::vector<std::pair<std::size_t, std::size_t>>& scratch,
									 std::size_t& current)
		{
			scratch.clear();
			current = 0;

			for (std::size_t index = 0; index < input.Hits.size(); ++index)
			{
				const SearchHit& hit = input.Hits[index];

				if (hit.Line != line)
				{
					continue;
				}

				if (index == input.Current)
				{
					current = scratch.size();
				}

				scratch.push_back({lineStart + hit.From, lineStart + hit.To});
			}

			// By name, not positionally: three of these are std::size_t and LineMatchInput's own
			// header asks for exactly this, because a transposition in a positional list is
			// silent.
			return LineMatchInput{.Matches = scratch,
								  .LineText = text,
								  .LineStart = lineStart,
								  .CurrentMatch = current,
								  .TabWidth = input.TabWidth};
		}
	}

	void DrawSearchPreview(PreviewContent& content, const SearchPreviewInput& input)
	{
		switch (content.GetState())
		{
		case PreviewState::Empty:
			DrawMessage("Select a result to preview it.");
			return;

		case PreviewState::Unreadable:
		case PreviewState::Skipped:
			DrawMessage(content.GetDetail().c_str());
			return;

		case PreviewState::Ready:
			break;
		}

		const Core::TextBuffer* buffer = content.GetBuffer();

		if (buffer == nullptr || input.Current >= input.Hits.size())
		{
			DrawMessage("Select a result to preview it.");
			return;
		}

		content.SetTabWidth(input.TabWidth);

		// The editor's font, so a preview of a file reads exactly as the file does in a tab.
		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont(), input.FontSize);

		// Rows of text butt against each other, as the editor's do. The Dummy that claims each
		// line below is an ordinary item, so without this ImGui puts ItemSpacing.y between
		// them — five pixels of panel background striping every line, and a window of twenty-one
		// lines a hundred pixels taller than the text in it.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

		const EditorPalette palette = ReadEditorPalette(input.Scheme);
		const SyntaxPalette syntax = ReadSyntaxPalette(input.Scheme);

		const std::size_t hitLine = input.Hits[input.Current].Line;
		const PreviewWindow window = WindowAround(hitLine, buffer->GetLineCount(), kPreviewSpan);

		const float cellWidth = ImGui::CalcTextSize("0").x;
		const float lineHeight = ImGui::GetTextLineHeight();
		const float gutter = GutterWidth(window.Last, cellWidth);

		ImDrawList* draw = ImGui::GetWindowDrawList();

		std::vector<std::pair<std::size_t, std::size_t>> scratch;
		std::string expanded;

		for (std::size_t line = window.First; line <= window.Last; ++line)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float textLeft = at.x + gutter;

			const std::string raw = buffer->LineText(line);
			expanded = Core::ExpandTabs(raw, input.TabWidth);

			const float lineWidth = gutter + static_cast<float>(expanded.size()) * cellWidth;

			// The scheme's own background, not the panel's, so the pane reads as a view of the
			// file rather than as a list that happens to hold code. Widened to the pane when
			// the line is shorter, so short lines do not leave the panel's colour showing
			// through beside them.
			draw->AddRectFilled(
				at, ImVec2(at.x + std::max(lineWidth, ImGui::GetContentRegionAvail().x),
						   at.y + lineHeight),
				palette.Background);

			draw->AddRectFilled(at, ImVec2(textLeft, at.y + lineHeight), palette.GutterFill);

			// Highlights next, so the text lands on top of them.
			std::size_t current = 0;
			const LineMatches matches = MatchColumnsOnLine(
				MatchesOnLine(input, buffer->LineStart(line), line, raw, scratch, current));

			for (std::size_t index = 0; index < matches.Columns.size(); ++index)
			{
				const MatchColumns& columns = matches.Columns[index];

				const float left = textLeft + static_cast<float>(columns.FromColumn) * cellWidth;
				const float right = textLeft + static_cast<float>(columns.ToColumn) * cellWidth;

				draw->AddRectFilled(ImVec2(left, at.y), ImVec2(right, at.y + lineHeight),
									index == matches.Current ? palette.FindCurrentMatch
															 : palette.FindMatch);
			}

			// The same two colours the editor's gutter uses, and for the same reason: the line
			// being looked at is told apart from the ones around it.
			const std::string number = std::to_string(line + 1);
			const float numberLeft =
				textLeft - cellWidth - static_cast<float>(number.size()) * cellWidth;

			draw->AddText(ImVec2(numberLeft, at.y),
						  line == hitLine ? palette.Text : palette.Number, number.c_str());

			// Tokens are byte ranges of the expanded text — SyntaxCache feeds the lexer
			// tab-expanded lines — so they index `expanded` directly. The gaps between them are
			// whitespace and punctuation, drawn in the plain text colour.
			const std::vector<Token>& tokens = content.TokensOf(line);

			std::size_t drawn = 0;

			const auto drawRun = [&](std::size_t from, std::size_t to, ImU32 colour)
			{
				if (from >= to || from >= expanded.size())
				{
					return;
				}

				const std::size_t end = std::min(to, expanded.size());

				draw->AddText(ImVec2(textLeft + static_cast<float>(from) * cellWidth, at.y), colour,
							  expanded.data() + from, expanded.data() + end);
			};

			for (const Token& token : tokens)
			{
				drawRun(drawn, token.From, palette.Text);
				drawRun(token.From, token.To, syntax.Of(token.Kind));
				drawn = token.To;
			}

			drawRun(drawn, expanded.size(), palette.Text);

			ImGui::Dummy(ImVec2(lineWidth, lineHeight));
		}

		ImGui::PopStyleVar();
		ImGui::PopFont();
	}
}
