#include "corepch.h"

#include "Core/ImGui/ToastStyle.h"

#include "Core/ImGui/UiScale.h"

namespace Core {

	namespace {
		constexpr float kCardWidth = 460.0f;
		constexpr float kMargin = 12.0f;
		constexpr float kPaddingX = 14.0f;
		constexpr float kPaddingY = 12.0f;
		constexpr float kIconDiameter = 24.0f;
		constexpr float kMarkThickness = 2.0f;

		// The dim colour alone is too faint to read a several-line git message in; the full one
		// would leave the bold title nothing but its weight to stand apart by.
		constexpr float kMessageBrightness = 0.85f;

		ImVec4 ToImVec4(const Color& colour)
		{
			return ImVec4(colour.R, colour.G, colour.B, colour.A);
		}

		// The kind's mark, drawn rather than typed: a glyph from the icon font sits where its
		// font's metrics put it, which is never quite the circle's centre, and is drawn thin.
		// Lengths are at 100% and in pixels from the centre, y downwards. Every stroke is a
		// polyline, never AddLine, which shifts both ends by half a pixel and would put the
		// cross and the exclamation stem off the circle's centre.
		void DrawMark(ImDrawList& draw, ToastKind kind, ImVec2 centre, ImU32 colour)
		{
			const float thickness = Scaled(kMarkThickness);
			const auto at = [&](float x, float y) { return ImVec2(centre.x + Scaled(x), centre.y + Scaled(y)); };
			const auto stroke = [&](ImVec2 from, ImVec2 to) {
				const ImVec2 points[] = {from, to};
				draw.AddPolyline(points, 2, colour, thickness, ImDrawFlags_None);
			};

			switch (kind)
			{
			case ToastKind::Success:
			{
				const ImVec2 tick[] = {at(-5.0f, 0.0f), at(-1.5f, 3.5f), at(5.0f, -3.5f)};
				draw.AddPolyline(tick, 3, colour, thickness, ImDrawFlags_None);
				return;
			}
			case ToastKind::Warning:
				stroke(at(0.0f, -5.0f), at(0.0f, 1.5f));
				draw.AddCircleFilled(at(0.0f, 4.5f), Scaled(1.2f), colour);
				return;
			case ToastKind::Error:
				break;
			}

			stroke(at(-5.0f, -5.0f), at(5.0f, 5.0f));
			stroke(at(-5.0f, 5.0f), at(5.0f, -5.0f));
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
		const Color& from = palette.TextDim;
		const Color& to = palette.Text;
		const float towardsText = kMessageBrightness;

		return {.R = from.R + (to.R - from.R) * towardsText,
				.G = from.G + (to.G - from.G) * towardsText,
				.B = from.B + (to.B - from.B) * towardsText,
				.A = from.A + (to.A - from.A) * towardsText};
	}

	void DrawToastIcon(ImDrawList& draw, ToastKind kind, ImVec2 centre, const ToastPalette& palette)
	{
		draw.AddCircleFilled(centre, ToastIconDiameter() * 0.5f, ImGui::GetColorU32(ToImVec4(ToastAccentOf(palette, kind))));
		DrawMark(draw, kind, centre, ImGui::GetColorU32(ToImVec4(palette.Surface)));
	}
}
