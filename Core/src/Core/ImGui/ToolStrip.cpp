#include "corepch.h"

#include "Core/ImGui/ToolStrip.h"

#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/UiScale.h"

#include <algorithm>
#include <cmath>

namespace Core {

	namespace {
		using Groups = std::vector<std::vector<ToolStripItem>>;

		constexpr float kStripWidth = 42.0f;
		constexpr float kButtonSize = 30.0f;
		constexpr float kButtonRounding = 7.0f;
		constexpr float kButtonSpacing = 4.0f;
		constexpr float kEndPadding = 6.0f;
		constexpr float kIconSize = 18.0f;
		constexpr float kSeparatorLength = 18.0f;
		constexpr float kSeparatorMargin = 4.0f;
		constexpr float kLineThickness = 1.0f;
		constexpr float kOpenBarWidth = 2.0f;
		constexpr float kOpenBarInset = 5.0f;
		constexpr float kBadgeDiameter = 7.0f;
		constexpr float kBadgeRingWidth = 2.0f;
		constexpr float kBadgeCentreInset = 6.5f;
		constexpr float kHalf = 0.5f;
		constexpr float kSmallestLength = 1.0f;

		// WARNING: floored to whole pixels, so at 125 % a fill edge or a 1 px line does not straddle two pixel
		// rows and read blurred; never below one, so a line still draws below 100 %.
		float WholePixels(float length)
		{
			return std::max(kSmallestLength, std::floor(Scaled(length)));
		}

		struct StripMetrics
		{
			float Width = 0.0f;
			float Button = 0.0f;
			float Spacing = 0.0f;
			float Padding = 0.0f;
			float SeparatorMargin = 0.0f;
			float Line = 0.0f;

			float SeparatorSpacing() const { return SeparatorMargin + Line + SeparatorMargin; }
		};

		StripMetrics CurrentMetrics()
		{
			return StripMetrics{.Width = WholePixels(kStripWidth),
								.Button = WholePixels(kButtonSize),
								.Spacing = WholePixels(kButtonSpacing),
								.Padding = WholePixels(kEndPadding),
								.SeparatorMargin = WholePixels(kSeparatorMargin),
								.Line = WholePixels(kLineThickness)};
		}

		struct StripLayout
		{
			std::vector<ImRect> Items;
			std::vector<float> SeparatorTops;
		};

		float BlockHeight(const Groups& groups, const StripMetrics& metrics)
		{
			float height = 0.0f;
			bool firstGroup = true;

			for (const std::vector<ToolStripItem>& group : groups)
			{
				if (group.empty())
				{
					continue;
				}

				if (!firstGroup)
				{
					height += metrics.SeparatorSpacing();
				}

				const float itemCount = static_cast<float>(group.size());
				height += itemCount * metrics.Button + (itemCount - 1.0f) * metrics.Spacing;
				firstGroup = false;
			}

			return height;
		}

		float PlaceBlock(const Groups& groups, ImVec2 topLeft, const StripMetrics& metrics, StripLayout& layout)
		{
			float y = topLeft.y;
			bool firstGroup = true;

			for (const std::vector<ToolStripItem>& group : groups)
			{
				if (group.empty())
				{
					continue;
				}

				if (!firstGroup)
				{
					layout.SeparatorTops.push_back(y + metrics.SeparatorMargin);
					y += metrics.SeparatorSpacing();
				}

				for (std::size_t index = 0; index < group.size(); ++index)
				{
					if (index > 0)
					{
						y += metrics.Spacing;
					}

					layout.Items.emplace_back(topLeft.x, y, topLeft.x + metrics.Button, y + metrics.Button);
					y += metrics.Button;
				}

				firstGroup = false;
			}

			return y;
		}

		StripLayout LayOut(const ToolStripContent& content, ImVec2 origin, float height, const StripMetrics& metrics)
		{
			StripLayout layout;
			const float left = origin.x + std::floor((metrics.Width - metrics.Button) * kHalf);

			const float topBlockBottom = PlaceBlock(content.Top, ImVec2(left, origin.y + metrics.Padding), metrics, layout);
			const bool hasTopBlock = !layout.Items.empty();

			const float bottomBlockHeight = BlockHeight(content.Bottom, metrics);
			const float anchoredTop = origin.y + height - metrics.Padding - bottomBlockHeight;
			const float followingTop = hasTopBlock ? topBlockBottom + metrics.SeparatorSpacing() : origin.y + metrics.Padding;
			const bool bottomFollowsTop = hasTopBlock && bottomBlockHeight > 0.0f && anchoredTop <= followingTop;

			if (bottomFollowsTop)
			{
				layout.SeparatorTops.push_back(topBlockBottom + metrics.SeparatorMargin);
			}

			PlaceBlock(content.Bottom, ImVec2(left, std::max(anchoredTop, followingTop)), metrics, layout);

			return layout;
		}

