#include "Editor/HoverTooltip.h"

#include "Lang/Registry/LanguageRegistry.h"

#include "Core/App/Redraw.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Text/Columns.h"
#include "Core/Text/Encoding.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		// The tooltip's widest, in cells of the editor's font.
		constexpr float kMaxWidthCells = 60.0f;

		// The preview's tallest, in rows; a longer definition scrolls.
		constexpr std::size_t kPreviewMaxRows = 20;

		constexpr std::string_view kDot = " \xC2\xB7 "; // " · "

		bool HasInfo(const HoverActions& actions, const HoverState& hover)
		{
			return actions.ShowInfo && hover.Info &&
				   (hover.Info->Parsed || !hover.Info->Raw.empty());
		}

		// A preview with nothing to draw is no part at all, as an empty hover is: an identifier
		// with no definition to show gets no box saying so.
		bool HasPreview(const HoverActions& actions, const HoverState& hover)
		{
			return actions.ShowPreview && hover.Preview.has_value() && IsDrawable(*hover.Preview);
		}

		void Join(std::string& into, std::string_view part)
		{
			if (part.empty())
				return;

			if (!into.empty())
				into += kDot;

			into += part;
		}

		void TextIn(ImU32 colour, std::string_view text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			ImGui::TextUnformatted(text.data(), text.data() + text.size());
			ImGui::PopStyleColor();
		}

		void DisabledText(std::string_view text)
		{
			TextIn(ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
		}

		void PlainText(std::string_view text)
		{
			ImGui::TextUnformatted(text.data(), text.data() + text.size());
		}

		// Where each row after the first starts, for `text` laid out `columns` cells wide. A row
		// breaks after the last space or comma that fits — a declaration reads better broken
		// between parameters than inside a name — and only mid-word when a row holds neither.
		std::vector<std::size_t> RowStarts(std::string_view text, std::size_t columns)
		{
			std::vector<std::size_t> starts;

			if (columns == 0)
				return starts;

			std::size_t rowStart = 0;
			std::size_t column = 0;
			std::size_t lastGood = 0; // a byte a row may start at; 0 for "none in this row"

			for (std::size_t at = 0; at < text.size();)
			{
				const std::size_t next = Core::NextCharacter(text, at);

				if (column == columns)
				{
					const std::size_t cut = lastGood > rowStart ? lastGood : at;

					starts.push_back(cut);
					rowStart = cut;
					lastGood = 0;
					// tabWidth is inert: the text has had its tabs expanded away.
					column = Core::ColumnCount(text.substr(cut, at - cut), 1);
				}

				if (text[at] == ' ' || text[at] == ',')
					lastGood = next;

				++column;
				at = next;
			}

			return starts;
		}

		// Bytes [from, to) of `text`, coloured by `tokens` (offsets into `text`), at the cursor,
		// as one row of cells. Ends in a Dummy as every row the editor draws does: drawing alone
		// moves no cursor, and the window has to be told what it holds.
		void DrawColouredRow(std::string_view text, const std::vector<Token>& tokens,
							 std::size_t from, std::size_t to, const HoverTooltipLook& look)
		{
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImDrawList* const draw = ImGui::GetWindowDrawList();

			std::size_t at = from;
			std::size_t column = 0;

			const auto run = [&](std::size_t until, ImU32 colour)
			{
				if (until <= at)
					return;

				const std::string_view piece = text.substr(at, until - at);

				const float x = origin.x + static_cast<float>(column) * look.CellWidth;

				draw->AddText(ImVec2(x, origin.y), colour, piece.data(),
							  piece.data() + piece.size());

				column += Core::ColumnCount(piece, look.TabWidth);
				at = until;
			};

			for (const Token& token : tokens)
			{
				if (token.To <= from)
					continue;

				if (token.From >= to)
					break;

				run(std::max(token.From, from), look.Palette.Text);
				run(std::min(token.To, to), look.Syntax.Of(token.Kind));
			}

			run(to, look.Palette.Text);

			ImGui::Dummy(ImVec2(static_cast<float>(column) * look.CellWidth, look.LineHeight));
		}

		std::vector<Token> LexOne(const Lexer* lexer, std::string_view text)
		{
			std::vector<Token> tokens;

			if (lexer != nullptr)
				lexer->LexLine(text, LineState{}, tokens);

			return tokens;
		}

		void DrawDeclaration(std::string_view declaration, const HoverTooltipLook& look,
							 float maxContentWidth)
		{
			const std::string text = Core::ExpandTabs(declaration, look.TabWidth);
			const std::vector<Token> tokens = LexOne(look.DocumentLexer, text);

			const auto columns =
				static_cast<std::size_t>(std::max(1.0f, maxContentWidth / look.CellWidth));

			std::size_t from = 0;

			// A wrapped declaration's rows butt against each other, as the editor's do: each
			// Dummy is an ordinary item, and ImGui would otherwise put ItemSpacing.y below it. The
			// last row is drawn after the pop, so the line that follows the declaration keeps the
			// usual spacing.
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
								ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

			for (const std::size_t start : RowStarts(text, columns))
			{
				DrawColouredRow(text, tokens, from, start, look);
				from = start;
			}

			ImGui::PopStyleVar();

			DrawColouredRow(text, tokens, from, text.size(), look);
		}

		void DrawInfo(const HoverInfo& info, const HoverTooltipLook& look, float maxContentWidth)
		{
			if (!info.Parsed)
			{
				PlainText(info.Raw);
				return;
			}

			// The name in the editor's own text colour, the kind beside it dimmed: the bold face
			// this would ideally use is not one the editor's font stack holds.
			if (!info.Kind.empty())
			{
				DisabledText(info.Kind);

				if (!info.Name.empty())
					ImGui::SameLine();
			}

			if (!info.Name.empty())
				PlainText(info.Name);

			std::string where;

			if (!info.Scope.empty())
				where = "in " + info.Scope;

			Join(where, info.Access);

			for (const std::string& qualifier : info.Qualifiers)
				Join(where, qualifier);

			if (!where.empty())
				DisabledText(where);

			const bool body = !info.Declaration.empty() || !info.ReturnType.empty() ||
							  !info.Parameters.empty() || !info.Type.empty() ||
							  !info.Value.empty() || !info.Offset.empty() || !info.Size.empty() ||
							  !info.Alignment.empty();

			if (body)
				ImGui::Separator();

			if (!info.Declaration.empty())
				DrawDeclaration(info.Declaration, look, maxContentWidth);

			if (!info.ReturnType.empty())
				PlainText("\xE2\x86\x92 " + info.ReturnType); // "→ "

			if (!info.Parameters.empty())
			{
				PlainText("Parameters");
				ImGui::Indent();

				for (const std::string& parameter : info.Parameters)
					PlainText(parameter);

				ImGui::Unindent();
			}

			if (!info.Type.empty())
			{
				PlainText("Type: " + info.Type);

				if (!info.TypeAka.empty())
				{
					ImGui::Indent();
					DisabledText("aka " + info.TypeAka);
					ImGui::Unindent();
				}
			}

			if (!info.Value.empty())
				PlainText("Value = " + info.Value);

			// A field has an offset, and a padding only then; a type has a size and an alignment
			// alone. Either way, whatever clangd left out is left out here.
			std::string layout;

			if (!info.Offset.empty())
				Join(layout, "Offset " + info.Offset);

			if (!info.Size.empty())
			{
				std::string size = "Size " + info.Size;

				if (!info.Padding.empty())
					size += " (+" + info.Padding + " padding)";

				Join(layout, size);
			}

			if (!info.Alignment.empty())
				Join(layout, "Align " + info.Alignment);

			if (!layout.empty())
				PlainText(layout);

			if (!info.Documentation.empty())
			{
				ImGui::Separator();
				PlainText(info.Documentation);
			}
		}

		void DrawPreview(const DefinitionPreview& preview, const HoverTooltipLook& look,
						 float maxContentWidth)
		{
			const std::string file = Core::ToUtf8(preview.Path.filename());

			if (preview.TooLarge)
			{
				DisabledText(file + " \xE2\x80\x94 file too large to preview"); // " — "
				return;
			}

			char line[32];
			std::snprintf(line, sizeof(line), ":%zu", preview.FirstLine + 1);
			DisabledText(file + line);

			// Expanded once per frame for the width and the drawing alike. A preview is a
			// definition's extent, a few dozen lines as a rule, so lexing it every frame it shows
			// costs less than keeping a cache of it honest would.
			std::vector<std::string> expanded;
			expanded.reserve(preview.Lines.size());

			std::size_t widest = 0;

			for (const std::string& raw : preview.Lines)
			{
				expanded.push_back(Core::ExpandTabs(raw, look.TabWidth));
				widest = std::max(widest, Core::ColumnCount(expanded.back(), look.TabWidth));
			}

			const float scrollbar = ImGui::GetStyle().ScrollbarSize;
			const bool tall = expanded.size() > kPreviewMaxRows;
			const float contentWidth = static_cast<float>(widest) * look.CellWidth;
			const float wanted = contentWidth + (tall ? scrollbar : 0.0f);
			const bool wide = wanted > maxContentWidth;
			const float width = std::min(wanted, maxContentWidth);
			const float height =
				static_cast<float>(std::min(expanded.size(), kPreviewMaxRows)) * look.LineHeight +
				(wide ? scrollbar : 0.0f);

			// The wheel over this child scrolls it natively, Ctrl held or not. It never reaches
			// the editor's zoom: TextEditorWidget's RouteMouseWheel acts only while the editor
			// surface itself is the hovered window, and over the tooltip it is not.
			if (ImGui::BeginChild("##preview", ImVec2(width, height), ImGuiChildFlags_None,
								  ImGuiWindowFlags_HorizontalScrollbar))
			{
				const Lexer* const lexer = LexerFor(LanguageOfPath(preview.Path));

				// The state runs from the definition's first line, not the file's: a definition
				// rarely starts inside a block comment, and reading the file from the top to be
				// sure would cost what the size limit exists to avoid.
				LineState state;
				std::vector<Token> tokens;
				std::size_t lexed = 0;

				// Rows butt against each other, as the editor's do. Each Dummy is an ordinary item,
				// so without this ImGui puts ItemSpacing.y below every row: the content grows past
				// the LineHeight-per-row height the child was given, even a three-line preview
				// scrolls, and the clipper's pitch disagrees with the rows it steps over. Held
				// through the last row too, since nothing follows it inside the child, and popped
				// before EndChild so the child's own item in the tooltip keeps the usual spacing.
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
									ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(expanded.size()), look.LineHeight);

				while (clipper.Step())
				{
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
					{
						const auto index = static_cast<std::size_t>(row);

						// Lines above the clipped range are lexed only for the state they leave.
						while (lexed <= index)
						{
							tokens.clear();

							if (lexer != nullptr)
								state = lexer->LexLine(expanded[lexed], state, tokens);

							++lexed;
						}

						DrawColouredRow(expanded[index], tokens, 0, expanded[index].size(), look);
					}
				}

				clipper.End();

				ImGui::PopStyleVar();
			}

			ImGui::EndChild();
		}

		// Below the anchor if the tooltip fits there, above it if it fits there instead, else on
		// whichever side has more room. Flush against the anchor's row either way: the shorter
		// the path from the token to the tooltip, the less of it the mouse has to cross.
		ImVec2 PlaceTooltip(const HoverAnchor& anchor, ImVec2 size, const ImGuiViewport& viewport,
							ImVec2& pivot)
		{
			const ImVec2 min = viewport.WorkPos;
			const ImVec2 max(viewport.WorkPos.x + viewport.WorkSize.x,
							 viewport.WorkPos.y + viewport.WorkSize.y);

			const float below = max.y - anchor.Max.y;
			const float above = anchor.Min.y - min.y;
			const bool fitsBelow = size.y <= below;
			const bool fitsAbove = size.y <= above;
			const bool flip = !fitsBelow && (fitsAbove || above > below);

			const float x = std::max(min.x, std::min(anchor.Min.x, max.x - size.x));

			pivot = flip ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);

			return ImVec2(x, flip ? anchor.Min.y : anchor.Max.y);
		}
	}

	void DrawDiagnosticMessages(const std::vector<DiagnosticSpan>& spans)
	{
		for (std::size_t index = 0; index < spans.size(); ++index)
		{
			if (index != 0)
			{
				ImGui::Separator();
			}

			const DiagnosticSpan& span = spans[index];
			ImGui::TextUnformatted(span.Message.c_str());

			if (!span.Source.empty())
			{
				ImGui::TextDisabled("%s", span.Source.c_str());
			}
		}
	}

	void DrawHoverTooltip(const HoverActions& actions, HoverState& hover,
						  const std::vector<DiagnosticSpan>& diagnostics,
						  const HoverTooltipLook& look, const HoverAnchor& anchor,
						  ImGuiWindow* returnFocusTo)
	{
		const bool showDiagnostic = actions.ShowDiagnostic && !diagnostics.empty();
		const bool showInfo = HasInfo(actions, hover);
		const bool showPreview = HasPreview(actions, hover);

		// An empty or failed answer leaves the tracker showing a part with nothing in it; with
		// nothing else left, there is no tooltip at all rather than an empty box. Nor is there one
		// while a modal is up: the tooltip is raised to the front every frame, which would put it
		// over the very dialog that is meant to be the only thing taking input.
		if ((!showDiagnostic && !showInfo && !showPreview) ||
			ImGui::GetTopMostPopupModal() != nullptr)
		{
			hover.TooltipShown = false;
			return;
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		const float maxWidth = kMaxWidthCells * look.CellWidth;
		const float maxContentWidth = maxWidth - style.WindowPadding.x * 2.0f;

		// Placed from last frame's size. A tooltip that has just changed size is one frame out,
		// and asks for that frame below rather than waiting for the mouse to move.
		const ImVec2 lastSize(hover.TooltipMax.x - hover.TooltipMin.x,
							  hover.TooltipMax.y - hover.TooltipMin.y);

		ImGuiViewport* const viewport =
			look.Viewport != nullptr ? look.Viewport : ImGui::GetMainViewport();

		ImVec2 pivot;
		const ImVec2 position = PlaceTooltip(anchor, lastSize, *viewport, pivot);

		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(maxWidth, FLT_MAX));
		ImGui::SetNextWindowBgAlpha(1.0f);

		// A window rather than BeginTooltip, because a tooltip can be neither hovered nor
		// scrolled; dressed in the tooltip's colours and border so it still reads as one.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.WindowPadding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, style.PopupRounding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, style.PopupBorderSize);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));

		constexpr ImGuiWindowFlags kFlags =
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoMove;

		// One window per document: two editors side by side must never append to each other's.
		char name[48];
		std::snprintf(name, sizeof(name), "##HoverTooltip%p", static_cast<const void*>(&hover));

		ImGui::Begin(name, nullptr, kFlags);

		// A window never focused still falls behind whatever is focused after it — the first click
		// into the editor would bury it for good. Raised every frame it shows instead, which
		// moves no focus.
		ImGuiWindow* const window = ImGui::GetCurrentWindow();

		// A click inside the tooltip focuses it, as a click does any window, and the editor would
		// lose the keyboard to a box that goes away when the mouse leaves. Handed back as soon as
		// no button is down: during a press it would end the drag of the preview's scrollbar.
		//
		// Handed back to whichever window held focus before the click, not to the surface the
		// mouse is over: with two editors side by side, hovering the unfocused one must not move
		// the keyboard to it. ImGui moves focus on a click only at the end of the frame, so every
		// frame the tooltip is not focused, the focused window is still the one to return to.
		const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		if (!focused)
		{
			ImGuiWindow* const holder = ImGui::GetCurrentContext()->NavWindow;
			hover.FocusBeforeTooltip = holder != nullptr ? holder->ID : 0;
		}
		else if (!ImGui::IsAnyMouseDown())
		{
			// A window that has since gone — a popup the click closed — falls back to the surface.
			ImGuiWindow* const holder = ImGui::FindWindowByID(hover.FocusBeforeTooltip);
			ImGuiWindow* const target =
				holder != nullptr && holder->WasActive ? holder : returnFocusTo;

			if (target != nullptr)
				ImGui::FocusWindow(target);
		}

		ImGui::BringWindowToDisplayFront(window);

		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maxContentWidth);

		if (showDiagnostic)
		{
			DrawDiagnosticMessages(diagnostics);

			if (showInfo || showPreview)
				ImGui::Separator();
		}

		if (showInfo)
		{
			DrawInfo(*hover.Info, look, maxContentWidth);

			if (showPreview)
				ImGui::Separator();
		}

		if (showPreview)
			DrawPreview(*hover.Preview, look, maxContentWidth);

		ImGui::PopTextWrapPos();

		const ImVec2 windowPos = ImGui::GetWindowPos();
		const ImVec2 windowSize = ImGui::GetWindowSize();

		ImGui::End();

		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);

		if (!hover.TooltipShown || windowSize.x != lastSize.x || windowSize.y != lastSize.y)
			Core::Redraw::Request();

		hover.TooltipMin = windowPos;
		hover.TooltipMax = ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
		hover.TooltipShown = true;
	}
}
