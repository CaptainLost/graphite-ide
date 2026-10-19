#include "Editor/StatusBar/StatusBar.h"

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <IconsLucide.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr float kDotShareOfFont = 0.18f;
		constexpr float kNoBorder = 0.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kInvisibleAlpha = 0.0f;
		constexpr float kNoGap = 0.0f;
		constexpr float kBarTop = 0.0f;

		constexpr float kItemFillPadding = 6.0f;
		constexpr float kBothSides = 2.0f;
		constexpr float kMarkToTextGap = 4.0f;
		constexpr float kBetweenCountsGap = 8.0f;

		constexpr const char* kErrorGlyph = ICON_LC_CIRCLE_X;
		constexpr const char* kWarningGlyph = ICON_LC_TRIANGLE_ALERT;
		constexpr const char* kNoProblemsGlyph = ICON_LC_CHECK;

		constexpr const char* kReadOnlyWarning = "read-only: not valid text for its encoding";

		enum class PieceKind
		{
			Text,
			Dot
		};

		struct Piece
		{
			PieceKind Kind = PieceKind::Text;
			std::string Text;
			ImU32 Colour = 0;
			float GapBefore = kNoGap;
		};

		enum class ItemFace
		{
			Interface,
			Code
		};

		struct StatusItem
		{
			const char* Id = nullptr;
			StatusBarClick Reports = StatusBarClick::None;
			std::vector<Piece> Pieces;
			std::string Tooltip;
			ItemFace Face = ItemFace::Interface;
		};

		struct DrawnItem
		{
			bool Clicked = false;
			float Width = 0.0f;
		};

		const char* LineEndingName(Core::LineEnding ending, bool mixed)
		{
			if (ending == Core::LineEnding::Crlf)
			{
				return mixed ? "CRLF (mixed)" : "CRLF";
			}

			return mixed ? "LF (mixed)" : "LF";
		}

		float CentredTextY(float barHeight)
		{
			return (barHeight - ImGui::GetTextLineHeight()) * kHalf;
		}

		float BellWidth()
		{
			return Core::IconButtonSize(Core::IconButtonFit::Bar);
		}

		ImU32 ToPackedColour(const Core::Color& colour)
		{
			return ImGui::GetColorU32(ImVec4(colour.R, colour.G, colour.B, colour.A));
		}

		ImU32 TextColour()
		{
			return ImGui::GetColorU32(ImGuiCol_Text);
		}

		ImU32 DimColour()
		{
			return ImGui::GetColorU32(ImGuiCol_TextDisabled);
		}

		bool DrawNotificationBell(const std::optional<Core::Color>& missedColour)
		{
			ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - BellWidth() - ImGui::GetStyle().WindowPadding.x, kBarTop));

			// WARNING: Without NoFocus the press focuses the bar, and the popup the click opens hands the
			// keyboard back to the bar when it closes rather than to the editor.
			constexpr bool kNotEngaged = false;
			const Core::IconButtonOptions bellButton{.Fit = Core::IconButtonFit::Bar,
													 .Flags = ImGuiButtonFlags_NoFocus | ImGuiButtonFlags_NoNavFocus};
			const bool clicked =
				Core::DrawIconButton("##NotificationBell", ICON_LC_BELL, kNotEngaged, "Notifications", bellButton);

			if (missedColour.has_value())
			{
				const float dotRadius = ImGui::GetFontSize() * kDotShareOfFont;
				const float glyphHalf = ImGui::GetTextLineHeight() * kHalf;
				const ImVec2 bellCentre = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()).GetCenter();
				const ImVec2 dotCentre(bellCentre.x + glyphHalf - dotRadius, bellCentre.y - glyphHalf + dotRadius);
				ImGui::GetWindowDrawList()->AddCircleFilled(dotCentre, dotRadius, ToPackedColour(*missedColour));
			}

			return clicked;
		}

		void DrawCentred(const std::string& text, float barHeight)
		{
			const float width = ImGui::CalcTextSize(text.c_str()).x;

			ImGui::SetCursorPos(
				ImVec2((ImGui::GetWindowWidth() - width) * kHalf, CentredTextY(barHeight)));
			ImGui::TextUnformatted(text.c_str());
		}

		float PieceWidth(const Piece& piece)
		{
			if (piece.Kind == PieceKind::Dot)
			{
				return Core::Scaled(kServerDotDiameter);
			}

			return ImGui::CalcTextSize(piece.Text.c_str()).x;
		}

		float PiecesWidth(const std::vector<Piece>& pieces)
		{
			float width = 0.0f;

			for (const Piece& piece : pieces)
			{
				width += piece.GapBefore + PieceWidth(piece);
			}

			return width;
		}

		void DrawPieces(const std::vector<Piece>& pieces, ImVec2 lineStart)
		{
			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			const float lineMiddle = lineStart.y + ImGui::GetTextLineHeight() * kHalf;
			float x = lineStart.x;

			for (const Piece& piece : pieces)
			{
				x += piece.GapBefore;
				const float width = PieceWidth(piece);

				if (piece.Kind == PieceKind::Dot)
				{
					const float radius = width * kHalf;
					drawList->AddCircleFilled(ImVec2(x + radius, lineMiddle), radius, piece.Colour);
				}
				else
				{
					drawList->AddText(ImVec2(x, lineStart.y), piece.Colour, piece.Text.c_str());
				}

				x += width;
			}
		}

		void PushItemFace(ItemFace face)
		{
			if (face == ItemFace::Code)
			{
				Core::PushCodeFont(Core::kInterfaceFontSize);
			}
		}

		void PopItemFace(ItemFace face)
		{
			if (face == ItemFace::Code)
			{
				ImGui::PopFont();
			}
		}

		float ItemWidth(const StatusItem& item)
		{
			PushItemFace(item.Face);
			const float width = PiecesWidth(item.Pieces);
			PopItemFace(item.Face);

			return width;
		}

		// WARNING: A clickable item is an invisible button with its pieces drawn over it, never a text item
		// asked IsItemClicked. A text item has no id, so its press focuses the bar itself and the dock
		// node then selects the bar's tab instead of the panel the click revealed.
		DrawnItem DrawItem(const StatusItem& item, float x, float barHeight)
		{
			PushItemFace(item.Face);

			DrawnItem drawn{.Width = PiecesWidth(item.Pieces)};
			const float lineTop = CentredTextY(barHeight);

			ImGui::SetCursorPos(ImVec2(x, lineTop));
			const ImVec2 lineStart = ImGui::GetCursorScreenPos();

			// WARNING: InvisibleButton refuses a zero size, so an empty item is laid out as an inert one.
			const bool clickable = item.Reports != StatusBarClick::None && drawn.Width > 0.0f;

			if (clickable)
			{
				const float fillPadding = Core::Scaled(kItemFillPadding);

				ImGui::SetCursorPos(ImVec2(x - fillPadding, kBarTop));
				drawn.Clicked = ImGui::InvisibleButton(item.Id, ImVec2(drawn.Width + fillPadding * kBothSides, barHeight));

				const bool hovered = ImGui::IsItemHovered();
				const bool held = hovered && ImGui::IsItemActive();
				const ImVec4 fill = Core::IconButtonFill(ImGui::GetItemID(), hovered, held);

				if (fill.w > kInvisibleAlpha)
				{
					ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
															  ImGui::GetColorU32(fill));
				}

				if (hovered)
				{
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				}
			}
			else
			{
				ImGui::Dummy(ImVec2(drawn.Width, ImGui::GetTextLineHeight()));
			}

			DrawPieces(item.Pieces, lineStart);

			PopItemFace(item.Face);

			if (ImGui::IsItemHovered() && !item.Tooltip.empty())
			{
				ImGui::SetTooltip("%s", item.Tooltip.c_str());
			}

			return drawn;
		}

		class ItemRow
		{
		public:
			ItemRow(float start, float barHeight) : m_Next(start), m_BarHeight(barHeight) {}

			void Place(const StatusItem& item, float spacing)
			{
				const float x = m_Started ? m_Next + spacing : m_Next;
				const DrawnItem drawn = DrawItem(item, x, m_BarHeight);

				if (drawn.Clicked)
				{
					m_Clicked = item.Reports;
				}

				m_Next = x + drawn.Width;
				m_Started = true;
			}

			StatusBarClick GetClicked() const { return m_Clicked; }

		private:
			float m_Next = 0.0f;
			float m_BarHeight = 0.0f;
			bool m_Started = false;
			StatusBarClick m_Clicked = StatusBarClick::None;
		};

		StatusItem TextItem(std::string text, ImU32 colour, std::string tooltip = {})
		{
			StatusItem item;
			item.Pieces.push_back(Piece{.Text = std::move(text), .Colour = colour});
			item.Tooltip = std::move(tooltip);

			return item;
		}

		StatusItem ClickableTextItem(const char* id, StatusBarClick reports, std::string text, std::string tooltip)
		{
			StatusItem item = TextItem(std::move(text), TextColour(), std::move(tooltip));
			item.Id = id;
			item.Reports = reports;

			return item;
		}

		std::string DescribeCaret(const Core::OpenItem& item)
		{
			const Core::TextBuffer& buffer = item.Document->GetBuffer();
			const Core::Position where = buffer.ToPosition(item.Caret.Offset);
			const std::size_t column = Core::ColumnOfByte(buffer.LineText(where.Line), where.Column,
														  item.TabWidth);

			std::string description =
				"Ln " + std::to_string(where.Line + 1) + ", Col " + std::to_string(column + 1);

			const auto [from, to] = Core::SelectionRange(item.Caret);

			if (from != to)
			{
				description += " (" + std::to_string(to - from) + " selected)";
			}

			return description;
		}

		StatusItem ProblemsItem(const ProblemCounts& problems, const StatusBarColours& colours, std::string tooltip)
		{
			StatusItem item;
			item.Tooltip = std::move(tooltip);

			const float markGap = Core::Scaled(kMarkToTextGap);

			if (problems.Errors > 0)
			{
				item.Pieces.push_back(Piece{.Text = kErrorGlyph, .Colour = colours.Error});
				item.Pieces.push_back(Piece{.Text = std::to_string(problems.Errors), .Colour = colours.Error, .GapBefore = markGap});
			}

			if (problems.Warnings > 0)
			{
				const float gapBefore = item.Pieces.empty() ? kNoGap : Core::Scaled(kBetweenCountsGap);
				item.Pieces.push_back(Piece{.Text = kWarningGlyph, .Colour = colours.Warning, .GapBefore = gapBefore});
				item.Pieces.push_back(Piece{.Text = std::to_string(problems.Warnings), .Colour = colours.Warning, .GapBefore = markGap});
			}

			if (item.Pieces.empty())
			{
				item.Pieces.push_back(Piece{.Text = kNoProblemsGlyph, .Colour = DimColour()});
			}

			return item;
		}

		std::optional<ImU32> DotColourOf(ServerHealth health, const StatusBarColours& colours)
		{
			switch (health)
			{
			case ServerHealth::Starting:
			case ServerHealth::Limited:
				return colours.Warning;

			case ServerHealth::Ready:
				return colours.Ready;

			case ServerHealth::Failed:
				return colours.Error;

			case ServerHealth::None:
				break;
			}

			return std::nullopt;
		}

		StatusItem ServerItem(const StatusMessages& messages)
		{
			StatusItem item;
			item.Id = "##ServerStatus";
			item.Reports = messages.ServerOpensSettings ? StatusBarClick::Server : StatusBarClick::None;
			item.Tooltip = messages.ServerDetail.value_or(std::string{});

			if (const std::optional<ImU32> dotColour = DotColourOf(messages.Health, messages.Colours))
			{
				item.Pieces.push_back(Piece{.Kind = PieceKind::Dot, .Colour = *dotColour});
			}

			const float gapBefore = item.Pieces.empty() ? kNoGap : Core::Scaled(kMarkToTextGap);
			item.Pieces.push_back(Piece{.Text = *messages.Server, .Colour = TextColour(), .GapBefore = gapBefore});

			return item;
		}

		std::vector<StatusItem> RightHandItems(const StatusMessages& messages, const Core::OpenItem* document)
		{
			std::vector<StatusItem> items;

			if (messages.Problems)
			{
				items.push_back(ProblemsItem(*messages.Problems, messages.Colours, messages.ProblemsDetail.value_or(std::string{})));
			}

			if (messages.Server && !messages.Server->empty())
			{
				items.push_back(ServerItem(messages));
			}

			if (messages.Background && !messages.Background->empty())
			{
				items.push_back(TextItem(*messages.Background, TextColour(), messages.BackgroundDetail.value_or(std::string{})));
			}

			if (document != nullptr)
			{
				StatusItem caret = TextItem(DescribeCaret(*document), TextColour());
				caret.Face = ItemFace::Code;
				items.push_back(std::move(caret));

				items.push_back(TextItem(std::string{Core::EncodingName(document->Document->GetFileEncoding())}, TextColour()));
				items.push_back(TextItem(LineEndingName(document->Document->GetDocumentLineEnding(),
														document->Document->HasMixedLineEndings()),
										 TextColour()));
			}

			return items;
		}

		StatusBarClick DrawLeftHandItems(const StatusMessages& messages, const Core::OpenItem* document, float barHeight)
		{
			const float gap = Core::Scaled(kStatusItemGap);
			ItemRow row(ImGui::GetStyle().WindowPadding.x, barHeight);

			if (messages.Git)
			{
				const GitStatusSegment& segment = *messages.Git;

				if (segment.HasRepository)
				{
					row.Place(ClickableTextItem("##GitBranch", StatusBarClick::GitBranch, segment.Branch, segment.BranchTooltip), gap);
				}
				else
				{
					row.Place(TextItem(segment.Branch, DimColour(), segment.BranchTooltip), gap);
				}

				if (!segment.Distance.empty())
				{
					row.Place(TextItem(segment.Distance, TextColour(), segment.DistanceTooltip), ImGui::GetStyle().ItemSpacing.x);
				}

				if (!segment.Changes.empty())
				{
					row.Place(ClickableTextItem("##GitChanges", StatusBarClick::GitChanges, segment.Changes, segment.ChangesTooltip), gap);
				}
			}

			if (document != nullptr && document->Document->IsReadOnly())
			{
				row.Place(TextItem(kReadOnlyWarning, messages.Colours.Warning), gap);
			}

			return row.GetClicked();
		}

		StatusBarClick DrawRightHandItems(const std::vector<StatusItem>& items, float barHeight)
		{
			if (items.empty())
			{
				return StatusBarClick::None;
			}

			const float gap = Core::Scaled(kStatusItemGap);
			float width = gap * static_cast<float>(items.size() - 1);

			for (const StatusItem& item : items)
			{
				width += ItemWidth(item);
			}

			const float start = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - BellWidth() - gap - width;
			ItemRow row(start, barHeight);

			for (const StatusItem& item : items)
			{
				row.Place(item, gap);
			}

			return row.GetClicked();
		}
	}

	StatusBarClick DrawStatusBar(const Core::DocumentSet& documents,
								 std::optional<std::size_t> shown,
								 const StatusMessages& messages)
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float height = ImGui::GetFrameHeight();

		// WARNING: Without NoScrollWithMouse a wheel over the bar scrolls it: every item spans the bar's full height,
		// and with the window padding that reaches past the window's inner height.
		constexpr ImGuiWindowFlags windowFlags =
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kNoBorder);
		const bool barBegun = ImGui::BeginViewportSideBar(kStatusBarWindowName, viewport, ImGuiDir_Down, height, windowFlags);
		ImGui::PopStyleVar();

		if (!barBegun)
		{
			ImGui::End();
			return StatusBarClick::None;
		}

		const bool hasDocument = shown && documents.At(*shown).Document != nullptr;
		const Core::OpenItem* document = hasDocument ? &documents.At(*shown) : nullptr;

		StatusBarClick clicked = DrawLeftHandItems(messages, document, height);

		if (const StatusBarClick rightClick = DrawRightHandItems(RightHandItems(messages, document), height);
			rightClick != StatusBarClick::None)
		{
			clicked = rightClick;
		}

		if (DrawNotificationBell(messages.MissedNotificationColour))
		{
			clicked = StatusBarClick::Notifications;
		}

		const std::optional<std::string>& centred = messages.Transient ? messages.Transient : messages.Search;

		if (centred && !centred->empty())
		{
			DrawCentred(*centred, height);
		}

		ImGui::End();

		return clicked;
	}
}
