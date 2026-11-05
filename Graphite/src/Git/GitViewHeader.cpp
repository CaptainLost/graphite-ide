#include "Git/GitView.h"

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/Theme.h"

#include "Git/Window/WindowBar.h"
#include "UI/FittedText.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <string>
#include <vector>

namespace Graphite {

	namespace {

		constexpr float kHalf = 0.5f;
		constexpr float kKeepFontSize = 0.0f;
		constexpr float kMockupFont = 13.0f;

		constexpr float kVsHeight = 22.0f;
		constexpr float kVsSide = 6.0f;
		constexpr float kVsGap = 5.0f;
		constexpr float kVsRounding = 5.0f;
		constexpr float kVsText = 12.0f;
		constexpr float kMarkText = 10.5f;
		constexpr float kMarkSide = 5.0f;
		constexpr float kMarkRounding = 4.0f;
		constexpr float kMarkPadding = 1.0f;
		constexpr float kSwitchBorder = 1.0f;

		constexpr float kSwitchHeight = 26.0f;
		constexpr float kSwitchRounding = 7.0f;
		constexpr float kSwitchPadding = 2.0f;
		constexpr float kSwitchGap = 2.0f;
		constexpr float kSegmentHeight = 20.0f;
		constexpr float kSegmentSide = 8.0f;
		constexpr float kSegmentRounding = 5.0f;
		constexpr float kSwitchText = 12.0f;

		constexpr float kBannerSide = 10.0f;
		constexpr float kBannerAbove = 6.0f;
		constexpr float kBannerBelow = 6.0f;
		constexpr float kBannerRounding = 6.0f;
		constexpr float kBannerText = 12.5f;

		constexpr const char* kSeparator = " \xC2\xB7 ";
		constexpr const char* kMergesLabel = "Merges";
		constexpr const char* kAllCommitsLabel = "All commits";

		float TextWidth(const std::string& text)
		{
			return text.empty() ? 0.0f : ImGui::CalcTextSize(text.c_str()).x;
		}

		float BoldTextWidth(const std::string& text)
		{
			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			const float width = TextWidth(text);
			ImGui::PopFont();

			return width;
		}

		// A piece of the vs button: the lead, the line, the counts split where "N ahead" ends.
		struct VsPiece
		{
			std::string Text;
			ImU32 Colour = 0;
			bool Bold = false;
		};

		float WidthOf(const VsPiece& piece)
		{
			return piece.Bold ? BoldTextWidth(piece.Text) : TextWidth(piece.Text);
		}

		void DrawPiece(ImDrawList* drawList, const VsPiece& piece, ImVec2 at)
		{
			if (piece.Bold)
			{
				ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			}

			drawList->AddText(at, piece.Colour, piece.Text.c_str());

			if (piece.Bold)
			{
				ImGui::PopFont();
			}
		}

		// `line` is the label's line cut to the room it has.
		std::vector<VsPiece> PiecesOf(const VsLabel& label, const WindowBarColours& colours, const std::string& line)
		{
			const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
			std::vector<VsPiece> pieces;

			pieces.push_back(VsPiece{label.Lead, colours.Dim, false});

			if (!label.Line.empty())
			{
				pieces.push_back(VsPiece{" ", colours.Dim, false});
				pieces.push_back(VsPiece{line, text, true});
			}

			if (!label.Counts.empty())
			{
				constexpr const char* kAheadEnd = ", ";
				const std::size_t split = label.Counts.find(kAheadEnd);

				pieces.push_back(VsPiece{kSeparator, colours.Dim, false});
				pieces.push_back(VsPiece{label.Counts.substr(0, split), colours.Match, true});

				if (split != std::string::npos)
				{
					pieces.push_back(VsPiece{label.Counts.substr(split), colours.Dim, false});
				}
			}

			return pieces;
		}

		struct VsLayout
		{
			std::vector<VsPiece> Pieces;
			std::string Mark;
			float MarkWidth = 0.0f;
			float Width = 0.0f;
		};

