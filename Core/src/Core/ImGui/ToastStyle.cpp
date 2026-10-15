#include "corepch.h"

#include "Core/ImGui/ToastStyle.h"

#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/UiScale.h"

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
}