		bool DrawItem(const ToolStripItem& item, const ImRect& rect, float stripLeft, const ChromePalette& palette)
		{
			ImGui::SetCursorScreenPos(rect.Min);

			// WARNING: Without NoFocus the press focuses the strip, and a click that hides a panel or the
			// terminals leaves the keyboard there, so the editor reads no keys until it is clicked again.
			const bool pressed = ImGui::InvisibleButton(item.Id.c_str(), rect.GetSize(),
														ImGuiButtonFlags_NoFocus | ImGuiButtonFlags_NoNavFocus);
			const bool hovered = ImGui::IsItemHovered();

			ImDrawList* drawList = ImGui::GetWindowDrawList();

			if (hovered)
			{
				drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(ToImVec4(palette.SurfaceHovered)),
										Scaled(kButtonRounding));
			}

			if (item.Open)
			{
				const float barWidth = WholePixels(kOpenBarWidth);
				const float barInset = WholePixels(kOpenBarInset);

				drawList->AddRectFilled(ImVec2(stripLeft, rect.Min.y + barInset),
										ImVec2(stripLeft + barWidth, rect.Max.y - barInset),
										ImGui::GetColorU32(ToImVec4(palette.Accent)), barWidth * kHalf);
			}

			// WARNING: the size is the unscaled one: ImGui multiplies it by the interface scale itself, so
			// Scaled(kIconSize) here would scale the icon twice.
			ImGui::PushFont(nullptr, kIconSize);
			const Color& glyphColour = item.Open || hovered ? palette.Text : palette.TextDisabled;
			DrawCentredGlyph(drawList, rect, item.Icon.c_str(), ImGui::GetColorU32(ToImVec4(glyphColour)));
			ImGui::PopFont();

			if (item.Badge)
			{
				const ImVec2 centre(rect.Max.x - Scaled(kBadgeCentreInset), rect.Min.y + Scaled(kBadgeCentreInset));
				const float dotRadius = Scaled(kBadgeDiameter) * kHalf;

				drawList->AddCircleFilled(centre, dotRadius + Scaled(kBadgeRingWidth),
										  ImGui::GetColorU32(ToImVec4(palette.Background)));
				drawList->AddCircleFilled(centre, dotRadius, ImGui::GetColorU32(*item.Badge));
			}

			ImGui::SetItemTooltip("%s", item.Tooltip.c_str());

			return pressed;
		}
	}

	float ToolStripWidth()
	{
		return WholePixels(kStripWidth);
	}

	std::vector<ImRect> LayOutToolStrip(const ToolStripContent& content, ImVec2 origin, float height)
	{
		return LayOut(content, origin, height, CurrentMetrics()).Items;
	}

	std::optional<std::string> DrawToolStrip(const ToolStripContent& content, const ChromePalette& palette)
	{
		if (ImGui::GetCurrentWindowRead()->SkipItems)
		{
			return std::nullopt;
		}

		const StripMetrics metrics = CurrentMetrics();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size = ImGui::GetContentRegionAvail();
		const ImVec2 end(origin.x + size.x, origin.y + size.y);
		const StripLayout layout = LayOut(content, origin, size.y, metrics);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 border = ImGui::GetColorU32(ToImVec4(palette.Border));

		drawList->AddRectFilled(origin, end, ImGui::GetColorU32(ToImVec4(palette.Background)));

		std::optional<std::string> clicked;
		std::size_t itemIndex = 0;

		for (const Groups* block : {&content.Top, &content.Bottom})
		{
			for (const std::vector<ToolStripItem>& group : *block)
			{
				for (const ToolStripItem& item : group)
				{
					if (DrawItem(item, layout.Items[itemIndex], origin.x, palette))
					{
						clicked = item.Id;
					}

					++itemIndex;
				}
			}
		}

		const float separatorLength = WholePixels(kSeparatorLength);
		const float separatorLeft = origin.x + std::floor((metrics.Width - separatorLength) * kHalf);

		for (const float separatorTop : layout.SeparatorTops)
		{
			drawList->AddRectFilled(ImVec2(separatorLeft, separatorTop),
									ImVec2(separatorLeft + separatorLength, separatorTop + metrics.Line), border);
		}

		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy(size);

		return clicked;
	}
}
