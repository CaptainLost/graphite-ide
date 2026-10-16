#include "corepch.h"

#include "Core/ImGui/ToastStyle.h"

#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/UiScale.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Core {

	namespace {
		constexpr float kCardWidth = 460.0f;
		constexpr float kMargin = 12.0f;
		constexpr float kPaddingX = 14.0f;
		constexpr float kPaddingY = 12.0f;
		constexpr float kIconDiameter = 24.0f;
		constexpr float kMarkThickness = 2.0f;
		constexpr float kHalf = 0.5f;

		constexpr float kMessageShareTowardsText = 0.85f;
		constexpr float kIconGap = 12.0f;
		constexpr float kTitleGap = 4.0f;
		constexpr float kNoWrap = 0.0f;
		constexpr float kKeepFontSize = 0.0f;
		constexpr float kShortestTitleWidth = 1.0f;

		ImFont* TitleFont()
		{
			ImFont* const bold = ImGuiRenderer::GetBoldFont();
			return bold != nullptr ? bold : ImGui::GetFont();
		}

		// WARNING: Unrounded, as AddText advances; CalcTextSize rounds each width up.
		float WidthIn(ImFont* font, std::string_view text)
		{
			return font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, kNoWrap, text.data(), text.data() + text.size()).x;
		}

		float TitleRoom(float width, std::string_view count)
		{
			const float countRoom = count.empty() ? 0.0f : WidthIn(ImGui::GetFont(), count) + ImGui::GetStyle().ItemSpacing.x;
			return std::max(width - countRoom, kShortestTitleWidth);
		}

		constexpr int kPointsInStroke = 2;
		constexpr int kPointsInTick = 3;

		constexpr ImVec2 kTickStart(-5.0f, 0.0f);
		constexpr ImVec2 kTickBend(-1.5f, 3.5f);
		constexpr ImVec2 kTickEnd(5.0f, -3.5f);

		constexpr ImVec2 kStemTop(0.0f, -5.0f);
		constexpr ImVec2 kStemBottom(0.0f, 1.5f);
		constexpr ImVec2 kStemDot(0.0f, 4.5f);
		constexpr float kStemDotRadius = 1.2f;

		constexpr ImVec2 kCrossTopLeft(-5.0f, -5.0f);
		constexpr ImVec2 kCrossBottomRight(5.0f, 5.0f);
		constexpr ImVec2 kCrossBottomLeft(-5.0f, 5.0f);
		constexpr ImVec2 kCrossTopRight(5.0f, -5.0f);

		ImVec2 PointFromCentre(ImVec2 centre, ImVec2 offsetAtFullScale)
		{
			return ImVec2(centre.x + Scaled(offsetAtFullScale.x), centre.y + Scaled(offsetAtFullScale.y));
		}

		// WARNING: a stroke is a polyline, never AddLine, which shifts both ends by half a pixel and would put the cross and the exclamation stem off the circle's centre.
		void DrawStroke(ImDrawList& drawList, ImVec2 from, ImVec2 to, ImU32 colour)
		{
			const ImVec2 points[] = {from, to};
			drawList.AddPolyline(points, kPointsInStroke, colour, Scaled(kMarkThickness), ImDrawFlags_None);
		}

		void DrawMark(ImDrawList& drawList, ToastKind kind, ImVec2 centre, ImU32 colour)
		{
			switch (kind)
			{
			case ToastKind::Success:
			{
				const ImVec2 tick[] = {PointFromCentre(centre, kTickStart), PointFromCentre(centre, kTickBend),
									   PointFromCentre(centre, kTickEnd)};
				drawList.AddPolyline(tick, kPointsInTick, colour, Scaled(kMarkThickness), ImDrawFlags_None);
				return;
			}
			case ToastKind::Warning:
				DrawStroke(drawList, PointFromCentre(centre, kStemTop), PointFromCentre(centre, kStemBottom), colour);
				drawList.AddCircleFilled(PointFromCentre(centre, kStemDot), Scaled(kStemDotRadius), colour);
				return;
			case ToastKind::Error:
				break;
			}

			DrawStroke(drawList, PointFromCentre(centre, kCrossTopLeft), PointFromCentre(centre, kCrossBottomRight), colour);
			DrawStroke(drawList, PointFromCentre(centre, kCrossBottomLeft), PointFromCentre(centre, kCrossTopRight), colour);
		}
	}

	float ToastCardWidth()
	{
		return Scaled(kCardWidth);
	}

	float ToastMargin()
	{
		return Scaled(kMargin);
	}

	float ToastPaddingX()
	{
		return Scaled(kPaddingX);
	}

	float ToastPaddingY()
	{
		return Scaled(kPaddingY);
	}

	float ToastIconDiameter()
	{
		return Scaled(kIconDiameter);
	}

	const Color& ToastAccentOf(const ToastPalette& palette, ToastKind kind)
	{
		switch (kind)
		{
		case ToastKind::Success:
			return palette.Success;
		case ToastKind::Warning:
			return palette.Warning;
		case ToastKind::Error:
			break;
		}

		return palette.Error;
	}

	Color ToastMessageColour(const ToastPalette& palette)
	{
		const Color& dimText = palette.TextDim;
		const Color& fullText = palette.Text;

		return {.R = dimText.R + (fullText.R - dimText.R) * kMessageShareTowardsText,
				.G = dimText.G + (fullText.G - dimText.G) * kMessageShareTowardsText,
				.B = dimText.B + (fullText.B - dimText.B) * kMessageShareTowardsText,
				.A = dimText.A + (fullText.A - dimText.A) * kMessageShareTowardsText};
	}

	// WARNING: GetColorU32 applies ImGui's style alpha, so a toast fading out fades its icon with it.
	void DrawToastIcon(ImDrawList& drawList, ToastKind kind, ImVec2 centre, const ToastPalette& palette)
	{
		const float iconRadius = ToastIconDiameter() * kHalf;
		const ImU32 accentColour = ImGui::GetColorU32(ToImVec4(ToastAccentOf(palette, kind)));
		const ImU32 markColour = ImGui::GetColorU32(ToImVec4(palette.Surface));

		drawList.AddCircleFilled(centre, iconRadius, accentColour);
		DrawMark(drawList, kind, centre, markColour);
	}

	float NotificationIconGap()
	{
		return Scaled(kIconGap);
	}

	float NotificationTitleGap()
	{
		return Scaled(kTitleGap);
	}

	// WARNING: The line's middle is rounded up to a whole pixel. The icon's ring is crisper there, and a row that floors
	// its first line's top would otherwise put the icon half a pixel above the row's inset.
	ImVec2 NotificationIconCentre(float textLeft, float firstLineTop)
	{
		return ImVec2(textLeft - NotificationIconGap() - ToastIconDiameter() * kHalf,
					  firstLineTop + std::ceil(ImGui::GetFontSize() * kHalf));
	}

	float NotificationTitleHeight(std::string_view title, std::string_view count, float widthWithCount,
								  NotificationTitleFit fit)
	{
		const float fontSize = ImGui::GetFontSize();

		if (fit == NotificationTitleFit::Ellipsis)
		{
			return fontSize;
		}

		const float wrapped =
			TitleFont()->CalcTextSizeA(fontSize, FLT_MAX, TitleRoom(widthWithCount, count), title.data(), title.data() + title.size()).y;
		return std::max(wrapped, fontSize);
	}

	void DrawNotificationTitle(ImDrawList& drawList, ImVec2 topLeft, float widthWithCount, std::string_view title,
							   std::string_view count, NotificationTitleFit fit, const ToastPalette& palette)
	{
		ImFont* const font = TitleFont();
		const float fontSize = ImGui::GetFontSize();
		const float room = TitleRoom(widthWithCount, count);
		const ImU32 textColour = ImGui::GetColorU32(ToImVec4(palette.Text));

		if (fit == NotificationTitleFit::Wrap)
		{
			drawList.AddText(font, fontSize, topLeft, textColour, title.data(), title.data() + title.size(), room);
		}
		else
		{
			// WARNING: RenderTextEllipsis draws in the current font and text colour, so the bold face and the palette's
			// text colour are pushed for it.
			ImGui::PushFont(font, kKeepFontSize);
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(palette.Text));
			const float right = topLeft.x + room;
			ImGui::RenderTextEllipsis(&drawList, topLeft, ImVec2(right, topLeft.y + fontSize), right, title.data(),
									  title.data() + title.size(), nullptr);
			ImGui::PopStyleColor();
			ImGui::PopFont();
		}

		if (count.empty())
		{
			return;
		}

		const float wrappedWidth =
			font->CalcTextSizeA(fontSize, FLT_MAX, room, title.data(), title.data() + title.size()).x;
		const float titleWidth =
			fit == NotificationTitleFit::Wrap ? wrappedWidth : std::min(WidthIn(font, title), room);
		const ImVec2 countTopLeft(topLeft.x + titleWidth + ImGui::GetStyle().ItemSpacing.x, topLeft.y);
		drawList.AddText(ImGui::GetFont(), fontSize, countTopLeft, ImGui::GetColorU32(ToImVec4(palette.TextDim)), count.data(),
						 count.data() + count.size());
	}

	void DrawNotificationMessage(ImDrawList& drawList, ImVec2 topLeft, std::span<const ToastTextLine> lines,
								 const ToastPalette& palette)
	{
		ImFont* const regular = ImGui::GetFont();
		ImFont* const strong = TitleFont();
		const float fontSize = ImGui::GetFontSize();
		const ImU32 plainColour = ImGui::GetColorU32(ToImVec4(ToastMessageColour(palette)));
		const ImU32 strongColour = ImGui::GetColorU32(ToImVec4(palette.Text));

		float lineTop = topLeft.y;

		for (const ToastTextLine& line : lines)
		{
			for (const ToastTextRun& run : line.Runs)
			{
				const bool isStrong = run.Style == ToastTextStyle::Strong;
				drawList.AddText(isStrong ? strong : regular, fontSize, ImVec2(topLeft.x + run.X, lineTop),
								 isStrong ? strongColour : plainColour, run.Text.data(), run.Text.data() + run.Text.size());
			}

			lineTop += fontSize;
		}
	}
}