		// With the vs button's font pushed: its pieces, the line's name cut to what `maxWidth` leaves, and the width taken.
		VsLayout LayOutVs(const VsLabel& label, const WindowBarColours& colours, float maxWidth, float unit)
		{
			const auto Em = [unit](float mockupPixels) { return mockupPixels * unit; };
			const float caretWidth = TextWidth(ICON_LC_CHEVRON_DOWN);

			VsLayout layout;

			if (label.Mark.has_value())
			{
				layout.Mark = *label.Mark;

				Core::PushInterfaceFont(ImGui::GetFontSize() * kMarkText / kMockupFont);
				layout.MarkWidth = TextWidth(layout.Mark) + 2.0f * Em(kMarkSide);
				ImGui::PopFont();
			}

			// What the line's name may take: the room less the lead, the counts, the chip and the caret.
			const float fixedWidth =
				2.0f * Em(kVsSide) + Em(kVsGap) + caretWidth + (layout.Mark.empty() ? 0.0f : Em(kVsGap) + layout.MarkWidth);
			float piecesWithoutLine = 0.0f;
			float reservedForLaterCounts = 0.0f;

			// The behind count lands later: its place is as wide as the landed text, so nothing on the row moves then.
			if (!label.Counts.empty())
			{
				const auto restOf = [](const std::string& counts) {
					return counts.substr(std::min(counts.find(", "), counts.size()));
				};

				reservedForLaterCounts = std::max(0.0f, TextWidth(restOf(label.CountsRoom)) - TextWidth(restOf(label.Counts)));
			}

			for (const VsPiece& piece : PiecesOf(label, colours, std::string{}))
			{
				piecesWithoutLine += WidthOf(piece);
			}

			const float lineRoom = std::max(0.0f, maxWidth - fixedWidth - piecesWithoutLine - reservedForLaterCounts);

			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			const std::string line = FitToWidth(label.Line, lineRoom);
			ImGui::PopFont();

			layout.Pieces = PiecesOf(label, colours, line);

			float piecesWidth = reservedForLaterCounts;

			for (const VsPiece& piece : layout.Pieces)
			{
				piecesWidth += WidthOf(piece);
			}

			layout.Width = std::min(maxWidth, fixedWidth + piecesWidth);

			return layout;
		}

		ImU32 Faded(ImU32 colour, bool enabled)
		{
			if (enabled)
			{
				return colour;
			}

			ImVec4 faded = ImGui::ColorConvertU32ToFloat4(colour);
			faded.w *= ImGui::GetStyle().DisabledAlpha;

			return ImGui::ColorConvertFloat4ToU32(faded);
		}
	}

	std::string GitView::ShownRefName() const
	{
		if (m_SelectedStash.has_value() || RestoreWaits())
		{
			return {};
		}

		if (m_SelectedRef.has_value())
		{
			return *m_SelectedRef;
		}

		if (!m_HeadRef.has_value() || !m_SidebarBuiltFrom.has_value() || !m_SidebarBuiltFrom->Read)
		{
			return {};
		}

		return m_SidebarBuiltFrom->Read->Refs[*m_HeadRef].FullName;
	}

	std::optional<Core::LineDistance> GitView::ShownDistance(const LineRead& read, std::string_view shownRef)
	{
		const BranchLines* const lines = CurrentLines();

		if (lines == nullptr)
		{
			return std::nullopt;
		}

		const std::string_view shown = lines->ShownAs(shownRef);

		// A pick for a base or an old copy is measured from the reference whose history is shown, as its fold row is.
		if (const std::optional<std::size_t> picked = lines->PickedLineOf(shown))
		{
			const std::optional<std::size_t> self = lines->IndexOf(shownRef);

			return self.has_value() ? m_Lines.DistanceOf(read.Refs[*self].Tip, read.Refs[*picked].Tip) : std::nullopt;
		}

		if (const Core::OldCopy* const copy = lines->OldCopyNamed(shown); copy != nullptr)
		{
			return m_Lines.DistanceOf(read.Refs[copy->Ref].Tip, read.Refs[copy->Of].Tip);
		}

		const Core::BranchLine* const line = lines->LineOf(shownRef);

		// Behind is said only for a branch that is neither merged nor without commits of its own.
		if (line == nullptr || !line->ComparedWith.has_value() || line->State != Core::BranchState::NotMerged)
		{
			return std::nullopt;
		}

		return m_Lines.DistanceOf(read.Refs[line->Ref].Tip, read.Refs[*line->ComparedWith].Tip);
	}

