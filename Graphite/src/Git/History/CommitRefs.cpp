#include "Git/History/CommitRefs.h"

#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/TextMetrics.h"
#include "Git/Window/WindowBar.h"
#include "UI/FittedText.h"
#include "UI/TypographicMarks.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		bool IsRemoteSymbolicHead(const Core::Branch& branch)
		{
			return branch.Kind == Core::RefKind::Remote && branch.Name.ends_with("/HEAD");
		}

		// The mockup's pill, in its pixels at a 13 pixel font.
		constexpr float kPillTextPixels = 11.0f;
		constexpr float kPillHeightPixels = 16.0f;
		constexpr float kPillSidePaddingPixels = 6.0f;
		constexpr float kPillRoundingPixels = 4.0f;
		constexpr float kPillMaxWidthPixels = 230.0f;
		constexpr float kHalf = 0.5f;

		struct PillFont
		{
			PillFont() { ImGui::PushFont(nullptr, WindowBarEm(kPillTextPixels)); }
			~PillFont() { ImGui::PopFont(); }
			PillFont(const PillFont&) = delete;
			PillFont& operator=(const PillFont&) = delete;
		};

		// WARNING: Measured in the pill's font, so call it with a PillFont alive.

		bool ListsBefore(const CommitRef& left, const CommitRef& right)
		{
			if (left.Kind != right.Kind)
			{
				return left.Kind < right.Kind;
			}

			return left.Name < right.Name;
		}
	}

	CommitRefMap BuildCommitRefs(const std::vector<Core::Branch>& branches)
	{
		CommitRefMap refs;

		for (const Core::Branch& branch : branches)
		{
			if (IsRemoteSymbolicHead(branch))
			{
				continue;
			}

			CommitRefKind kind = CommitRefKind::Local;

			if (branch.Kind == Core::RefKind::Remote)
			{
				kind = CommitRefKind::Remote;
			}
			else if (branch.IsHead)
			{
				kind = CommitRefKind::Head;
			}

			refs[branch.Tip].push_back(CommitRef{.Name = branch.Name, .Kind = kind});
		}

		for (auto& [tip, commitRefs] : refs)
		{
			std::ranges::sort(commitRefs, ListsBefore);
		}

		return refs;
	}

	std::string RefPillText(const CommitRef& ref)
	{
		return ref.Kind == CommitRefKind::Head ? "HEAD " + std::string{kRightwardsArrow} + " " + ref.Name : ref.Name;
	}

	RefPillColours RefPillColoursOf(CommitRefKind kind, const GitPalette& palette)
	{
		switch (kind)
		{
		case CommitRefKind::Head:
			return {palette.RefHeadFill, palette.RefHeadText};
		case CommitRefKind::Local:
			return {palette.RefLocalFill, palette.RefLocalText};
		case CommitRefKind::Remote:
			return {palette.RefRemoteFill, palette.RefRemoteText};
		case CommitRefKind::Tag:
			return {palette.RefTagFill, palette.RefTagText};
		}

		return {palette.RefLocalFill, palette.RefLocalText};
	}

	RefPill MeasureRefPill(const CommitRef& ref)
	{
		const PillFont font;

		RefPill pill;
		pill.Kind = ref.Kind;
		pill.Full = RefPillText(ref);

		const float padding = WindowBarEm(kPillSidePaddingPixels);
		const float room = WindowBarEm(kPillMaxWidthPixels) - padding * 2.0f;
		const std::optional<std::size_t> cut = CutToFit(pill.Full, room, Core::TextWidth(kEllipsis), Core::TextWidth);

		pill.IsCut = cut.has_value();
		pill.Shown = pill.IsCut ? pill.Full.substr(0, *cut) + std::string{kEllipsis} : pill.Full;
		pill.Width = Core::TextWidth(pill.Shown) + padding * 2.0f;

		return pill;
	}

	void DrawRefPill(const RefPill& pill, const GitPalette& palette, float alpha)
	{
		const float lineHeight = ImGui::GetTextLineHeight();
		const RefPillColours colours = RefPillColoursOf(pill.Kind, palette);
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		{
			const PillFont font;

			const float padding = WindowBarEm(kPillSidePaddingPixels);
			const float height = std::floor(WindowBarEm(kPillHeightPixels));
			const float top = origin.y + std::floor((lineHeight - height) * kHalf);

			ImDrawList* const drawList = ImGui::GetWindowDrawList();

			drawList->AddRectFilled(ImVec2(origin.x, top), ImVec2(origin.x + pill.Width, top + height),
									Core::WithAlphaScaled(colours.Fill, alpha), WindowBarEm(kPillRoundingPixels));
			drawList->AddText(ImVec2(origin.x + padding, top + std::floor((height - ImGui::GetFontSize()) * kHalf)),
							  Core::WithAlphaScaled(colours.Text, alpha), pill.Shown.c_str());
		}

		ImGui::Dummy(ImVec2(pill.Width, lineHeight));

		if (pill.IsCut)
		{
			ImGui::SetItemTooltip("%s", pill.Full.c_str());
		}
	}

	std::size_t RefLabelsThatFit(const std::vector<float>& labelWidths, float gap,
								 float availableWidth, float summaryWidth,
								 float minimumSummaryWidth)
	{
		const float room = availableWidth - std::min(summaryWidth, minimumSummaryWidth) - gap;

		std::size_t fittingCount = 0;
		float usedWidth = 0.0f;

		for (const float labelWidth : labelWidths)
		{
			const float gapBefore = fittingCount == 0 ? 0.0f : gap;
			const float widthWithThisLabel = usedWidth + gapBefore + labelWidth;

			if (widthWithThisLabel > room)
			{
				break;
			}

			usedWidth = widthWithThisLabel;
			++fittingCount;
		}

		return fittingCount;
	}
}
