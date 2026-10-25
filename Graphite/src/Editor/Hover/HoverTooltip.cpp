#include "Editor/Hover/HoverTooltip.h"

#include "Core/Base/Narrow.h"
#include "Core/Base/Strings.h"

#include "Editor/Hover/HoverColouring.h"
#include "Editor/Hover/HoverLayout.h"
#include "Editor/Hover/InteractiveTooltip.h"
#include "Editor/Server/PreviewSemantics.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/ColourLines.h"

#include "UI/TypographicMarks.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/Text/Columns.h"
#include "Core/Text/Encoding.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		constexpr float kTextWidthCells = 60.0f;
		constexpr float kBothSides = 2.0f;
		constexpr std::string_view kNoLabel = "";

		bool HasInfo(const HoverActions& actions, const HoverState& hover)
		{
			return actions.ShowInfo && hover.Info &&
				   (hover.Info->Parsed || !hover.Info->Raw.empty());
		}

		bool HasPreview(const HoverActions& actions, const HoverState& hover)
		{
			return actions.ShowPreview && hover.Preview.has_value() && IsDrawable(*hover.Preview);
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

		void DisabledProse(std::string_view text)
		{
			Core::PushInterfaceFont();
			DisabledText(text);
			ImGui::PopFont();
		}

		void PlainProse(std::string_view text)
		{
			Core::PushInterfaceFont();
			PlainText(text);
			ImGui::PopFont();
		}

		class SectionSeparator
		{
		public:
			void BeforeSection()
			{
				if (m_AnyDrawn)
				{
					ImGui::Separator();
				}

				m_AnyDrawn = true;
			}

		private:
			bool m_AnyDrawn = false;
		};

		std::vector<std::size_t> RowStarts(std::string_view text, std::size_t columns)
		{
			constexpr std::size_t kUnusedTabWidth = 1;
			constexpr std::size_t kNoBreakInRow = 0;

			std::vector<std::size_t> rowStarts;

			if (columns == 0)
			{
				return rowStarts;
			}

			std::size_t rowStart = 0;
			std::size_t column = 0;
			std::size_t lastBreakOffset = kNoBreakInRow;

			for (std::size_t byteOffset = 0; byteOffset < text.size();)
			{
				const std::size_t nextByteOffset = Core::NextCharacter(text, byteOffset);

				if (column == columns)
				{
					const std::size_t cut = lastBreakOffset > rowStart ? lastBreakOffset : byteOffset;

					rowStarts.push_back(cut);
					rowStart = cut;
					lastBreakOffset = kNoBreakInRow;
					column = Core::ColumnCount(text.substr(cut, byteOffset - cut), kUnusedTabWidth);
				}

				if (text[byteOffset] == ' ' || text[byteOffset] == ',')
				{
					lastBreakOffset = nextByteOffset;
				}

				++column;
				byteOffset = nextByteOffset;
			}

			return rowStarts;
		}

		struct RowPainter
		{
			std::string_view Text;
			ImVec2 Origin;
			ImDrawList* DrawList = nullptr;
			const HoverTooltipLook& Look;
			std::size_t At = 0;
			std::size_t Column = 0;

			void PaintUntil(std::size_t until, ImU32 colour)
			{
				if (until <= At)
				{
					return;
				}

				const std::string_view piece = Text.substr(At, until - At);
				const float pieceLeft = Origin.x + static_cast<float>(Column) * Look.CellWidth;

				DrawList->AddText(ImVec2(pieceLeft, Origin.y), colour, piece.data(),
								  piece.data() + piece.size());

				Column += Core::ColumnCount(piece, Look.TabWidth);
				At = until;
			}
		};

		void DrawColouredRow(std::string_view text, const std::vector<Token>& tokens,
							 const std::vector<SemanticSpan>& spans, std::size_t from,
							 std::size_t to, const HoverTooltipLook& look)
		{
			RowPainter painter{.Text = text,
							   .Origin = ImGui::GetCursorScreenPos(),
							   .DrawList = ImGui::GetWindowDrawList(),
							   .Look = look,
							   .At = from};

			for (const Token& token : tokens)
			{
				if (token.To <= from)
				{
					continue;
				}

				if (token.From >= to)
				{
					break;
				}

				painter.PaintUntil(std::max(token.From, from), look.Palette.Text);
				painter.PaintUntil(std::min(token.To, to), look.Syntax.Of(ColourOf(token, spans)));
			}

			painter.PaintUntil(to, look.Palette.Text);

			ImGui::Dummy(ImVec2(static_cast<float>(painter.Column) * look.CellWidth, look.LineHeight));
		}

		using SpansOfLine = std::function<std::vector<SemanticSpan>(std::string_view, const std::vector<Token>&)>;

		using LinesColouring = std::vector<std::vector<Token>> (*)(LanguageId, std::span<const std::string_view>);

		void DrawCodeLine(std::string_view label, const std::string& expanded, std::vector<Token> tokens,
						  const SpansOfLine& spansOf, const HoverTooltipLook& look, float maxContentWidth)
		{
			std::vector<SemanticSpan> spans = spansOf(std::string_view(expanded), tokens);

			const std::string text = std::string(label) + expanded;

			for (Token& token : tokens)
			{
				token.From += label.size();
				token.To += label.size();
			}

			for (SemanticSpan& span : spans)
			{
				span.From += label.size();
				span.To += label.size();
			}

			const float widthInCells = std::max(1.0f, maxContentWidth / look.CellWidth);
			const std::size_t columns = static_cast<std::size_t>(widthInCells);

			std::size_t rowFrom = 0;

			// WARNING: A wrapped line's rows butt against each other, as the editor's do. Each Dummy
			// is an ordinary item, so ImGui would put ItemSpacing.y below it. The last row is drawn
			// after the pop, so the line that follows keeps the usual spacing.
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
								ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

			for (const std::size_t rowStart : RowStarts(text, columns))
			{
				DrawColouredRow(text, tokens, spans, rowFrom, rowStart, look);
				rowFrom = rowStart;
			}

			ImGui::PopStyleVar();

			DrawColouredRow(text, tokens, spans, rowFrom, text.size(), look);
		}

		void DrawCode(std::string_view label, std::string_view code, LinesColouring colouring, const SpansOfLine& spansOf,
					  const HoverTooltipLook& look, float maxContentWidth)
		{
			std::vector<std::string> expandedLines;

			for (const std::string_view line : Core::SplitLines(code))
			{
				expandedLines.push_back(Core::ExpandTabs(line, look.TabWidth));
			}

			const std::vector<std::string_view> lineViews(expandedLines.begin(), expandedLines.end());
			std::vector<std::vector<Token>> colouredLines = colouring(look.Language, lineViews);

			for (std::size_t lineIndex = 0; lineIndex < expandedLines.size(); ++lineIndex)
			{
				const bool hasColours = lineIndex < colouredLines.size();
				std::vector<Token> tokens = hasColours ? std::move(colouredLines[lineIndex]) : std::vector<Token>{};
				const std::string_view lineLabel = lineIndex == 0 ? label : kNoLabel;

				DrawCodeLine(lineLabel, expandedLines[lineIndex], std::move(tokens), spansOf, look,
							 maxContentWidth);
			}
		}

		std::vector<SemanticSpan> NoSemanticSpans(std::string_view, const std::vector<Token>&)
		{
			return {};
		}

		struct DeclarationSpansOfName
		{
			std::string_view Name;
			TokenKind NameKind = TokenKind::Text;

			std::vector<SemanticSpan> operator()(std::string_view text, const std::vector<Token>& tokens) const
			{
				return DeclarationSpans(text, tokens, Name, NameKind);
			}
		};

		void DrawInfo(const HoverInfo& info, const HoverTooltipLook& look, float maxContentWidth)
		{
			if (!info.Parsed)
			{
				PlainText(info.Raw);
				return;
			}

			if (!info.Kind.empty())
			{
				DisabledProse(info.Kind);

				if (!info.Name.empty())
				{
					ImGui::SameLine();
				}
			}

			if (!info.Name.empty())
			{
				const TokenKind nameKind = KindOfHoverSymbol(info.Kind);

				TextIn(nameKind != TokenKind::Text ? look.Syntax.Of(nameKind) : look.Palette.Text,
					   info.Name);
			}

			std::string where;

			if (!info.Scope.empty())
			{
				where = "in " + info.Scope;
			}

			Core::AppendSeparatedIfNotEmpty(where, kMiddleDotSeparator, info.Access);

			for (const std::string& qualifier : info.Qualifiers)
			{
				Core::AppendSeparatedIfNotEmpty(where, kMiddleDotSeparator, qualifier);
			}

			if (!where.empty())
			{
				DisabledProse(where);
			}

			const bool heading = !info.Kind.empty() || !info.Name.empty() || !where.empty();
			const bool body = !info.Declaration.empty() || !info.ReturnType.empty() ||
							  !info.Parameters.empty() || !info.Type.empty() ||
							  !info.Value.empty() || !info.Offset.empty() || !info.Size.empty() ||
							  !info.Alignment.empty();

			if (body && heading)
			{
				ImGui::Separator();
			}

			if (!info.Declaration.empty())
			{
				const TokenKind nameKind = KindOfHoverSymbol(info.Kind);
				const float declarationLeft = ImGui::GetCursorScreenPos().x;

				SpansOfLine spansOfDeclarationLine = NoSemanticSpans;

				if (NeedsDeclarationSpans(look.Language))
				{
					spansOfDeclarationLine = DeclarationSpansOfName{info.Name, nameKind};
				}

				DrawCode(kNoLabel, info.Declaration, ColourDeclaration, spansOfDeclarationLine, look,
						 maxContentWidth);

				if (!info.OverloadNote.empty())
				{
					// WARNING: Text after SameLine wraps at the wrap position, not where it began, so
					// a note that does not fit beside the declaration would come out a word to a row.
					const float noteLeft = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;

					Core::PushInterfaceFont();

					const float noteWidth = ImGui::CalcTextSize(info.OverloadNote.c_str()).x;

					if (noteLeft + noteWidth <= declarationLeft + maxContentWidth)
					{
						ImGui::SameLine();
					}

					DisabledText(info.OverloadNote);

					ImGui::PopFont();
				}
			}

			if (!info.ReturnType.empty())
			{
				DrawCode(std::format("{} ", kRightwardsArrow), info.ReturnType, ColourLines, TypeSpans, look,
						 maxContentWidth);
			}

			if (!info.Parameters.empty())
			{
				PlainProse("Parameters");
				ImGui::Indent();

				for (const std::string& parameter : info.Parameters)
				{
					DrawCode(kNoLabel, parameter, ColourLines, ParameterSpans, look, maxContentWidth);
				}

				ImGui::Unindent();
			}

			if (!info.Type.empty())
			{
				DrawCode("Type: ", info.Type, ColourLines, TypeSpans, look, maxContentWidth);

				if (!info.TypeAka.empty())
				{
					ImGui::Indent();
					DisabledProse("aka " + info.TypeAka);
					ImGui::Unindent();
				}
			}

			if (!info.Value.empty())
			{
				DrawCode("Value = ", info.Value, ColourLines, NoSemanticSpans, look, maxContentWidth);
			}

			std::string layout;

			if (!info.Offset.empty())
			{
				Core::AppendSeparatedIfNotEmpty(layout, kMiddleDotSeparator, "Offset " + info.Offset);
			}

			if (!info.Size.empty())
			{
				std::string size = "Size " + info.Size;

				if (!info.Padding.empty())
				{
					size += " (+" + info.Padding + " padding)";
				}

				Core::AppendSeparatedIfNotEmpty(layout, kMiddleDotSeparator, size);
			}

			if (!info.Alignment.empty())
			{
				Core::AppendSeparatedIfNotEmpty(layout, kMiddleDotSeparator, "Align " + info.Alignment);
			}

			Core::PushInterfaceFont();

			if (!layout.empty())
			{
				PlainText(layout);
			}

			if (!info.Documentation.empty())
			{
				if (heading || body)
				{
					ImGui::Separator();
				}

				PlainText(info.Documentation);
			}

			ImGui::PopFont();
		}

		const std::vector<SemanticSpan>& ServerSpansOnPreviewLine(const DefinitionPreview& preview,
																  const HoverTooltipLook& look,
																  bool declared, std::size_t fileLine,
																  const std::vector<SemanticSpan>& noSpans)
		{
			if (look.PreviewOverlay != nullptr)
			{
				return look.PreviewOverlay->SpansOn(fileLine);
			}

			if (declared)
			{
				return look.Semantics->SpansOn(preview.Path, fileLine);
			}

			return noSpans;
		}

		void DrawPreview(HoverState& hover, const HoverTooltipLook& look)
		{
			const DefinitionPreview& preview = *hover.Preview;

			const std::string file = Core::ToUtf8(preview.Path.filename());

			if (preview.TooLarge)
			{
				DisabledProse(file + std::string{kEmDashSeparator} + "file too large to preview");
				return;
			}

			DisabledProse(file + std::format(":{}", preview.FirstLine + 1));

			const bool declared = look.PreviewOverlay == nullptr && look.Semantics != nullptr;

			if (declared)
			{
				constexpr bool kEditorHolds = false;

				look.Semantics->Declare(preview.Path, preview.Text, look.TabWidth, kEditorHolds);
			}

			const ColouredPreview& coloured = ColouredPreviewOf(hover, look.TabWidth);
			const std::vector<std::string>& expanded = coloured.Dedented.Lines;

			std::size_t widest = 0;

			for (const std::string& text : expanded)
			{
				widest = std::max(widest, Core::ColumnCount(text, look.TabWidth));
			}

			const std::vector<std::vector<Token>>& previewTokens = coloured.Tokens;
			const std::vector<Token> noTokens;

			const std::vector<SemanticSpan> noSpans;
			constexpr std::size_t kLineStart = 0;

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
								ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

			// WARNING: The widest line's width is claimed before any row. The clipper submits only
			// the rows in view, or none while the preview is scrolled out of it, and a tooltip
			// sized by those alone would change width as it scrolls.
			ImGui::Dummy(ImVec2(PreviewWidth(widest, look.CellWidth,
											 ImGui::GetCurrentWindow()->ScrollbarY),
								0.0f));

			ImGuiListClipper clipper;
			clipper.Begin(Core::Narrow<int>(expanded.size()), look.LineHeight);

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const auto lineIndex = static_cast<std::size_t>(row);

					const std::size_t fileLine = preview.FirstLine + lineIndex;

					const std::vector<SemanticSpan>& serverSpans =
						ServerSpansOnPreviewLine(preview, look, declared, fileLine, noSpans);
					const std::vector<Token>& lineTokens = lineIndex < previewTokens.size() ? previewTokens[lineIndex] : noTokens;

					DrawColouredRow(expanded[lineIndex], lineTokens, DedentSpans(serverSpans, coloured.Dedented.Removed),
									kLineStart, expanded[lineIndex].size(), look);
				}
			}

			clipper.End();

			ImGui::PopStyleVar();
		}
	}

	HoverSections SectionsToShow(const HoverActions& actions, const HoverState& hover,
								 const std::vector<DiagnosticSpan>& diagnostics, const HoverTooltipLook& look)
	{
		HoverSections sections;
		sections.Diagnostic = actions.ShowDiagnostic && !diagnostics.empty();
		sections.Info = HasInfo(actions, hover);
		sections.InfoFailure = !sections.Info && actions.InfoGaveUp && !hover.InfoFailureText.empty();
		sections.Preview = HasPreview(actions, hover);
		sections.NoDefinition = !sections.Preview && actions.ShowPreview && actions.PreviewHeldByCtrl &&
								hover.Preview.has_value() && hover.NoDefinitionFound;

		const bool previewFailureSaysWhatInfoSaid = sections.InfoFailure && hover.PreviewFailureText == hover.InfoFailureText;

		sections.PreviewFailure = !sections.Preview && !sections.NoDefinition && actions.PreviewGaveUp &&
								  !hover.PreviewFailureText.empty() && !previewFailureSaysWhatInfoSaid;
		sections.Loading = (actions.InfoLoading || actions.PreviewLoading) && !look.LoadingText.empty();

		return sections;
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
		const HoverSections sections = SectionsToShow(actions, hover, diagnostics, look);

		if (!sections.Any())
		{
			hover.Tooltip.Shown = false;
			return;
		}

		const ImGuiStyle& style = ImGui::GetStyle();

		const InteractiveTooltipPlace place{.Viewport = look.Viewport,
											.BoundsMin = look.BoundsMin,
											.BoundsMax = look.BoundsMax,
											.AnchorMin = anchor.Min,
											.AnchorMax = anchor.Max,
											.LeastHeight = look.LineHeight + style.WindowPadding.y * kBothSides};

		const TooltipBounds bounds = InteractiveTooltipBounds(place);

		const float maxWidth = bounds.Max.x - bounds.Min.x;
		const float maxContentWidth =
			std::max(look.CellWidth,
					 std::min(kTextWidthCells * look.CellWidth,
							  maxWidth - style.WindowPadding.x * kBothSides - style.ScrollbarSize));

		const std::string name = std::format("##HoverTooltip{}", static_cast<const void*>(&hover));

		if (!BeginInteractiveTooltip(name.c_str(), hover.Tooltip, place))
		{
			return;
		}

		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maxContentWidth);

		SectionSeparator separator;

		if (sections.Diagnostic)
		{
			separator.BeforeSection();
			Core::PushInterfaceFont();
			DrawDiagnosticMessages(diagnostics);
			ImGui::PopFont();
		}

		if (sections.Info)
		{
			separator.BeforeSection();
			DrawInfo(*hover.Info, look, maxContentWidth);
		}
		else if (sections.InfoFailure)
		{
			separator.BeforeSection();
			DisabledProse(hover.InfoFailureText);
		}

		if (sections.Preview)
		{
			separator.BeforeSection();
			DrawPreview(hover, look);
		}
		else if (sections.NoDefinition)
		{
			separator.BeforeSection();
			DisabledProse(kHoverNoDefinition);
		}
		else if (sections.PreviewFailure)
		{
			separator.BeforeSection();
			DisabledProse(hover.PreviewFailureText);
		}

		if (sections.Loading)
		{
			separator.BeforeSection();
			Core::PushInterfaceFont();
			Core::LoadingLine(look.LoadingText.c_str(), Core::WhileLoaderWaits::ShowText);
			ImGui::PopFont();
		}

		ImGui::PopTextWrapPos();

		EndInteractiveTooltip(hover.Tooltip, returnFocusTo);
	}
}