	float GitView::VsButtonWidth(const VsLabel& label) const
	{
		const WindowBarColours colours = ReadWindowBarColours(m_Scheme.Get());
		const float unit = WindowBarEm(1.0f); // taken before a smaller font is pushed

		Core::PushInterfaceFont(ImGui::GetFontSize() * kVsText / kMockupFont);
		const float width = LayOutVs(label, colours, FLT_MAX, unit).Width;
		ImGui::PopFont();

		return width;
	}

	bool GitView::DrawVsButton(const VsLabel& label, float maxWidth)
	{
		const WindowBarColours colours = ReadWindowBarColours(m_Scheme.Get());
		const float unit = WindowBarEm(1.0f); // taken before a smaller font is pushed
		const auto Em = [unit](float mockupPixels) { return mockupPixels * unit; };
		const float side = Em(kVsSide);
		const float gap = Em(kVsGap);
		const float markSide = Em(kMarkSide);
		const float height = ImGui::GetFrameHeight();

		Core::PushInterfaceFont(ImGui::GetFontSize() * kVsText / kMockupFont);

		const VsLayout layout = LayOutVs(label, colours, maxWidth, unit);
		const std::vector<VsPiece>& pieces = layout.Pieces;
		const std::string& mark = layout.Mark;
		const float markWidth = layout.MarkWidth;
		const float width = layout.Width;

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton("##vsButton", ImVec2(width, height));
		const bool hovered = ImGui::IsItemHovered();

		if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		{
			ImGui::SetTooltip("What this branch is compared with, and why");
		}

		ImDrawList* const drawList = ImGui::GetWindowDrawList();
		const float visualHeight = Em(kVsHeight);
		const float top = origin.y + (height - visualHeight) * kHalf;

		if (hovered)
		{
			drawList->AddRectFilled(ImVec2(origin.x, top), ImVec2(origin.x + width, top + visualHeight), colours.ChipHoveredFill,
									Em(kVsRounding));
			drawList->AddRect(ImVec2(origin.x, top), ImVec2(origin.x + width, top + visualHeight), colours.Rule, Em(kVsRounding));
		}

		const float textTop = origin.y + (height - ImGui::GetTextLineHeight()) * kHalf;
		float x = origin.x + side;

		for (const VsPiece& piece : pieces)
		{
			DrawPiece(drawList, piece, ImVec2(x, textTop));
			x += WidthOf(piece);
		}

		if (!mark.empty())
		{
			x += gap;

			Core::PushInterfaceFont(ImGui::GetFontSize() * kMarkText / kMockupFont);

			const float chipHeight = ImGui::GetTextLineHeight() + 2.0f * kMarkPadding;
			const float chipTop = origin.y + (height - chipHeight) * kHalf;
			drawList->AddRectFilled(ImVec2(x, chipTop), ImVec2(x + markWidth, chipTop + chipHeight), colours.AskFill,
									Em(kMarkRounding));
			drawList->AddText(ImVec2(x + markSide, chipTop + kMarkPadding), colours.AskQuestion, mark.c_str());
			ImGui::PopFont();

			x += markWidth;
		}

		drawList->AddText(ImVec2(x + gap, textTop), colours.Dim, ICON_LC_CHEVRON_DOWN);

		ImGui::PopFont();

		return clicked;
	}

	float GitView::HistorySwitchWidth() const
	{
		const float unit = WindowBarEm(1.0f); // taken before a smaller font is pushed
		const auto Em = [unit](float mockupPixels) { return mockupPixels * unit; };

		Core::PushInterfaceFont(ImGui::GetFontSize() * kSwitchText / kMockupFont);
		const float labels = TextWidth(kMergesLabel) + TextWidth(kAllCommitsLabel);
		ImGui::PopFont();

		return labels + 4.0f * Em(kSegmentSide) + Em(kSwitchGap) + 2.0f * Em(kSwitchPadding) + 2.0f * kSwitchBorder;
	}

