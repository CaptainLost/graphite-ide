#include "Editor/StatusBar/StatusBar.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
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
		constexpr float kAtRest = 0.0f;
		constexpr float kFullyHovered = 1.0f;

		constexpr float kItemFillPadding = 7.0f;
		constexpr float kBothSides = 2.0f;
		constexpr float kMarkToTextGap = 4.0f;
		constexpr float kBetweenCountsGap = 8.0f;

		constexpr float kStatusTextShareOfDisabled = 0.47f;

		// The mockup's ready green on a dark chrome. On a light one, the light git added colour, (40, 120, 40), as
		// much darker as the dark green is brighter than the dark git added colour: a quarter.
		constexpr ImU32 kReadyOnDark = IM_COL32(127, 191, 106, 255);
		constexpr ImU32 kReadyOnLight = IM_COL32(32, 96, 32, 255);

		constexpr const char* kBellId = "##NotificationBell";

		constexpr const char* kErrorGlyph = ICON_LC_CIRCLE_X;
		constexpr const char* kWarningGlyph = ICON_LC_TRIANGLE_ALERT;
		constexpr const char* kNoProblemsGlyph = ICON_LC_CHECK;

		constexpr const char* kReadOnlyWarning = "read-only: not valid text for its encoding";

		enum class PieceKind
		{
			Text,
			Dot,
			Loader
		};

		struct Piece
		{
			PieceKind Kind = PieceKind::Text;
			std::string Text;

			// None is the bar's own text: the status grey at rest, brightening to the text colour as its item is hovered.
			std::optional<ImU32> Colour;

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
			bool Hovered = false;
			float Width = 0.0f;
		};

		// WARNING: A tooltip takes the font in effect where it is begun, and the bar's 15 px font and frame padding stay
		// pushed while its items are drawn, so the item under the pointer leaves its tooltip here and the bar shows it
		// once they are popped, at the interface size every other tooltip is drawn at.
		class PendingTooltip
		{
		public:
			void Leave(std::string text) { m_Text = std::move(text); }

			void Show() const
			{
				if (m_Text.has_value())
				{
					ImGui::SetTooltip("%s", m_Text->c_str());
				}
			}

		private:
			std::optional<std::string> m_Text;
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

		// WARNING: rounded to a whole pixel because ImGui truncates a window's position and size: at 125 % a bar
		// 28.75 px tall would stop a pixel short of the bottom of the application's window.
		float BarHeight()
		{
			return std::round(Core::Scaled(kStatusBarHeight));
		}

		float BellWidth()
		{
			return Core::IconButtonSize(Core::IconButtonFit::Bar);
		}

		ImU32 ToPackedColour(const Core::Color& colour)
		{
			return ImGui::GetColorU32(ImVec4(colour.R, colour.G, colour.B, colour.A));
		}

		ImU32 DimColour()
		{
			return ImGui::GetColorU32(ImGuiCol_TextDisabled);
		}

		// The bar's own text, from the status grey at rest to the text colour under the pointer.
		ImU32 BarTextColour(float hoverAmount)
		{
			if (hoverAmount <= kAtRest)
			{
				return StatusBarTextColour();
			}

			return ImGui::GetColorU32(Core::Animation::Blend(ImGui::ColorConvertU32ToFloat4(StatusBarTextColour()),
															 ImGui::GetStyleColorVec4(ImGuiCol_Text), hoverAmount));
		}

		// How far the item's fill has faded in, which its words follow toward the text colour.
		float HoverAmountOf(const ImVec4& fill)
		{
			const float hoveredAlpha = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered).w;

			return hoveredAlpha > kInvisibleAlpha ? std::clamp(fill.w / hoveredAlpha, kAtRest, kFullyHovered) : kAtRest;
		}

		bool DrawNotificationBell(const std::optional<Core::Color>& missedColour, PendingTooltip& tooltip)
		{
			ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - BellWidth() - ImGui::GetStyle().WindowPadding.x, kBarTop));

			// The bell rests dim, and a dim icon button eases its glyph from the disabled-text colour to the text colour
			// with its fill, so the status grey stands in for the disabled-text colour while it is drawn.
			ImGui::PushStyleColor(ImGuiCol_TextDisabled, StatusBarTextColour());

			// WARNING: Without NoFocus the press focuses the bar, and the popup the click opens hands the
			// keyboard back to the bar when it closes rather than to the editor.
			constexpr bool kNotEngaged = false;
			constexpr const char* kNoTooltipOfItsOwn = nullptr;
			const Core::IconButtonOptions bellButton{.Fit = Core::IconButtonFit::Bar,
													 .Rest = Core::IconButtonRest::Dim,
													 .Flags = ImGuiButtonFlags_NoFocus | ImGuiButtonFlags_NoNavFocus};
			const bool clicked =
				Core::DrawIconButton(kBellId, ICON_LC_BELL, kNotEngaged, kNoTooltipOfItsOwn, bellButton);

			ImGui::PopStyleColor();

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				tooltip.Leave("Notifications");
			}

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
			ImGui::PushStyleColor(ImGuiCol_Text, StatusBarTextColour());
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopStyleColor();
		}

		float PieceWidth(const Piece& piece)
		{
			if (piece.Kind == PieceKind::Dot)
			{
				return Core::Scaled(kServerDotDiameter);
			}

			if (piece.Kind == PieceKind::Loader)
			{
				return Core::LoadingLineWidth(piece.Text.c_str());
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

		// WARNING: The loader keeps its wait under the current id, so it is drawn under the item's own.
		void DrawLoaderPiece(const char* itemId, const Piece& piece, ImVec2 at, ImU32 colour)
		{
			ImGui::SetCursorScreenPos(at);
			ImGui::PushID(itemId);
			ImGui::PushStyleColor(ImGuiCol_TextDisabled, colour);

			Core::LoadingLine(piece.Text.c_str(), Core::WhileLoaderWaits::ShowText);

			ImGui::PopStyleColor();
			ImGui::PopID();
		}

		void DrawPieces(const char* itemId, const std::vector<Piece>& pieces, ImVec2 lineStart, float hoverAmount)
		{
			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			const float lineMiddle = lineStart.y + ImGui::GetTextLineHeight() * kHalf;
			const ImU32 barText = BarTextColour(hoverAmount);
			float x = lineStart.x;

			for (const Piece& piece : pieces)
			{
				x += piece.GapBefore;
				const float width = PieceWidth(piece);
				const ImU32 colour = piece.Colour.value_or(barText);

				if (piece.Kind == PieceKind::Dot)
				{
					const float radius = width * kHalf;
					drawList->AddCircleFilled(ImVec2(x + radius, lineMiddle), radius, colour);
				}
				else if (piece.Kind == PieceKind::Loader)
				{
					DrawLoaderPiece(itemId, piece, ImVec2(x, lineStart.y), colour);
				}
				else
				{
					drawList->AddText(ImVec2(x, lineStart.y), colour, piece.Text.c_str());
				}

				x += width;
			}
		}

		void PushItemFace(ItemFace face)
		{
			if (face == ItemFace::Code)
			{
				Core::PushCodeFont(kStatusBarFontSize);
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
			float hoverAmount = kAtRest;

			if (clickable)
			{
				const float fillPadding = Core::Scaled(kItemFillPadding);

				ImGui::SetCursorPos(ImVec2(x - fillPadding, kBarTop));
				drawn.Clicked = ImGui::InvisibleButton(item.Id, ImVec2(drawn.Width + fillPadding * kBothSides, barHeight));

				const bool hovered = ImGui::IsItemHovered();
				const bool held = hovered && ImGui::IsItemActive();
				const ImVec4 fill = Core::IconButtonFill(ImGui::GetItemID(), hovered, held);
				hoverAmount = HoverAmountOf(fill);

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

			// WARNING: Asked before the pieces draw, since a loader piece submits items of its own.
			drawn.Hovered = ImGui::IsItemHovered();

			DrawPieces(item.Id, item.Pieces, lineStart, hoverAmount);

			PopItemFace(item.Face);

			return drawn;
		}

		class ItemRow
		{
		public:
			ItemRow(float start, float barHeight, PendingTooltip& tooltip)
				: m_Next(start), m_BarHeight(barHeight), m_Tooltip(&tooltip)
			{
			}

			void Place(const StatusItem& item, float spacing)
			{
				const float x = m_Started ? m_Next + spacing : m_Next;
				const DrawnItem drawn = DrawItem(item, x, m_BarHeight);

				if (drawn.Clicked)
				{
					m_Clicked = item.Reports;
				}

				if (drawn.Hovered && !item.Tooltip.empty())
				{
					m_Tooltip->Leave(item.Tooltip);
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
			PendingTooltip* m_Tooltip = nullptr;
		};

		StatusItem TextItem(std::string text, std::optional<ImU32> colour, std::string tooltip = {})
		{
			StatusItem item;
			item.Pieces.push_back(Piece{.Text = std::move(text), .Colour = colour});
			item.Tooltip = std::move(tooltip);

			return item;
		}

		StatusItem BarTextItem(std::string text, std::string tooltip = {})
		{
			return TextItem(std::move(text), std::nullopt, std::move(tooltip));
		}

		StatusItem ClickableTextItem(const char* id, StatusBarClick reports, std::string text, std::string tooltip)
		{
			StatusItem item = BarTextItem(std::move(text), std::move(tooltip));
			item.Id = id;
			item.Reports = reports;

			return item;
		}

		StatusItem BranchItem(const GitStatusSegment& segment)
		{
			StatusItem item = ClickableTextItem("##GitBranch", StatusBarClick::GitBranch, segment.Branch, segment.BranchTooltip);

			if (segment.BranchBusy)
			{
				item.Pieces.front().Kind = PieceKind::Loader;
			}

			return item;
		}

		StatusItem ActivityItem(const GitActivity& activity)
		{
			StatusItem item = BarTextItem(activity.Text, activity.Tooltip);
			item.Id = "##GitActivity";
			item.Reports = activity.OpensGit ? StatusBarClick::GitBranch : StatusBarClick::None;

			if (activity.Busy)
			{
				item.Pieces.front().Kind = PieceKind::Loader;
			}

			return item;
		}

		StatusItem FileOperationItem(const std::string& text)
		{
			StatusItem item = BarTextItem(text);
			item.Id = "##FileOperation";
			item.Pieces.front().Kind = PieceKind::Loader;

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
			item.Pieces.push_back(Piece{.Text = *messages.Server, .GapBefore = gapBefore});

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
				items.push_back(BarTextItem(*messages.Background, messages.BackgroundDetail.value_or(std::string{})));
			}

			if (document != nullptr)
			{
				StatusItem caret = BarTextItem(DescribeCaret(*document));
				caret.Face = ItemFace::Code;
				items.push_back(std::move(caret));

				items.push_back(BarTextItem(std::string{Core::EncodingName(document->Document->GetFileEncoding())}));
				items.push_back(BarTextItem(LineEndingName(document->Document->GetDocumentLineEnding(),
														   document->Document->HasMixedLineEndings())));
			}

			return items;
		}

		StatusBarClick DrawLeftHandItems(const StatusMessages& messages, const Core::OpenItem* document, float barHeight,
										 PendingTooltip& tooltip)
		{
			const float gap = Core::Scaled(kStatusItemGap);
			ItemRow row(ImGui::GetStyle().WindowPadding.x, barHeight, tooltip);

			if (messages.Git)
			{
				const GitStatusSegment& segment = *messages.Git;

				if (segment.HasRepository)
				{
					row.Place(BranchItem(segment), gap);
				}
				else
				{
					row.Place(TextItem(segment.Branch, DimColour(), segment.BranchTooltip), gap);
				}

				if (!segment.Distance.empty())
				{
					row.Place(BarTextItem(segment.Distance, segment.DistanceTooltip), ImGui::GetStyle().ItemSpacing.x);
				}

				if (!segment.Changes.empty())
				{
					row.Place(ClickableTextItem("##GitChanges", StatusBarClick::GitChanges, segment.Changes, segment.ChangesTooltip), gap);
				}

				if (segment.Activity.has_value())
				{
					row.Place(ActivityItem(*segment.Activity), gap);
				}
			}

			if (messages.FileOperation.has_value())
			{
				row.Place(FileOperationItem(*messages.FileOperation), gap);
			}

			if (document != nullptr && document->Document->IsReadOnly())
			{
				row.Place(TextItem(kReadOnlyWarning, messages.Colours.Warning), gap);
			}

			return row.GetClicked();
		}

		StatusBarClick DrawRightHandItems(const std::vector<StatusItem>& items, float barHeight, PendingTooltip& tooltip)
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
			ItemRow row(start, barHeight, tooltip);

			for (const StatusItem& item : items)
			{
				row.Place(item, gap);
			}

			return row.GetClicked();
		}
	}

	ImU32 StatusBarTextColour()
	{
		const ImVec4* colours = ImGui::GetStyle().Colors;

		return ImGui::GetColorU32(ImLerp(colours[ImGuiCol_Text], colours[ImGuiCol_TextDisabled], kStatusTextShareOfDisabled));
	}

	ImU32 ServerReadyColour(const Core::Color& chromeBackground)
	{
		return Core::IsLightBackground(chromeBackground) ? kReadyOnLight : kReadyOnDark;
	}

	StatusBarClick DrawStatusBar(const Core::DocumentSet& documents,
								 std::optional<std::size_t> shown,
								 const StatusMessages& messages)
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float height = BarHeight();

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

		Core::PushInterfaceFont(kStatusBarFontSize);

		// WARNING: The bell is an icon button as tall as a frame, so the frame padding is set to make a frame as tall as
		// the bar, whatever padding the theme gives frames elsewhere.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
							ImVec2(ImGui::GetStyle().FramePadding.x, (height - ImGui::GetFontSize()) * kHalf));

		const bool hasDocument = shown && documents.At(*shown).Document != nullptr;
		const Core::OpenItem* document = hasDocument ? &documents.At(*shown) : nullptr;

		PendingTooltip tooltip;
		StatusBarClick clicked = DrawLeftHandItems(messages, document, height, tooltip);

		if (const StatusBarClick rightClick = DrawRightHandItems(RightHandItems(messages, document), height, tooltip);
			rightClick != StatusBarClick::None)
		{
			clicked = rightClick;
		}

		if (DrawNotificationBell(messages.MissedNotificationColour, tooltip))
		{
			clicked = StatusBarClick::Notifications;
		}

		const std::optional<std::string>& centred = messages.Transient ? messages.Transient : messages.Search;

		if (centred && !centred->empty())
		{
			DrawCentred(*centred, height);
		}

		ImGui::PopStyleVar();
		ImGui::PopFont();

		tooltip.Show();
		ImGui::End();

		return clicked;
	}
}