	bool GitView::DrawHistorySwitch(bool enabled)
	{
		const WindowBarColours colours = ReadWindowBarColours(m_Scheme.Get());
		const float unit = WindowBarEm(1.0f); // taken before a smaller font is pushed
		const auto Em = [unit](float mockupPixels) { return mockupPixels * unit; };
		const float frameHeight = ImGui::GetFrameHeight();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float totalWidth = HistorySwitchWidth();

		ImDrawList* const drawList = ImGui::GetWindowDrawList();
		const float containerTop = origin.y + (frameHeight - Em(kSwitchHeight)) * kHalf;
		drawList->AddRectFilled(ImVec2(origin.x, containerTop), ImVec2(origin.x + totalWidth, containerTop + Em(kSwitchHeight)),
								Faded(colours.ChipFill, enabled), Em(kSwitchRounding));
		drawList->AddRect(ImVec2(origin.x, containerTop), ImVec2(origin.x + totalWidth, containerTop + Em(kSwitchHeight)),
						  Faded(colours.Rule, enabled), Em(kSwitchRounding));

		struct Segment
		{
			const char* Label;
			const char* Tooltip;
			bool Chosen;
			bool FirstParents;
		};

		const Segment segments[] = {
			{kMergesLabel, "First parents only: one row per merge, squash or direct commit", m_FirstParentsOnly, true},
			{kAllCommitsLabel, "Every commit with the full graph", !m_FirstParentsOnly, false}};

		Core::PushInterfaceFont(ImGui::GetFontSize() * kSwitchText / kMockupFont);

		bool changed = false;
		float x = origin.x + Em(kSwitchPadding) + kSwitchBorder;

		for (const Segment& segment : segments)
		{
			const float width = ImGui::CalcTextSize(segment.Label).x + 2.0f * Em(kSegmentSide);

			ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
			ImGui::PushID(segment.Label);

			if (ImGui::InvisibleButton("##segment", ImVec2(width, frameHeight)) && enabled && !segment.Chosen)
			{
				m_FirstParentsOnly = segment.FirstParents;
				changed = true;
			}

			ImGui::PopID();

			const bool hovered = ImGui::IsItemHovered();

			if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				ImGui::SetTooltip("%s", enabled ? segment.Tooltip : "Not for a stash or a file's history");
			}

			const float segmentTop = origin.y + (frameHeight - Em(kSegmentHeight)) * kHalf;

			if (segment.Chosen)
			{
				drawList->AddRectFilled(ImVec2(x, segmentTop), ImVec2(x + width, segmentTop + Em(kSegmentHeight)),
										Faded(colours.ChipHoveredFill, enabled), Em(kSegmentRounding));
			}

			const ImU32 textColour = segment.Chosen || (hovered && enabled) ? ImGui::GetColorU32(ImGuiCol_Text) : colours.Dim;
			drawList->AddText(ImVec2(x + Em(kSegmentSide), origin.y + (frameHeight - ImGui::GetTextLineHeight()) * kHalf),
							  Faded(textColour, enabled), segment.Label);

			x += width + Em(kSwitchGap);
		}

		ImGui::PopFont();

		return changed;
	}

	void GitView::DrawHistoryBanner(const std::string& text)
	{
		const WindowBarColours colours = ReadWindowBarColours(m_Scheme.Get());
		const float unit = WindowBarEm(1.0f); // taken before a smaller font is pushed
		const auto Em = [unit](float mockupPixels) { return mockupPixels * unit; };
		const float side = Em(kBannerSide);
		const float width = ImGui::GetContentRegionAvail().x - 2.0f * side;

		if (width <= 0.0f)
		{
			return;
		}

		Core::PushInterfaceFont(ImGui::GetFontSize() * kBannerText / kMockupFont);

		const float padding = Em(kBannerAbove);
		const float wrapWidth = width - 2.0f * side;
		const ImVec2 textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth);

		ImGui::Dummy(ImVec2(0.0f, Em(kBannerAbove) * kHalf));

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 corner(origin.x + side, origin.y);
		const ImVec2 size(width, textSize.y + 2.0f * padding);

		ImDrawList* const drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(corner, ImVec2(corner.x + size.x, corner.y + size.y), colours.ChipFill, Em(kBannerRounding));
		drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(corner.x + side, corner.y + padding),
						  ImGui::GetColorU32(ImGuiCol_Text), text.c_str(), nullptr, wrapWidth);

		ImGui::PopFont();

		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy(ImVec2(size.x + 2.0f * side, size.y + Em(kBannerBelow)));
	}
}
